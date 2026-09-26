#include "support/AllocationCounter.h"

#include <visona/ClockTimeMapper.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>

using visona::BlockTiming;
using visona::ClockTimeMapper;
using visona::test::AllocationCounter;

namespace
{

constexpr double nominalRate = 96'000.0;
constexpr std::uint32_t blockFrames = 512;
constexpr std::uint64_t hostStartNs = 5'000'000'000'000; // well into the host clock's range

/**
    Synthetic audio blocks: the true host time of frame s is hostStartNs + s / actualRate seconds,
    and each block's timestamp is off by uniform jitter up to ±jitterNs.
*/
class SyntheticStream
{
public:
    SyntheticStream(double actualRate, double jitterNs, unsigned seed)
        : actualRate_(actualRate)
        , jitter_(-jitterNs, jitterNs)
        , random_(seed)
    {
    }

    [[nodiscard]] double trueHostTimeNs(double sampleTime) const
    {
        return static_cast<double>(hostStartNs) + sampleTime / actualRate_ * 1.0e9;
    }

    [[nodiscard]] double trueSampleTime(double hostTimeNs) const
    {
        return (hostTimeNs - static_cast<double>(hostStartNs)) * actualRate_ / 1.0e9;
    }

    BlockTiming next()
    {
        const auto hostTime = trueHostTimeNs(static_cast<double>(sampleIndex_)) + jitter_(random_);
        const BlockTiming block{sampleIndex_, static_cast<std::uint64_t>(std::llround(hostTime)),
                                blockFrames};
        sampleIndex_ += blockFrames;
        return block;
    }

    [[nodiscard]] std::uint64_t sampleIndex() const
    {
        return sampleIndex_;
    }

    std::mt19937& random()
    {
        return random_;
    }

private:
    double actualRate_;
    std::uniform_real_distribution<double> jitter_;
    std::mt19937 random_;
    std::uint64_t sampleIndex_ = 0;
};

struct MappingError
{
    double meanAbs = 0.0;
    double maxAbs = 0.0;
};

/**
    Feeds `seconds` of blocks and, after each, maps a MIDI event at a random true time between
    20 ms before the newest block and one block after it, the window where MIDI events land.
*/
MappingError measure(SyntheticStream& stream, ClockTimeMapper& mapper, double seconds)
{
    std::uniform_real_distribution<double> offset(-0.020 * nominalRate, blockFrames);
    double totalAbs = 0.0;
    double maxAbs = 0.0;
    std::size_t count = 0;
    const auto blocks = static_cast<std::size_t>(seconds * nominalRate / blockFrames);
    for (std::size_t block = 0; block < blocks; ++block)
    {
        const auto timing = stream.next();
        mapper.addBlock(timing);
        if (block < blocks / 5) // let the history fill first
            continue;
        const auto eventSample = static_cast<double>(timing.sampleIndex) + offset(stream.random());
        const auto hostTime = static_cast<std::uint64_t>(stream.trueHostTimeNs(eventSample));
        const auto error =
            mapper.sampleTimeOf(hostTime) - stream.trueSampleTime(static_cast<double>(hostTime));
        totalAbs += std::abs(error);
        maxAbs = std::max(maxAbs, std::abs(error));
        ++count;
    }
    return {totalAbs / static_cast<double>(count), maxAbs};
}

} // namespace

