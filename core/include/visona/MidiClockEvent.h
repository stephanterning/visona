#pragma once

#include <visona/SpscQueue.h>

#include <cstdint>

namespace visona
{

/** A MIDI Clock real-time message or Song Position Pointer. The MIDI thread pushes one per
    message. */
struct MidiClockEvent
{
    enum class Type : std::uint8_t
    {
        Clock,              ///< F8, sent 24 times per quarter note
        Start,              ///< FA
        Continue,           ///< FB
        Stop,               ///< FC
        SongPositionPointer ///< F2, the position is in sppValue
    };

    /** The largest Song Position Pointer value (14 bits). */
    static constexpr std::uint16_t maxSppValue = 16383;

    Type type = Type::Clock;

    /** Song Position Pointer only: the position in MIDI beats, 0..maxSppValue. One MIDI beat is a
        sixteenth note, or 6 clock ticks. 0 for every other type. */
    std::uint16_t sppValue = 0;

    /** Host time of the message in nanoseconds, on the same clock as BlockTiming::hostTimeNs. */
    std::uint64_t hostTimeNs = 0;

    friend bool operator==(const MidiClockEvent&, const MidiClockEvent&) = default;
};

/** Carries MidiClockEvents from the MIDI thread to the analysis thread. */
using MidiClockQueue = SpscQueue<MidiClockEvent>;

} // namespace visona
