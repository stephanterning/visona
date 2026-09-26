#include "support/AllocationCounter.h"
#include "support/Signals.h"

#include <visona/AnalysisPipeline.h>
#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/SweepAnalyzer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

using visona::AnalysisPipeline;
using visona::AudioInputWriter;
using visona::AudioRingBuffer;
using visona::freeRunningWindowFrames;
using visona::SweepAnalyzer;
using visona::SweepCell;
using visona::test::AllocationCounter;
using visona::test::sine;

namespace
{

/** Pushes `channels` to `writer` in blocks of `blockSize` frames. */
void pushAll(AudioInputWriter& writer, const std::vector<std::vector<float>>& channels,
             std::size_t blockSize)
{
    const auto numFrames = channels.front().size();
    std::vector<const float*> pointers(channels.size());
    for (std::size_t start = 0; start < numFrames; start += blockSize)
    {
        const auto frames = std::min(blockSize, numFrames - start);
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        writer.write(pointers, static_cast<std::uint32_t>(frames), 0);
    }
}

std::vector<std::vector<float>> stereoTestSignal(double sampleRate, std::size_t numFrames)
{
    return {sine(220.0, sampleRate, 0.5f, numFrames), sine(3'000.0, sampleRate, 0.25f, numFrames)};
}

} // namespace

TEST_CASE("AnalysisPipeline publishes an empty snapshot while it follows no stream", "[analysis]")
{
    AnalysisPipeline pipeline(2, 256);
    CHECK(pipeline.numChannels() == 2);
    CHECK(pipeline.poll() == 0);

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    const auto& snapshot = snapshots.readBuffer();
    CHECK_FALSE(snapshot.hasStream);
    CHECK(snapshot.sweep.numChannels() == 2);
    CHECK(snapshot.sweep.numBins() == 256);
    CHECK(snapshot.sweep.pass() == 0);

    // Nothing changes, so nothing more is published.
    CHECK(pipeline.poll() == 0);
    CHECK_FALSE(snapshots.fetch());
}

TEST_CASE("AnalysisPipeline analyzes a stream into the same sweep as the analyzer", "[analysis]")
{
    constexpr double sampleRate = 48'000.0;
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    AnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    const auto input = stereoTestSignal(sampleRate, 30'000);
    SweepAnalyzer reference(2);
    reference.start(freeRunningWindowFrames(sampleRate));
    const std::array<const float*, 2> pointers{input[0].data(), input[1].data()};
    reference.process(0, pointers, input[0].size());

    auto& snapshots = pipeline.snapshots();
    std::size_t framesAnalyzed = 0;
    for (std::size_t part = 0; part < 3; ++part)
    {
        const std::vector<std::vector<float>> slice{
            {input[0].begin() + static_cast<std::ptrdiff_t>(part * 10'000),
             input[0].begin() + static_cast<std::ptrdiff_t>((part + 1) * 10'000)},
            {input[1].begin() + static_cast<std::ptrdiff_t>(part * 10'000),
             input[1].begin() + static_cast<std::ptrdiff_t>((part + 1) * 10'000)}};
        pushAll(writer, slice, 441);
        framesAnalyzed += pipeline.poll();
        REQUIRE(snapshots.fetch());
    }
    CHECK(framesAnalyzed == 30'000);

    const auto& snapshot = snapshots.readBuffer();
    CHECK(snapshot.hasStream);
    CHECK(snapshot.sampleRate == sampleRate);
    CHECK(snapshot.windowFrames == 96'000);
    CHECK(snapshot.nextSampleIndex == 30'000);
    CHECK(snapshot.overruns == 0);
    CHECK(snapshot.sweep == reference.buffer());

    CHECK(pipeline.takePeak(0) == std::ranges::max(input[0]));
    CHECK(pipeline.takePeak(1) == std::ranges::max(input[1]));
    CHECK(pipeline.takePeak(0) == 0.0f);
}

TEST_CASE("AnalysisPipeline shows a dropped block as a gap and counts it", "[analysis]")
{
    constexpr double sampleRate = 1'000.0; // a 2 000-frame window: 2 048 bins would not fit
    AudioRingBuffer ring(1, 256, 64);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    AnalysisPipeline pipeline(1, 200); // 10 frames per bin
    pipeline.setStream(&ring, sampleRate);

    const std::vector<std::vector<float>> block{std::vector<float>(200, 0.5f)};
    pushAll(writer, block, 200);
    pushAll(writer, block, 200); // dropped: only 56 frames are free
    pipeline.poll();
    pushAll(writer, block, 200);
    pipeline.poll();

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    const auto& snapshot = snapshots.readBuffer();
    CHECK(snapshot.overruns == 1);
    CHECK(snapshot.droppedFrames == 200);
    CHECK(snapshot.nextSampleIndex == 600);
    const auto cells = snapshot.sweep.channel(0);
    for (std::size_t bin = 0; bin < 60; ++bin)
    {
        CAPTURE(bin);
        CHECK(cells[bin].isEmpty() == (bin >= 20 && bin < 40));
    }
}

TEST_CASE("AnalysisPipeline starts a new, cleared sweep for every stream", "[analysis]")
{
    AudioRingBuffer first(2, 4'096, 64);
    AudioRingBuffer second(2, 4'096, 64);
    AudioInputWriter firstWriter(first);
    firstWriter.route(0, 0);

    AnalysisPipeline pipeline(2, 512);
    auto& snapshots = pipeline.snapshots();
    pipeline.setStream(&first, 44'100.0);
    pushAll(firstWriter, stereoTestSignal(44'100.0, 1'000), 100);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    const auto firstStream = snapshots.readBuffer().streamId;
    const auto firstGeneration = snapshots.readBuffer().sweep.generation();
    CHECK(snapshots.readBuffer().sweep.pass() == 1);

    pipeline.setStream(&second, 96'000.0);
    CHECK(pipeline.poll() == 0);
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().streamId == firstStream + 1);
    CHECK(snapshots.readBuffer().sweep.generation() > firstGeneration);
    CHECK(snapshots.readBuffer().sweep.pass() == 0);
    CHECK(snapshots.readBuffer().sampleRate == 96'000.0);
    CHECK(snapshots.readBuffer().windowFrames == 192'000);

    pipeline.setStream(nullptr, 0.0);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    CHECK_FALSE(snapshots.readBuffer().hasStream);
    CHECK(snapshots.readBuffer().windowFrames == 0);
}

TEST_CASE("AnalysisPipeline does not allocate while it runs", "[analysis][realtime]")
{
    AudioRingBuffer ring(2, 96'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);
    AnalysisPipeline pipeline(2);
    const auto input = stereoTestSignal(96'000.0, 480);
    const std::array<const float*, 2> pointers{input[0].data(), input[1].data()};
    STATIC_REQUIRE(noexcept(pipeline.setStream(&ring, 96'000.0)));
    STATIC_REQUIRE(noexcept(pipeline.poll()));

    const AllocationCounter allocations;
    pipeline.setStream(&ring, 96'000.0);
    for (int block = 0; block < 500; ++block)
    {
        writer.write(pointers, 480, 0);
        if (block % 3 == 0)
            pipeline.poll();
    }
    pipeline.poll();
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}

namespace
{

constexpr std::size_t stressBins = 256;
constexpr std::uint64_t stressWindowFrames = 4'800; // 2 s at 2 400 Hz
constexpr std::uint64_t stressPassCodes = 7;

/** A signal whose value names the pass and bin each frame belongs to, so the consumer can check
    that every cell of a snapshot agrees with the bin's pass stamp. */
float stressSample(std::uint64_t position)
{
    const auto pass = position / stressWindowFrames + 1;
    const auto bin = position % stressWindowFrames * stressBins / stressWindowFrames;
    return static_cast<float>((pass % stressPassCodes) * stressBins + bin + 1);
}

} // namespace

TEST_CASE("AnalysisPipeline hands consistent snapshots across threads", "[analysis][stress]")
{
    constexpr double sampleRate = 2'400.0;
    constexpr std::uint64_t totalFrames = 600'000;
    constexpr std::uint32_t blockSize = 32;

    AudioRingBuffer ring(1, 2'048, 128);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    AnalysisPipeline pipeline(1, stressBins);
    pipeline.setStream(&ring, sampleRate);

    // The ring may drop blocks if the analysis thread falls behind; the snapshots must stay
    // consistent either way.
    std::atomic<bool> producerDone{false};
    std::uint64_t endOfLastDeliveredBlock = 0;
    std::thread producer(
        [&]
        {
            std::array<float, blockSize> block{};
            for (std::uint64_t position = 0; position < totalFrames; position += blockSize)
            {
                for (std::uint32_t frame = 0; frame < blockSize; ++frame)
                    block[frame] = stressSample(position + frame);
                const float* const channel = block.data();
                if (writer.write(std::span(&channel, 1), blockSize, 0))
                    endOfLastDeliveredBlock = position + blockSize;
                std::this_thread::yield();
            }
            producerDone.store(true, std::memory_order_release);
        });

    std::atomic<bool> analysisDone{false};
    std::size_t analysisAllocations = 0;
    std::thread analysis(
        [&]
        {
            const AllocationCounter allocations;
            while (!producerDone.load(std::memory_order_acquire))
            {
                pipeline.poll();
                std::this_thread::yield();
            }
            pipeline.poll();
            analysisAllocations = allocations.count();
            analysisDone.store(true, std::memory_order_release);
        });

    std::size_t fetches = 0;
    std::size_t inconsistentCells = 0;
    std::size_t inconsistentPasses = 0;
    std::size_t backwards = 0;
    std::uint64_t lastPosition = 0;
    auto& snapshots = pipeline.snapshots();
    const auto check = [&]
    {
        ++fetches;
        const auto& sweep = snapshots.readBuffer().sweep;
        if (sweep.pass() == 0)
            return;
        const auto position = sweep.pass() * stressBins + sweep.head();
        if (position < lastPosition)
            ++backwards;
        lastPosition = position;

        const auto cells = sweep.channel(0);
        const auto passes = sweep.passes();
        for (std::size_t bin = 0; bin < stressBins; ++bin)
        {
            const auto expectedPass = bin <= sweep.head() ? sweep.pass() : sweep.pass() - 1;
            if (passes[bin] != expectedPass)
                ++inconsistentPasses;
            const auto expected =
                static_cast<float>((passes[bin] % stressPassCodes) * stressBins + bin + 1);
            if (!cells[bin].isEmpty() && (cells[bin].min != expected || cells[bin].max != expected))
                ++inconsistentCells;
        }
    };

    while (!analysisDone.load(std::memory_order_acquire))
        if (snapshots.fetch())
            check();
    if (snapshots.fetch())
        check();
    producer.join();
    analysis.join();

    CHECK(fetches > 0);
    CHECK(inconsistentCells == 0);
    CHECK(inconsistentPasses == 0);
    CHECK(backwards == 0);
    CHECK(analysisAllocations == 0);
    CHECK(snapshots.readBuffer().nextSampleIndex == endOfLastDeliveredBlock);
}
