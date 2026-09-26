#include "visona/ColumnReduction.h"

#include <algorithm>
#include <cassert>

namespace visona
{

ColumnMapping::ColumnMapping(std::size_t numBins, std::size_t numColumns) noexcept
    : numBins_(std::max<std::size_t>(numBins, 1))
    , numColumns_(std::max<std::size_t>(numColumns, 1))
{
    assert(numBins > 0 && numColumns > 0);
}

std::size_t ColumnMapping::firstBin(std::size_t column) const noexcept
{
    return column * numBins_ / numColumns_;
}

std::size_t ColumnMapping::endBin(std::size_t column) const noexcept
{
    return std::max((column + 1) * numBins_ / numColumns_, firstBin(column) + 1);
}

std::size_t ColumnMapping::firstColumn(std::size_t bin) const noexcept
{
    // The first column whose bins reach past `bin`.
    if (numColumns_ > numBins_)
        return (bin * numColumns_ + numBins_ - 1) / numBins_;
    return ((bin + 1) * numColumns_ + numBins_ - 1) / numBins_ - 1;
}

std::size_t ColumnMapping::lastColumn(std::size_t bin) const noexcept
{
    // The last column whose first bin is not after `bin`.
    return ((bin + 1) * numColumns_ + numBins_ - 1) / numBins_ - 1;
}

void reduceColumns(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                   std::size_t firstColumn, std::span<ColumnSpan> out) noexcept
{
    assert(mapping.numBins() == sweep.numBins());
    assert(firstColumn + out.size() <= mapping.numColumns());

    const auto cells = sweep.channel(channel);
    const auto passes = sweep.passes();
    const auto headPass = sweep.pass();

    for (std::size_t index = 0; index < out.size(); ++index)
    {
        const auto column = firstColumn + index;
        SweepCell current;
        SweepCell previous;
        const auto end = std::min(mapping.endBin(column), cells.size());
        for (auto bin = mapping.firstBin(column); bin < end; ++bin)
            (passes[bin] == headPass && headPass > 0 ? current : previous).merge(cells[bin]);

        if (!current.isEmpty())
            out[index] = {current.min, current.max, ColumnSpan::Pass::current};
        else if (!previous.isEmpty())
            out[index] = {previous.min, previous.max, ColumnSpan::Pass::previous};
        else
            out[index] = {};
    }
}

} // namespace visona
