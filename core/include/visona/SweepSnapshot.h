#pragma once

#include <visona/MidiClockTransport.h>
#include <visona/SweepBuffer.h>
#include <visona/TimeSignature.h>

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

    /** Free-running: frames per window. 0 while the sweep follows musical time. */
    std::uint64_t windowFrames = 0;

    /** Stream position of the next frame to analyze: every frame analyzed or dropped so far. */
    std::uint64_t nextSampleIndex = 0;

    /** The audio ring's counters for the stream (D-062). */
    std::uint64_t overruns = 0;
    std::uint64_t droppedFrames = 0;

    /** The MIDI Clock transport (architecture.md 3.3). The tempo is 0 when not known. */
    TransportState transportState = TransportState::waiting;
    double bpm = 0.0;
    std::int64_t nextTick = 0;
    TimeSignature timeSignature;

    /** Whether the sweep follows musical time; before the first Start it runs free. */
    bool musical = false;

    /** The selected window, an index into sweepWindowBars, and its length in ticks. */
    std::size_t window = 0;
    double windowTicks = 0.0;

    /** The tick at which the window the head is in starts, for bar numbers. */
    double windowStartTick = 0.0;

    /** MIDI Clock messages received, and Song Position Pointers ignored while running. */
    std::uint64_t midiEvents = 0;
    std::uint64_t ignoredSpp = 0;
};

} // namespace visona
