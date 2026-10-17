#include "support/AllocationCounter.h"

#include <visona/BandAnalyzer.h>
#include <visona/SpectrumAnalyzer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>
using namespace visona;
namespace
{
std::vector<float> bandTone(double rate, double frequency, std::size_t count)
{
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = static_cast<float>(
            0.5 * std::sin(2 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
    return samples;
}
BandSnapshot read(BandAnalyzer& analyzer)
{
    REQUIRE(analyzer.snapshots().fetch());
    return analyzer.snapshots().readBuffer();
}
} // namespace
TEST_CASE("Band filters preserve centre tone levels and reject adjacent bands", "[bands]")
{
    for (const auto rate : {44100.0, 48000.0, 96000.0})
        for (std::size_t band : {0U, 8U, 17U, 26U})
        {
            BandAnalyzer analyzer;
            analyzer.reset(rate);
            analyzer.setEnabled(true);
            const auto tone =
                bandTone(rate, BandAnalyzer::frequency(band), static_cast<std::size_t>(rate));
            analyzer.process(tone, tone, 0);
            const auto s = read(analyzer);
            REQUIRE(s.valid);
            REQUIRE(s.db[band] == Catch::Approx(-6.0206).margin(1.0));
            REQUIRE(s.db[band + 1] < s.db[band] - 5);
            REQUIRE(s.peakDb[band] >= s.db[band]);
        }
}
TEST_CASE("Band stereo energy does not cancel opposite phase", "[bands]")
{
    BandAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const auto tone = bandTone(48000, 1000, 48000);
    auto opposite = tone;
    for (auto& v : opposite)
        v = -v;
    analyzer.process(tone, opposite, 0);
    const auto stereo = read(analyzer);
    REQUIRE(stereo.db[17] == Catch::Approx(-6.0206).margin(0.2));
    analyzer.reset(48000);
    std::fill(opposite.begin(), opposite.end(), 0);
    analyzer.process(tone, opposite, 0);
    REQUIRE(read(analyzer).db[17] == Catch::Approx(stereo.db[17] - 3.0103f).margin(0.01));
}
TEST_CASE("Band analysis responds without waiting for an FFT and preserves peak hold", "[bands]")
{
    BandAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    auto tone = bandTone(48000, 1000, 2400);
    analyzer.process(tone, tone, 0);
    const auto on = read(analyzer);
    REQUIRE(on.db[17] > -7.0f);
    std::vector<float> silence(24000);
    analyzer.process(silence, silence, tone.size());
    const auto after = read(analyzer);
    REQUIRE(after.db[17] < on.db[17] - 20);
    REQUIRE(after.peakDb[17] == Catch::Approx(on.peakDb[17]).margin(0.1));
    analyzer.clearPeaks();
    analyzer.process(silence, silence, tone.size() + silence.size());
    REQUIRE(read(analyzer).peakDb[17] < on.peakDb[17] - 20);
}
TEST_CASE("Band stream gaps and disable clear stale peaks and processing never allocates",
          "[bands]")
{
    BandAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const auto tone = bandTone(48000, 1000, 4800);
    std::size_t allocations = 0;
    {
        visona::test::AllocationCounter counter;
        analyzer.process(tone, tone, 0);
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
    read(analyzer);
    analyzer.setEnabled(false);
    analyzer.process(tone, tone, tone.size());
    REQUIRE_FALSE(analyzer.snapshots().fetch());
    analyzer.setEnabled(true);
    std::vector<float> silence(4800, std::numeric_limits<float>::quiet_NaN());
    analyzer.process(silence, silence, 100000);
    for (auto db : read(analyzer).db)
        REQUIRE(db <= -150.0f);
}
TEST_CASE("Band results are independent of audio block boundaries", "[bands]")
{
    BandAnalyzer a, b;
    a.reset(48000);
    b.reset(48000);
    a.setEnabled(true);
    b.setEnabled(true);
    const auto tone = bandTone(48000, 997, 12000);
    a.process(tone, tone, 0);
    for (std::size_t offset = 0; offset < tone.size();)
    {
        const auto count = std::min<std::size_t>(137, tone.size() - offset);
        const auto block = std::span(tone).subspan(offset, count);
        b.process(block, block, offset);
        offset += count;
    }
    REQUIRE(read(a).db == read(b).db);
}
TEST_CASE("Spectrum dispatcher runs only the enabled analysis", "[bands]")
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.snapshots().fetch();
    analyzer.bands().snapshots().fetch();
    analyzer.bands().setEnabled(true);
    const auto tone = bandTone(48000, 1000, 12000);
    analyzer.process(tone, tone, 0);
    REQUIRE(analyzer.bands().snapshots().fetch());
    REQUIRE_FALSE(analyzer.snapshots().fetch());
    analyzer.bands().setEnabled(false);
    analyzer.setEnabled(true);
    analyzer.process(tone, tone, tone.size());
    REQUIRE_FALSE(analyzer.bands().snapshots().fetch());
    REQUIRE(analyzer.snapshots().fetch());
}

TEST_CASE("Band peak markers decay at twelve dB per second after hold", "[bands]")
{
    BandAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const auto tone = bandTone(48000, 1000, 48000);
    analyzer.process(tone, tone, 0);
    std::vector<float> silence(96000);
    analyzer.process(silence, silence, 48000);
    const auto before = read(analyzer);
    silence.resize(48000);
    analyzer.process(silence, silence, 144000);
    const auto after = read(analyzer);
    REQUIRE(after.peakDb[17] - before.peakDb[17] == Catch::Approx(-12.0f).margin(0.05));
}

TEST_CASE("Band octave edges have the expected half-power response", "[bands]")
{
    for (std::size_t band : {5U, 17U, 23U})
        for (const double side : {-1.0, 1.0})
        {
            BandAnalyzer analyzer;
            analyzer.reset(48000);
            analyzer.setEnabled(true);
            const auto frequency = BandAnalyzer::frequency(band) * std::pow(10.0, side * 0.05);
            const auto tone = bandTone(48000, frequency, 48000);
            analyzer.process(tone, tone, 0);
            REQUIRE(read(analyzer).db[band] == Catch::Approx(-9.0309).margin(0.8));
        }
}
