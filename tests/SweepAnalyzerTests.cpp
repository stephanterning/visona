#include "support/AllocationCounter.h"
#include "support/Signals.h"

#include <visona/SweepAnalyzer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::Band;
using visona::BandSplitter;
using visona::freeRunningWindowFrames;
using visona::splitBands;
using visona::SweepAnalyzer;
using visona::SweepBuffer;
using visona::SweepCell;
using visona::test::AllocationCounter;
using visona::test::clicks;
using visona::test::sine;

namespace
{

constexpr double sampleRate96k = 96'000.0;
constexpr std::uint64_t window96k = 192'000; // 2 s at 96 kHz, which is also 1 bar at 120 BPM

/** Feeds `channels` to `analyzer` from `firstSampleIndex` on, in blocks of `blockSize` frames. */
void feed(SweepAnalyzer& analyzer, const std::vector<std::vector<float>>& channels,
          std::size_t blockSize, std::uint64_t firstSampleIndex = 0)
{
    const auto numFrames = channels.front().size();
    std::vector<const float*> pointers(channels.size());
    for (std::size_t start = 0; start < numFrames; start += blockSize)
    {
        const auto frames = std::min(blockSize, numFrames - start);
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        analyzer.process(firstSampleIndex + start, pointers, frames);
    }
}

/** The bin of the frame at stream position `position`. */
std::size_t binOf(std::uint64_t position, std::uint64_t windowFrames, std::size_t numBins)
{
    return static_cast<std::size_t>(position % windowFrames * numBins / windowFrames);
}

/** The min and max of every bin of the latest pass of `samples`, computed frame by frame. */
std::vector<SweepCell> referenceCells(const std::vector<float>& samples, std::uint64_t windowFrames,
                                      std::size_t numBins)
{
    std::vector<SweepCell> cells(numBins);
    const auto lastPass = (samples.size() - 1) / windowFrames;
    for (std::size_t frame = 0; frame < samples.size(); ++frame)
    {
        const auto pass = frame / windowFrames;
        const auto bin = binOf(frame, windowFrames, numBins);
        const auto lastBin = binOf(samples.size() - 1, windowFrames, numBins);
        // The latest pass behind the head, and the pass before it ahead of the head.
        if (pass == lastPass || (pass + 1 == lastPass && bin > lastBin))
            cells[bin].merge({samples[frame], samples[frame]});
    }
    return cells;
}

} // namespace

