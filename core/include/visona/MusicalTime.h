#pragma once

#include <cstdint>

namespace visona
{

/** MIDI Clock resolution: Clock messages per quarter note (24 PPQN). Positions count these. */
inline constexpr int ticksPerQuarterNote = 24;

/** Ticks per Song Position Pointer unit. SPP counts MIDI beats, which are sixteenth notes. */
inline constexpr int ticksPerSppUnit = ticksPerQuarterNote / 4;

/**
    A time signature (D-017). 4/4 is the MVP default; the model allows others.

    A beat is one note of the denominator's value, so 4/4 has four beats of 24 ticks and 6/8 has
    six beats of 12 ticks. That needs a whole number of ticks per beat, so the denominator must be
    1, 2, 4, 8, 16 or 32.
*/
struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
        return numerator >= 1 && powerOfTwo && denominator <= 32;
    }

    [[nodiscard]] constexpr int ticksPerBeat() const noexcept
    {
        return ticksPerQuarterNote * 4 / denominator;
    }

    /** 24 × numerator × 4 / denominator: 96 in 4/4. */
    [[nodiscard]] constexpr int ticksPerBar() const noexcept
    {
        return ticksPerBeat() * numerator;
    }

    friend bool operator==(const TimeSignature&, const TimeSignature&) = default;
};

/** A position as a musician counts it. Bars and beats count from 1: tick 0 is bar 1, beat 1. */
struct MusicalPosition
{
    std::int64_t bar = 1;
    int beat = 1;

    /** Ticks since the start of the beat, from 0 up to, but not including, ticksPerBeat(). */
    double tickInBeat = 0.0;

    friend bool operator==(const MusicalPosition&, const MusicalPosition&) = default;
};

/** The bar and beat of `ticks`, a position of 0 or more. `timeSignature` must be valid. */
[[nodiscard]] MusicalPosition musicalPosition(double ticks, TimeSignature timeSignature) noexcept;

} // namespace visona
