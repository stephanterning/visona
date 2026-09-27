#pragma once

#include <visona/TimeSignature.h>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace visona
{

/**
    The measurement ruler (D-094): a rectangle drawn on the scope, read against the axes as shown.
    Its width is read against the time axis and its top and bottom edges against the amplitude
    scale. It never looks at the audio or the waveform.
*/

/** The time axis as shown: the window, how much of it is in view, and the tempo it runs at. */
struct RulerTimeAxis
{
    double windowTicks = 0.0;
    /** The fraction of the window in view, SweepZoom::span. */
    double viewSpan = 1.0;
    /** The tempo of the sweep: the free tempo in FREE, MIDI Clock's otherwise. 0 if unknown. */
    double bpm = 0.0;
    /** 0 if unknown. */
    double sampleRate = 0.0;
    TimeSignature timeSignature;
};

/** A length on the time axis. */
struct RulerTime
{
    double ticks = 0.0;
    double milliseconds = 0.0;
    /** 0 if the sample rate is not known. */
    double frames = 0.0;
    /** 1 / the length; infinite for a length of 0. */
    double hertz = 0.0;
};

/** The length of `viewFraction` of the view's width, or nothing without a window and a tempo. */
[[nodiscard]] std::optional<RulerTime> rulerTimeOf(const RulerTimeAxis& axis,
                                                   double viewFraction) noexcept;

/** The note nearest a frequency. Octaves are numbered as in Ableton Live and Oszillos Mega
    Scope: middle C is C3, and A3 is 440 Hz. */
struct NoteName
{
    /** 0 for C to 11 for B. */
    int pitchClass = 0;
    int octave = 0;
    /** How far the frequency is from the note, from -50 to +50. */
    int cents = 0;
};

/** The note nearest `hertz`, or nothing for a frequency that is not positive and finite. */
[[nodiscard]] std::optional<NoteName> noteNameOf(double hertz) noexcept;

/** Such as "G#-2 +22 ct". */
[[nodiscard]] std::string formatNote(const NoteName& note);

/** One lane as the ruler reads it, in any vertical unit: rows from `top` for `height`. */
struct RulerLane
{
    double top = 0.0;
    double height = 1.0;
};

/** A level read off the amplitude scale. */
struct RulerLevel
{
    /** dBFS of the level's magnitude, minus infinity on the centre line. */
    double db = 0.0;
    /** Whether the position is below the lane's centre line. */
    bool belowCentre = false;
};

/**
    The level at height `y` on the amplitude scale as shown, with `gain` the linear display gain:
    the value that would be drawn there, as the reference lines label it (D-088). `y` is read in
    the lane it lies in; between lanes, or outside all of them, at the nearest lane's edge.
    Nothing without lanes.
*/
[[nodiscard]] std::optional<RulerLevel> rulerLevelAt(double y, std::span<const RulerLane> lanes,
                                                     float gain) noexcept;

/**
    The musical length of `ticks`, such as "0.75 beat · 3/16" or "4.00 beats · 1 bar": the number
    of beats, and the note value or whole bars it is within 2 % of, if any. Note values go down to
    sixty-fourths.
*/
[[nodiscard]] std::string formatMusicalLength(double ticks, const TimeSignature& timeSignature);

/** One row of the ruler's readout: a label and its value. */
using RulerRow = std::pair<std::string, std::string>;

/**
    The readout of a ruler `viewFraction` of the view wide, from `start` to `end`: ms, samples,
    frequency, note, musical length, and the start, end and delta levels. Values that cannot be
   known show a dash.
*/
[[nodiscard]] std::vector<RulerRow> rulerReadout(const RulerTimeAxis& axis, double viewFraction,
                                                 std::optional<RulerLevel> start,
                                                 std::optional<RulerLevel> end);

} // namespace visona