TEST_CASE("The free-running window is 2 s at every sample rate", "[sweep]")
{
    CHECK(freeRunningWindowFrames(44'100.0) == 88'200);
    CHECK(freeRunningWindowFrames(48'000.0) == 96'000);
    CHECK(freeRunningWindowFrames(88'200.0) == 176'400);
    CHECK(freeRunningWindowFrames(96'000.0) == window96k);
    CHECK(freeRunningWindowFrames(192'000.0) == 384'000);
    CHECK(freeRunningWindowFrames(0.0) == 1);
}

TEST_CASE("A free-running window of any length rounds to whole frames", "[sweep]")
{
    CHECK(freeRunningWindowFrames(96'000.0, 0.125) == 12'000);
    CHECK(freeRunningWindowFrames(44'100.0, 0.125) == 5'513);
    CHECK(freeRunningWindowFrames(48'000.0, 8.0) == 384'000);
    CHECK(freeRunningWindowFrames(48'000.0, 0.0) == 1);
}

TEST_CASE("A 1 kHz sine at 96 kHz gives a deterministic sweep", "[sweep]")
{
    constexpr double amplitude = 0.5;
    const std::vector<std::vector<float>> input{
        sine(1'000.0, sampleRate96k, static_cast<float>(amplitude), window96k)};

    SweepAnalyzer analyzer(1);
    analyzer.start(window96k);
    feed(analyzer, input, 512);
    const auto& sweep = analyzer.buffer();

    SECTION("every bin holds exactly the min and max of its frames")
    {
        const auto expected = referenceCells(input[0], window96k, SweepBuffer::defaultBinCount);
        const auto cells = sweep.channel(0);
        for (std::size_t bin = 0; bin < cells.size(); ++bin)
        {
            CAPTURE(bin);
            REQUIRE(cells[bin] == expected[bin]);
        }
    }

    SECTION("the window is one complete pass")
    {
        CHECK(sweep.pass() == 1);
        CHECK(sweep.head() == SweepBuffer::defaultBinCount - 1);
        for (const auto pass : sweep.passes())
            CHECK(pass == 1);
        CHECK(analyzer.binStartFrame(SweepBuffer::defaultBinCount) == window96k);
    }

    SECTION("the waveform reaches -A and +A across every three neighbouring bins")
    {
        // A bin is 46.875 frames, about half a cycle of 1 kHz at 96 kHz, so one bin alone does not
        // span a whole cycle; three neighbouring bins always do.
        const auto cells = sweep.channel(0);
        for (std::size_t bin = 0; bin + 2 < cells.size(); ++bin)
        {
            SweepCell span = cells[bin];
            span.merge(cells[bin + 1]);
            span.merge(cells[bin + 2]);
            CAPTURE(bin);
            REQUIRE_THAT(static_cast<double>(span.min), WithinAbs(-amplitude, 1.0e-3));
            REQUIRE_THAT(static_cast<double>(span.max), WithinAbs(amplitude, 1.0e-3));
        }
    }
}

TEST_CASE("A sine with at least one cycle per bin fills every bin from -A to +A", "[sweep]")
{
    // 4 kHz at 96 kHz is 24 frames per cycle; each bin of a 2 s window has 46 or 47 frames.
    constexpr double amplitude = 0.8;
    const std::vector<std::vector<float>> input{
        sine(4'000.0, sampleRate96k, static_cast<float>(amplitude), window96k)};

    SweepAnalyzer analyzer(1);
    analyzer.start(window96k);
    feed(analyzer, input, 480);

    for (const auto& cell : analyzer.buffer().channel(0))
    {
        REQUIRE_THAT(static_cast<double>(cell.min), WithinAbs(-amplitude, 1.0e-5));
        REQUIRE_THAT(static_cast<double>(cell.max), WithinAbs(amplitude, 1.0e-5));
    }
}

TEST_CASE("A click every quarter window lands in the bin at that quarter", "[sweep]")
{
    const auto sampleRate = GENERATE(44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0);
    CAPTURE(sampleRate);
    const auto windowFrames = freeRunningWindowFrames(sampleRate);
    std::vector<std::uint64_t> clickFrames;
    for (std::uint64_t quarter = 0; quarter < 4; ++quarter)
        clickFrames.push_back(quarter * windowFrames / 4);
    const std::vector<std::vector<float>> input{
        clicks(static_cast<std::size_t>(windowFrames), clickFrames, 1.0f)};

    SweepAnalyzer analyzer(1);
    analyzer.start(windowFrames);
    feed(analyzer, input, 256);

    const auto cells = analyzer.buffer().channel(0);
    constexpr auto quarterBins = SweepBuffer::defaultBinCount / 4;
    for (std::size_t bin = 0; bin < cells.size(); ++bin)
    {
        CAPTURE(bin);
        const bool clickBin = bin % quarterBins == 0;
        REQUIRE(cells[bin].max == (clickBin ? 1.0f : 0.0f));
        REQUIRE(cells[bin].min == 0.0f);
    }
}

TEST_CASE("The sweep does not depend on how the audio is split into blocks", "[sweep]")
{
    constexpr std::uint64_t windowFrames = 9'600;
    std::mt19937 random(42);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<std::vector<float>> input(2, std::vector<float>(25'000));
    for (auto& channel : input)
        for (auto& sample : channel)
            sample = noise(random);

    SweepAnalyzer reference(2, 1'024);
    reference.start(windowFrames);
    feed(reference, input, input[0].size());

    const auto blockSize = GENERATE(std::size_t{1}, std::size_t{7}, std::size_t{64},
                                    std::size_t{480}, std::size_t{4'096});
    CAPTURE(blockSize);
    SweepAnalyzer analyzer(2, 1'024);
    analyzer.start(windowFrames);
    feed(analyzer, input, blockSize);
    CHECK(analyzer.buffer() == reference.buffer());
}

TEST_CASE("The head and pass follow the stream position", "[sweep]")
{
    constexpr std::uint64_t windowFrames = 1'000;
    constexpr std::size_t numBins = 100;
    SweepAnalyzer analyzer(1, numBins);
    analyzer.start(windowFrames);
    const std::vector<float> samples(1, 0.25f);

    std::uint64_t position = 0;
    for (const std::uint64_t target : {1ULL, 10ULL, 999ULL, 1'000ULL, 1'455ULL, 3'001ULL})
    {
        for (; position < target; ++position)
        {
            const float* const channel = samples.data();
            analyzer.process(position, std::span(&channel, 1), 1);
        }
        const auto& sweep = analyzer.buffer();
        const auto last = target - 1;
        const auto headBin = binOf(last, windowFrames, numBins);
        CAPTURE(target);
        CHECK(sweep.head() == headBin);
        CHECK(sweep.pass() == last / windowFrames + 1);
        for (std::size_t bin = 0; bin < numBins; ++bin)
        {
            const auto expectedPass = bin <= headBin ? sweep.pass() : sweep.pass() - 1;
            CHECK(sweep.passes()[bin] == expectedPass);
            CHECK(sweep.channel(0)[bin].isEmpty() == (expectedPass == 0));
        }
    }
}

TEST_CASE("Frames missing from the stream leave their bins empty", "[sweep]")
{
    constexpr std::uint64_t windowFrames = 1'000;
    constexpr std::size_t numBins = 100; // 10 frames per bin
    SweepAnalyzer analyzer(1, numBins);
    analyzer.start(windowFrames);
    const std::vector<std::vector<float>> full{std::vector<float>(windowFrames, 0.5f)};
    feed(analyzer, full, 100);
    REQUIRE(analyzer.buffer().pass() == 1);

    SECTION("a gap within a pass")
    {
        // Pass 2: frames 1000..1234 arrive, 1235..1604 are dropped, then 1605 onwards.
        const std::vector<std::vector<float>> before{std::vector<float>(235, -0.25f)};
        const std::vector<std::vector<float>> after{std::vector<float>(100, -0.75f)};
        feed(analyzer, before, 64, 1'000);
        feed(analyzer, after, 64, 1'605);

        const auto& sweep = analyzer.buffer();
        CHECK(sweep.pass() == 2);
        CHECK(sweep.head() == 70);
        const auto cells = sweep.channel(0);
        for (std::size_t bin = 0; bin < numBins; ++bin)
        {
            CAPTURE(bin);
            // Bins 23 and 60 are partly in the gap; the frames they did get are kept.
            if (bin <= 23)
                CHECK(cells[bin] == SweepCell{-0.25f, -0.25f});
            else if (bin < 60)
                CHECK(cells[bin].isEmpty());
            else if (bin <= 70)
                CHECK(cells[bin] == SweepCell{-0.75f, -0.75f});
            else
                CHECK(cells[bin] == SweepCell{0.5f, 0.5f});
            CHECK(sweep.passes()[bin] == (bin <= 70 ? 2u : 1u));
        }
    }

    SECTION("a gap of more than a window")
    {
        const std::vector<std::vector<float>> after{std::vector<float>(10, 0.125f)};
        feed(analyzer, after, 10, 3'500);
        const auto& sweep = analyzer.buffer();
        CHECK(sweep.pass() == 4);
        CHECK(sweep.head() == 50);
        for (std::size_t bin = 0; bin < numBins; ++bin)
        {
            CAPTURE(bin);
            CHECK(sweep.channel(0)[bin].isEmpty() == (bin != 50));
            CHECK(sweep.passes()[bin] == (bin <= 50 ? 4u : 3u));
        }
    }

    SECTION("a jump backwards starts over")
    {
        const auto generation = analyzer.buffer().generation();
        const std::vector<std::vector<float>> again{std::vector<float>(10, 0.125f)};
        feed(analyzer, again, 10, 0);
        const auto& sweep = analyzer.buffer();
        CHECK(sweep.generation() == generation + 1);
        CHECK(sweep.pass() == 1);
        CHECK(sweep.head() == 0);
        CHECK(sweep.channel(0)[0] == SweepCell{0.125f, 0.125f});
        for (std::size_t bin = 1; bin < numBins; ++bin)
            CHECK(sweep.channel(0)[bin].isEmpty());
    }
}

TEST_CASE("A window shorter than the bin count leaves the bins between frames empty", "[sweep]")
{
    constexpr std::uint64_t windowFrames = 10;
    constexpr std::size_t numBins = 32;
    const std::vector<std::vector<float>> input{std::vector<float>(25, 1.0f)};

    SweepAnalyzer analyzer(1, numBins);
    analyzer.start(windowFrames);
    feed(analyzer, input, 3);

    const auto& sweep = analyzer.buffer();
    for (std::size_t bin = 0; bin < numBins; ++bin)
    {
        const bool hasFrame = analyzer.binStartFrame(bin) < analyzer.binStartFrame(bin + 1);
        CAPTURE(bin);
        CHECK(sweep.channel(0)[bin].isEmpty() == !hasFrame);
    }
    CHECK(sweep.pass() == 3);
    CHECK(sweep.head() == binOf(24, windowFrames, numBins));
}

TEST_CASE("Each channel's sweep is the same whatever the channel count", "[sweep]")
{
    constexpr std::uint64_t windowFrames = 4'800;
    const auto signal = sine(440.0, 48'000.0, 0.7f, 7'000);

    SweepAnalyzer mono(1);
    mono.start(windowFrames);
    feed(mono, {signal}, 128);

    const auto numChannels = GENERATE(std::size_t{2}, std::size_t{6});
    CAPTURE(numChannels);
    std::vector<std::vector<float>> input(numChannels, std::vector<float>(signal.size(), 0.0f));
    for (std::size_t channel = 0; channel < numChannels; ++channel)
        for (std::size_t frame = 0; frame < signal.size(); ++frame)
            input[channel][frame] = signal[frame] * (channel % 2 == 0 ? 1.0f : -0.5f);

    SweepAnalyzer analyzer(numChannels);
    analyzer.start(windowFrames);
    feed(analyzer, input, 128);

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        const auto expected = mono.buffer().channel(0);
        const auto actual = analyzer.buffer().channel(channel);
        for (std::size_t bin = 0; bin < expected.size(); ++bin)
        {
            CAPTURE(channel, bin);
            if (channel % 2 == 0)
                REQUIRE(actual[bin] == expected[bin]);
            else
                REQUIRE(actual[bin] ==
                        SweepCell{expected[bin].max * -0.5f, expected[bin].min * -0.5f});
        }
    }
    CHECK(analyzer.buffer().passes().size() == mono.buffer().passes().size());
    CHECK(std::equal(analyzer.buffer().passes().begin(), analyzer.buffer().passes().end(),
                     mono.buffer().passes().begin()));
}

TEST_CASE("A null channel pointer is silence", "[sweep]")
{
    SweepAnalyzer analyzer(2, 8);
    analyzer.start(80);
    const std::vector<float> samples(80, 0.5f);
    const std::array<const float*, 2> channels{samples.data(), nullptr};
    analyzer.process(0, channels, samples.size());
    for (const auto& cell : analyzer.buffer().channel(1))
        CHECK(cell == SweepCell{0.0f, 0.0f});
}

TEST_CASE("A sweep with a window of 0 frames ignores its input", "[sweep]")
{
    SweepAnalyzer analyzer(1, 8);
    const std::vector<float> samples(80, 0.5f);
    const float* const channel = samples.data();
    analyzer.process(0, std::span(&channel, 1), samples.size());
    CHECK(analyzer.buffer().pass() == 0);

    analyzer.start(0);
    analyzer.process(0, std::span(&channel, 1), samples.size());
    CHECK(analyzer.buffer().pass() == 0);
}

TEST_CASE("SweepAnalyzer does not allocate while it runs", "[sweep][realtime]")
{
    SweepAnalyzer analyzer(2);
    const auto input = sine(1'000.0, sampleRate96k, 1.0f, 4'096);
    const std::array<const float*, 2> channels{input.data(), input.data()};
    STATIC_REQUIRE(noexcept(analyzer.setSampleRate(sampleRate96k)));
    STATIC_REQUIRE(noexcept(analyzer.start(window96k)));
    STATIC_REQUIRE(noexcept(analyzer.process(0, channels, input.size())));

    const AllocationCounter allocations;
    analyzer.setSampleRate(sampleRate96k);
    analyzer.start(window96k);
    for (std::uint64_t block = 0; block < 100; ++block)
        analyzer.process(block * 5'000, channels, input.size());
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}

namespace
{

/** The largest absolute value in the cells [first, end) of `band` of channel 0. */
float peakOf(const SweepBuffer& sweep, Band band, std::size_t first, std::size_t end)
{
    float peak = 0.0f;
    const auto cells = sweep.band(0, band);
    for (auto bin = first; bin < end; ++bin)
        if (!cells[bin].isEmpty())
            peak = std::max({peak, std::abs(cells[bin].min), std::abs(cells[bin].max)});
    return peak;
}

} // namespace

TEST_CASE("Without a sample rate, the split bands stay empty", "[sweep][bands]")
{
    SweepAnalyzer analyzer(1, 64);
    analyzer.start(6'400);
    feed(analyzer, {sine(50.0, 48'000.0, 0.5f, 6'400)}, 256);
    for (const auto band : splitBands)
    {
        CHECK(analyzer.bandDelayFrames(band) == 0.0);
        for (const auto& cell : analyzer.buffer().band(0, band))
            CHECK(cell.isEmpty());
    }
}

TEST_CASE("Each bin holds the min and max of every band", "[sweep][bands]")
{
    const auto sampleRate = GENERATE(44'100.0, 96'000.0, 192'000.0);
    CAPTURE(sampleRate);
    const auto windowFrames = freeRunningWindowFrames(sampleRate);
    constexpr float amplitude = 0.5f;

    struct Tone
    {
        double frequency;
        Band band;
    };
    for (const auto tone :
         {Tone{50.0, Band::low}, Tone{1'000.0, Band::mid}, Tone{8'000.0, Band::high}})
    {
        CAPTURE(tone.frequency);
        SweepAnalyzer analyzer(1);
        analyzer.setSampleRate(sampleRate);
        analyzer.start(windowFrames);
        feed(analyzer,
             {sine(tone.frequency, sampleRate, amplitude, static_cast<std::size_t>(windowFrames))},
             512);
        const auto& sweep = analyzer.buffer();

        // After the first half second, the filters have settled.
        constexpr auto numBins = SweepBuffer::defaultBinCount;
        for (const auto band : splitBands)
        {
            const auto peak = peakOf(sweep, band, numBins / 4, numBins);
            CAPTURE(static_cast<int>(band), peak);
            if (band == tone.band)
                CHECK_THAT(static_cast<double>(peak),
                           WithinAbs(static_cast<double>(amplitude), 0.02));
            else
                CHECK(peak < amplitude * 0.04f); // at least 28 dB down
        }
    }
}

TEST_CASE("The full band does not depend on the band split", "[sweep][bands]")
{
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> input(40'000);
    for (auto& sample : input)
        sample = noise(random);

    SweepAnalyzer fullOnly(1, 512);
    fullOnly.start(30'000);
    feed(fullOnly, {input}, 333);

    SweepAnalyzer split(1, 512);
    split.setSampleRate(48'000.0);
    split.start(30'000);
    feed(split, {input}, 333);

    const auto expected = fullOnly.buffer().channel(0);
    const auto actual = split.buffer().channel(0);
    CHECK(std::equal(expected.begin(), expected.end(), actual.begin(), actual.end()));
    CHECK_FALSE(split.buffer().band(0, Band::low)[0].isEmpty());
}

TEST_CASE("The bands do not depend on how the audio is split into blocks", "[sweep][bands]")
{
    constexpr std::uint64_t windowFrames = 9'600;
    std::mt19937 random(99);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<std::vector<float>> input(2, std::vector<float>(25'000));
    for (auto& channel : input)
        for (auto& sample : channel)
            sample = noise(random);

    SweepAnalyzer reference(2, 1'024);
    reference.setSampleRate(48'000.0);
    reference.start(windowFrames);
    feed(reference, input, input[0].size());

    const auto blockSize = GENERATE(std::size_t{1}, std::size_t{7}, std::size_t{480});
    CAPTURE(blockSize);
    SweepAnalyzer analyzer(2, 1'024);
    analyzer.setSampleRate(48'000.0);
    analyzer.start(windowFrames);
    feed(analyzer, input, blockSize);
    CHECK(analyzer.buffer() == reference.buffer());
}

TEST_CASE("Each channel's bands are the same whatever the channel count", "[sweep][bands]")
{
    constexpr std::uint64_t windowFrames = 4'800;
    const auto signal = sine(440.0, 48'000.0, 0.7f, 7'000);

    SweepAnalyzer mono(1);
    mono.setSampleRate(48'000.0);
    mono.start(windowFrames);
    feed(mono, {signal}, 128);

    const auto numChannels = GENERATE(std::size_t{2}, std::size_t{6});
    CAPTURE(numChannels);
    SweepAnalyzer analyzer(numChannels);
    analyzer.setSampleRate(48'000.0);
    analyzer.start(windowFrames);
    feed(analyzer, std::vector<std::vector<float>>(numChannels, signal), 128);

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        for (const auto band : splitBands)
        {
            const auto expected = mono.buffer().band(0, band);
            const auto actual = analyzer.buffer().band(channel, band);
            CAPTURE(channel, static_cast<int>(band));
            CHECK(std::equal(expected.begin(), expected.end(), actual.begin(), actual.end()));
        }
    }
}

TEST_CASE("A jump in the stream restarts the band split", "[sweep][bands]")
{
    constexpr std::uint64_t windowFrames = 48'000;
    constexpr std::size_t numBins = 480; // 100 frames per bin
    const auto tone = sine(60.0, 48'000.0, 0.5f, 10'000);

    // The same audio, after a gap and from the start of a fresh stream, gives the same bands.
    SweepAnalyzer afterGap(1, numBins);
    afterGap.setSampleRate(48'000.0);
    afterGap.start(windowFrames);
    feed(afterGap, {sine(3'000.0, 48'000.0, 0.9f, 10'000)}, 500);
    feed(afterGap, {tone}, 500, 20'000);

    SweepAnalyzer fresh(1, numBins);
    fresh.setSampleRate(48'000.0);
    fresh.start(windowFrames);
    feed(fresh, {tone}, 500, 20'000);

    for (const auto band : splitBands)
    {
        const auto expected = fresh.buffer().band(0, band);
        const auto actual = afterGap.buffer().band(0, band);
        for (std::size_t bin = 200; bin < 300; ++bin)
        {
            CAPTURE(static_cast<int>(band), bin);
            REQUIRE(actual[bin] == expected[bin]);
        }
    }
}

TEST_CASE("SweepAnalyzer reports the delay of each band at the sample rate", "[sweep][bands]")
{
    SweepAnalyzer analyzer(2);
    analyzer.setSampleRate(96'000.0);
    const BandSplitter splitter(96'000.0);
    for (const auto band : splitBands)
        CHECK(analyzer.bandDelayFrames(band) == splitter.delayFrames(band));
    CHECK(analyzer.bandDelayFrames(Band::full) == 0.0);

    analyzer.setSampleRate(0.0);
    CHECK(analyzer.bandDelayFrames(Band::low) == 0.0);
}
