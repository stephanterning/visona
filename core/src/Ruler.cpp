#include "visona/Ruler.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace visona
{

namespace
{

// UTF-8, as the UI draws them.
constexpr const char* minusSign = "\xe2\x88\x92";
constexpr const char* dash = "\xe2\x80\x94";
constexpr const char* middleDot = " \xc2\xb7 ";

/** Within this fraction of a length, it counts as a whole note value or whole bars. */
constexpr double musicalTolerance = 0.02;
constexpr int finestNoteValue = 64;
constexpr int ticksPerWholeNote = 4 * TimeSignature::ticksPerQuarterNote;

/** `value` with `decimals` decimals and a real minus sign, never "-0". With `withSign`, positive
    values get a plus sign. */
std::string formatNumber(double value, int decimals, bool withSign = false)
{
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f", decimals, std::abs(value));
    std::string digits(buffer.data());
    const bool zero = digits.find_first_not_of("0.") == std::string::npos;
    if (zero)
        return digits;
    if (value < 0.0)
        return minusSign + digits;
    return withSign ? "+" + digits : digits;
}

bool isNear(double value, double whole) noexcept
{
    return whole >= 1.0 && std::abs(value - whole) <= musicalTolerance * value;
}

} // namespace

std::optional<RulerTime> rulerTimeOf(const RulerTimeAxis& axis, double viewFraction) noexcept
{
    if (!(axis.windowTicks > 0.0) || !(axis.bpm > 0.0) || !std::isfinite(axis.windowTicks) ||
        !std::isfinite(axis.bpm) || !std::isfinite(viewFraction) || !std::isfinite(axis.viewSpan))
        return std::nullopt;

    RulerTime time;
    time.ticks = std::abs(viewFraction) * axis.viewSpan * axis.windowTicks;
    const auto seconds = time.ticks * 60.0 / (TimeSignature::ticksPerQuarterNote * axis.bpm);
    time.milliseconds = seconds * 1000.0;
    time.frames = axis.sampleRate > 0.0 ? seconds * axis.sampleRate : 0.0;
    time.hertz = seconds > 0.0 ? 1.0 / seconds : std::numeric_limits<double>::infinity();
    return time;
}

std::optional<NoteName> noteNameOf(double hertz) noexcept
{
    if (!(hertz > 0.0) || !std::isfinite(hertz))
        return std::nullopt;
    const auto midi = 69.0 + 12.0 * std::log2(hertz / 440.0);
    const auto nearest = std::round(midi);
    const auto note = static_cast<long long>(nearest);
    const auto octave = static_cast<long long>(std::floor(nearest / 12.0));
    NoteName name;
    name.pitchClass = static_cast<int>(note - 12 * octave);
    // MIDI note 60 is middle C, C3 in Live's numbering.
    name.octave = static_cast<int>(octave - 2);
    name.cents = static_cast<int>(std::lround((midi - nearest) * 100.0));
    return name;
}

std::string formatNote(const NoteName& note)
{
    static constexpr std::array<const char*, 12> names{"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};
    const auto octave = note.octave < 0 ? std::string(minusSign) + std::to_string(-note.octave)
                                        : std::to_string(note.octave);
    const auto cents = note.cents == 0 ? std::string("+0")
                                       : formatNumber(static_cast<double>(note.cents), 0, true);
    return names[static_cast<std::size_t>(note.pitchClass % 12)] + octave + " " + cents + " ct";
}

std::string formatMusicalLength(double ticks, const TimeSignature& timeSignature)
{
    const auto ticksPerBeat = timeSignature.ticksPerBeat();
    const auto ticksPerBar = timeSignature.ticksPerBar();
    if (!(ticks >= 0.0) || !std::isfinite(ticks) || ticksPerBeat <= 0 || ticksPerBar <= 0)
        return dash;

    const auto beats = formatNumber(ticks / ticksPerBeat, 2);
    auto text = beats + (beats == "1.00" ? " beat" : " beats");

    const auto bars = ticks / ticksPerBar;
    if (isNear(bars, std::round(bars)))
    {
        const auto count = std::llround(bars);
        return text + middleDot + std::to_string(count) + (count == 1 ? " bar" : " bars");
    }
    for (int value = 1; value <= finestNoteValue; value *= 2)
    {
        const auto notes = ticks * value / ticksPerWholeNote;
        if (isNear(notes, std::round(notes)))
            return text + middleDot + std::to_string(std::llround(notes)) + "/" +
                   std::to_string(value);
    }
    return text;
}

std::vector<RulerRow> rulerReadout(const RulerTimeAxis& axis, double viewFraction)
{
    std::vector<RulerRow> rows;
    rows.reserve(5);

    const auto time = rulerTimeOf(axis, viewFraction);
    const auto note = time ? noteNameOf(time->hertz) : std::nullopt;
    rows.emplace_back("ms", time ? formatNumber(time->milliseconds, 2) : dash);
    rows.emplace_back("samples", time && axis.sampleRate > 0.0
                                     ? std::to_string(std::llround(time->frames))
                                     : dash);
    rows.emplace_back("frequency", time && std::isfinite(time->hertz)
                                       ? formatNumber(time->hertz, 2) + " Hz"
                                       : dash);
    rows.emplace_back("note", note ? formatNote(*note) : dash);
    rows.emplace_back("length", time ? formatMusicalLength(time->ticks, axis.timeSignature) : dash);
    return rows;
}

} // namespace visona
