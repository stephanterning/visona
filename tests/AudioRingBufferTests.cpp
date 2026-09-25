#include "support/AllocationCounter.h"

#include <visona/AudioRingBuffer.h>
#include <visona/BlockTiming.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

using visona::AudioRingBuffer;
using visona::BlockTiming;
using visona::test::AllocationCounter;

namespace
{

constexpr std::uint64_t hostTimeFor(std::uint64_t sampleIndex)
{
    return 5'000'000'000 + sampleIndex * 10'417;
}

/** A test signal that is unique per frame and channel, and exact in float. */
float expectedSample(std::uint64_t sampleIndex, std::size_t channel)
{
    return static_cast<float>((sampleIndex * 8 + channel) % (std::uint64_t{1} << 24));
}

/** Planar sample storage for building blocks to push. */
class TestBlock
{
public:
    TestBlock(std::size_t numChannels, std::size_t maxFrames)
        : samples_(numChannels, std::vector<float>(maxFrames))
    {
        for (const auto& channel : samples_)
            pointers_.push_back(channel.data());
    }

    /** Fills the first numFrames frames with the test signal from sampleIndex on, and returns the
        matching timing. */
    BlockTiming fill(std::uint64_t sampleIndex, std::uint32_t numFrames) noexcept
    {
        for (std::size_t channel = 0; channel < samples_.size(); ++channel)
            for (std::uint32_t frame = 0; frame < numFrames; ++frame)
                samples_[channel][frame] = expectedSample(sampleIndex + frame, channel);
        return {sampleIndex, hostTimeFor(sampleIndex), numFrames};
    }

    [[nodiscard]] std::span<const float* const> channels() const noexcept
    {
        return pointers_;
    }

private:
    std::vector<std::vector<float>> samples_;
    std::vector<const float*> pointers_;
};

/** Samples in `region` that differ from the test signal. */
std::size_t countMismatches(const AudioRingBuffer::ReadRegion& region) noexcept
{
    std::size_t mismatches = 0;
    for (std::size_t channel = 0; channel < region.numChannels(); ++channel)
    {
        const auto samples = region.channel(channel);
        for (std::size_t frame = 0; frame < samples.size(); ++frame)
            if (samples[frame] != expectedSample(region.sampleIndex() + frame, channel))
                ++mismatches;
    }
    return mismatches;
}

std::size_t randomBetween(std::minstd_rand& random, std::size_t low, std::size_t high)
{
    return std::uniform_int_distribution<std::size_t>(low, high)(random);
}

} // namespace

TEST_CASE("AudioRingBuffer rejects zero capacities and zero channels", "[audio-ring]")
{
    CHECK_THROWS_AS(AudioRingBuffer(0, 64, 4), std::invalid_argument);
    CHECK_THROWS_AS(AudioRingBuffer(2, 0, 4), std::invalid_argument);
    CHECK_THROWS_AS(AudioRingBuffer(2, 64, 0), std::invalid_argument);
}

