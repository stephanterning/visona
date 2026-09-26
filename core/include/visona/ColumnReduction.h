#pragma once

#include <visona/Band.h>
#include <visona/SweepBuffer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace visona
{

/**
    How the bins of a sweep map to the pixel columns of a display `numColumns` wide.

    Column x shows bins [firstBin(x), endBin(x)). With more bins than columns, the columns divide
    the bins between them without overlap. With more columns than bins, each column shows one bin
    and neighbouring columns may show the same bin.
*/
class ColumnMapping
{
public:
    /** Both counts must be at least 1. */
    ColumnMapping(std::size_t numBins, std::size_t numColumns) noexcept;

    [[nodiscard]] std::size_t numBins() const noexcept
    {
        return numBins_;
    }

    [[nodiscard]] std::size_t numColumns() const noexcept
    {
        return numColumns_;
    }

    [[nodiscard]] std::size_t firstBin(std::size_t column) const noexcept;
    [[nodiscard]] std::size_t endBin(std::size_t column) const noexcept;

    /** The first and last columns that show `bin`. */
    [[nodiscard]] std::size_t firstColumn(std::size_t bin) const noexcept;
    [[nodiscard]] std::size_t lastColumn(std::size_t bin) const noexcept;

private:
    std::size_t numBins_;
    std::size_t numColumns_;
};

/** What one pixel column of one channel shows. */
struct ColumnSpan
{
    enum class Pass : std::uint8_t
    {
        none,     ///< Nothing to draw: every bin of the column is empty.
        previous, ///< Only bins of earlier passes.
        current   ///< Bins of the head's pass.
    };

    float min = 0.0f;
    float max = 0.0f;
    Pass pass = Pass::none;

    friend bool operator==(const ColumnSpan&, const ColumnSpan&) = default;
};

/**
    Reduces the bins of `channel` to the columns [firstColumn, firstColumn + out.size()).

    Each column gets the min and max of its non-empty bins of the head's pass, if it has any, and
    otherwise of its non-empty bins of earlier passes. The column at the head therefore shows only
    the new pass. The span is the waveform's shape in every drawing mode (D-056).
*/
void reduceColumns(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                   std::size_t firstColumn, std::span<ColumnSpan> out) noexcept;

/**
    How the renderer reads the split bands for the frequency colouring (D-092). Presentation only:
    it never changes the sweep, and the full band is always read as it is. Arrays are indexed by
    splitIndex().
*/
struct BandReading
{
    /** Read each band this many frames after the column, to make up for its group delay. */
    std::array<double, splitBands.size()> delayFrames{};

    /** Take each band's peak over this many frames on either side of the column, so that the
        band's level does not ripple with its own oscillation. */
    std::array<double, splitBands.size()> holdFrames{};
};

/** The lowest frequency whose level holdFrames keeps free of ripple, per split band. */
inline constexpr std::array<double, splitBands.size()> bandHoldHz{50.0, 200.0, 2'500.0};

/**
    The reading the renderer uses at `sampleRate`: each band held over a quarter period of its
    bandHoldHz on either side, and, if `compensateDelay`, read later by `delayFrames` (from the
    snapshot).
*/
[[nodiscard]] BandReading bandReadingFor(double sampleRate,
                                         const std::array<double, splitBands.size()>& delayFrames,
                                         bool compensateDelay) noexcept;

/** For each split band, its peak absolute value in a column, or -1 if the column has no data of
    that band. */
using BandLevels = std::array<float, splitBands.size()>;

/**
    Reduces the split bands of `channel` to the levels of the columns [firstColumn, firstColumn +
    out.size()) of a sweep whose window is `windowFrames` long.

    A column covers the same bins as in reduceColumns(). Each band is read over those bins' stream
    positions, moved later by the band's delay and widened by its hold on both sides. Only bins that
    still hold those positions count: bins the head has not reached yet, or has already written
    again in a later pass, are left out. The level of a column behind the head can therefore still
    rise until the head is bandReachBins() past it.
*/
void reduceBandLevels(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                      std::uint64_t windowFrames, const BandReading& reading,
                      std::size_t firstColumn, std::span<BandLevels> out) noexcept;

/** How far past a bin, in bins, reduceBandLevels() reads with `reading`: the delay plus the hold of
    the band that reaches furthest. */
[[nodiscard]] std::size_t bandReachBins(const BandReading& reading, std::size_t numBins,
                                        std::uint64_t windowFrames) noexcept;

} // namespace visona
