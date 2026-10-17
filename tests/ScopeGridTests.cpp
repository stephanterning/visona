#include <visona/ScopeGrid.h>
#include <visona/SweepWindow.h>
#include <visona/SweepZoom.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using visona::amplitudeReferencesAt;
using visona::divisionMilliseconds;
using visona::gridDivisionFor;

TEST_CASE("Each window shows its own grid division unzoomed", "[grid]")
{
    CHECK(gridDivisionFor(4.0) == 4);
    CHECK(gridDivisionFor(2.0) == 8);
    CHECK(gridDivisionFor(1.0) == 16);
    CHECK(gridDivisionFor(0.5) == 32);
    CHECK(gridDivisionFor(0.25) == 64);
}

TEST_CASE("The grid gets finer as less of the window is in view", "[grid]")
{
    CHECK(gridDivisionFor(3.0) == 4);
    CHECK(gridDivisionFor(2.01) == 4);
    CHECK(gridDivisionFor(1.5) == 8);
    CHECK(gridDivisionFor(0.75) == 16);
    CHECK(gridDivisionFor(0.3) == 32);
    CHECK(gridDivisionFor(1.0 / 128.0) == 64);

    // A 4-bar window zoomed in to 4x shows 1 bar, rounding and all.
    const auto span = visona::SweepZoom{}.zoomedAround(0.5, 4.0).span;
    CHECK(gridDivisionFor(visona::sweepWindowBars[4] * span) == 16);

    // Never coarser as the view narrows.
    int previous = 0;
    for (double bars = 8.0; bars > 0.01; bars *= 0.9)
    {
        const auto division = gridDivisionFor(bars);
        CHECK(division >= previous);
        previous = division;
    }
}

TEST_CASE("A division's length follows the tempo", "[grid]")
{
    CHECK_THAT(divisionMilliseconds(4, 120.0), WithinRel(500.0, 1.0e-12));
    CHECK_THAT(divisionMilliseconds(16, 120.0), WithinRel(125.0, 1.0e-12));
    CHECK_THAT(divisionMilliseconds(64, 174.0), WithinRel(21.5517, 1.0e-4));
    CHECK_THAT(divisionMilliseconds(8, 60.0), WithinRel(500.0, 1.0e-12));
    CHECK(divisionMilliseconds(16, 0.0) == 0.0);
    CHECK(divisionMilliseconds(16, std::nan("")) == 0.0);
}

TEST_CASE("Amplitude references continue in 3 dB steps to pixel resolution", "[grid]")
{
    for (const int height : {64, 256, 1024})
        for (int gain = 0; gain <= 64; ++gain)
        {
            int previous = 3 - ((gain + 2) / 3) * 3;
            float lastDistance = 0.0f;
            for (const auto reference : amplitudeReferencesAt(gain, height))
            {
                CHECK(reference.levelDb == previous - 3);
                CHECK(reference.levelDb <= -gain);
                CHECK_THAT(static_cast<double>(reference.level),
                           WithinRel(std::pow(10.0, reference.levelDb / 20.0), 1.0e-5));
                lastDistance = reference.level * std::pow(10.0f, static_cast<float>(gain) / 20.0f) *
                               static_cast<float>(height - 1) / 2.0f;
                CHECK(lastDistance >= 0.999f);
                previous = reference.levelDb;
            }
            CHECK(lastDistance * std::pow(10.0f, -3.0f / 20.0f) < 1.0f);
            CHECK(previous < -gain - 18);
        }
}
