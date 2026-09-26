#include "visona/ColumnReduction.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace visona
{

namespace
{

/** `frames` as a whole number of bins, at most one window. */
std::int64_t framesToBins(double frames, std::size_t numBins, std::uint64_t windowFrames,
                          bool roundUp) noexcept
{
    if (!(frames > 0.0) || windowFrames == 0)
        return 0;
    const auto bins = frames * static_cast<double>(numBins) / static_cast<double>(windowFrames);
    const auto rounded = roundUp ? std::ceil(bins) : std::round(bins);
    return static_cast<std::int64_t>(std::min(rounded, static_cast<double>(numBins)));
}

bool isHeadPass(std::uint64_t pass, std::uint64_t headPass) noexcept
{
    return headPass > 0 && pass == headPass;
}

} // namespace

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
            (isHeadPass(passes[bin], headPass) ? current : previous).merge(cells[bin]);

        if (!current.isEmpty())
            out[index] = {current.min, current.max, ColumnSpan::Pass::current};
        else if (!previous.isEmpty())
            out[index] = {previous.min, previous.max, ColumnSpan::Pass::previous};
        else
            out[index] = {};
    }
}

BandReading bandReadingFor(double sampleRate,
                           const std::array<double, splitBands.size()>& delayFrames,
                           bool compensateDelay) noexcept
{
    BandReading reading;
    for (std::size_t band = 0; band < splitBands.size(); ++band)
    {
        reading.delayFrames[band] = compensateDelay ? delayFrames[band] : 0.0;
        reading.holdFrames[band] = sampleRate > 0.0 ? sampleRate / (4.0 * bandHoldHz[band]) : 0.0;
    }
    return reading;
}

void reduceBandLevels(const SweepBuffer& sweep, std::size_t channel, const ColumnMapping& mapping,
                      std::uint64_t windowFrames, const BandReading& reading,
                      std::size_t firstColumn, std::span<BandLevels> out) noexcept
{
    assert(mapping.numBins() == sweep.numBins());
    assert(firstColumn + out.size() <= mapping.numColumns());

    const auto numBins = sweep.numBins();
    const auto bins = static_cast<std::int64_t>(numBins);
    const auto full = sweep.channel(channel);
    const auto passes = sweep.passes();
    const auto headPass = sweep.pass();

    std::array<std::span<const SweepCell>, splitBands.size()> bandCells;
    std::array<std::int64_t, splitBands.size()> shift{};
    std::array<std::int64_t, splitBands.size()> hold{};
    for (const auto band : splitBands)
    {
        const auto index = splitIndex(band);
        bandCells[index] = sweep.band(channel, band);
        shift[index] = framesToBins(reading.delayFrames[index], numBins, windowFrames, false);
        hold[index] = framesToBins(reading.holdFrames[index], numBins, windowFrames, true);
    }

    for (std::size_t index = 0; index < out.size(); ++index)
    {
        const auto column = firstColumn + index;
        const auto first = mapping.firstBin(column);
        const auto end = std::min(mapping.endBin(column), numBins);

        // The stream positions, in bins, of the bins the column shows (see reduceColumns()).
        bool showsHeadPass = false;
        for (auto bin = first; bin < end && !showsHeadPass; ++bin)
            showsHeadPass = isHeadPass(passes[bin], headPass) && !full[bin].isEmpty();
        auto lowest = std::numeric_limits<std::int64_t>::max();
        auto highest = std::numeric_limits<std::int64_t>::min();
        for (auto bin = first; bin < end; ++bin)
        {
            if (isHeadPass(passes[bin], headPass) != showsHeadPass || full[bin].isEmpty())
                continue;
            const auto position =
                static_cast<std::int64_t>(passes[bin]) * bins + static_cast<std::int64_t>(bin);
            lowest = std::min(lowest, position);
            highest = std::max(highest, position);
        }

        auto& levels = out[index];
        levels.fill(-1.0f);
        if (lowest > highest)
            continue;

        for (std::size_t band = 0; band < splitBands.size(); ++band)
        {
            const auto last = highest + shift[band] + hold[band];
            const auto from =
                std::max({lowest + shift[band] - hold[band], last - bins + 1, std::int64_t{0}});
            auto peak = -1.0f;
            auto bin = static_cast<std::size_t>(from % bins);
            auto pass = static_cast<std::uint64_t>(from / bins);
            for (auto position = from; position <= last; ++position)
            {
                const auto& cell = bandCells[band][bin];
                if (passes[bin] == pass && !cell.isEmpty())
                    peak = std::max({peak, std::abs(cell.min), std::abs(cell.max)});
                if (++bin == numBins)
                {
                    bin = 0;
                    ++pass;
                }
            }
            levels[band] = peak;
        }
    }
}

std::size_t bandReachBins(const BandReading& reading, std::size_t numBins,
                          std::uint64_t windowFrames) noexcept
{
    std::int64_t reach = 0;
    for (std::size_t band = 0; band < splitBands.size(); ++band)
        reach = std::max(reach,
                         framesToBins(reading.delayFrames[band], numBins, windowFrames, false) +
                             framesToBins(reading.holdFrames[band], numBins, windowFrames, true));
    return static_cast<std::size_t>(reach);
}

} // namespace visona
