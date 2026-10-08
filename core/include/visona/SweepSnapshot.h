#pragma once

#include <visona/BarPeaks.h>
#include <visona/MidiClockTransport.h>
#include <visona/SidechainSyncDetector.h>
#include <visona/SweepBuffer.h>
#include <visona/TimeSignature.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace visona
{

/**
    What the analysis thread hands the UI thread through a TripleBuffer: the sweep, the stream it
    comes from and the transport. The UI reads it in place. Display settings such as gain are not
    part of it (D-024).
*/
struct SweepSnapshot
{
    SweepBuffer sweep;

    /** Counts the audio streams the analysis has followed, so the UI can tell them apart. */
    std::uint64_t streamId = 0;

    /** Whether an audio stream is being analyzed. Without one, the sweep is empty. */
    bool hasStream = false;

    double sampleRate = 0.0;

    /** Stream position of the next frame to analyze: every frame analyzed or dropped so far. */
    std::uint64_t nextSampleIndex = 0;

    /** The audio ring's counters for the stream (D-062). */
    std::uint64_t overruns = 0;
    std::uint64_t droppedFrames = 0;

    /** The MIDI Clock transport (architecture.md 3.3). The tempo is the free tempo while the sweep
        runs free, and otherwise MIDI Clock's, or 0 when not known. */
    TransportState transportState = TransportState::freeRunning;
    double bpm = 0.0;
    std::int64_t nextTick = 0;
    TimeSignature timeSignature;

    /** Whether a sweep is running. It is always in bars, following MIDI Clock or running free at
        the free tempo (D-090); only before the stream's first audio is there none. */
    bool musical = false;

    /** The selected window, an index into sweepWindowBars, and its length in ticks. */
    std::size_t window = 0;
    double windowTicks = 0.0;

    /** The tick at which the window the head is in starts, for bar numbers. */
    double windowStartTick = 0.0;

    /** How many frames the low, mid and high band levels lag the full band, while band splitting
        is on, so the renderer can line the colours up with the shape (D-092). */
    std::array<double, 3> bandDelayFrames{};

    /** The peaks of the latest bars that have ended, for auto gain (D-100). */
    RecentBarPeaks barPeaks;

    /** MIDI Clock messages received, and Song Position Pointers ignored while running. */
    std::uint64_t midiEvents = 0;
    std::uint64_t ignoredSpp = 0;

    /** The app's sync input (D-109): its state, the frames the audio arrives after MIDI Clock's
        bar lines once locked, and the peak level of the last bar impulse. Off in the plugin,
        which measures its sidechain itself. */
    SidechainSyncState syncState = SidechainSyncState::off;
    double syncOffsetFrames = 0.0;
    float syncImpulsePeak = 0.0f;
};

} // namespace visona
