#include "support/AllocationCounter.h"

#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <thread>
#include <vector>

using visona::AudioInputWriter;
using visona::AudioRingBuffer;
using visona::test::AllocationCounter;

namespace
{

constexpr std::uint64_t framesPerInput = 100'000;

/** A test signal that identifies both the input and the frame. It is exact in float and never 0,
    so it cannot be mistaken for silence. */
float inputSample(std::size_t input, std::uint64_t sampleIndex)
{
    return static_cast<float>((input + 1) * framesPerInput + sampleIndex % framesPerInput);
}

/** The input a sample of the test signal came from. */
std::size_t inputOf(float sample)
{
    return static_cast<std::size_t>(sample) / framesPerInput - 1;
}

/** Planar device input, as a device callback delivers it. */
class TestInputs
{
public:
    TestInputs(std::size_t numInputs, std::size_t maxFrames)
        : samples_(numInputs, std::vector<float>(maxFrames))
    {
        for (const auto& input : samples_)
            pointers_.push_back(input.data());
    }

    /** Fills the first numFrames frames of every input with the test signal from sampleIndex on. */
    void fill(std::uint64_t sampleIndex, std::uint32_t numFrames) noexcept
    {
        for (std::size_t input = 0; input < samples_.size(); ++input)
            for (std::uint32_t frame = 0; frame < numFrames; ++frame)
                samples_[input][frame] = inputSample(input, sampleIndex + frame);
    }

    [[nodiscard]] std::span<const float* const> pointers() const noexcept
    {
        return pointers_;
    }

private:
    std::vector<std::vector<float>> samples_;
    std::vector<const float*> pointers_;
};

/** Frames in `region` whose channel `channel` does not hold the test signal of `input`. */
std::size_t countMismatches(const AudioRingBuffer::ReadRegion& region, std::size_t channel,
                            std::size_t input)
{
    const auto samples = region.channel(channel);
    std::size_t mismatches = 0;
    for (std::size_t frame = 0; frame < samples.size(); ++frame)
        if (samples[frame] != inputSample(input, region.sampleIndex() + frame))
            ++mismatches;
    return mismatches;
}

/** Frames in `region` whose channel `channel` is not silent. */
std::size_t countNonSilent(const AudioRingBuffer::ReadRegion& region, std::size_t channel)
{
    std::size_t nonSilent = 0;
    for (const auto sample : region.channel(channel))
        if (sample != 0.0f)
            ++nonSilent;
    return nonSilent;
}

} // namespace

TEST_CASE("AudioInputWriter stores silence until a channel is routed", "[audio-input]")
{
    AudioRingBuffer ring(2, 64, 8);
    AudioInputWriter writer(ring);
    CHECK(writer.numChannels() == 2);
    CHECK(writer.routeOf(0) == AudioInputWriter::noInput);
    CHECK(writer.routeOf(1) == AudioInputWriter::noInput);

    TestInputs inputs(4, 16);
    inputs.fill(0, 16);
    REQUIRE(writer.write(inputs.pointers(), 16, 0));

    const auto region = ring.peek();
    REQUIRE(region.has_value());
    CHECK(countNonSilent(*region, 0) == 0);
    CHECK(countNonSilent(*region, 1) == 0);
}

TEST_CASE("AudioInputWriter copies any pair of inputs into the ring", "[audio-input]")
{
    // Inputs 9 and 10 of 12: a digital input that is not on inputs 1 and 2.
    const auto left = GENERATE(std::size_t{8}, std::size_t{0}, std::size_t{11}, std::size_t{9});
    const auto right = GENERATE(std::size_t{9}, std::size_t{1}, std::size_t{8}, std::size_t{9});
    CAPTURE(left, right);

    AudioRingBuffer ring(2, 64, 8);
    AudioInputWriter writer(ring);
    writer.route(0, static_cast<int>(left));
    writer.route(1, static_cast<int>(right));
    CHECK(writer.routeOf(0) == static_cast<int>(left));
    CHECK(writer.routeOf(1) == static_cast<int>(right));

    TestInputs inputs(12, 32);
    inputs.fill(0, 32);
    REQUIRE(writer.write(inputs.pointers(), 32, 0));

    const auto region = ring.peek();
    REQUIRE(region.has_value());
    REQUIRE(region->numFrames() == 32);
    CHECK(countMismatches(*region, 0, left) == 0);
    CHECK(countMismatches(*region, 1, right) == 0);
}

