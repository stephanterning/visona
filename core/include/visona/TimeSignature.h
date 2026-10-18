#pragma once

namespace visona
{

/**
    A time signature, numerator over denominator. MIDI Clock carries none, so 4/4 is the default,
    but the transport and the sweep count in ticks per bar derived from it (D-017).
*/
struct TimeSignature
{
    /** MIDI Clock sends 24 clocks per quarter note (D-015). */
    static constexpr int ticksPerQuarterNote = 24;

    int numerator = 4;
    int denominator = 4;

    /** Supported signatures have whole MIDI ticks per beat and a bounded numerator. */
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return numerator >= 1 && numerator <= 64 && denominator >= 1 && denominator <= 32 &&
               (denominator & (denominator - 1)) == 0;
    }

    /** 96 in 4/4, 72 in 3/4 and 6/8. */
    [[nodiscard]] constexpr int ticksPerBar() const noexcept
    {
        return ticksPerQuarterNote * numerator * 4 / denominator;
    }

    /** One beat is one denominator note: 24 ticks in 4/4, 12 in 6/8. */
    [[nodiscard]] constexpr int ticksPerBeat() const noexcept
    {
        return ticksPerQuarterNote * 4 / denominator;
    }

    friend constexpr bool operator==(const TimeSignature&, const TimeSignature&) = default;
};

} // namespace visona
