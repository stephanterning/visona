#pragma once

#include <visona/TimeSignature.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace visona
{

/**
    The measurement ruler (D-094): a rectangle drawn on the scope, whose width is read against the
    time axis as shown. It never looks at the audio or the waveform.
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

/**
    The musical length of `ticks`, such as "0.75 beat · 3/16" or "4.00 beats · 1 bar": the number
    of beats, and the note value or whole bars it is within 2 % of, if any. Note values go down to
    sixty-fourths.
*/
[[nodiscard]] std::string formatMusicalLength(double ticks, const TimeSignature& timeSignature);

/** One row of the ruler's readout: a label and its value. */
using RulerRow = std::pair<std::string, std::string>;

/** The readout of a ruler `viewFraction` of the view wide: ms, samples, frequency, note and
    musical length. Values that cannot be known show a dash. */
[[nodiscard]] std::vector<RulerRow> rulerReadout(const RulerTimeAxis& axis, double viewFraction);

} // namespace visona
