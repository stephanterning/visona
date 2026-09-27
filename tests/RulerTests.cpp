#include <visona/Ruler.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using visona::RulerLane;
using visona::RulerLevel;
using visona::RulerTimeAxis;

namespace
{

const std::string minus = "\xe2\x88\x92";
const std::string infinity = "\xe2\x88\x9e";
const std::string dash = "\xe2\x80\x94";
const std::string below = " \xe2\x96\xbe";
const std::string dot = " \xc2\xb7 ";

RulerTimeAxis oneBarAt120()
{
    RulerTimeAxis axis;
    axis.windowTicks = 96.0;
    axis.bpm = 120.0;
    axis.sampleRate = 96000.0;
    return axis;
}

std::string valueOf(const std::vector<visona::RulerRow>& rows, const std::string& label)
{
    for (const auto& [name, value] : rows)
        if (name == label)
            return value;
    FAIL("No row " << label);
    return {};
}

} // namespace

TEST_CASE("The ruler reads its width off the time axis", "[ruler]")
{
    const auto whole = visona::rulerTimeOf(oneBarAt120(), 1.0);
    REQUIRE(whole.has_value());
    CHECK_THAT(whole->ticks, WithinRel(96.0, 1e-12));
    CHECK_THAT(whole->milliseconds, WithinRel(2000.0, 1e-12));
    CHECK_THAT(whole->frames, WithinRel(192000.0, 1e-12));
    CHECK_THAT(whole->hertz, WithinRel(0.5, 1e-12));

    // Half of a view showing a quarter of a 4-bar window is half a bar.
    auto axis = oneBarAt120();
    axis.windowTicks = 384.0;
    axis.viewSpan = 0.25;
    const auto half = visona::rulerTimeOf(axis, 0.5);
    REQUIRE(half.has_value());
    CHECK_THAT(half->milliseconds, WithinRel(1000.0, 1e-12));
    CHECK_THAT(half->frames, WithinRel(96000.0, 1e-12));

    // Dragging leftwards measures the same.
    CHECK_THAT(visona::rulerTimeOf(axis, -0.5)->milliseconds, WithinRel(1000.0, 1e-12));
}

TEST_CASE("The ruler's time follows the tempo", "[ruler]")
{
    auto axis = oneBarAt120();
    axis.bpm = 174.0;
    // A sixteenth at 174 BPM.
    const auto sixteenth = visona::rulerTimeOf(axis, 1.0 / 16.0);
    REQUIRE(sixteenth.has_value());
    CHECK_THAT(sixteenth->milliseconds, WithinRel(60000.0 / 174.0 / 4.0, 1e-12));
}

TEST_CASE("Without a window or a tempo the ruler knows no time", "[ruler]")
{
    auto axis = oneBarAt120();
    axis.bpm = 0.0;
    CHECK_FALSE(visona::rulerTimeOf(axis, 0.5).has_value());
    axis = oneBarAt120();
    axis.windowTicks = 0.0;
    CHECK_FALSE(visona::rulerTimeOf(axis, 0.5).has_value());

    // A length of nothing has no frequency.
    const auto zero = visona::rulerTimeOf(oneBarAt120(), 0.0);
    REQUIRE(zero.has_value());
    CHECK(std::isinf(zero->hertz));
}

TEST_CASE("Notes are named as in Live, with middle C as C3", "[ruler]")
{
    const auto a = visona::noteNameOf(440.0);
    REQUIRE(a.has_value());
    CHECK(a->pitchClass == 9);
    CHECK(a->octave == 3);
    CHECK(a->cents == 0);
    CHECK(visona::formatNote(*a) == "A3 +0 ct");

    CHECK(visona::formatNote(*visona::noteNameOf(261.6256)) == "C3 +0 ct");
    CHECK(visona::formatNote(*visona::noteNameOf(27.5)) == "A" + minus + "1 +0 ct");
    // The value in Oszillos Mega Scope's measure overlay.
    CHECK(visona::formatNote(*visona::noteNameOf(13.15)) == "G#" + minus + "2 +23 ct");
    // A quarter tone below A3 rounds to A3, a little more to G#3.
    CHECK(visona::formatNote(*visona::noteNameOf(440.0 * std::exp2(-0.4 / 12.0))) ==
          "A3 " + minus + "40 ct");
    CHECK(visona::formatNote(*visona::noteNameOf(440.0 * std::exp2(-0.6 / 12.0))) == "G#3 +40 ct");

    CHECK_FALSE(visona::noteNameOf(0.0).has_value());
    CHECK_FALSE(visona::noteNameOf(std::numeric_limits<double>::infinity()).has_value());
}

