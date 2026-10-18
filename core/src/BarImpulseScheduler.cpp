#include "visona/BarImpulseScheduler.h"

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

double quarterNotesPerBar(TimeSignature timeSignature) noexcept
{
    return static_cast<double>(timeSignature.numerator) * 4.0 /
           static_cast<double>(timeSignature.denominator);
}

double samplesPerQuarterNote(double bpm, double sampleRate) noexcept
{
    return sampleRate * 60.0 / std::max(bpm, 1.0e-9);
}

} // namespace

double BarImpulseScheduler::sampleIndexOfBarBoundary(double ppqAtReference,
                                                     std::uint64_t referenceSample, double barPpq,
                                                     double bpm, double sampleRate) noexcept
{
    return static_cast<double>(referenceSample) +
           (barPpq - ppqAtReference) * samplesPerQuarterNote(bpm, sampleRate);
}

std::size_t BarImpulseScheduler::impulsesInBlock(std::uint64_t /*blockStartSample*/,
                                                 std::uint32_t numFrames, double ppqAtBlockStart,
                                                 double bpm, TimeSignature timeSignature,
                                                 double sampleRate,
                                                 std::span<std::uint32_t> offsetsOut) noexcept
{
    if (numFrames == 0 || offsetsOut.empty() || !std::isfinite(ppqAtBlockStart) ||
        !std::isfinite(bpm) || bpm <= 0.0 || !std::isfinite(sampleRate) || sampleRate <= 0.0 ||
        !timeSignature.isValid())
        return 0;

    const auto framesPerQuarter = samplesPerQuarterNote(bpm, sampleRate);
    const auto barLength = quarterNotesPerBar(timeSignature);
    const auto framesPerBar = framesPerQuarter * barLength;
    if (!std::isfinite(framesPerBar) || framesPerBar < 1.0)
        return 0;

    auto phase = std::fmod(ppqAtBlockStart, barLength);
    if (phase < 0.0)
        phase += barLength;
    auto sample = -phase * framesPerQuarter;
    // Decide ownership after rounding. In particular, a boundary just before this block
    // can round to its first sample. Work locally to avoid rounding large stream indices.
    constexpr double roundingTolerance = 1.0e-7;
    if (sample < -0.5 - roundingTolerance)
        sample += framesPerBar;

    std::size_t count = 0;
    for (; count < offsetsOut.size(); sample += framesPerBar)
    {
        const auto rounded = std::floor(sample + 0.5 + roundingTolerance);
        if (rounded >= static_cast<double>(numFrames))
            break;
        if (rounded >= 0.0)
            offsetsOut[count++] = static_cast<std::uint32_t>(rounded);
    }
    return count;
}

} // namespace visona
