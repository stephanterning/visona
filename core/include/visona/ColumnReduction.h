#pragma once

#include <visona/SweepBuffer.h>

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
        previous, ///< Only bins of earlier passes: drawn dimmed.
        current   ///< Bins of the head's pass: drawn at full brightness.
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

} // namespace visona
