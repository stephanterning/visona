#include "visona/ColumnReduction.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace visona
{

namespace
{

double wrap(double position) noexcept
{
    return position - std::floor(position);
}

} // namespace

ColumnMapping::ColumnMapping(std::size_t numBins, std::size_t numColumns) noexcept
    : numBins_(std::max<std::size_t>(numBins, 1))
    , numColumns_(std::max<std::size_t>(numColumns, 1))
{
    assert(numBins > 0 && numColumns > 0);
}

ColumnMapping::ColumnMapping(std::size_t numBins, std::size_t numColumns, double offset,
                             double span) noexcept
    : ColumnMapping(numBins, numColumns)
{
    offset_ = std::isfinite(offset) ? wrap(offset) : 0.0;
    span_ = std::isfinite(span) ? std::clamp(span, 1.0 / static_cast<double>(numBins_), 1.0) : 1.0;
    zoomed_ = offset_ > 0.0 || span_ < 1.0;
}

std::size_t ColumnMapping::firstBin(std::size_t column) const noexcept
{
    if (!zoomed_)
        return column * numBins_ / numColumns_;
    const auto bins = static_cast<double>(numBins_);
    return static_cast<std::size_t>(
        std::floor(offset_ * bins +
                   static_cast<double>(column) * span_ * bins / static_cast<double>(numColumns_)));
}

std::size_t ColumnMapping::endBin(std::size_t column) const noexcept
{
    const auto next = column + 1 < numColumns_ || !zoomed_
                          ? firstBin(column + 1)
                          : static_cast<std::size_t>(
                                std::floor((offset_ + span_) * static_cast<double>(numBins_)));
    return std::max(next, firstBin(column) + 1);
}

std::size_t ColumnMapping::binsOf(std::size_t column,
                                  std::array<BinRange, 2>& ranges) const noexcept
{
    const auto first = firstBin(column);
    const auto end = endBin(column);
    if (end <= numBins_)
    {
        ranges[0] = {first, end};
        return 1;
    }
    if (first >= numBins_)
    {
        ranges[0] = {first - numBins_, end - numBins_};
        return 1;
    }
    ranges[0] = {first, numBins_};
    ranges[1] = {0, end - numBins_};
    return 2;
}

std::size_t ColumnMapping::columnsOf(std::size_t first, std::size_t last,
                                     std::array<ColumnRange, 2>& ranges) const noexcept
{
    assert(first < numBins_ && last < numBins_);
    const auto length = (last + numBins_ - first) % numBins_ + 1;
    const auto viewFirst = firstBin(0);
    const auto viewEnd = endBin(numColumns_ - 1);

    // Positions here are one window later than the view's own, so that the bins can be tried a
    // window earlier (their end, past the end of the window, may reach into the view), as they
    // are, and a window later (for a view that runs past the end of the window). Two arcs of a
    // circle meet in at most two arcs.
    std::size_t count = 0;
    for (const auto start : {first, first + numBins_, first + 2 * numBins_})
    {
        const auto from = std::max(start, viewFirst + numBins_);
        const auto to = std::min(start + length, viewEnd + numBins_);
        if (from >= to || count == ranges.size())
            continue;
        ranges[count++] = {firstColumnOfBin(from - numBins_), lastColumnOfBin(to - 1 - numBins_)};
    }
    if (count == 2 && ranges[1].first <= ranges[0].last + 1)
    {
        ranges[0] = {std::min(ranges[0].first, ranges[1].first),
                     std::max(ranges[0].last, ranges[1].last)};
        count = 1;
    }
    return count;
}

std::size_t ColumnMapping::firstColumn(std::size_t bin) const noexcept
{
    std::array<ColumnRange, 2> ranges;
    return columnsOf(bin, bin, ranges) > 0 ? ranges[0].first : 0;
}

std::size_t ColumnMapping::lastColumn(std::size_t bin) const noexcept
{
    std::array<ColumnRange, 2> ranges;
    const auto count = columnsOf(bin, bin, ranges);
    return count > 0 ? ranges[count - 1].last : 0;
}

double ColumnMapping::windowPositionOf(double column) const noexcept
{
    return wrap(offset_ + column / static_cast<double>(numColumns_) * span_);
}

double ColumnMapping::columnOf(double position) const noexcept
{
    const auto intoView = wrap(position - offset_);
    if (intoView >= span_)
        return -1.0;
    return intoView / span_ * static_cast<double>(numColumns_);
}

std::size_t ColumnMapping::firstColumnOfBin(std::size_t absoluteBin) const noexcept
{
    // An estimate from the column width, corrected against firstBin() and endBin() themselves.
    const auto binsPerColumn =
        span_ * static_cast<double>(numBins_) / static_cast<double>(numColumns_);
    auto column = static_cast<std::size_t>(
        std::clamp((static_cast<double>(absoluteBin) - offset_ * static_cast<double>(numBins_)) /
                       binsPerColumn,
                   0.0, static_cast<double>(numColumns_ - 1)));
    while (column > 0 && endBin(column - 1) > absoluteBin)
        --column;
    while (column + 1 < numColumns_ && endBin(column) <= absoluteBin)
        ++column;
    return column;
}

std::size_t ColumnMapping::lastColumnOfBin(std::size_t absoluteBin) const noexcept
{
    auto column = firstColumnOfBin(absoluteBin);
    while (column + 1 < numColumns_ && firstBin(column + 1) <= absoluteBin)
        ++column;
    return column;
}

void reduceColumns(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                   std::size_t firstColumn, std::span<ColumnSpan> out) noexcept
{
    assert(mapping.numBins() == sweep.numBins());
    assert(firstColumn + out.size() <= mapping.numColumns());

    const auto cells = sweep.channel(channel);
    const auto passes = sweep.passes();
    const auto headPass = sweep.pass();

    std::array<ColumnMapping::BinRange, 2> ranges;
    for (std::size_t index = 0; index < out.size(); ++index)
    {
        SweepCell current;
        SweepCell previous;
        const auto count = mapping.binsOf(firstColumn + index, ranges);
        for (std::size_t range = 0; range < count; ++range)
        {
            const auto end = std::min(ranges[range].end, cells.size());
            for (auto bin = ranges[range].first; bin < end; ++bin)
                (passes[bin] == headPass && headPass > 0 ? current : previous).merge(cells[bin]);
        }

        if (!current.isEmpty())
            out[index] = {current.min, current.max, ColumnSpan::Pass::current};
        else if (!previous.isEmpty())
            out[index] = {previous.min, previous.max, ColumnSpan::Pass::previous};
        else
            out[index] = {};
    }
}

void reduceColumnBands(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                       std::size_t firstColumn, const std::array<std::size_t, 3>& shift,
                       std::span<BandLevels> out) noexcept
{
    assert(mapping.numBins() == sweep.numBins());
    assert(firstColumn + out.size() <= mapping.numColumns());

    const auto bands = sweep.bands(channel);
    const auto passes = sweep.passes();
    const auto headPass = sweep.pass();
    const auto numBins = sweep.numBins();
    const auto delayed = [&](std::size_t bin, std::size_t band) -> const BandLevels&
    {
        const auto later = (bin + shift[band] % numBins) % numBins;
        return passes[later] == passes[bin] ? bands[later] : bands[bin];
    };

    std::array<ColumnMapping::BinRange, 2> ranges;
    for (std::size_t index = 0; index < out.size(); ++index)
    {
        BandLevels current;
        BandLevels previous;
        bool hasCurrent = false;
        const auto count = mapping.binsOf(firstColumn + index, ranges);
        for (std::size_t range = 0; range < count; ++range)
        {
            const auto end = std::min(ranges[range].end, numBins);
            for (auto bin = ranges[range].first; bin < end; ++bin)
            {
                if (passes[bin] == 0)
                    continue;
                const bool isCurrent = passes[bin] == headPass;
                hasCurrent = hasCurrent || isCurrent;
                auto& levels = isCurrent ? current : previous;
                levels.merge({delayed(bin, 0).low, delayed(bin, 1).mid, delayed(bin, 2).high});
            }
        }
        out[index] = hasCurrent ? current : previous;
    }
}

void sampleColumnEdges(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                       std::size_t firstColumn, std::span<float> out) noexcept
{
    assert(mapping.numBins() == sweep.numBins());

    const auto starts = sweep.starts(channel);
    const auto passes = sweep.passes();
    const auto numBins = static_cast<double>(sweep.numBins());
    for (std::size_t index = 0; index < out.size(); ++index)
    {
        const auto position =
            mapping.windowPositionOf(static_cast<double>(firstColumn + index)) * numBins;
        const auto bin = std::min(static_cast<std::size_t>(position), sweep.numBins() - 1);
        const auto next = (bin + 1) % sweep.numBins();
        const auto start = starts[bin];
        if (start == SweepBuffer::unknownStart)
        {
            out[index] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const auto end = starts[next];
        const auto fraction = static_cast<float>(position - std::floor(position));
        out[index] = end != SweepBuffer::unknownStart && passes[next] == passes[bin]
                         ? start + (end - start) * fraction
                         : start;
    }
}

} // namespace visona
