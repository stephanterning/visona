#include <visona/LaneMapping.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

using Catch::Matchers::WithinRel;
using visona::DisplayGain;
using visona::LaneMapping;
using visona::LaneRows;

TEST_CASE("Display gain runs from 0 to +18 dB", "[lane]")
{
    CHECK(DisplayGain::clampDb(-3) == 0);
    CHECK(DisplayGain::clampDb(12) == 12);
    CHECK(DisplayGain::clampDb(18) == 18);
    CHECK(DisplayGain::clampDb(36) == 18);
    CHECK(DisplayGain::toLinear(0) == 1.0f);
    CHECK_THAT(static_cast<double>(DisplayGain::toLinear(6)), WithinRel(1.9953, 1.0e-4));
    CHECK_THAT(static_cast<double>(DisplayGain::toLinear(18)), WithinRel(7.9433, 1.0e-4));
}

TEST_CASE("LaneMapping puts full scale at the lane edges at 0 dB", "[lane]")
{
    const LaneMapping lane(100, 200, 1.0f);
    CHECK(lane.rowOf(1.0f) == 100);
    CHECK(lane.rowOf(-1.0f) == 299);
    CHECK(lane.rowOf(0.0f) == 200);
    CHECK(lane.rowOf(0.5f) == 150);
    CHECK(lane.rowOf(-0.5f) == 250);
    CHECK(lane.isInside(1.0f));
    CHECK(lane.isInside(-1.0f));

    // Full scale reaches the edge but is not display overshoot.
    CHECK(lane.rowsOf(-1.0f, 1.0f) == LaneRows{100, 299, false, false});
}

TEST_CASE("LaneMapping scales by display gain and reports clipping at the edges", "[lane]")
{
    const LaneMapping lane(0, 100, DisplayGain::toLinear(12));
    // 0.1 at +12 dB is 0.398 of the half height: 19.9 rows above the centre at row 50.
    CHECK(lane.rowOf(0.1f) == 30);
    CHECK_FALSE(lane.isInside(0.5f));
    CHECK(lane.rowsOf(-0.1f, 0.5f) == LaneRows{0, 69, true, false});
    CHECK(lane.rowsOf(-0.9f, -0.5f) == LaneRows{99, 99, false, true});
    CHECK(lane.rowsOf(-0.9f, 0.9f) == LaneRows{0, 99, true, true});
}

TEST_CASE("LaneMapping keeps asymmetric spans asymmetric and at least one row tall", "[lane]")
{
    const LaneMapping lane(0, 400, 1.0f);
    const auto asymmetric = lane.rowsOf(-0.25f, 0.75f);
    CHECK(asymmetric == LaneRows{50, 250, false, false});

    const auto flat = lane.rowsOf(0.3f, 0.3f);
    CHECK(flat.top == flat.bottom);
    CHECK(flat.top == 140);
}

TEST_CASE("LaneMapping survives values that are not finite", "[lane]")
{
    const LaneMapping lane(10, 50, 2.0f);
    CHECK(lane.rowOf(std::numeric_limits<float>::infinity()) == 10);
    CHECK(lane.rowOf(-std::numeric_limits<float>::infinity()) == 59);
    CHECK(lane.rowOf(std::numeric_limits<float>::quiet_NaN()) == 35);
}
