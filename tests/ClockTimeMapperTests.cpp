#include "support/AllocationCounter.h"
#include "support/SyntheticClock.h"

#include <visona/BlockTiming.h>
#include <visona/ClockTimeMapper.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using visona::BlockTiming;
using visona::ClockTimeMapper;
using visona::test::AllocationCounter;
using visona::test::AudioClock;
using visona::test::blockTimings;

namespace
{

constexpr double sampleRate96k = 96'000.0;

// Host times like those of a Mac that has been up for about a day.
constexpr std::uint64_t streamStart = 86'400'000'000'000;

/** Mapping errors in frames: the mapped position minus the true one. */
struct ErrorStats
{
    std::size_t count = 0;
    double sum = 0.0;
    double sumAbsolute = 0.0;
    double largest = 0.0;

    void add(double error)
    {
        ++count;
        sum += error;
        sumAbsolute += std::abs(error);
        largest = std::max(largest, std::abs(error));
    }

    [[nodiscard]] double mean() const
    {
        return sum / static_cast<double>(count);
    }

    [[nodiscard]] double meanAbsolute() const
    {
        return sumAbsolute / static_cast<double>(count);
    }
};

} // namespace

TEST_CASE("ClockTimeMapper has no model before the first block", "[mapper]")
{
    ClockTimeMapper mapper(sampleRate96k);
    CHECK_FALSE(mapper.hasModel());
    CHECK_FALSE(mapper.sampleAt(streamStart).has_value());
    CHECK_FALSE(mapper.hostTimeAt(0.0).has_value());
    CHECK_FALSE(mapper.midiSampleTime(streamStart).has_value());
    CHECK_FALSE(mapper.measuredSampleRate().has_value());
    CHECK(mapper.blockCount() == 0);

    ClockTimeMapper noRate(0.0);
    noRate.addBlock({0, streamStart, 512});
    CHECK_FALSE(noRate.hasModel());
    CHECK_FALSE(noRate.sampleAt(streamStart).has_value());
}

