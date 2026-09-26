#pragma once

#include <visona/SweepBuffer.h>

#include <cstdint>

namespace visona
{

/**
    How the waveform is drawn (D-091). The shape is always the full-band signal (D-056).

    - standard: a thin line through the signal, sampled once per pixel column. The cheapest to
      draw, but with many samples per column it can miss peaks.
    - precise: the full-band signed min/max of every column, filled (D-050). Nothing is missed.
    - dj: precise, coloured by frequency: bass red, mids green and highs blue (D-092).

    Standard and precise use the waveform colour chosen in the settings.
*/
enum class WaveformMode : std::uint8_t
{
    standard,
    precise,
    dj
};

/** A colour with components from 0 to 1. */
struct Rgb
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/**
    The DJ colour of a column from its band levels (D-092), in the way of Oszillos Mega Scope: the
    three bands mixed as red, green and blue.

    Each band is weighted, since music has far more energy in the bass than in the highs, and
    squared, so that the strongest band sets the hue: a kick is red, a kick with mids orange, a
    hi-hat blue. The mix is scaled so that its brightest component is 1, so the colour shows the
    balance between the bands and never the level; the height already shows that. Levels with no
    band above 0 give black.
*/
[[nodiscard]] Rgb djColour(const BandLevels& levels) noexcept;

} // namespace visona
