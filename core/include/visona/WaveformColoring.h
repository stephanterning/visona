#pragma once

#include <visona/ColumnReduction.h>
#include <visona/LaneMapping.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace visona
{

/**
    How the waveform is coloured (D-056, D-093). The shape is the full-band span in every mode;
    only the colours inside it differ.
*/
enum class WaveformColoring : std::uint8_t
{
    /** Mono/precise: the full band in one neutral colour. */
    precise,
    /** One colour per column, mixed from the bands' shares of the column. */
    blended,
    /** The bands' envelopes drawn inside the full-band outline, lows behind mids behind highs. */
    layered
};

/** What a run of rows is painted with. The UI maps each ink to a colour of its palette. */
enum class Ink : std::uint8_t
{
    neutral,
    low,
    mid,
    high,
    /** The bands mixed by ColumnPaint::blend. */
    blend
};

/** The ink of a split band. */
[[nodiscard]] constexpr Ink inkOf(Band band) noexcept
{
    switch (band)
    {
    case Band::low:
        return Ink::low;
    case Band::mid:
        return Ink::mid;
    case Band::high:
        return Ink::high;
    case Band::full:
        break;
    }
    return Ink::neutral;
}

/** How to paint the waveform in one pixel column of a lane: runs of rows, painted in order. */
struct ColumnPaint
{
    struct Run
    {
        int top = 0;
        int bottom = -1;
        Ink ink = Ink::neutral;

        friend bool operator==(const Run&, const Run&) = default;
    };

    /** At most a run over the whole span, then one per band. */
    std::array<Run, 1 + splitBands.size()> runs{};
    std::size_t count = 0;

    /** For Ink::blend: the weight of each split band (see splitIndex()), summing to 1. */
    std::array<float, splitBands.size()> blend{};
};

/**
    Paints the rows [top, bottom] of a column, which the full-band span covers (D-056):

    - precise: the rows in the neutral ink.
    - blended: the rows in one blended ink, with each band weighted by its level squared, i.e. by
      its share of the column's energy.
    - layered: the rows in the ink of the loudest band, then each band's envelope, from -level to
      +level, in the order low, mid, high, cut to the rows. A later band hides an earlier one where
      they overlap; an envelope thinner than one row is left out.

    Without band levels (all negative), every mode paints the neutral ink. In every mode the first
    run covers exactly [top, bottom] and no run reaches beyond it, so the painted shape never
    depends on the colouring. `lane` maps the envelopes to rows. Nothing is painted if top > bottom.
*/
[[nodiscard]] ColumnPaint paintColumn(WaveformColoring coloring, int top, int bottom,
                                      const BandLevels& levels, const LaneMapping& lane) noexcept;

} // namespace visona
