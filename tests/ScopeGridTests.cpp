#include <visona/ScopeGrid.h>
#include <visona/SweepWindow.h>
#include <visona/SweepZoom.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

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

TEST_CASE("Quieter amplitude references appear with more display gain", "[grid]")
{
    const auto levels = [](int gainDb)
    {
        std::vector<int> result;
        for (const auto& reference : amplitudeReferencesAt(gainDb))
            result.push_back(reference.levelDb);
        return result;
    };
    CHECK(levels(0) == std::vector{0, -6});
    CHECK(levels(3) == std::vector{0, -6});
    CHECK(levels(4) == std::vector{0, -6, -12});
    CHECK(levels(9) == std::vector{0, -6, -12});
    CHECK(levels(10) == std::vector{0, -6, -12, -18});
    CHECK(levels(36) == std::vector{0, -6, -12, -18});

    for (const auto& reference : amplitudeReferencesAt(36))
        CHECK_THAT(static_cast<double>(reference.level),
                   WithinAbs(std::pow(10.0, reference.levelDb / 20.0), 1.0e-6));
}
