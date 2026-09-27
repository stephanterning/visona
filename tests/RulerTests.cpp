#include <visona/Ruler.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <string>

using Catch::Matchers::WithinRel;
using visona::RulerTimeAxis;

namespace
{

const std::string minus = "\xe2\x88\x92";
const std::string dash = "\xe2\x80\x94";
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
    const auto rows = visona::rulerReadout(oneBarAt120(), 1.0 / 16.0);

    REQUIRE(rows.size() == 5);
    CHECK(rows[0].first == "ms");
    CHECK(valueOf(rows, "ms") == "125.00");
    CHECK(valueOf(rows, "samples") == "12000");
    CHECK(valueOf(rows, "frequency") == "8.00 Hz");
    CHECK(valueOf(rows, "note") == "C" + minus + "2 " + minus + "38 ct");
    CHECK(valueOf(rows, "length") == "0.25 beats" + dot + "1/16");
}

TEST_CASE("The readout shows a dash for what it cannot know", "[ruler]")
{
    auto axis = oneBarAt120();
    axis.sampleRate = 0.0;
    auto rows = visona::rulerReadout(axis, 0.5);
    CHECK(valueOf(rows, "ms") == "1000.00");
    CHECK(valueOf(rows, "samples") == dash);

    axis.bpm = 0.0;
    rows = visona::rulerReadout(axis, 0.5);
    for (const auto& label : {"ms", "samples", "frequency", "note", "length"})
        CHECK(valueOf(rows, label) == dash);

    // A ruler with no width has a length but no frequency.
    rows = visona::rulerReadout(oneBarAt120(), 0.0);
    CHECK(valueOf(rows, "ms") == "0.00");
    CHECK(valueOf(rows, "frequency") == dash);
    CHECK(valueOf(rows, "note") == dash);
}
