#include <visona/BarImpulseScheduler.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

using visona::BarImpulseScheduler;
using visona::TimeSignature;

TEST_CASE("BarImpulseScheduler places one impulse per bar boundary", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const TimeSignature timeSignature{4, 4};
    const auto samplesPerBar = static_cast<std::uint32_t>(sampleRate * 60.0 / bpm * 4.0);

    std::vector<std::uint32_t> offsets;
    BarImpulseScheduler::impulsesInBlock(0, samplesPerBar, 4.0, bpm, timeSignature, sampleRate,
                                         offsets);
    REQUIRE(offsets.size() == 1);
    CHECK(offsets.front() == 0);

    offsets.clear();
    BarImpulseScheduler::impulsesInBlock(samplesPerBar, samplesPerBar, 8.0, bpm, timeSignature,
                                         sampleRate, offsets);
    REQUIRE(offsets.size() == 1);
    CHECK(offsets.front() == 0);
}

TEST_CASE("BarImpulseScheduler matches sampleIndexOfBarBoundary", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto expected = BarImpulseScheduler::sampleIndexOfBarBoundary(8.0, 96000, 8.0, bpm,
                                                                        sampleRate);
    CHECK_THAT(expected, WithinAbs(96000.0, 1.0));
}
