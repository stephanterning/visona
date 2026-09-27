#include "support/AllocationCounter.h"
#include "support/Signals.h"

#include <visona/BandSplitter.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::Band;
using visona::BandSplitter;
using visona::splitBands;
using visona::splitIndex;
using visona::test::AllocationCounter;
using visona::test::sine;

namespace
{

/** The output of one split band per splitIndex(). */
using BandSignals = std::array<std::vector<double>, splitBands.size()>;

BandSignals split(BandSplitter& splitter, const std::vector<float>& input)
{
    BandSignals bands;
    for (auto& band : bands)
        band.reserve(input.size());
    for (const auto sample : input)
    {
        const auto out = splitter.process(sample);
        bands[0].push_back(static_cast<double>(out.low));
        bands[1].push_back(static_cast<double>(out.mid));
        bands[2].push_back(static_cast<double>(out.high));
    }
    return bands;
}

/** The DFT of `signal` at `frequency`, over [first, first + count), weighted by (n + 1) if
    `rampWeighted`. A rotating phasor keeps this fast. */
std::complex<double> dft(const std::vector<double>& signal, double frequency, double sampleRate,
                         std::size_t first, std::size_t count, bool rampWeighted = false)
{
    const auto step = std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate);
    std::complex<double> phasor = 1.0;
    std::complex<double> sum = 0.0;
    for (std::size_t n = 0; n < count; ++n)
    {
        const auto weight = rampWeighted ? static_cast<double>(n) : 1.0;
        sum += weight * signal[first + n] * phasor;
        phasor *= step;
        if (n % 1'024 == 0)
            phasor /= std::abs(phasor);
    }
    return sum;
}

double toDb(double gain)
{
    return 20.0 * std::log10(gain);
}

/**
    The steady-state gain of each band for a sine at `frequency`, a multiple of 10 Hz: the sine runs
    0.5 s to settle, then the gains come from the DFT over 0.1 s, which is a whole number of cycles.
*/
std::array<double, splitBands.size()> toneGainsDb(double sampleRate, double frequency)
{
    const auto settle = static_cast<std::size_t>(sampleRate / 2.0);
    const auto window = static_cast<std::size_t>(sampleRate / 10.0);
    const auto input = sine(frequency, sampleRate, 0.5f, settle + window);
    BandSplitter splitter(sampleRate);
    const auto bands = split(splitter, input);

    const std::vector<double> reference(input.begin(), input.end());
    const auto inputLevel = std::abs(dft(reference, frequency, sampleRate, settle, window));
    std::array<double, splitBands.size()> gains{};
    for (std::size_t band = 0; band < bands.size(); ++band)
        gains[band] =
            toDb(std::abs(dft(bands[band], frequency, sampleRate, settle, window)) / inputLevel);
    return gains;
}

/** The impulse response of each band, long enough to decay far below float precision. */
BandSignals impulseResponses(double sampleRate)
{
    std::vector<float> impulse(static_cast<std::size_t>(sampleRate * 0.4), 0.0f);
    impulse[0] = 1.0f;
    BandSplitter splitter(sampleRate);
    return split(splitter, impulse);
}

/** The group delay of `response` at `frequency`, in seconds: Re{DFT(n·h) / DFT(h)} / fs. */
double measuredGroupDelay(const std::vector<double>& response, double frequency, double sampleRate)
{
    const auto plain = dft(response, frequency, sampleRate, 0, response.size());
    const auto ramped = dft(response, frequency, sampleRate, 0, response.size(), true);
    return (ramped / plain).real() / sampleRate;
}

const auto sampleRates = {44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0};

} // namespace

