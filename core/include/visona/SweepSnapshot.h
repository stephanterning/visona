#pragma once

#include <visona/Band.h>
#include <visona/SweepBuffer.h>

#include <array>
#include <cstdint>

namespace visona
{

/**
    What the analysis thread hands the UI thread through a TripleBuffer: the sweep and the stream it
    comes from. The UI reads it in place. Display settings such as gain are not part of it (D-024).
*/
struct SweepSnapshot
{
    SweepBuffer sweep;

    /** Counts the audio streams the analysis has followed, so the UI can tell them apart. */
    std::uint64_t streamId = 0;

    /** Whether an audio stream is being analyzed. Without one, the sweep is empty. */
    bool hasStream = false;

    double sampleRate = 0.0;

    /** Frames per window of the sweep. */
    std::uint64_t windowFrames = 0;

    /** How many frames each split band (low, mid, high; see splitIndex()) lags the full band, for
        the renderer to make up for (D-074). */
    std::array<double, splitBands.size()> bandDelayFrames{};

    /** Stream position of the next frame to analyze: every frame analyzed or dropped so far. */
    std::uint64_t nextSampleIndex = 0;

    /** The audio ring's counters for the stream (D-062). */
    std::uint64_t overruns = 0;
    std::uint64_t droppedFrames = 0;
};

} // namespace visona
