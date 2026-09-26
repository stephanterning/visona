#pragma once

#include <visona/SweepBuffer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace visona
{

/**
    How the bins of a sweep map to the pixel columns of a display `numColumns` wide.

    The display shows the whole window, or, zoomed in, the part of it that starts at `offset` and
    is `span` long, both as fractions of the window (D-085). A zoomed view may run past the end of
    the window and carry on at its start, so that the downbeat can be seen from both sides.

    Column x shows bins [firstBin(x), endBin(x)), counted from the start of the window and, in a
    view that runs past its end, beyond numBins; binsOf() gives them as bins of the window. With
    more bins than columns, the columns divide the bins between them without overlap. With more
    columns than bins, each column shows one bin and neighbouring columns may show the same bin.
*/
class ColumnMapping
{
public:
    /** Bins [first, end) of the window. */
    struct BinRange
    {
        std::size_t first = 0;
        std::size_t end = 0;
    };

    /** Columns [first, last]. */
    struct ColumnRange
    {
        std::size_t first = 0;
        std::size_t last = 0;
    };

    /** The whole window. Both counts must be at least 1. */
    ColumnMapping(std::size_t numBins, std::size_t numColumns) noexcept;

    /** The part of the window from `offset`, taken modulo 1, that is `span` long, clamped to
        (0, 1]. */
    ColumnMapping(std::size_t numBins, std::size_t numColumns, double offset, double span) noexcept;

    [[nodiscard]] std::size_t numBins() const noexcept
    {
        return numBins_;
    }

    [[nodiscard]] std::size_t numColumns() const noexcept
    {
        return numColumns_;
    }

    [[nodiscard]] double offset() const noexcept
    {
        return offset_;
    }

    [[nodiscard]] double span() const noexcept
    {
        return span_;
    }

    [[nodiscard]] bool isZoomed() const noexcept
    {
        return zoomed_;
    }

    [[nodiscard]] std::size_t firstBin(std::size_t column) const noexcept;
    [[nodiscard]] std::size_t endBin(std::size_t column) const noexcept;

    /** The bins column `column` shows, as one range, or two where the column crosses the end of
        the window. Returns the number of ranges. */
    std::size_t binsOf(std::size_t column, std::array<BinRange, 2>& ranges) const noexcept;

    /**
        The columns that show any of the bins from `first` forward to `last`, wrapping around the
        end of the window if `last` comes before `first`. Returns up to two ranges; none if the
        bins are out of view.
    */
    std::size_t columnsOf(std::size_t first, std::size_t last,
                          std::array<ColumnRange, 2>& ranges) const noexcept;

    /** The first and last columns that show `bin`, which must be in view. */
    [[nodiscard]] std::size_t firstColumn(std::size_t bin) const noexcept;
    [[nodiscard]] std::size_t lastColumn(std::size_t bin) const noexcept;

    /** The fraction of the window at column position `column`, from 0 at the left edge of column
        0 to numColumns at the right edge of the last column, modulo 1. */
    [[nodiscard]] double windowPositionOf(double column) const noexcept;

    /** The column position of window fraction `position`, or a negative number if it is out of
        view. */
    [[nodiscard]] double columnOf(double position) const noexcept;

private:
    [[nodiscard]] std::size_t firstColumnOfBin(std::size_t absoluteBin) const noexcept;
    [[nodiscard]] std::size_t lastColumnOfBin(std::size_t absoluteBin) const noexcept;

    std::size_t numBins_;
    std::size_t numColumns_;
    double offset_ = 0.0;
    double span_ = 1.0;
    bool zoomed_ = false;
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
    The band levels of the columns [firstColumn, firstColumn + out.size()) of `channel`, for DJ
    colouring (D-092): the peak of each band over the column's bins, from the head's pass where
    the column has any, like reduceColumns().

    Each band is read `shift` bins later, low, mid and high, to make up for the band's delay
    behind the full band, but only from bins of the same pass, so that a column at the head never
    takes the previous pass's colour.
*/
void reduceColumnBands(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                       std::size_t firstColumn, const std::array<std::size_t, 3>& shift,
                       std::span<BandLevels> out) noexcept;

/**
    The signal of `channel` at the column edges [firstColumn, firstColumn + out.size()), where edge
    x is the left edge of column x, for drawing the waveform as a line (D-091). Between the starts
    of two neighbouring bins of the same pass the value is interpolated, which is exact at deep
    zoom, where the bins hold the lines between samples (D-084). With many bins per column it
    samples the signal once per column, which is cheap but may miss peaks. NaN where the bin is
    empty.
*/
void sampleColumnEdges(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                       std::size_t firstColumn, std::span<float> out) noexcept;

} // namespace visona
