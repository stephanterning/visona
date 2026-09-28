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
                                                     std::uint64_t referenceSample,
                                                     double barPpq, double bpm,
                                                     double sampleRate) noexcept
{
    return static_cast<double>(referenceSample) +
           (barPpq - ppqAtReference) * samplesPerQuarterNote(bpm, sampleRate);
}

void BarImpulseScheduler::impulsesInBlock(std::uint64_t blockStartSample,
                                          std::uint32_t numFrames, double ppqAtBlockEnd,
                                          double bpm, TimeSignature timeSignature,
                                          double sampleRate, std::vector<std::uint32_t>& offsetsOut)
{
    if (numFrames == 0 || bpm <= 0.0 || sampleRate <= 0.0)
        return;

    const auto blockEndSample = blockStartSample + numFrames;
    const auto ppqAtBlockStart =
        ppqAtBlockEnd - static_cast<double>(numFrames) / samplesPerQuarterNote(bpm, sampleRate);
    const auto barLength = quarterNotesPerBar(timeSignature);

    const auto firstBar =
        static_cast<std::int64_t>(std::floor(ppqAtBlockStart / barLength + 1.0e-12));
    const auto lastBar =
        static_cast<std::int64_t>(std::floor(ppqAtBlockEnd / barLength + 1.0e-12));

    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto barPpq = static_cast<double>(bar) * barLength;
        const auto sample = sampleIndexOfBarBoundary(ppqAtBlockEnd, blockEndSample, barPpq, bpm,
                                                     sampleRate);
        if (sample < static_cast<double>(blockStartSample) ||
            sample >= static_cast<double>(blockEndSample))
            continue;
        const auto boundarySample = static_cast<std::uint64_t>(std::llround(sample));
        if (boundarySample < blockStartSample)
            continue;
        const auto offset = static_cast<std::uint32_t>(boundarySample - blockStartSample);
        if (offset < numFrames)
            offsetsOut.push_back(offset);
    }
}

} // namespace visona
