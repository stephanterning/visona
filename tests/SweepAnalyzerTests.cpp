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
using visona::freeRunningWindowFrames;
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
    STATIC_REQUIRE(noexcept(analyzer.start(window96k)));
    STATIC_REQUIRE(noexcept(analyzer.process(0, channels, input.size())));

    const AllocationCounter allocations;
    analyzer.start(window96k);
    for (std::uint64_t block = 0; block < 100; ++block)
        analyzer.process(block * 5'000, channels, input.size());
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}

namespace
{

using visona::TransportSpan;

/** Musical spans for clock ticks at the given frame times, tick `firstTick` at the first. */
std::vector<TransportSpan> musicalSpans(const std::vector<double>& tickTimes,
                                        double firstTick = 0.0)
{
    std::vector<TransportSpan> spans;
    for (std::size_t tick = 0; tick + 1 < tickTimes.size(); ++tick)
        spans.push_back({TransportSpan::Kind::musical, tickTimes[tick], tickTimes[tick + 1],
                         firstTick + static_cast<double>(tick),
                         firstTick + static_cast<double>(tick + 1), 1});
    return spans;
}

/** Evenly spaced clock ticks: `count` of them, `interval` frames apart, from `first` on. */
std::vector<double> evenTicks(double first, std::size_t count, double interval)
{
    std::vector<double> times;
    for (std::size_t tick = 0; tick < count; ++tick)
        times.push_back(first + static_cast<double>(tick) * interval);
    return times;
}

/**
    Feeds the frames of `spans` to `analyzer` in blocks of `blockSize` frames, split where the
    spans end, as the analysis thread does. `sample(frame)` gives each frame's value in every
    channel.
*/
template <typename Signal>
void feedMusical(SweepAnalyzer& analyzer, const std::vector<TransportSpan>& spans,
                 std::size_t blockSize, Signal sample)
{
    std::vector<float> samples(blockSize);
    std::vector<const float*> channels(analyzer.buffer().numChannels(), samples.data());
    for (const auto& span : spans)
    {
        auto frame = static_cast<std::uint64_t>(std::ceil(span.start));
        const auto end = static_cast<std::uint64_t>(std::ceil(span.end));
        while (frame < end)
        {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(blockSize - frame % blockSize, end - frame));
            for (std::size_t i = 0; i < count; ++i)
                samples[i] = sample(frame + i);
            analyzer.processMusical(frame, channels, count, span);
            frame += count;
        }
    }
}

} // namespace

