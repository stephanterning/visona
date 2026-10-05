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

TEST_CASE("Panning moves the view and wraps around the window's ends", "[zoom]")
{
    const SweepZoom zoom{0.5, 0.25};

    const auto later = zoom.panned(0.125);
    CHECK_THAT(later.offset, WithinAbs(0.625, 1.0e-12));
    CHECK(later.span == zoom.span);

    // Past the end, the view carries on at the window's start: the end of one bar and the start
    // of the next.
    const auto acrossEnd = zoom.panned(0.375);
    CHECK_THAT(acrossEnd.offset, WithinAbs(0.875, 1.0e-12));
    CHECK_THAT(acrossEnd.windowPositionOf(0.5), WithinAbs(0.0, 1.0e-12));
    const auto wrapped = acrossEnd.panned(0.25);
    CHECK_THAT(wrapped.offset, WithinAbs(0.125, 1.0e-12));

    // And the other way round.
    const auto earlier = zoom.panned(-0.75);
    CHECK_THAT(earlier.offset, WithinAbs(0.75, 1.0e-12));
    CHECK(earlier.span == zoom.span);

    // Any distance keeps the offset in the window.
    for (double distance = -3.0; distance <= 3.0; distance += 0.0371)
    {
        const auto moved = zoom.panned(distance);
        CHECK(moved.offset >= 0.0);
        CHECK(moved.offset < 1.0);
        CHECK(moved.span == zoom.span);
        CHECK_THAT(circularDistance(moved.offset, 0.5 + distance - std::floor(0.5 + distance)),
                   WithinAbs(0.0, 1.0e-12));
    }
}

TEST_CASE("The whole window does not pan", "[zoom]")
{
    CHECK(isWholeWindow(SweepZoom{}.panned(0.3)));
    CHECK(isWholeWindow(SweepZoom{}.centredOn(0.8)));
    const SweepZoom zoom{0.2, 0.1};
    CHECK(zoom.panned(std::numeric_limits<double>::quiet_NaN()).offset == zoom.offset);
    CHECK(zoom.centredOn(std::numeric_limits<double>::infinity()).offset == zoom.offset);
}

TEST_CASE("Centring puts the view's middle on a position, across the ends too", "[zoom]")
{
    const SweepZoom zoom{0.3, 0.2};
    const auto centred = zoom.centredOn(0.6);
    CHECK_THAT(centred.offset, WithinAbs(0.5, 1.0e-12));
    CHECK_THAT(centred.windowPositionOf(0.5), WithinAbs(0.6, 1.0e-12));

    const auto atStart = zoom.centredOn(0.0);
    CHECK_THAT(atStart.offset, WithinAbs(0.9, 1.0e-12));
    CHECK_THAT(circularDistance(atStart.windowPositionOf(0.5), 0.0), WithinAbs(0.0, 1.0e-12));
    CHECK(atStart.span == zoom.span);
}

TEST_CASE("A pinch keeps what was between the fingers between them", "[zoom]")
{
    const auto start = GENERATE(SweepZoom{0.3, 0.25}, SweepZoom{0.9, 0.2}, SweepZoom{0.0, 0.5});
    const auto startAnchor = GENERATE(0.2, 0.5, 0.8);
    const auto anchor = GENERATE(0.1, 0.5, 0.95);
    const auto factor = GENERATE(1.0, 1.5, 3.0);
    CAPTURE(start.offset, start.span, startAnchor, anchor, factor);

    const auto pinched = start.pinched(startAnchor, anchor, factor);
    CHECK_THAT(pinched.span, WithinAbs(start.span / factor, 1.0e-12));
    CHECK(circularDistance(pinched.windowPositionOf(anchor), start.windowPositionOf(startAnchor)) <
          1.0e-12);
    CHECK(pinched.offset >= 0.0);
    CHECK(pinched.offset < 1.0);
}

TEST_CASE("Moving both fingers of a pinch pans, round the window's ends", "[zoom]")
{
    const SweepZoom start{0.1, 0.25};

    // The fingers move a fifth of the view to the right, so the view moves earlier.
    const auto moved = start.pinched(0.5, 0.7, 1.0);
    CHECK(moved.span == start.span);
    CHECK_THAT(moved.offset, WithinAbs(0.05, 1.0e-12));

    // Further, and the view runs past the window's start, on from its end.
    const auto past = start.pinched(0.2, 1.0, 1.0);
    CHECK_THAT(past.offset, WithinAbs(0.9, 1.0e-12));

    // The whole window stays where it is.
    CHECK(isWholeWindow(SweepZoom{}.pinched(0.5, 0.9, 1.0)));
    CHECK(SweepZoom{0.4, 0.5}.pinched(0.5, std::nan(""), 2.0).span == 0.5);
}