TEST_CASE("AudioInputWriter stores silence for an input the block does not have", "[audio-input]")
{
    AudioRingBuffer ring(3, 64, 8);
    AudioInputWriter writer(ring);
    TestInputs inputs(4, 16);
    inputs.fill(0, 16);

    writer.route(0, 4);
    writer.route(1, -7);
    writer.route(2, 1);
    CHECK(writer.routeOf(1) == AudioInputWriter::noInput);

    // A null pointer is an input the device did not deliver.
    std::vector<const float*> pointers(inputs.pointers().begin(), inputs.pointers().end());
    pointers[1] = nullptr;
    REQUIRE(writer.write(pointers, 16, 0));

    const auto region = ring.peek();
    REQUIRE(region.has_value());
    CHECK(countNonSilent(*region, 0) == 0);
    CHECK(countNonSilent(*region, 1) == 0);
    CHECK(countNonSilent(*region, 2) == 0);
}

TEST_CASE("AudioInputWriter applies a route change from the next block", "[audio-input]")
{
    AudioRingBuffer ring(2, 64, 8);
    AudioInputWriter writer(ring);
    TestInputs inputs(4, 16);
    writer.route(0, 0);
    writer.route(1, 1);

    inputs.fill(0, 16);
    REQUIRE(writer.write(inputs.pointers(), 16, 0));
    writer.route(0, 3);
    writer.route(1, 2);
    inputs.fill(16, 16);
    REQUIRE(writer.write(inputs.pointers(), 16, 0));

    const auto first = ring.peek();
    REQUIRE(first.has_value());
    REQUIRE(first->numFrames() == 16);
    CHECK(countMismatches(*first, 0, 0) == 0);
    CHECK(countMismatches(*first, 1, 1) == 0);
    ring.consume(16);

    const auto second = ring.peek();
    REQUIRE(second.has_value());
    REQUIRE(second->numFrames() == 16);
    CHECK(countMismatches(*second, 0, 3) == 0);
    CHECK(countMismatches(*second, 1, 2) == 0);
}

