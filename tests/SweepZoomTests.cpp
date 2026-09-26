#include <visona/SweepZoom.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;
using visona::maxSweepZoom;
using visona::SweepZoom;

namespace
{

/** The distance between two window positions, the short way round. */
double circularDistance(double a, double b)
{
    const auto d = std::abs(a - b);
    return std::min(d, 1.0 - d);
}

bool isWholeWindow(const SweepZoom& zoom)
{
    return !zoom.isZoomed() && zoom.offset == 0.0 && zoom.span == 1.0;
}

} // namespace

TEST_CASE("A zoom starts as the whole window", "[zoom]")
{
    const SweepZoom zoom;
    CHECK(isWholeWindow(zoom));
    CHECK(zoom.factor() == 1.0);
}

TEST_CASE("Zooming in keeps the anchor where it is", "[zoom]")
{
    const auto anchor = GENERATE(0.0, 0.1, 0.5, 0.93, 1.0);
    const auto start = GENERATE(SweepZoom{}, SweepZoom{0.3, 0.25}, SweepZoom{0.9, 0.2});
    CAPTURE(anchor, start.offset, start.span);

    const auto zoomed = start.zoomedAround(anchor, 1.5);
    CHECK_THAT(zoomed.span, WithinAbs(start.span / 1.5, 1.0e-12));
    CHECK(circularDistance(zoomed.windowPositionOf(anchor), start.windowPositionOf(anchor)) <
          1.0e-12);
    CHECK(zoomed.offset >= 0.0);
    CHECK(zoomed.offset < 1.0);
}

TEST_CASE("Zooming in stops at 1/32 of the window", "[zoom]")
{
    SweepZoom zoom;
    for (int step = 0; step < 100; ++step)
        zoom = zoom.zoomedAround(0.4, 1.3);
    CHECK(zoom.span == 1.0 / maxSweepZoom);
    CHECK(zoom.factor() == maxSweepZoom);
    CHECK(zoom.zoomedAround(0.4, 2.0).span == zoom.span);
}

TEST_CASE("Zooming out arrives at the whole window", "[zoom]")
{
    const auto start = GENERATE(SweepZoom{0.4, 0.1}, SweepZoom{0.97, 0.06}, SweepZoom{0.8, 0.1},
                                SweepZoom{0.5, 0.5}, SweepZoom{0.0, 1.0 / 32.0});
    const auto anchor = GENERATE(0.0, 0.5, 1.0);
    const auto factor = GENERATE(0.8, 0.95, 0.5);
    CAPTURE(start.offset, start.span, anchor, factor);

    auto zoom = start;
    int steps = 0;
    while (zoom.isZoomed() && steps < 1'000)
    {
        const auto next = zoom.zoomedAround(anchor, factor);
        REQUIRE(next.span > zoom.span);
        zoom = next;
        ++steps;
    }
    CHECK(isWholeWindow(zoom));
    CHECK(steps < 1'000);
}

TEST_CASE("Zooming out one step from 1/2 lands on the whole window", "[zoom]")
{
    const SweepZoom half{0.75, 0.5};
    CHECK(isWholeWindow(half.zoomedAround(0.3, 0.5)));
    CHECK(isWholeWindow(half.zoomedAround(0.3, 0.1)));
}

TEST_CASE("Zooming out at the start of the window runs past its start", "[zoom]")
{
    // The view starts at the downbeat; zooming out around its centre shows what comes before it,
    // from the end of the window.
    const SweepZoom zoom{0.0, 1.0 / 16.0};
    const auto wider = zoom.zoomedAround(0.5, 0.8);
    CHECK(wider.offset > 0.95);
    CHECK(wider.offset + wider.span > 1.0);
}

TEST_CASE("Zooming out turns a view that runs past the end of the window towards the start",
          "[zoom]")
{
    // Centred on the downbeat. Each step out moves the downbeat towards the left edge, where it
    // is in the whole window.
    SweepZoom zoom{1.0 - 1.0 / 64.0, 1.0 / 32.0};
    auto downbeatAt = 0.5;
    while (zoom.isZoomed())
    {
        zoom = zoom.zoomedAround(0.5, 0.8);
        const auto intoView = zoom.offset == 0.0 ? 0.0 : (1.0 - zoom.offset) / zoom.span;
        CHECK(intoView <= downbeatAt + 1.0e-12);
        downbeatAt = intoView;
    }
    CHECK(downbeatAt == 0.0);
}

TEST_CASE("A selection zooms to that part of the view", "[zoom]")
{
    SECTION("from the whole window")
    {
        const auto zoom = SweepZoom{}.selected(0.5, 0.75);
        CHECK(zoom.offset == 0.5);
        CHECK(zoom.span == 0.25);
    }

    SECTION("in either direction")
    {
        const auto zoom = SweepZoom{}.selected(0.75, 0.5);
        CHECK(zoom.offset == 0.5);
        CHECK(zoom.span == 0.25);
    }

    SECTION("nested, across the end of the window")
    {
        const auto zoom = SweepZoom{0.875, 0.25}.selected(0.25, 0.75);
        CHECK_THAT(zoom.offset, WithinAbs(0.9375, 1.0e-12));
        CHECK_THAT(zoom.span, WithinAbs(0.125, 1.0e-12));
        CHECK_THAT(zoom.windowPositionOf(1.0), WithinAbs(0.0625, 1.0e-12));
    }

    SECTION("a part narrower than the deepest zoom is widened around its centre")
    {
        const auto zoom = SweepZoom{}.selected(0.5, 0.51);
        CHECK(zoom.span == 1.0 / maxSweepZoom);
        CHECK_THAT(zoom.windowPositionOf(0.5), WithinAbs(0.505, 1.0e-12));
    }

    SECTION("the whole view is the whole window")
    {
        CHECK(isWholeWindow(SweepZoom{}.selected(0.0, 1.0)));
        CHECK(isWholeWindow(SweepZoom{}.selected(-0.5, 1.5)));
    }
}

TEST_CASE("A zoom is normalized", "[zoom]")
{
    CHECK(SweepZoom::normalized({1.25, 0.5}).offset == 0.25);
    CHECK(SweepZoom::normalized({-0.25, 0.5}).offset == 0.75);
    CHECK(SweepZoom::normalized({0.25, 0.001}).span == 1.0 / maxSweepZoom);
    CHECK(isWholeWindow(SweepZoom::normalized({0.25, 1.0})));
    CHECK(isWholeWindow(SweepZoom::normalized({0.25, 2.0})));
    CHECK(isWholeWindow(SweepZoom::normalized({std::nan(""), 0.5})));
    CHECK(isWholeWindow(SweepZoom::normalized({0.5, std::numeric_limits<double>::infinity()})));

    const SweepZoom zoom{0.5, 0.25};
    CHECK(zoom.zoomedAround(0.5, std::nan("")).span == 0.25);
    CHECK(zoom.zoomedAround(0.5, 0.0).span == 0.25);
    CHECK(zoom.selected(std::nan(""), 0.5).span == 0.25);
}