TEST_CASE("The musical sweep puts every beat at its quarter of a one-bar window", "[sweep]")
{
    const auto bpm = GENERATE(120.0, 126.0, 174.0);
    CAPTURE(bpm);
    const auto interval = 60.0 * sampleRate96k / (bpm * 24.0);
    const auto ticks = evenTicks(10'000.0, 97, interval);

    SweepAnalyzer analyzer(1);
    analyzer.startMusical(96.0);
    CHECK(analyzer.isMusical());
    feedMusical(analyzer, musicalSpans(ticks), 512,
                [&](std::uint64_t frame)
                {
                    for (std::size_t beat = 0; beat < 4; ++beat)
                        if (frame == static_cast<std::uint64_t>(std::ceil(ticks[beat * 24])))
                            return 1.0f;
                    return 0.0f;
                });

    const auto cells = analyzer.buffer().channel(0);
    for (std::size_t bin = 0; bin < cells.size(); ++bin)
    {
        CAPTURE(bin);
        const bool beatBin = bin % 1'024 <= 1;
        if (!beatBin)
            REQUIRE(cells[bin].max == 0.0f);
    }
    for (std::size_t beat = 0; beat < 4; ++beat)
        CHECK(std::max(cells[beat * 1'024].max, cells[beat * 1'024 + 1].max) == 1.0f);
    CHECK(analyzer.buffer().pass() == 1);
}

TEST_CASE("The musical sweep does not depend on how the audio is split into blocks", "[sweep]")
{
    const auto ticks = evenTicks(123.4, 200, 60.0 * 96'000.0 / (126.0 * 24.0));
    const auto spans = musicalSpans(ticks);
    const auto signal = [](std::uint64_t frame)
    { return static_cast<float>(std::sin(static_cast<double>(frame) * 0.013)); };

    SweepAnalyzer reference(1, 1'024);
    reference.startMusical(24.0);
    feedMusical(reference, spans, 100'000, signal);

    const auto blockSize =
        GENERATE(std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{333});
    CAPTURE(blockSize);
    SweepAnalyzer analyzer(1, 1'024);
    analyzer.startMusical(24.0);
    feedMusical(analyzer, spans, blockSize, signal);
    CHECK(analyzer.buffer() == reference.buffer());
}

TEST_CASE("A tempo ramp leaves no holes and no bin written twice", "[sweep]")
{
    // From 120 to 140 BPM over 4 bars, in a one-bar window. Each frame's value is its position in
    // ticks, so every bin must hold exactly the positions of its own slice of its own window.
    std::vector<double> ticks{5'000.0};
    const auto totalTicks = 4 * 96;
    for (int tick = 0; tick < totalTicks + 10; ++tick)
    {
        const auto bpm = 120.0 + 20.0 * std::min(1.0, tick / static_cast<double>(totalTicks));
        ticks.push_back(ticks.back() + 60.0 * sampleRate96k / (bpm * 24.0));
    }
    const auto spans = musicalSpans(ticks);

    SweepAnalyzer analyzer(1);
    analyzer.startMusical(96.0);
    feedMusical(analyzer, spans, 480,
                [&](std::uint64_t frame)
                {
                    for (const auto& span : spans)
                        if (static_cast<double>(frame) < span.end)
                            return static_cast<float>(span.tickAt(static_cast<double>(frame)));
                    return 0.0f;
                });

    const auto& sweep = analyzer.buffer();
    const auto head = sweep.head();
    const auto currentWindow = std::floor(analyzer.windowStartTick() / 96.0);
    for (std::size_t bin = 0; bin < sweep.numBins(); ++bin)
    {
        CAPTURE(bin);
        const auto& cell = sweep.channel(0)[bin];
        REQUIRE_FALSE(cell.isEmpty());
        const auto window = bin <= head ? currentWindow : currentWindow - 1.0;
        const auto low = (window + static_cast<double>(bin) / 4'096.0) * 96.0;
        const auto high = (window + static_cast<double>(bin + 1) / 4'096.0) * 96.0;
        REQUIRE(static_cast<double>(cell.min) >= low - 1.0e-3);
        REQUIRE(static_cast<double>(cell.max) <= high + 1.0e-3);
    }
    CHECK(sweep.pass() == 5);
}

TEST_CASE("A relocation after a freeze jumps the head without emptying anything", "[sweep]")
{
    constexpr double interval = 2'000.0;
    SweepAnalyzer analyzer(1);
    analyzer.startMusical(96.0);
    feedMusical(analyzer, musicalSpans(evenTicks(0.0, 161, interval)), 512,
                [](std::uint64_t) { return 0.5f; });
    REQUIRE(analyzer.buffer().pass() == 2);
    const auto headBefore = analyzer.buffer().head();
    CHECK(analyzer.windowStartTick() == 96.0);

    analyzer.freeze();
    SECTION("to bar 18, beat 2")
    {
        const double tick = 17 * 96 + 24;
        feedMusical(analyzer, musicalSpans(evenTicks(1'000'000.0, 3, interval), tick), 512,
                    [](std::uint64_t) { return -0.5f; });
        const auto& sweep = analyzer.buffer();
        CHECK(sweep.pass() == 3);
        CHECK(analyzer.windowStartTick() == 17 * 96.0);
        CHECK(sweep.passes()[1'024] == 3);
        CHECK(sweep.channel(0)[1'024].min == -0.5f);
        // The rest of the old passes stays as it was.
        CHECK(sweep.passes()[1'000] == 2);
        CHECK(sweep.channel(0)[1'000] == SweepCell{0.5f, 0.5f});
        CHECK(sweep.passes()[headBefore + 1] == 1);
        CHECK(sweep.channel(0)[headBefore + 1] == SweepCell{0.5f, 0.5f});
    }

    SECTION("carrying on where it stopped")
    {
        feedMusical(analyzer, musicalSpans(evenTicks(1'000'000.0, 3, interval), 160.0), 512,
                    [](std::uint64_t) { return -0.5f; });
        CHECK(analyzer.buffer().pass() == 2);
        CHECK(analyzer.buffer().head() > headBefore);
    }
}

TEST_CASE("Frames missing without a freeze leave their bins empty in musical time", "[sweep]")
{
    constexpr double interval = 2'000.0;
    const auto spans = musicalSpans(evenTicks(0.0, 41, interval));
    SweepAnalyzer analyzer(1);
    analyzer.startMusical(96.0);
    // Ticks 0 to 20, then the frames of ticks 20 to 30 are dropped, then ticks 30 to 40.
    feedMusical(analyzer, {spans.begin(), spans.begin() + 20}, 512,
                [](std::uint64_t) { return 0.5f; });
    feedMusical(analyzer, {spans.begin() + 30, spans.end()}, 512,
                [](std::uint64_t) { return 0.5f; });

    const auto& sweep = analyzer.buffer();
    CHECK(sweep.pass() == 1);
    const auto binOfTick = [](double tick)
    { return static_cast<std::size_t>(tick / 96.0 * 4'096.0); };
    for (auto bin = binOfTick(20.0) + 1; bin < binOfTick(30.0); ++bin)
    {
        CAPTURE(bin);
        REQUIRE(sweep.channel(0)[bin].isEmpty());
        REQUIRE(sweep.passes()[bin] == 1);
    }
    CHECK_FALSE(sweep.channel(0)[binOfTick(35.0)].isEmpty());
}

TEST_CASE("Starting a musical sweep clears the buffer", "[sweep]")
{
    SweepAnalyzer analyzer(1);
    analyzer.start(1'000);
    const std::vector<float> samples(1'000, 0.5f);
    const float* const channel = samples.data();
    analyzer.process(0, std::span(&channel, 1), samples.size());
    const auto generation = analyzer.buffer().generation();

    analyzer.startMusical(24.0);
    CHECK(analyzer.buffer().generation() == generation + 1);
    CHECK(analyzer.buffer().pass() == 0);
    CHECK(analyzer.windowFrames() == 0);
    CHECK(analyzer.windowTicks() == 24.0);
    CHECK(analyzer.windowStartTick() == 0.0);

    analyzer.start(1'000);
    CHECK_FALSE(analyzer.isMusical());
}
