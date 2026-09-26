#include "visona/WaveformColoring.h"

#include <algorithm>
#include <iterator>

namespace visona
{

ColumnPaint paintColumn(WaveformColoring coloring, int top, int bottom, const BandLevels& levels,
                        const LaneMapping& lane) noexcept
{
    ColumnPaint paint;
    const auto add = [&](int runTop, int runBottom, Ink ink)
    {
        runTop = std::max(runTop, top);
        runBottom = std::min(runBottom, bottom);
        if (runTop <= runBottom)
            paint.runs[paint.count++] = {runTop, runBottom, ink};
    };

    const auto loudest = static_cast<std::size_t>(
        std::distance(levels.begin(), std::max_element(levels.begin(), levels.end())));
    if (coloring == WaveformColoring::precise || !(levels[loudest] > 0.0f))
    {
        add(top, bottom, Ink::neutral);
        return paint;
    }

    if (coloring == WaveformColoring::blended)
    {
        float total = 0.0f;
        for (std::size_t band = 0; band < levels.size(); ++band)
        {
            const auto level = std::max(levels[band], 0.0f);
            paint.blend[band] = level * level;
            total += paint.blend[band];
        }
        for (auto& weight : paint.blend)
            weight /= total;
        add(top, bottom, Ink::blend);
        return paint;
    }

    add(top, bottom, inkOf(splitBands[loudest]));
    for (std::size_t band = 0; band < levels.size(); ++band)
    {
        const auto level = levels[band];
        // The envelope spans 2 · level · rowsPerUnit rows.
        if (!(2.0f * level * lane.rowsPerUnit() >= 1.0f))
            continue;
        const auto rows = lane.rowsOf(-level, level);
        add(rows.top, rows.bottom, inkOf(splitBands[band]));
    }
    return paint;
}

} // namespace visona