TEST_CASE("The ruler reads levels off the amplitude scale as shown", "[ruler]")
{
    const std::array lane{RulerLane{0.0, 200.0}};

    CHECK_THAT(visona::rulerLevelAt(0.0, lane, 1.0f)->db, WithinAbs(0.0, 1e-9));
    CHECK_THAT(visona::rulerLevelAt(50.0, lane, 1.0f)->db, WithinAbs(-6.0206, 1e-4));
    CHECK_FALSE(visona::rulerLevelAt(50.0, lane, 1.0f)->belowCentre);

    // Below the centre line the scale is mirrored, as the reference lines are.
    const auto lower = visona::rulerLevelAt(150.0, lane, 1.0f);
    CHECK_THAT(lower->db, WithinAbs(-6.0206, 1e-4));
    CHECK(lower->belowCentre);

    CHECK(std::isinf(visona::rulerLevelAt(100.0, lane, 1.0f)->db));

    // With +12 dB of display gain, the lane's edge is -12 dBFS, where the reference line says.
    const auto gain = std::pow(10.0f, 12.0f / 20.0f);
    CHECK_THAT(visona::rulerLevelAt(0.0, lane, gain)->db, WithinAbs(-12.0, 1e-4));
    CHECK_THAT(visona::rulerLevelAt(50.0, lane, gain)->db, WithinAbs(-18.0206, 1e-4));
}

TEST_CASE("The ruler reads each height in the lane it lies in", "[ruler]")
{
    const std::array lanes{RulerLane{0.0, 100.0}, RulerLane{104.0, 100.0}};

    // The centre of the lower lane.
    CHECK(std::isinf(visona::rulerLevelAt(154.0, lanes, 1.0f)->db));
    // Halfway up the lower lane's upper half.
    CHECK_THAT(visona::rulerLevelAt(129.0, lanes, 1.0f)->db, WithinAbs(-6.0206, 1e-4));

    // On the divider, the nearest lane's edge.
    const auto upperEdge = visona::rulerLevelAt(101.0, lanes, 1.0f);
    CHECK_THAT(upperEdge->db, WithinAbs(0.0, 1e-9));
    CHECK(upperEdge->belowCentre);
    const auto lowerEdge = visona::rulerLevelAt(103.5, lanes, 1.0f);
    CHECK_THAT(lowerEdge->db, WithinAbs(0.0, 1e-9));
    CHECK_FALSE(lowerEdge->belowCentre);

    // Past the scope's edges, the outer lanes' edges.
    CHECK_THAT(visona::rulerLevelAt(-20.0, lanes, 1.0f)->db, WithinAbs(0.0, 1e-9));
    CHECK_THAT(visona::rulerLevelAt(400.0, lanes, 1.0f)->db, WithinAbs(0.0, 1e-9));

    CHECK_FALSE(visona::rulerLevelAt(10.0, {}, 1.0f).has_value());
}

