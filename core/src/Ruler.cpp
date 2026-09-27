#include "visona/Ruler.h"

#include <algorithm>
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
constexpr const char* infinitySign = "\xe2\x88\x9e";
constexpr const char* dash = "\xe2\x80\x94";
constexpr const char* belowCentreMark = " \xe2\x96\xbe";
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

std::string formatLevel(const RulerLevel& level)
{
    auto text = std::isfinite(level.db) ? formatNumber(level.db, 2) + " dB"
                                        : std::string(minusSign) + infinitySign + " dB";
    if (level.belowCentre)
        text += belowCentreMark;
    return text;
}

std::string formatDelta(const RulerLevel& start, const RulerLevel& end)
{
    const bool startFinite = std::isfinite(start.db);
    const bool endFinite = std::isfinite(end.db);
    if (!startFinite && !endFinite)
        return formatNumber(0.0, 2) + " dB";
    if (!startFinite)
        return std::string("+") + infinitySign + " dB";
    if (!endFinite)
        return std::string(minusSign) + infinitySign + " dB";
    return formatNumber(end.db - start.db, 2, true) + " dB";
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

std::optional<RulerLevel> rulerLevelAt(double y, std::span<const RulerLane> lanes,
                                       float gain) noexcept
{
    if (lanes.empty() || !std::isfinite(y))
        return std::nullopt;

    // The lane y lies in, or the one whose edge is nearest.
    const RulerLane* nearest = nullptr;
    auto nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& lane : lanes)
    {
        const auto bottom = lane.top + lane.height;
        const auto distance = y < lane.top ? lane.top - y : (y > bottom ? y - bottom : 0.0);
        if (distance < nearestDistance)
        {
            nearest = &lane;
            nearestDistance = distance;
        }
    }
    const auto halfHeight = nearest->height * 0.5;
    const auto clamped = std::clamp(y, nearest->top, nearest->top + nearest->height);
    const auto scale = gain > 0.0f ? static_cast<double>(gain) : 1.0;
    const auto value =
        halfHeight > 0.0 ? (nearest->top + halfHeight - clamped) / (scale * halfHeight) : 0.0;

    RulerLevel level;
    level.belowCentre = value < 0.0;
    level.db = value == 0.0 ? -std::numeric_limits<double>::infinity()
                            : 20.0 * std::log10(std::abs(value));
    return level;
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

std::vector<RulerRow> rulerReadout(const RulerTimeAxis& axis, double viewFraction,
                                   std::optional<RulerLevel> start, std::optional<RulerLevel> end)
{
    std::vector<RulerRow> rows;
    rows.reserve(8);

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

    rows.emplace_back("start", start ? formatLevel(*start) : dash);
    rows.emplace_back("end", end ? formatLevel(*end) : dash);
    rows.emplace_back("delta", start && end ? formatDelta(*start, *end) : dash);
    return rows;
}

} // namespace visona