TEST_CASE("ClockTimeMapper maps through the nominal rate until it can fit a line", "[clock]")
{
    ClockTimeMapper mapper(nominalRate);
    CHECK_FALSE(mapper.isReady());
    CHECK_THROWS_AS(ClockTimeMapper(nominalRate, 1), std::invalid_argument);

    mapper.addBlock({1'000, hostStartNs, blockFrames});
    REQUIRE(mapper.isReady());
    CHECK(mapper.sampleTimeOf(hostStartNs) == 1'000.0);
    CHECK(mapper.sampleTimeOf(hostStartNs + 10'000'000) == 1'960.0); // 10 ms at 96 kHz
    CHECK(mapper.framesPerSecond() == nominalRate);
}

TEST_CASE("ClockTimeMapper is exact for perfect timestamps", "[clock]")
{
    SyntheticStream stream(nominalRate, 0.0, 1);
    ClockTimeMapper mapper(nominalRate);
    const auto error = measure(stream, mapper, 5.0);
    CHECK(error.maxAbs < 1.0e-3);
}

TEST_CASE("ClockTimeMapper averages out callback jitter to under a sample", "[clock]")
{
    // Jitter of ±100 µs is ±9.6 frames per block timestamp.
    const auto seed = GENERATE(1u, 2u, 3u);
    SyntheticStream stream(nominalRate, 100'000.0, seed);
    ClockTimeMapper mapper(nominalRate);
    const auto error = measure(stream, mapper, 20.0);
    CAPTURE(seed, error.meanAbs, error.maxAbs);
    CHECK(error.meanAbs < 1.0);
    CHECK(error.maxAbs < 4.0);
}

TEST_CASE("ClockTimeMapper follows drift between the audio clock and the host clock", "[clock]")
{
    // The audio clock runs 100 ppm fast relative to the host clock.
    SyntheticStream stream(nominalRate * 1.0001, 50'000.0, 4);
    ClockTimeMapper mapper(nominalRate);
    const auto error = measure(stream, mapper, 30.0);
    CAPTURE(error.meanAbs, error.maxAbs);
    CHECK(error.meanAbs < 1.0);
    CHECK(error.maxAbs < 3.0);
    CHECK(std::abs(mapper.framesPerSecond() - nominalRate * 1.0001) < 0.5);
}

TEST_CASE("ClockTimeMapper stays bounded under heavy jitter", "[clock]")
{
    // ±1 ms, as with the fallback clock on a loaded system.
    SyntheticStream stream(nominalRate, 1'000'000.0, 5);
    ClockTimeMapper mapper(nominalRate);
    const auto error = measure(stream, mapper, 20.0);
    CAPTURE(error.meanAbs, error.maxAbs);
    CHECK(error.meanAbs < 8.0);
    CHECK(error.maxAbs < 40.0);
}

TEST_CASE("ClockTimeMapper ignores blocks the ring dropped", "[clock]")
{
    SyntheticStream stream(nominalRate, 20'000.0, 6);
    ClockTimeMapper mapper(nominalRate);
    for (int block = 0; block < 2'000; ++block)
    {
        const auto timing = stream.next();
        if (block % 7 != 3) // a dropped block's timing never reaches the analysis
            mapper.addBlock(timing);
    }
    const auto sample = static_cast<double>(stream.sampleIndex());
    const auto hostTime = static_cast<std::uint64_t>(stream.trueHostTimeNs(sample));
    CHECK(std::abs(mapper.sampleTimeOf(hostTime) - sample) < 1.0);
}

TEST_CASE("ClockTimeMapper starts over when a block goes back", "[clock]")
{
    ClockTimeMapper mapper(nominalRate);
    for (std::uint64_t block = 0; block < 100; ++block)
        mapper.addBlock({block * blockFrames, hostStartNs + block * 5'333'333, blockFrames});
    REQUIRE(mapper.numBlocks() == 100);

    mapper.addBlock({0, hostStartNs + 900'000'000, blockFrames});
    CHECK(mapper.numBlocks() == 1);
    CHECK(mapper.sampleTimeOf(hostStartNs + 900'000'000) == 0.0);

    mapper.reset(48'000.0);
    CHECK_FALSE(mapper.isReady());
}

TEST_CASE("ClockTimeMapper keeps only the recent history", "[clock]")
{
    ClockTimeMapper mapper(nominalRate, 4'096, 1.0);
    SyntheticStream stream(nominalRate, 0.0, 7);
    for (int block = 0; block < 1'000; ++block)
        mapper.addBlock(stream.next());
    // One second of 512-frame blocks at 96 kHz, plus the block at the start of that second.
    CHECK(mapper.numBlocks() == 188);
}

TEST_CASE("ClockTimeMapper does not allocate", "[clock][realtime]")
{
    ClockTimeMapper mapper(nominalRate);
    SyntheticStream stream(nominalRate, 50'000.0, 8);
    STATIC_REQUIRE(noexcept(mapper.addBlock({})));
    STATIC_REQUIRE(noexcept(mapper.sampleTimeOf(0)));

    const AllocationCounter allocations;
    double sum = 0.0;
    for (int block = 0; block < 1'000; ++block)
    {
        mapper.addBlock(stream.next());
        sum += mapper.sampleTimeOf(hostStartNs);
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(std::isfinite(sum));
}