TEST_CASE("AudioInputWriter stamps every block with its timing", "[audio-input]")
{
    AudioRingBuffer ring(1, 256, 8);
    AudioInputWriter writer(ring);
    TestInputs inputs(1, 64);
    writer.route(0, 0);
    CHECK(writer.nextSampleIndex() == 0);

    constexpr std::array<std::uint32_t, 4> blockSizes{64, 13, 0, 40};
    std::uint64_t sampleIndex = 0;
    for (std::size_t block = 0; block < blockSizes.size(); ++block)
    {
        inputs.fill(sampleIndex, blockSizes[block]);
        REQUIRE(writer.write(inputs.pointers(), blockSizes[block], 1'000 + block));
        sampleIndex += blockSizes[block];
        CHECK(writer.nextSampleIndex() == sampleIndex);
    }

    // The empty block stores nothing, so three blocks come back.
    constexpr std::array<std::uint64_t, 3> expectedHostTimes{1'000, 1'001, 1'003};
    constexpr std::array<std::uint32_t, 3> expectedSizes{64, 13, 40};
    std::uint64_t expectedSampleIndex = 0;
    for (std::size_t block = 0; block < expectedSizes.size(); ++block)
    {
        const auto region = ring.peek();
        REQUIRE(region.has_value());
        CHECK(region->block().sampleIndex == expectedSampleIndex);
        CHECK(region->block().hostTimeNs == expectedHostTimes[block]);
        CHECK(region->block().numFrames == expectedSizes[block]);
        CHECK(countMismatches(*region, 0, 0) == 0);
        expectedSampleIndex += expectedSizes[block];
        ring.consume(region->numFrames());
    }
    CHECK_FALSE(ring.peek().has_value());
}

TEST_CASE("AudioInputWriter keeps counting frames through blocks the ring drops", "[audio-input]")
{
    AudioRingBuffer ring(2, 48, 8);
    AudioInputWriter writer(ring);
    TestInputs inputs(2, 32);
    writer.route(0, 0);
    writer.route(1, 1);

    inputs.fill(0, 32);
    REQUIRE(writer.write(inputs.pointers(), 32, 0));
    inputs.fill(32, 32);
    CHECK_FALSE(writer.write(inputs.pointers(), 32, 0));
    CHECK(ring.overrunCount() == 1);
    CHECK(ring.droppedFrameCount() == 32);
    CHECK(writer.nextSampleIndex() == 64);

    ring.consume(32);
    inputs.fill(64, 32);
    REQUIRE(writer.write(inputs.pointers(), 32, 0));

    // The consumer sees the dropped block as a jump in sampleIndex from 32 to 64.
    const auto afterGap = ring.peek();
    REQUIRE(afterGap.has_value());
    CHECK(afterGap->sampleIndex() == 64);
    CHECK(countMismatches(*afterGap, 0, 0) == 0);
    CHECK(countMismatches(*afterGap, 1, 1) == 0);
}

TEST_CASE("AudioInputWriter gives the same per-channel result for any channel count",
          "[audio-input]")
{
    const auto numChannels = GENERATE(std::size_t{1}, std::size_t{2}, std::size_t{6});
    CAPTURE(numChannels);

    constexpr std::size_t numInputs = 8;
    AudioRingBuffer ring(numChannels, 256, 16);
    AudioInputWriter writer(ring);
    for (std::size_t channel = 0; channel < numChannels; ++channel)
        writer.route(channel, static_cast<int>(numInputs - 1 - channel));

    TestInputs inputs(numInputs, 37);
    std::uint64_t written = 0;
    std::size_t mismatches = 0;
    for (std::uint32_t numFrames = 1; numFrames <= 37; numFrames += 6)
    {
        inputs.fill(written, numFrames);
        REQUIRE(writer.write(inputs.pointers(), numFrames, 0));
        written += numFrames;
        while (const auto region = ring.peek())
        {
            CHECK(region->numChannels() == numChannels);
            for (std::size_t channel = 0; channel < numChannels; ++channel)
                mismatches += countMismatches(*region, channel, numInputs - 1 - channel);
            ring.consume(region->numFrames());
        }
    }
    CHECK(writer.nextSampleIndex() == written);
    CHECK(mismatches == 0);
}

TEST_CASE("AudioInputWriter does not allocate after construction", "[audio-input][realtime]")
{
    AudioRingBuffer ring(2, 100, 8);
    AudioInputWriter writer(ring);
    TestInputs inputs(12, 64);
    std::size_t drops = 0;

    const AllocationCounter allocations;
    for (std::uint32_t numFrames = 1; numFrames <= 64; ++numFrames)
    {
        writer.route(0, static_cast<int>(numFrames % 12));
        writer.route(1, static_cast<int>(numFrames % 13) - 1);
        inputs.fill(writer.nextSampleIndex(), numFrames);
        if (!writer.write(inputs.pointers(), numFrames, numFrames))
            ++drops;
        if (numFrames % 3 == 0)
            while (const auto region = ring.peek())
                ring.consume(region->numFrames());
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(drops > 0);
}

TEST_CASE("AudioInputWriter hot paths are noexcept", "[audio-input][realtime]")
{
    AudioRingBuffer ring(1, 8, 2);
    AudioInputWriter writer(ring);
    const std::vector<const float*> inputs{nullptr};
    STATIC_REQUIRE(noexcept(writer.write(inputs, 1, 0)));
    STATIC_REQUIRE(noexcept(writer.route(0, 0)));
    STATIC_REQUIRE(noexcept(writer.routeOf(0)));
    STATIC_REQUIRE(noexcept(writer.nextSampleIndex()));
}

TEST_CASE("AudioInputWriter routes whole blocks while routes change on another thread",
          "[audio-input][stress]")
{
    constexpr std::size_t numChannels = 2;
    constexpr std::size_t numInputs = 12;
    constexpr std::uint64_t totalFrames = 1'000'000;
    constexpr std::uint32_t blockFrames = 32;
    AudioRingBuffer ring(numChannels, 1'000, 64);
    AudioInputWriter writer(ring);

    std::atomic<bool> producerDone{false};
    std::size_t producerAllocations = 0;
    std::thread producer(
        [&]
        {
            TestInputs inputs(numInputs, blockFrames);
            const AllocationCounter allocations;
            for (std::uint64_t sampleIndex = 0; sampleIndex < totalFrames;
                 sampleIndex += blockFrames)
            {
                inputs.fill(sampleIndex, blockFrames);
                static_cast<void>(writer.write(inputs.pointers(), blockFrames, sampleIndex));
                std::this_thread::yield();
            }
            producerAllocations = allocations.count();
            producerDone.store(true, std::memory_order_release);
        });

    std::thread router(
        [&]
        {
            std::minstd_rand random(5);
            std::uniform_int_distribution<int> anyInput(-1, static_cast<int>(numInputs));
            while (!producerDone.load(std::memory_order_acquire))
            {
                writer.route(random() % numChannels, anyInput(random));
                std::this_thread::yield();
            }
        });

    // Every block copies one input, or silence, per channel: a route never changes mid-block.
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t receivedFrames = 0;
    std::size_t backwardJumps = 0;
    std::size_t mixedRegions = 0;
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
        for (std::size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto samples = region->channel(channel);
            if (countNonSilent(*region, channel) == 0)
                continue;
            const auto input = inputOf(samples[0]);
            if (countNonSilent(*region, channel) != samples.size())
                ++mixedRegions;
            else
                mismatches += countMismatches(*region, channel, input);
        }
        receivedFrames += region->numFrames();
        nextSampleIndex = region->sampleIndex() + region->numFrames();
        ring.consume(region->numFrames());
    }
    producer.join();
    router.join();

    CHECK(backwardJumps == 0);
    CHECK(mixedRegions == 0);
    CHECK(mismatches == 0);
    CHECK(writer.nextSampleIndex() == totalFrames);
    CHECK(receivedFrames + ring.droppedFrameCount() == totalFrames);
    CHECK(producerAllocations == 0);
}