TEST_CASE("With one block the mapper uses the nominal sample rate", "[mapper]")
{
    ClockTimeMapper mapper(sampleRate96k);
    mapper.addBlock({1'000, streamStart, 512});
    REQUIRE(mapper.hasModel());
    CHECK(mapper.sampleAt(streamStart) == 1'000.0);
    CHECK_THAT(*mapper.sampleAt(streamStart + 1'000'000'000), WithinAbs(97'000.0, 1.0e-6));
    CHECK_THAT(*mapper.sampleAt(streamStart - 500'000'000), WithinAbs(-47'000.0, 1.0e-6));
    CHECK(mapper.hostTimeAt(97'000.0) == streamStart + 1'000'000'000);
    CHECK(mapper.measuredSampleRate() == sampleRate96k);
}

TEST_CASE("Exact block timestamps map exactly, including the drift", "[mapper]")
{
    const auto sampleRate = GENERATE(44'100.0, 48'000.0, 96'000.0, 192'000.0);
    const auto blockSize = GENERATE(std::uint32_t{64}, std::uint32_t{512}, std::uint32_t{1'000});
    const auto driftPpm = GENERATE(-80.0, 0.0, 120.0);
    CAPTURE(sampleRate, blockSize, driftPpm);

    const AudioClock clock{sampleRate, streamStart, driftPpm};
    const auto blocks = blockTimings(clock, 3'000, blockSize);
    ClockTimeMapper mapper(sampleRate);

    double largestError = 0.0;
    for (std::size_t block = 0; block < blocks.size(); ++block)
    {
        mapper.addBlock(blocks[block]);
        if (block + 1 < ClockTimeMapper::minBlocksForFittedRate && driftPpm != 0.0)
            continue;

        // A MIDI event somewhere between the previous block and 10 ms from now.
        const auto newest = static_cast<double>(blocks[block].sampleIndex);
        for (const auto ahead : {-0.5 * blockSize, 0.0, 0.01 * sampleRate})
        {
            const auto truth = newest + ahead;
            const auto mapped = mapper.sampleAt(clock.hostTimeOf(truth));
            largestError = std::max(largestError, std::abs(*mapped - truth));
        }
    }
    CHECK(largestError < 1.0e-3);
    CHECK_THAT(*mapper.measuredSampleRate(),
               WithinRel(sampleRate / (1.0 + driftPpm * 1.0e-6), 1.0e-9));
    CHECK(mapper.blockCount() == ClockTimeMapper::defaultBlockCapacity);

    const auto roundTrip = *mapper.hostTimeAt(*mapper.sampleAt(blocks.back().hostTimeNs));
    CHECK(std::max(roundTrip, blocks.back().hostTimeNs) -
              std::min(roundTrip, blocks.back().hostTimeNs) <=
          1);
}

TEST_CASE("Under callback jitter and drift the mapping error averages under 1 sample", "[mapper]")
{
    const auto driftPpm = GENERATE(-80.0, 80.0);
    const auto blockSize = GENERATE(std::uint32_t{64}, std::uint32_t{512});
    const auto maxJitterUs = GENERATE(100.0, 250.0);
    CAPTURE(driftPpm, blockSize, maxJitterUs);

    const AudioClock clock{sampleRate96k, streamStart, driftPpm};
    const auto numBlocks = static_cast<std::size_t>(20.0 * sampleRate96k / blockSize);
    const auto blocks = blockTimings(clock, numBlocks, blockSize, maxJitterUs * 1'000.0, 7);

    ClockTimeMapper mapper(sampleRate96k);
    std::mt19937 random(11);
    std::uniform_real_distribution<double> when(-1.0, 2.0);
    ErrorStats warmUp;
    ErrorStats steady;
    for (std::size_t block = 0; block < blocks.size(); ++block)
    {
        mapper.addBlock(blocks[block]);

        // MIDI events from one block before the newest block's start to two blocks after it.
        for (int event = 0; event < 3; ++event)
        {
            const auto truth =
                static_cast<double>(blocks[block].sampleIndex) + when(random) * blockSize;
            const auto error = *mapper.sampleAt(clock.hostTimeOf(truth)) - truth;
            (block < ClockTimeMapper::defaultBlockCapacity ? warmUp : steady).add(error);
        }
    }

    WARN("Callback jitter ±" << maxJitterUs << " us, drift " << driftPpm << " ppm, " << blockSize
                             << "-frame blocks: steady-state error mean " << steady.mean()
                             << ", mean absolute " << steady.meanAbsolute() << ", largest "
                             << steady.largest << " samples; while the window fills, largest "
                             << warmUp.largest << " samples");
    CHECK(mapper.restartCount() == 0);
    CHECK(std::abs(steady.mean()) < 1.0);
    CHECK(steady.meanAbsolute() < 1.0);
    CHECK(steady.largest < 4.0);
    // While the window fills, a single block's jitter is the worst case.
    CHECK(warmUp.largest < maxJitterUs * 1.0e-6 * sampleRate96k + 2.0);
}

TEST_CASE("A block far off the line is skipped as an outlier", "[mapper]")
{
    const AudioClock clock{sampleRate96k, streamStart, 50.0};
    auto blocks = blockTimings(clock, 400, 512);
    blocks[300].hostTimeNs += 30'000'000; // a callback 30 ms late
    blocks[350].hostTimeNs -= 25'000'000;

    ClockTimeMapper mapper(sampleRate96k);
    double largestError = 0.0;
    for (std::size_t block = 0; block < blocks.size(); ++block)
    {
        mapper.addBlock(blocks[block]);
        if (block + 1 < ClockTimeMapper::minBlocksForFittedRate)
            continue;
        const auto truth = static_cast<double>(blocks[block].sampleIndex);
        largestError =
            std::max(largestError, std::abs(*mapper.sampleAt(clock.hostTimeOf(truth)) - truth));
    }
    CHECK(largestError < 0.01);
    CHECK(mapper.blockCount() == 398);
    CHECK(mapper.restartCount() == 0);
}

TEST_CASE("When the line moves the mapper starts over after four blocks off it", "[mapper]")
{
    const AudioClock before{sampleRate96k, streamStart, 0.0};
    const AudioClock after{sampleRate96k, streamStart + 3'000'000'000, 0.0};
    auto blocks = blockTimings(before, 200, 512);
    const auto moved = blockTimings(after, 500, 512);
    std::copy(moved.begin() + 200, moved.end(), std::back_inserter(blocks));

    ClockTimeMapper mapper(sampleRate96k);
    for (std::size_t block = 0; block < blocks.size(); ++block)
    {
        mapper.addBlock(blocks[block]);
        if (block == 202)
        {
            // Three blocks off the line are still treated as outliers.
            CHECK(mapper.restartCount() == 0);
            CHECK(mapper.blockCount() == 200);
        }
    }
    CHECK(mapper.restartCount() == 1);
    CHECK(mapper.blockCount() == 300 - 3);
    const auto truth = static_cast<double>(blocks.back().sampleIndex) + 100.0;
    CHECK_THAT(*mapper.sampleAt(after.hostTimeOf(truth)), WithinAbs(truth, 1.0e-3));
}

TEST_CASE("A block seen again is ignored, and a lower sampleIndex starts over", "[mapper]")
{
    const AudioClock clock{sampleRate96k, streamStart, 0.0};
    const auto blocks = blockTimings(clock, 10, 512);
    ClockTimeMapper mapper(sampleRate96k);
    for (const auto& block : blocks)
    {
        mapper.addBlock(block);
        mapper.addBlock(block);
        // A block read in parts shows its timing more than once, even with a changed host time.
        mapper.addBlock({block.sampleIndex, block.hostTimeNs + 50'000'000, block.numFrames});
    }
    CHECK(mapper.blockCount() == 10);
    CHECK(mapper.restartCount() == 0);

    const AudioClock newStream{sampleRate96k, streamStart + 60'000'000'000, 0.0};
    mapper.addBlock({0, newStream.streamStartNs, 512});
    CHECK(mapper.restartCount() == 1);
    CHECK(mapper.blockCount() == 1);
    CHECK_THAT(*mapper.sampleAt(newStream.hostTimeOf(1'000.0)), WithinAbs(1'000.0, 1.0e-3));
}

TEST_CASE("Blocks dropped by the audio ring do not disturb the mapping", "[mapper]")
{
    const AudioClock clock{sampleRate96k, streamStart, -30.0};
    const auto blocks = blockTimings(clock, 2'000, 480);
    ClockTimeMapper mapper(sampleRate96k);
    for (std::size_t block = 0; block < blocks.size(); ++block)
        if (block % 7 != 3 && (block < 900 || block > 1'100))
            mapper.addBlock(blocks[block]);

    CHECK(mapper.restartCount() == 0);
    const auto truth = static_cast<double>(blocks.back().sampleIndex) + 480.0;
    CHECK_THAT(*mapper.sampleAt(clock.hostTimeOf(truth)), WithinAbs(truth, 1.0e-3));
}

TEST_CASE("A MIDI event's sample time is the mapped time minus the latency offset", "[mapper]")
{
    ClockTimeMapper mapper(sampleRate96k);
    CHECK(mapper.latencyOffset() == 0.0);
    mapper.addBlock({10'000, streamStart, 512});
    const auto eventTime = streamStart + 5'000'000; // 480 frames after the block

    CHECK(mapper.midiSampleTime(eventTime) == mapper.sampleAt(eventTime));
    mapper.setLatencyOffset(123.5);
    CHECK(mapper.latencyOffset() == 123.5);
    CHECK_THAT(*mapper.midiSampleTime(eventTime), WithinAbs(10'480.0 - 123.5, 1.0e-6));
    mapper.setLatencyOffset(-40.0);
    CHECK_THAT(*mapper.midiSampleTime(eventTime), WithinAbs(10'520.0, 1.0e-6));

    // The offset is configuration and survives a new stream.
    mapper.reset(48'000.0);
    CHECK(mapper.latencyOffset() == -40.0);
    CHECK_FALSE(mapper.hasModel());
    mapper.addBlock({0, streamStart, 512});
    CHECK_THAT(*mapper.midiSampleTime(streamStart + 1'000'000'000), WithinAbs(48'040.0, 1.0e-6));
}

TEST_CASE("The mapper stays exact over a long stream with large host times", "[mapper]")
{
    // About 3 hours of 256-frame blocks at 96 kHz, with host times near 2^63.
    const AudioClock clock{sampleRate96k, 9'000'000'000'000'000'000, 35.0};
    ClockTimeMapper mapper(sampleRate96k, 256);
    constexpr std::uint64_t numBlocks = 4'000'000;
    double largestError = 0.0;
    for (std::uint64_t block = 0; block < numBlocks; ++block)
    {
        const auto sampleIndex = block * 256;
        mapper.addBlock({sampleIndex, clock.hostTimeOf(static_cast<double>(sampleIndex)), 256});
        if (block % 1'000 == 999)
        {
            const auto truth = static_cast<double>(sampleIndex) + 700.0;
            largestError =
                std::max(largestError, std::abs(*mapper.sampleAt(clock.hostTimeOf(truth)) - truth));
        }
    }
    CHECK(largestError < 1.0e-3);
    CHECK(mapper.blockCount() == 256);
}

TEST_CASE("reset() forgets the blocks and takes the new sample rate", "[mapper]")
{
    ClockTimeMapper mapper(sampleRate96k, 8);
    const auto blocks = blockTimings({sampleRate96k, streamStart, 0.0}, 20, 512);
    for (const auto& block : blocks)
        mapper.addBlock(block);
    CHECK(mapper.blockCount() == 8);

    mapper.reset(44'100.0);
    CHECK(mapper.blockCount() == 0);
    CHECK(mapper.restartCount() == 0);
    mapper.addBlock({0, streamStart, 512});
    CHECK(mapper.measuredSampleRate() == 44'100.0);
    // Earlier stream positions are no longer "seen", so frame 0 is not ignored as a repeat.
    CHECK(mapper.blockCount() == 1);
}

TEST_CASE("ClockTimeMapper needs room for at least 2 blocks", "[mapper]")
{
    CHECK_THROWS_AS(ClockTimeMapper(sampleRate96k, 1), std::invalid_argument);
    CHECK_NOTHROW(ClockTimeMapper(sampleRate96k, 2));
}

TEST_CASE("ClockTimeMapper does not allocate while it runs", "[mapper][realtime]")
{
    ClockTimeMapper mapper(sampleRate96k);
    const BlockTiming timing;
    STATIC_REQUIRE(noexcept(mapper.addBlock(timing)));
    STATIC_REQUIRE(noexcept(mapper.sampleAt(0)));
    STATIC_REQUIRE(noexcept(mapper.hostTimeAt(0.0)));
    STATIC_REQUIRE(noexcept(mapper.midiSampleTime(0)));
    STATIC_REQUIRE(noexcept(mapper.reset(sampleRate96k)));

    const auto blocks = blockTimings({sampleRate96k, streamStart, 20.0}, 5'000, 512, 100'000.0);
    double sum = 0.0;
    const AllocationCounter allocations;
    for (const auto& block : blocks)
    {
        mapper.addBlock(block);
        sum += mapper.midiSampleTime(block.hostTimeNs + 1'000'000).value_or(0.0);
    }
    mapper.reset(sampleRate96k);
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(sum > 0.0);
}