TEST_CASE("BandSplitter keeps its crossovers below the Nyquist frequency", "[bands]")
{
    const BandSplitter standard(96'000.0);
    CHECK(standard.hasSampleRate());
    CHECK(standard.lowMidHz() == BandSplitter::defaultLowMidHz);
    CHECK(standard.midHighHz() == BandSplitter::defaultMidHighHz);

    const BandSplitter slow(2'400.0);
    CHECK(slow.midHighHz() == 0.45 * 2'400.0);
    CHECK(slow.lowMidHz() == BandSplitter::defaultLowMidHz);

    const BandSplitter verySlow(300.0);
    CHECK(verySlow.midHighHz() == 0.45 * 300.0);
    CHECK(verySlow.lowMidHz() == 0.5 * verySlow.midHighHz());

    CHECK_FALSE(BandSplitter().hasSampleRate());
    CHECK_FALSE(BandSplitter(0.0).hasSampleRate());
    CHECK_FALSE(BandSplitter(-48'000.0).hasSampleRate());
}

TEST_CASE("A BandSplitter without a sample rate is silent", "[bands]")
{
    BandSplitter splitter;
    const auto out = splitter.process(0.75f);
    CHECK(out.low == 0.0f);
    CHECK(out.mid == 0.0f);
    CHECK(out.high == 0.0f);
    CHECK(splitter.groupDelaySeconds(Band::low, 50.0) == 0.0);
    CHECK(splitter.delayFrames(Band::low) == 0.0);
}

TEST_CASE("50 Hz lands in the low band, 1 kHz in the mid band and 8 kHz in the high band",
          "[bands]")
{
    const auto sampleRate = GENERATE(values(sampleRates));
    CAPTURE(sampleRate);

    struct Tone
    {
        double frequency;
        Band band;
    };
    for (const auto tone :
         {Tone{50.0, Band::low}, Tone{1'000.0, Band::mid}, Tone{8'000.0, Band::high}})
    {
        CAPTURE(tone.frequency);
        const auto gains = toneGainsDb(sampleRate, tone.frequency);
        for (const auto band : splitBands)
        {
            CAPTURE(splitIndex(band), gains[splitIndex(band)]);
            if (band == tone.band)
                CHECK(gains[splitIndex(band)] > -0.5); // 50 Hz is -0.03 dB, 1 kHz -0.23 dB
            else
                CHECK(gains[splitIndex(band)] < -30.0); // the closest, 1 kHz in highs, is -32 dB
        }
    }
}

TEST_CASE("Both bands are at -6 dB at each crossover", "[bands]")
{
    const auto sampleRate = GENERATE(values(sampleRates));
    CAPTURE(sampleRate);
    const auto minus6dB = toDb(0.5);

    const auto atLowMid = toneGainsDb(sampleRate, BandSplitter::defaultLowMidHz);
    CHECK_THAT(atLowMid[splitIndex(Band::low)], WithinAbs(minus6dB, 0.05));
    CHECK_THAT(atLowMid[splitIndex(Band::mid)], WithinAbs(minus6dB, 0.05));

    const auto atMidHigh = toneGainsDb(sampleRate, BandSplitter::defaultMidHighHz);
    CHECK_THAT(atMidHigh[splitIndex(Band::mid)], WithinAbs(minus6dB, 0.05));
    CHECK_THAT(atMidHigh[splitIndex(Band::high)], WithinAbs(minus6dB, 0.05));
}

TEST_CASE("The bands sum flat within 0.1 dB from 10 Hz to 0.45 fs", "[bands]")
{
    const auto sampleRate = GENERATE(values(sampleRates));
    CAPTURE(sampleRate);
    const auto responses = impulseResponses(sampleRate);
    std::vector<double> sum(responses[0].size());
    for (std::size_t n = 0; n < sum.size(); ++n)
        sum[n] = responses[0][n] + responses[1][n] + responses[2][n];

    constexpr int points = 60;
    const auto highest = 0.45 * sampleRate;
    double worstDb = 0.0;
    for (int point = 0; point <= points; ++point)
    {
        const auto frequency = 10.0 * std::pow(highest / 10.0, point / static_cast<double>(points));
        const auto gainDb = toDb(std::abs(dft(sum, frequency, sampleRate, 0, sum.size())));
        CAPTURE(frequency, gainDb);
        CHECK(std::abs(gainDb) < 0.1);
        worstDb = std::max(worstDb, std::abs(gainDb));
    }
    // Float output limits the precision; the filters themselves sum to an exact allpass.
    CHECK(worstDb < 0.001);
}

TEST_CASE("The measured group delay of each band matches the documented values", "[bands]")
{
    const auto sampleRate = GENERATE(values(sampleRates));
    CAPTURE(sampleRate);
    const auto responses = impulseResponses(sampleRate);
    const BandSplitter splitter(sampleRate);

    // architecture.md, 3.4: the delay of each band at its reference frequency, which is what the
    // renderer compensates, and at the test tones.
    struct Expected
    {
        Band band;
        double frequency;
        double milliseconds;
        double tolerance;
    };
    const std::array<Expected, 6> expected{{
        {Band::low, splitter.referenceHz(Band::low), 2.63, 0.01},
        {Band::low, 50.0, 2.56, 0.01},
        {Band::mid, splitter.referenceHz(Band::mid), 0.386, 0.003},
        {Band::mid, 1'000.0, 0.297, 0.003},
        {Band::high, splitter.referenceHz(Band::high), 0.028, 0.002},
        {Band::high, 8'000.0, 0.021, 0.002},
    }};
    for (const auto& item : expected)
    {
        const auto measured =
            measuredGroupDelay(responses[splitIndex(item.band)], item.frequency, sampleRate);
        const auto measuredMs = measured * 1'000.0;
        CAPTURE(splitIndex(item.band), item.frequency, measuredMs);
        CHECK_THAT(measuredMs, WithinAbs(item.milliseconds, item.tolerance));
        CHECK_THAT(splitter.groupDelaySeconds(item.band, item.frequency),
                   WithinAbs(measured, 1.0e-7));
    }

    CHECK_THAT(splitter.referenceHz(Band::low), WithinAbs(std::sqrt(20.0 * 200.0), 1.0e-9));
    CHECK_THAT(splitter.referenceHz(Band::mid), WithinAbs(std::sqrt(200.0 * 2'500.0), 1.0e-9));
    CHECK_THAT(splitter.referenceHz(Band::high), WithinAbs(std::sqrt(2'500.0 * 20'000.0), 1.0e-9));
    for (const auto band : splitBands)
        CHECK_THAT(
            splitter.delayFrames(band),
            WithinAbs(splitter.groupDelaySeconds(band, splitter.referenceHz(band)) * sampleRate,
                      1.0e-9));
    CHECK(splitter.groupDelaySeconds(Band::full, 50.0) == 0.0);
}

TEST_CASE("BandSplitter reset forgets the past input", "[bands]")
{
    const auto input = sine(80.0, 48'000.0, 0.8f, 4'000);
    BandSplitter fresh(48'000.0);
    BandSplitter used(48'000.0);
    for (const auto sample : input)
        used.process(sample);
    used.reset();

    for (const auto sample : input)
    {
        const auto expected = fresh.process(sample);
        const auto actual = used.process(sample);
        REQUIRE(actual.low == expected.low);
        REQUIRE(actual.mid == expected.mid);
        REQUIRE(actual.high == expected.high);
    }
}

TEST_CASE("BandSplitter does not allocate", "[bands][realtime]")
{
    const auto input = sine(1'000.0, 96'000.0, 1.0f, 4'096);
    BandSplitter splitter;
    STATIC_REQUIRE(noexcept(BandSplitter(96'000.0)));
    STATIC_REQUIRE(noexcept(splitter.process(0.0f)));
    STATIC_REQUIRE(noexcept(splitter.reset()));

    const AllocationCounter allocations;
    splitter = BandSplitter(96'000.0);
    float sum = 0.0f;
    for (const auto sample : input)
    {
        const auto out = splitter.process(sample);
        sum += out.low + out.mid + out.high;
    }
    splitter.reset();
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(std::isfinite(sum));
}
