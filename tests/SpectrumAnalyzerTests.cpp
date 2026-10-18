#include "support/AllocationCounter.h"

#include <visona/AnalysisPipeline.h>
#include <visona/AudioInputWriter.h>
#include <visona/HostAnalysisPipeline.h>
#include <visona/SpectrumAnalyzer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>
#include <vector>

namespace
{
using namespace visona;
std::vector<float> sine(double rate, double frequency, float amplitude = 1.0f,
                        std::size_t count = spectrumFftSize * 2)
{
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency *
                                                             static_cast<double>(i) / rate));
    return samples;
}
SpectrumSnapshot analyze(const std::vector<float>& left, const std::vector<float>& right,
                         double rate, SpectrumSettings settings = {})
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(rate);
    analyzer.setEnabled(true);
    analyzer.setSettings(settings);
    analyzer.process(left, right, 0);
    REQUIRE(analyzer.snapshots().fetch());
    return analyzer.snapshots().readBuffer();
}
float maximum(const SpectrumSnapshot& snapshot, std::size_t curve = 0)
{
    return *std::max_element(snapshot.db[curve].begin(), snapshot.db[curve].end());
}
} // namespace
TEST_CASE("Spectrum locates tones and preserves calibrated level across sample rates", "[spectrum]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        const auto frequency = 340.0 * rate / static_cast<double>(spectrumFftSize);
        auto samples = sine(rate, frequency, 0.5f);
        SpectrumSettings settings;
        settings.curves[0].smoothing = 0;
        settings.curves[1].smoothing = 0;
        settings.curves[1].rms = true;
        const auto snapshot = analyze(samples, samples, rate, settings);
        REQUIRE(snapshot.valid);
        const auto peak = std::max_element(snapshot.db[0].begin(), snapshot.db[0].end());
        const auto index = static_cast<std::size_t>(peak - snapshot.db[0].begin());
        const auto found =
            20.0 * std::pow(snapshot.maxFrequency / 20.0,
                            static_cast<double>(index) / static_cast<double>(spectrumPoints - 1));
        REQUIRE(found == Catch::Approx(frequency).epsilon(0.012));
        // Log display interpolation and off-grid display points can lose a little peak level.
        REQUIRE(*peak > -9.5f);
        REQUIRE(*peak <= -6.0f);
        REQUIRE(maximum(snapshot, 1) - maximum(snapshot) == Catch::Approx(-3.0103f).margin(0.02f));
    }
}
TEST_CASE("Stereo energy average does not cancel opposite phase and Mid Side separate correctly",
          "[spectrum]")
{
    auto left = sine(48000, 48000.0 * 80 / spectrumFftSize);
    auto right = left;
    for (auto& sample : right)
        sample = -sample;
    SpectrumSettings s;
    s.curves[0].smoothing = 0;
    const auto stereo = analyze(left, right, 48000, s);
    s.channel = 3;
    REQUIRE(maximum(analyze(left, right, 48000, s)) <= -150.0f);
    s.channel = 4;
    REQUIRE(maximum(analyze(left, right, 48000, s)) ==
            Catch::Approx(maximum(stereo)).margin(0.01f));
    s.channel = 1;
    const auto l = maximum(analyze(left, right, 48000, s));
    std::fill(right.begin(), right.end(), 0.0f);
    s.channel = 0;
    REQUIRE(maximum(analyze(left, right, 48000, s)) == Catch::Approx(l - 3.0103f).margin(0.02f));
    s.channel = 2;
    REQUIRE(maximum(analyze(left, right, 48000, s)) <= -150.0f);
}
TEST_CASE(
    "Spectrum clears on gaps and stream changes, ignores invalid samples and pauses when disabled",
    "[spectrum]")
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    auto samples = sine(48000, 200);
    analyzer.process(samples, samples, 0);
    analyzer.snapshots().fetch();
    REQUIRE(analyzer.snapshots().readBuffer().valid);
    const std::vector<float> shortBlock(64, 0.0f);
    analyzer.process(shortBlock, shortBlock, 999999);
    REQUIRE_FALSE(analyzer.snapshots().fetch());
    analyzer.reset(96000);
    REQUIRE(analyzer.snapshots().fetch());
    REQUIRE_FALSE(analyzer.snapshots().readBuffer().valid);
    std::fill(samples.begin(), samples.end(), std::numeric_limits<float>::quiet_NaN());
    analyzer.process(samples, samples, 0);
    analyzer.snapshots().fetch();
    for (auto db : analyzer.snapshots().readBuffer().db[0])
        REQUIRE(std::isfinite(db));
    analyzer.setEnabled(false);
    analyzer.process(samples, samples, samples.size());
    REQUIRE_FALSE(analyzer.snapshots().fetch());
}
TEST_CASE("Peak hold persists and explicit reset releases it", "[spectrum]")
{
    SpectrumAnalyzer analyzer;
    SpectrumSettings s;
    s.curves[0].speed = 3;
    analyzer.setSettings(s);
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    auto samples = sine(48000, 300);
    analyzer.process(samples, samples, 0);
    analyzer.snapshots().fetch();
    const auto held = maximum(analyzer.snapshots().readBuffer());
    std::fill(samples.begin(), samples.end(), 0.0f);
    analyzer.process(samples, samples, samples.size());
    analyzer.snapshots().fetch();
    REQUIRE(maximum(analyzer.snapshots().readBuffer()) >= held - 0.01f);
    analyzer.clearHold();
    analyzer.process(samples, samples, samples.size() * 2);
    analyzer.snapshots().fetch();
    REQUIRE(maximum(analyzer.snapshots().readBuffer()) <= -150.0f);
}
TEST_CASE("Spectrum is independent of block boundaries", "[spectrum]")
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const auto samples = sine(48000, 917, 0.4f, 50000);
    const auto whole = analyze(samples, samples, 48000);
    for (std::size_t offset = 0; offset < samples.size();)
    {
        const auto count = std::min<std::size_t>(137, samples.size() - offset);
        const auto block = std::span(samples).subspan(offset, count);
        analyzer.process(block, block, offset);
        offset += count;
    }
    analyzer.snapshots().fetch();
    REQUIRE(analyzer.snapshots().readBuffer().db == whole.db);
}
TEST_CASE("Spectrum settings round trip only analysis values and sanitize invalid inputs",
          "[spectrum]")
{
    SpectrumSettings s;
    s.channel = 4;
    s.curves[0].rms = true;
    s.curves[0].speed = 1;
    s.curves[0].smoothing = 5;
    const auto key = s.analysisKey();
    REQUIRE(SpectrumSettings::fromAnalysisKey(key).analysisKey() == key);
    s.slope = 6;
    s.topDb = -12;
    s.curves[0].colour = 3;
    s.curves[1].style = 2;
    REQUIRE(s.analysisKey() == key);
    s.channel = -1;
    s.curves[0].smoothing = 99;
    s.curves[0].speed = 3;
    s.sanitize();
    REQUIRE(s.channel == 0);
    REQUIRE(s.curves[0].smoothing == 5);
    REQUIRE(s.curves[0].speed == 2);
    REQUIRE(s.curves[1].style == 3);
}
TEST_CASE("Spectrum processing and reconfiguration allocate no storage", "[spectrum][realtime]")
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    auto samples = sine(48000, 1200);
    std::size_t allocations = 0;
    {
        visona::test::AllocationCounter counter;
        analyzer.process(samples, samples, 0);
        SpectrumSettings s;
        s.channel = 4;
        s.curves[1].rms = true;
        analyzer.setSettings(s);
        analyzer.process(samples, samples, samples.size());
        analyzer.reset(96000);
        analyzer.process(samples, samples, 0);
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}
TEST_CASE("Raw spectrum retains narrow high frequency peaks on the log display", "[spectrum]")
{
    SpectrumSettings settings;
    settings.curves[0].smoothing = 0;
    for (double frequency : {7000.0, 10000.0, 17500.0})
    {
        auto samples = sine(48000, frequency);
        const auto snapshot = analyze(samples, samples, 48000, settings);
        REQUIRE(maximum(snapshot) > -1.5f);
    }
}
TEST_CASE("Both pipelines publish spectrum from incoming audio while transport is stopped",
          "[spectrum][analysis]")
{
    const auto samples = sine(48000, 220);
    const std::array<const float*, 2> input{samples.data(), samples.data()};
    AudioRingBuffer ring(2, 96000, 1024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);
    SECTION("Standalone free-running, pause and resume")
    {
        AnalysisPipeline pipeline(2, 256);
        pipeline.setStream(&ring, 48000);
        pipeline.spectrum().setEnabled(true);
        for (int i = 0; i < 64; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE(pipeline.spectrum().snapshots().fetch());
        REQUIRE(pipeline.spectrum().snapshots().readBuffer().valid);
        pipeline.setPaused(true);
        for (int i = 0; i < 64; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE_FALSE(pipeline.spectrum().snapshots().fetch());
        pipeline.setPaused(false);
        for (int i = 0; i < 64; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE(pipeline.spectrum().snapshots().fetch());
        REQUIRE(pipeline.spectrum().snapshots().readBuffer().valid);
    }
    SECTION("Plugin stopped host")
    {
        HostAnalysisPipeline pipeline(2, 256);
        pipeline.setStream(&ring, 48000);
        pipeline.spectrum().setEnabled(true);
        HostTransport::Playhead playhead;
        playhead.valid = true;
        playhead.isPlaying = false;
        playhead.bpm = 120;
        for (int i = 0; i < 64; ++i)
        {
            const auto first = writer.nextSampleIndex();
            writer.write(input, 512, 0);
            pipeline.pushPlayhead(first, playhead);
            pipeline.poll();
        }
        REQUIRE(pipeline.spectrum().snapshots().fetch());
        REQUIRE(pipeline.spectrum().snapshots().readBuffer().valid);
    }
}

TEST_CASE("Spectrum publishes coherent snapshots during concurrent settings changes",
          "[spectrum][stress]")
{
    SpectrumAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const auto samples = sine(48000, 220, 0.5f, 1024);
    std::atomic<bool> done{false};
    std::atomic<bool> valid{true};
    std::atomic<unsigned> received{0};
    std::thread producer(
        [&]
        {
            for (std::uint64_t block = 0; block < 1024; ++block)
                analyzer.process(samples, samples, block * samples.size());
            done.store(true, std::memory_order_release);
        });
    std::thread consumer(
        [&]
        {
            bool finished = false;
            do
            {
                // Observe completion before fetching, so the final published frame is
                // consumed even if the producer finished before this thread was scheduled.
                finished = done.load(std::memory_order_acquire);
                if (analyzer.snapshots().fetch())
                {
                    const auto& snapshot = analyzer.snapshots().readBuffer();
                    if (snapshot.valid)
                    {
                        received.fetch_add(1, std::memory_order_relaxed);
                        for (const auto& curve : snapshot.db)
                            for (auto db : curve)
                                if (!std::isfinite(db) || db > 6.0f)
                                    valid.store(false, std::memory_order_relaxed);
                    }
                }
                std::this_thread::yield();
            } while (!finished);
        });
    for (int i = 0; i < 20; ++i)
    {
        SpectrumSettings settings;
        settings.channel = i % 5;
        settings.curves[1].rms = (i % 2) == 0;
        analyzer.setSettings(settings);
        std::this_thread::yield();
    }
    producer.join();
    consumer.join();
    REQUIRE(valid.load());
    REQUIRE(received.load() > 0);
}

TEST_CASE("Shared smoothing preserves independent trace detection", "[spectrum]")
{
    const auto samples = sine(48000, 997.0, 0.7f);
    for (const bool rms : {false, true})
        for (int smoothing = 0; smoothing <= 5; ++smoothing)
        {
            SpectrumSettings shared;
            shared.curves[0].smoothing = smoothing;
            shared.curves[1].smoothing = smoothing;
            shared.curves[1].rms = rms;
            const auto reused = analyze(samples, samples, 48000, shared);
            auto independent = shared;
            independent.curves[0].smoothing = (smoothing + 1) % 6;
            const auto separate = analyze(samples, samples, 48000, independent);
            REQUIRE(reused.db[1] == separate.db[1]);
        }
}

TEST_CASE("Editing one trace preserves the other trace's held peaks", "[spectrum]")
{
    for (std::size_t heldCurve : {0U, 1U})
    {
        SpectrumAnalyzer analyzer;
        SpectrumSettings settings;
        settings.curves[heldCurve].speed = 3;
        analyzer.reset(48000);
        analyzer.setEnabled(true);
        analyzer.setSettings(settings);
        auto tone = sine(48000, 997.0);
        analyzer.process(tone, tone, 0);
        analyzer.snapshots().fetch();
        std::vector<float> silence(tone.size());
        analyzer.process(silence, silence, tone.size());
        analyzer.snapshots().fetch();
        const auto held = analyzer.snapshots().readBuffer().db[heldCurve];
        settings.curves[1 - heldCurve].rms = true;
        settings.curves[1 - heldCurve].smoothing = 5;
        analyzer.setSettings(settings);
        analyzer.process(silence, silence, tone.size() * 2);
        REQUIRE(analyzer.snapshots().fetch());
        REQUIRE(analyzer.snapshots().readBuffer().db[heldCurve] == held);
    }
}

TEST_CASE("Spectrum response times match their detector and both curves", "[spectrum]")
{
    constexpr std::array<double, 3> seconds{0.15, 0.5, 1.5};
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (bool rms : {false, true})
            for (int speed = 0; speed < 3; ++speed)
            {
                SpectrumAnalyzer analyzer;
                SpectrumSettings settings;
                for (auto& curve : settings.curves)
                {
                    curve.rms = rms;
                    curve.speed = speed;
                    curve.smoothing = 0;
                }
                analyzer.reset(rate);
                analyzer.setEnabled(true);
                analyzer.setSettings(settings);
                auto tone = sine(rate, rate * 340 / spectrumFftSize);
                analyzer.process(tone, tone, 0);
                std::vector<float> silence(spectrumFftSize);
                analyzer.process(silence, silence, tone.size());
                analyzer.snapshots().fetch();
                const auto before = analyzer.snapshots().readBuffer();
                silence.resize(spectrumHopSize * 4);
                analyzer.process(silence, silence, tone.size() + spectrumFftSize);
                REQUIRE(analyzer.snapshots().fetch());
                const auto after = analyzer.snapshots().readBuffer();
                const auto elapsed = static_cast<double>(after.frameEnd - before.frameEnd) / rate;
                const auto expected = (rms ? -10.0 : -20.0) * std::log10(std::exp(1.0)) * elapsed /
                                      seconds[static_cast<std::size_t>(speed)];
                for (std::size_t c : {0U, 1U})
                    REQUIRE(maximum(after, c) - maximum(before, c) ==
                            Catch::Approx(expected).margin(0.01));
            }
}

TEST_CASE("RMS attack differs by speed while Peak attack is immediate", "[spectrum]")
{
    for (bool rms : {false, true})
    {
        SpectrumAnalyzer analyzer;
        SpectrumSettings settings;
        settings.curves[0].rms = settings.curves[1].rms = rms;
        settings.curves[0].speed = 0;
        settings.curves[1].speed = 2;
        analyzer.reset(48000);
        analyzer.setSettings(settings);
        analyzer.setEnabled(true);
        std::vector<float> silence(spectrumFftSize);
        analyzer.process(silence, silence, 0);
        const auto tone = sine(48000, 997.0, 0.7f, spectrumFftSize);
        analyzer.process(tone, tone, silence.size());
        REQUIRE(analyzer.snapshots().fetch());
        const auto snapshot = analyzer.snapshots().readBuffer();
        if (rms)
            REQUIRE(maximum(snapshot, 0) > maximum(snapshot, 1) + 5.0f);
        else
            REQUIRE(maximum(snapshot, 0) == Catch::Approx(maximum(snapshot, 1)).margin(0.01));
    }
}

TEST_CASE("Response changes preserve the current envelope without refilling the FFT", "[spectrum]")
{
    SpectrumAnalyzer analyzer;
    SpectrumSettings settings;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    analyzer.setSettings(settings);
    auto tone = sine(48000, 997.0);
    analyzer.process(tone, tone, 0);
    std::vector<float> silence(spectrumFftSize);
    analyzer.process(silence, silence, tone.size());
    analyzer.snapshots().fetch();
    const auto before = analyzer.snapshots().readBuffer();
    settings.curves[0].speed = settings.curves[1].speed = 3;
    analyzer.setSettings(settings);
    silence.resize(spectrumHopSize);
    analyzer.process(silence, silence, tone.size() + spectrumFftSize);
    REQUIRE(analyzer.snapshots().fetch());
    REQUIRE(analyzer.snapshots().readBuffer().db == before.db);
}

TEST_CASE("Every octave smoothing setting matches a known tone's spectral power", "[spectrum]")
{
    constexpr double rate = 48000.0;
    constexpr double toneBin = 1000.0;
    const double frequency = toneBin * rate / spectrumFftSize;
    const auto samples = sine(rate, frequency, 0.5f);
    const auto index = static_cast<std::size_t>(
        std::round(std::log(frequency / 20.0) / std::log(20000.0 / 20.0) * (spectrumPoints - 1)));
    const auto bin = 20.0 * std::pow(1000.0, static_cast<double>(index) / (spectrumPoints - 1)) *
                     spectrumFftSize / rate;
    constexpr std::array<double, 5> octaves{1.0 / 24, 1.0 / 12, 1.0 / 6, 1.0 / 3, 1.0};
    for (std::size_t smoothing = 0; smoothing < octaves.size(); ++smoothing)
    {
        SpectrumSettings settings;
        for (auto& curve : settings.curves)
            curve.smoothing = static_cast<int>(smoothing + 1);
        const auto snapshot = analyze(samples, samples, rate, settings);
        const auto low = bin * std::exp2(-octaves[smoothing] / 2);
        const auto high = bin * std::exp2(octaves[smoothing] / 2);
        // A bin-centred Hann-windowed sine has power in exactly three FFT bins.
        double sum = 0;
        for (int offset : {-1, 0, 1})
        {
            const auto centre = toneBin + offset;
            const auto overlap =
                std::max(0.0, std::min(high, centre + 0.5) - std::max(low, centre - 0.5));
            sum += overlap * (offset == 0 ? 0.25 : 0.0625);
        }
        const auto expectedDb = 10.0 * std::log10(sum / (high - low));
        for (std::size_t curve : {0U, 1U})
            REQUIRE(snapshot.db[curve][index] == Catch::Approx(expectedDb).margin(0.02));
    }
}

TEST_CASE("Global FFT afterglow falls twelve dB per selected duration without holding",
          "[spectrum]")
{
    constexpr double rate = 48000;
    for (const int afterglow : {1, 2, 3})
        for (const bool rms : {false, true})
        {
            SpectrumAnalyzer analyzer;
            SpectrumSettings settings;
            settings.afterglow = afterglow;
            for (auto& curve : settings.curves)
            {
                curve.rms = rms;
                curve.smoothing = 0;
            }
            REQUIRE(SpectrumSettings::fromAnalysisKey(settings.analysisKey()).afterglow ==
                    afterglow);
            analyzer.reset(rate);
            analyzer.setEnabled(true);
            analyzer.setSettings(settings);
            const auto tone = sine(rate, 1000, 0.5f);
            analyzer.process(tone, tone, 0);
            REQUIRE(analyzer.snapshots().fetch());
            const auto initial = analyzer.snapshots().readBuffer();
            const auto peak = static_cast<std::size_t>(
                std::max_element(initial.db[1].begin(), initial.db[1].end()) -
                initial.db[1].begin());
            std::vector<float> silence(spectrumFftSize, 0);
            std::uint64_t frame = tone.size();
            analyzer.process(silence, silence, frame);
            frame += silence.size();
            REQUIRE(analyzer.snapshots().fetch());
            const auto before = analyzer.snapshots().readBuffer();
            constexpr std::array<double, 4> seconds{0, 0.5, 1, 2};
            REQUIRE(before.db[1][peak] < initial.db[1][peak]);
            const auto hops = static_cast<std::size_t>(
                seconds[static_cast<std::size_t>(afterglow)] * rate / spectrumHopSize);
            silence.assign(hops * spectrumHopSize, 0);
            test::AllocationCounter allocations;
            analyzer.process(silence, silence, frame);
            const auto allocated = allocations.count();
            REQUIRE(allocated == 0);
            REQUIRE(analyzer.snapshots().fetch());
            const auto after = analyzer.snapshots().readBuffer();
            const auto elapsed = static_cast<double>(after.frameEnd - before.frameEnd) / rate;
            REQUIRE(after.db[1][peak] - before.db[1][peak] ==
                    Catch::Approx(-12 * elapsed / seconds[static_cast<std::size_t>(afterglow)])
                        .margin(0.02));
        }
}