TEST_CASE("AudioRingBuffer hands back a pushed block and its timing unchanged", "[audio-ring]")
{
    AudioRingBuffer ring(2, 64, 4);
    CHECK(ring.numChannels() == 2);
    CHECK(ring.capacityFrames() == 64);
    CHECK(ring.capacityBlocks() == 4);
    CHECK_FALSE(ring.peek().has_value());

    TestBlock block(2, 16);
    const auto timing = block.fill(1'000, 16);
    REQUIRE(ring.push(block.channels(), timing));

    const auto region = ring.peek();
    REQUIRE(region.has_value());
    CHECK(region->block() == timing);
    CHECK(region->sampleIndex() == 1'000);
    CHECK(region->numFrames() == 16);
    CHECK(region->numChannels() == 2);
    CHECK(region->channel(0).size() == 16);
    CHECK(region->channel(0)[3] == expectedSample(1'003, 0));
    CHECK(region->channel(1)[3] == expectedSample(1'003, 1));
    CHECK(countMismatches(*region) == 0);

    ring.consume(16);
    CHECK_FALSE(ring.peek().has_value());
    CHECK(ring.overrunCount() == 0);
    CHECK(ring.droppedFrameCount() == 0);
}

TEST_CASE("AudioRingBuffer lets the consumer read part of a block", "[audio-ring]")
{
    AudioRingBuffer ring(2, 64, 4);
    TestBlock block(2, 16);
    const auto timing = block.fill(1'000, 16);
    REQUIRE(ring.push(block.channels(), timing));

    ring.consume(5);
    const auto region = ring.peek();
    REQUIRE(region.has_value());
    CHECK(region->block() == timing);
    CHECK(region->sampleIndex() == 1'005);
    CHECK(region->numFrames() == 11);
    CHECK(countMismatches(*region) == 0);
}

TEST_CASE("AudioRingBuffer consumes across block boundaries", "[audio-ring]")
{
    AudioRingBuffer ring(1, 64, 4);
    TestBlock block(1, 10);
    for (std::uint64_t sampleIndex = 0; sampleIndex < 30; sampleIndex += 10)
        REQUIRE(ring.push(block.channels(), block.fill(sampleIndex, 10)));

    ring.consume(25);
    const auto region = ring.peek();
    REQUIRE(region.has_value());
    CHECK(region->block().sampleIndex == 20);
    CHECK(region->sampleIndex() == 25);
    CHECK(region->numFrames() == 5);
    CHECK(countMismatches(*region) == 0);

    ring.consume(5);
    CHECK_FALSE(ring.peek().has_value());
}

TEST_CASE("AudioRingBuffer splits a block where its storage wraps around", "[audio-ring]")
{
    AudioRingBuffer ring(2, 10, 4);
    TestBlock block(2, 8);
    REQUIRE(ring.push(block.channels(), block.fill(0, 6)));
    ring.consume(6);

    // Frames 6..13 occupy storage slots 6..9 and then 0..3.
    const auto timing = block.fill(6, 8);
    REQUIRE(ring.push(block.channels(), timing));

    const auto first = ring.peek();
    REQUIRE(first.has_value());
    CHECK(first->block() == timing);
    CHECK(first->sampleIndex() == 6);
    CHECK(first->numFrames() == 4);
    CHECK(countMismatches(*first) == 0);
    ring.consume(first->numFrames());

    const auto second = ring.peek();
    REQUIRE(second.has_value());
    CHECK(second->block() == timing);
    CHECK(second->sampleIndex() == 10);
    CHECK(second->numFrames() == 4);
    CHECK(countMismatches(*second) == 0);
    ring.consume(second->numFrames());

    CHECK_FALSE(ring.peek().has_value());
}

TEST_CASE("AudioRingBuffer drops a whole block that does not fit and keeps unread audio",
          "[audio-ring]")
{
    AudioRingBuffer ring(2, 32, 8);
    TestBlock block(2, 20);

    REQUIRE(ring.push(block.channels(), block.fill(0, 20)));
    CHECK_FALSE(ring.push(block.channels(), block.fill(20, 20)));
    CHECK(ring.overrunCount() == 1);
    CHECK(ring.droppedFrameCount() == 20);

    // Exactly fills the remaining space.
    REQUIRE(ring.push(block.channels(), block.fill(40, 12)));

    const auto unread = ring.peek();
    REQUIRE(unread.has_value());
    CHECK(unread->sampleIndex() == 0);
    CHECK(unread->numFrames() == 20);
    CHECK(countMismatches(*unread) == 0);
    ring.consume(20);

    REQUIRE(ring.push(block.channels(), block.fill(52, 20)));

    // The consumer sees the dropped block as a jump in sampleIndex from 20 to 40.
    const auto afterGap = ring.peek();
    REQUIRE(afterGap.has_value());
    CHECK(afterGap->sampleIndex() == 40);
    CHECK(afterGap->numFrames() == 12);
    CHECK(countMismatches(*afterGap) == 0);
    ring.consume(12);

    std::size_t framesAfterGap = 0;
    while (const auto region = ring.peek())
    {
        CHECK(region->block().sampleIndex == 52);
        CHECK(countMismatches(*region) == 0);
        framesAfterGap += region->numFrames();
        ring.consume(region->numFrames());
    }
    CHECK(framesAfterGap == 20);
    CHECK(ring.overrunCount() == 1);
}

TEST_CASE("AudioRingBuffer drops a block when its timing storage is full", "[audio-ring]")
{
    AudioRingBuffer ring(1, 100, 2);
    TestBlock block(1, 4);

    REQUIRE(ring.push(block.channels(), block.fill(0, 4)));
    REQUIRE(ring.push(block.channels(), block.fill(4, 4)));
    CHECK_FALSE(ring.push(block.channels(), block.fill(8, 4)));
    CHECK(ring.overrunCount() == 1);
    CHECK(ring.droppedFrameCount() == 4);

    ring.consume(4);
    REQUIRE(ring.push(block.channels(), block.fill(12, 4)));

    const auto second = ring.peek();
    REQUIRE(second.has_value());
    CHECK(second->sampleIndex() == 4);
    CHECK(countMismatches(*second) == 0);
    ring.consume(4);

    const auto third = ring.peek();
    REQUIRE(third.has_value());
    CHECK(third->sampleIndex() == 12);
    CHECK(countMismatches(*third) == 0);
}

TEST_CASE("AudioRingBuffer drops a block larger than its capacity", "[audio-ring]")
{
    AudioRingBuffer ring(1, 8, 4);
    TestBlock block(1, 9);
    CHECK_FALSE(ring.push(block.channels(), block.fill(0, 9)));
    CHECK(ring.overrunCount() == 1);
    CHECK(ring.droppedFrameCount() == 9);
    CHECK_FALSE(ring.peek().has_value());
}

TEST_CASE("AudioRingBuffer ignores a block with no frames", "[audio-ring]")
{
    AudioRingBuffer ring(2, 8, 4);
    TestBlock block(2, 1);
    CHECK(ring.push(block.channels(), block.fill(0, 0)));
    CHECK_FALSE(ring.peek().has_value());
    CHECK(ring.overrunCount() == 0);
}

TEST_CASE("AudioRingBuffer stores silence for a null channel pointer", "[audio-ring]")
{
    AudioRingBuffer ring(2, 8, 4);
    TestBlock block(2, 8);

    // Fill the whole storage with non-zero samples first.
    REQUIRE(ring.push(block.channels(), block.fill(1, 8)));
    ring.consume(8);

    const std::array<const float*, 2> channels{block.channels()[0], nullptr};
    REQUIRE(ring.push(channels, block.fill(1, 8)));

    const auto region = ring.peek();
    REQUIRE(region.has_value());
    REQUIRE(region->numFrames() == 8);
    for (std::size_t frame = 0; frame < 8; ++frame)
    {
        CHECK(region->channel(0)[frame] == expectedSample(1 + frame, 0));
        CHECK(region->channel(1)[frame] == 0.0f);
    }
}

TEST_CASE("AudioRingBuffer gives the same per-channel result for any channel count", "[audio-ring]")
{
    const auto numChannels = GENERATE(std::size_t{1}, std::size_t{2}, std::size_t{6});
    CAPTURE(numChannels);

    AudioRingBuffer ring(numChannels, 100, 16);
    TestBlock block(numChannels, 37);
    std::uint64_t pushed = 0;
    std::uint64_t consumed = 0;
    std::size_t mismatches = 0;
    for (std::uint32_t numFrames = 1; numFrames <= 37; numFrames += 6)
    {
        REQUIRE(ring.push(block.channels(), block.fill(pushed, numFrames)));
        pushed += numFrames;
        while (const auto region = ring.peek())
        {
            CHECK(region->numChannels() == numChannels);
            CHECK(region->sampleIndex() == consumed);
            mismatches += countMismatches(*region);
            consumed += region->numFrames();
            ring.consume(region->numFrames());
        }
    }
    CHECK(consumed == pushed);
    CHECK(mismatches == 0);
}

TEST_CASE("AudioRingBuffer does not allocate after construction", "[audio-ring][realtime]")
{
    AudioRingBuffer ring(2, 100, 8);
    TestBlock block(2, 64);
    std::uint64_t sampleIndex = 0;
    std::size_t regions = 0;
    std::size_t mismatches = 0;

    const AllocationCounter allocations;
    for (std::uint32_t numFrames = 1; numFrames <= 64; ++numFrames)
    {
        static_cast<void>(ring.push(block.channels(), block.fill(sampleIndex, numFrames)));
        static_cast<void>(ring.push(block.channels(), block.fill(sampleIndex + numFrames, 64)));
        sampleIndex += numFrames + 64;
        while (const auto region = ring.peek())
        {
            mismatches += countMismatches(*region);
            ring.consume(region->numFrames());
            ++regions;
        }
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(regions > 0);
    CHECK(mismatches == 0);
    CHECK(ring.overrunCount() > 0);
}

TEST_CASE("AudioRingBuffer hot paths are noexcept", "[audio-ring][realtime]")
{
    AudioRingBuffer ring(1, 8, 2);
    const std::array<const float*, 1> channels{nullptr};
    const BlockTiming timing;
    STATIC_REQUIRE(noexcept(ring.push(channels, timing)));
    STATIC_REQUIRE(noexcept(ring.peek()));
    STATIC_REQUIRE(noexcept(ring.consume(1)));
    STATIC_REQUIRE(noexcept(ring.overrunCount()));
    STATIC_REQUIRE(noexcept(ring.droppedFrameCount()));
    STATIC_REQUIRE(noexcept(ring.peek()->channel(0)));
}

TEST_CASE("AudioRingBuffer delivers every frame intact across threads when nothing is dropped",
          "[audio-ring][stress]")
{
    constexpr std::size_t numChannels = 3;
    constexpr std::uint64_t totalFrames = 2'000'000;
    constexpr std::uint32_t maxBlockFrames = 64;

    // A small capacity that is not a power of two makes the storage wrap at every offset.
    AudioRingBuffer ring(numChannels, 1'000, 16);

    std::uint64_t failedPushes = 0;
    std::size_t producerAllocations = 0;
    std::thread producer(
        [&]
        {
            TestBlock block(numChannels, maxBlockFrames);
            std::minstd_rand random(1);
            const AllocationCounter allocations;
            for (std::uint64_t sampleIndex = 0; sampleIndex < totalFrames;)
            {
                const auto numFrames = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    randomBetween(random, 1, maxBlockFrames), totalFrames - sampleIndex));
                const auto timing = block.fill(sampleIndex, numFrames);
                // Retry so that nothing is lost; the real audio thread never waits.
                while (!ring.push(block.channels(), timing))
                {
                    ++failedPushes;
                    std::this_thread::yield();
                }
                sampleIndex += numFrames;
            }
            producerAllocations = allocations.count();
        });

    std::minstd_rand random(2);
    std::uint64_t nextSampleIndex = 0;
    std::size_t discontinuities = 0;
    std::size_t wrongTimings = 0;
    std::size_t mismatches = 0;
    const AllocationCounter allocations;
    while (nextSampleIndex < totalFrames)
    {
        const auto region = ring.peek();
        if (!region)
        {
            std::this_thread::yield();
            continue;
        }
        if (region->sampleIndex() != nextSampleIndex)
            ++discontinuities;
        if (region->block().hostTimeNs != hostTimeFor(region->block().sampleIndex))
            ++wrongTimings;
        mismatches += countMismatches(*region);

        // Partial reads leave the rest of the region for the next peek().
        const auto consumed = randomBetween(random, 1, region->numFrames());
        ring.consume(consumed);
        nextSampleIndex = region->sampleIndex() + consumed;
    }
    const auto consumerAllocations = allocations.count();
    producer.join();

    CHECK(nextSampleIndex == totalFrames);
    CHECK(discontinuities == 0);
    CHECK(wrongTimings == 0);
    CHECK(mismatches == 0);
    CHECK_FALSE(ring.peek().has_value());
    CHECK(ring.overrunCount() == failedPushes);
    CHECK(producerAllocations == 0);
    CHECK(consumerAllocations == 0);
}

TEST_CASE("AudioRingBuffer accounts for every frame it drops across threads",
          "[audio-ring][stress]")
{
    constexpr std::size_t numChannels = 2;
    constexpr std::uint64_t totalFrames = 2'000'000;
    constexpr std::uint32_t maxBlockFrames = 64;
    AudioRingBuffer ring(numChannels, 256, 8);

    // Like the audio thread, the producer never waits: a block that does not fit is dropped.
    std::atomic<bool> producerDone{false};
    std::thread producer(
        [&]
        {
            TestBlock block(numChannels, maxBlockFrames);
            std::minstd_rand random(3);
            for (std::uint64_t sampleIndex = 0; sampleIndex < totalFrames;)
            {
                const auto numFrames = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    randomBetween(random, 1, maxBlockFrames), totalFrames - sampleIndex));
                static_cast<void>(ring.push(block.channels(), block.fill(sampleIndex, numFrames)));
                sampleIndex += numFrames;
            }
            producerDone.store(true, std::memory_order_release);
        });

    std::minstd_rand random(4);
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t receivedFrames = 0;
    std::uint64_t skippedFrames = 0;
    std::size_t backwardJumps = 0;
    std::size_t mismatches = 0;
    for (;;)
    {
        const bool done = producerDone.load(std::memory_order_acquire);
        const auto region = ring.peek();
        if (!region)
        {
            if (done)
                break;
            std::this_thread::yield();
            continue;
        }
        if (region->sampleIndex() < nextSampleIndex)
            ++backwardJumps;
        else
            skippedFrames += region->sampleIndex() - nextSampleIndex;
        mismatches += countMismatches(*region);

        const auto consumed = randomBetween(random, 1, region->numFrames());
        ring.consume(consumed);
        receivedFrames += consumed;
        nextSampleIndex = region->sampleIndex() + consumed;
    }
    producer.join();

    CHECK(backwardJumps == 0);
    CHECK(mismatches == 0);
    CHECK(receivedFrames + ring.droppedFrameCount() == totalFrames);
    CHECK(skippedFrames + (totalFrames - nextSampleIndex) == ring.droppedFrameCount());
}
