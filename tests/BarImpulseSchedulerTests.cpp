#include "support/AllocationCounter.h"

#include <visona/BarImpulseScheduler.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using visona::BarImpulseScheduler;
using visona::TimeSignature;

TEST_CASE("BarImpulseScheduler places one impulse per bar boundary", "[sync]")
{
    std::array<std::uint32_t, 8> offsets{};
    for (const auto start : {0u, 96000u})
    {
        const auto count = BarImpulseScheduler::impulsesInBlock(
            96000, static_cast<double>(start) / 24000.0, 120, {4, 4}, 48000, offsets);
        REQUIRE(count == 1);
        CHECK(offsets[0] == 0);
    }
    CHECK(BarImpulseScheduler::impulsesInBlock(1024, 0, 120, {4, 4}, 48000, offsets) == 1);
    CHECK(offsets[0] == 0);
}

TEST_CASE("BarImpulseScheduler rounds boundary ownership across adjacent blocks", "[sync]")
{
    for (int tenth = -15; tenth <= 15; ++tenth)
    {
        const double boundary = 512.0 + static_cast<double>(tenth) / 10.0;
        const auto expected = static_cast<std::uint32_t>(std::floor(boundary + 0.5));
        for (const double barPpq : {-4.0, 0.0, 4.0})
        {
            CAPTURE(boundary, barPpq);
            std::array<std::uint32_t, 8> offsets{};
            unsigned total = 0;
            for (std::uint32_t start : {0u, 512u})
            {
                const double ppq = barPpq + (static_cast<double>(start) - boundary) / 24000.0;
                const auto count =
                    BarImpulseScheduler::impulsesInBlock(512, ppq, 120, {4, 4}, 48000, offsets);
                for (std::size_t i = 0; i < count; ++i)
                {
                    CHECK(start + offsets[i] == expected);
                    ++total;
                }
            }
            CHECK(total == 1);
        }
    }
}

TEST_CASE("BarImpulseScheduler bounds output and never allocates", "[sync][realtime]")
{
    std::array<std::uint32_t, 128> offsets{};
    visona::test::AllocationCounter allocations;
    // One sample per bar, the densest supported timeline.
    const auto count = BarImpulseScheduler::impulsesInBlock(128, 0, 60, {1, 4}, 1, offsets);
    const auto allocated = allocations.count();
    REQUIRE(allocated == 0);
    REQUIRE(count == offsets.size());
    for (std::size_t i = 0; i < count; ++i)
        CHECK(offsets[i] == i);
    CHECK(BarImpulseScheduler::impulsesInBlock(128, 0, 60, {1, 4}, 1,
                                               std::span(offsets).first(3)) == 3);
    CHECK(BarImpulseScheduler::impulsesInBlock(128, 0, 60, {1, 4}, 1, {}) == 0);
}

TEST_CASE("BarImpulseScheduler rejects malformed host values", "[sync]")
{
    std::array<std::uint32_t, 8> offsets{};
    for (const auto signature : {TimeSignature{0, 4}, TimeSignature{4, 0}, TimeSignature{5, 64},
                                 TimeSignature{-1, 4}, TimeSignature{4, 3}})
        CHECK(BarImpulseScheduler::impulsesInBlock(512, 0, 120, signature, 48000, offsets) == 0);
    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        CHECK(BarImpulseScheduler::impulsesInBlock(512, invalid, 120, {}, 48000, offsets) == 0);
        CHECK(BarImpulseScheduler::impulsesInBlock(512, 0, invalid, {}, 48000, offsets) == 0);
        CHECK(BarImpulseScheduler::impulsesInBlock(512, 0, 120, {}, invalid, offsets) == 0);
    }
}

TEST_CASE("BarImpulseScheduler matches sampleIndexOfBarBoundary", "[sync]")
{
    CHECK_THAT(BarImpulseScheduler::sampleIndexOfBarBoundary(8, 96000, 8, 120, 48000),
               Catch::Matchers::WithinAbs(96000, 1));
}

TEST_CASE("Bar impulses are independent of host block partitioning", "[sync]")
{
    for (const double bpm : {97.0, 120.0, 137.3})
    {
        constexpr std::uint32_t totalFrames = 200000;
        const double framesPerQuarter = 48000.0 * 60.0 / bpm;
        std::vector<std::uint32_t> expected;
        for (int bar = 0; bar < 5; ++bar)
        {
            const auto frame =
                std::floor((static_cast<double>(bar) * 4.0 + 0.25) * framesPerQuarter + 0.5);
            if (frame < totalFrames)
                expected.push_back(static_cast<std::uint32_t>(frame));
        }
        for (const std::uint32_t blockSize : {1u, 64u, 127u, 512u, 4096u})
        {
            CAPTURE(bpm, blockSize);
            std::vector<std::uint32_t> actual;
            std::array<std::uint32_t, 4096> offsets{};
            for (std::uint32_t start = 0; start < totalFrames;)
            {
                const auto frames = std::min(blockSize, totalFrames - start);
                const auto count = BarImpulseScheduler::impulsesInBlock(
                    frames, -0.25 + static_cast<double>(start) / framesPerQuarter, bpm, {4, 4},
                    48000, offsets);
                for (std::size_t i = 0; i < count; ++i)
                    actual.push_back(start + offsets[i]);
                start += frames;
            }
            CHECK(actual == expected);
        }
    }
}