TEST_CASE("The musical length names note values and bars", "[ruler]")
{
    const visona::TimeSignature fourFour;
    CHECK(visona::formatMusicalLength(18.0, fourFour) == "0.75 beats" + dot + "3/16");
    CHECK(visona::formatMusicalLength(24.0, fourFour) == "1.00 beat" + dot + "1/4");
    CHECK(visona::formatMusicalLength(36.0, fourFour) == "1.50 beats" + dot + "3/8");
    CHECK(visona::formatMusicalLength(48.0, fourFour) == "2.00 beats" + dot + "1/2");
    CHECK(visona::formatMusicalLength(96.0, fourFour) == "4.00 beats" + dot + "1 bar");
    CHECK(visona::formatMusicalLength(192.0, fourFour) == "8.00 beats" + dot + "2 bars");
    CHECK(visona::formatMusicalLength(1.5, fourFour) == "0.06 beats" + dot + "1/64");

    // Within 2 %, but no further.
    CHECK(visona::formatMusicalLength(24.4, fourFour) == "1.02 beats" + dot + "1/4");
    CHECK(visona::formatMusicalLength(10.0, fourFour) == "0.42 beats");
    CHECK(visona::formatMusicalLength(0.0, fourFour) == "0.00 beats");

    const visona::TimeSignature threeFour{3, 4};
    CHECK(visona::formatMusicalLength(72.0, threeFour) == "3.00 beats" + dot + "1 bar");
}

TEST_CASE("The readout lists the ruler's values", "[ruler]")
{
    const RulerLevel start{-7.3219, false};
    const RulerLevel end{-4.6519, true};
    const auto rows = visona::rulerReadout(oneBarAt120(), 1.0 / 16.0, start, end);

    REQUIRE(rows.size() == 8);
    CHECK(rows[0].first == "ms");
    CHECK(valueOf(rows, "ms") == "125.00");
    CHECK(valueOf(rows, "samples") == "12000");
    CHECK(valueOf(rows, "frequency") == "8.00 Hz");
    CHECK(valueOf(rows, "note") == "C" + minus + "2 " + minus + "38 ct");
    CHECK(valueOf(rows, "length") == "0.25 beats" + dot + "1/16");
    CHECK(valueOf(rows, "start") == minus + "7.32 dB");
    CHECK(valueOf(rows, "end") == minus + "4.65 dB" + below);
    CHECK(valueOf(rows, "delta") == "+2.67 dB");
}

TEST_CASE("The readout shows a dash for what it cannot know", "[ruler]")
{
    auto axis = oneBarAt120();
    axis.sampleRate = 0.0;
    auto rows = visona::rulerReadout(axis, 0.5, std::nullopt, std::nullopt);
    CHECK(valueOf(rows, "ms") == "1000.00");
    CHECK(valueOf(rows, "samples") == dash);
    CHECK(valueOf(rows, "start") == dash);
    CHECK(valueOf(rows, "delta") == dash);

    axis.bpm = 0.0;
    rows = visona::rulerReadout(axis, 0.5, std::nullopt, std::nullopt);
    for (const auto& label : {"ms", "samples", "frequency", "note", "length"})
        CHECK(valueOf(rows, label) == dash);

    // A ruler with no width has a length but no frequency.
    rows = visona::rulerReadout(oneBarAt120(), 0.0, std::nullopt, std::nullopt);
    CHECK(valueOf(rows, "ms") == "0.00");
    CHECK(valueOf(rows, "frequency") == dash);
    CHECK(valueOf(rows, "note") == dash);
}

TEST_CASE("The readout's levels handle the centre line", "[ruler]")
{
    const RulerLevel centre{-std::numeric_limits<double>::infinity(), false};
    const RulerLevel half{-6.02, false};
    auto rows = visona::rulerReadout(oneBarAt120(), 0.5, centre, half);
    CHECK(valueOf(rows, "start") == minus + infinity + " dB");
    CHECK(valueOf(rows, "delta") == "+" + infinity + " dB");

    rows = visona::rulerReadout(oneBarAt120(), 0.5, half, centre);
    CHECK(valueOf(rows, "delta") == minus + infinity + " dB");

    rows = visona::rulerReadout(oneBarAt120(), 0.5, centre, centre);
    CHECK(valueOf(rows, "delta") == "0.00 dB");

    rows = visona::rulerReadout(oneBarAt120(), 0.5, half, half);
    CHECK(valueOf(rows, "delta") == "0.00 dB");
}
