#pragma once

#include <visona/TimeSignature.h>

#include <cstdint>
#include <span>
#include <vector>

namespace visona
{

/** Peak amplitude for a Visona Sync bar impulse (−6 dBFS). */
constexpr float syncImpulseAmplitude = 0.501187233f;

/**
    Places one-sample bar impulses on the host musical grid.

    PPQ positions are quarter-note beats from the song start, matching JUCE host playheads.
*/
class BarImpulseScheduler
{
public:
    /**
        Appends sample offsets within `[0, numFrames)` where a bar impulse should fire.
        `ppqAtBlockStart` is the host PPQ at the first frame of this block. Ableton Live
        reports playhead PPQ at block start; anchoring placement there keeps impulses on the
        arrangement grid instead of one buffer late.
    */
    static void impulsesInBlock(std::uint64_t blockStartSample, std::uint32_t numFrames,
                                double ppqAtBlockStart, double bpm, TimeSignature timeSignature,
                                double sampleRate, std::vector<std::uint32_t>& offsetsOut);

    /** The stream sample index of the bar boundary for `ppq`. */
    [[nodiscard]] static double sampleIndexOfBarBoundary(double ppqAtReference,
                                                         std::uint64_t referenceSample,
                                                         double barPpq, double bpm,
                                                         double sampleRate) noexcept;
};

} // namespace visona
