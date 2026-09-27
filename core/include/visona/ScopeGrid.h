#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace visona
{

/**
    The finest note value the grid marks with `visibleBars` bars in view, as the denominator of
    the note: 4 for quarter notes, 8 for eighths, and so on down to 64 (D-087).

    Up to ¼ bar in view gives 64, up to ½ bar 32, up to 1 bar 16, up to 2 bars 8, and more than
    that 4. Each window shows its own value unzoomed, and zooming in makes the grid finer.
*/
[[nodiscard]] int gridDivisionFor(double visibleBars) noexcept;

/** How long one 1/`division` note lasts at `bpm` quarter notes per minute, in milliseconds, or 0
    if the tempo is not known. */
[[nodiscard]] double divisionMilliseconds(int division, double bpm) noexcept;

/** A faint amplitude reference line: where a level lands after display gain, and the display gain
    from which it is shown (D-088). */
struct AmplitudeReference
{
    int levelDb = 0;
    float level = 1.0f;
    int minGainDb = 0;
};

/** 0 dBFS and −6 dBFS always, −12 dBFS from +4 dB of display gain and −18 dBFS from +10 dB. */
inline constexpr std::array<AmplitudeReference, 4> amplitudeReferences{{
    {0, 1.0f, 0},
    {-6, 0.501187f, 0},
    {-12, 0.251189f, 4},
    {-18, 0.125893f, 10},
}};

/** The references shown at `gainDb` of display gain, loudest first. */
[[nodiscard]] std::span<const AmplitudeReference> amplitudeReferencesAt(int gainDb) noexcept;

} // namespace visona
