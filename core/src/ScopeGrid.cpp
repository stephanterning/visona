#include "visona/ScopeGrid.h"

#include <algorithm>
#include <cmath>

namespace visona
{

int gridDivisionFor(double visibleBars) noexcept
{
    // A little slack, so that a whole window, or a zoom that lands on one, is not tipped over by
    // rounding.
    constexpr double tolerance = 1.0e-9;
    if (visibleBars <= 0.25 + tolerance)
        return 64;
    if (visibleBars <= 0.5 + tolerance)
        return 32;
    if (visibleBars <= 1.0 + tolerance)
        return 16;
    if (visibleBars <= 2.0 + tolerance)
        return 8;
    return 4;
}

double divisionMilliseconds(int division, double bpm) noexcept
{
    if (!(bpm > 0.0) || division <= 0)
        return 0.0;
    return 60000.0 / bpm * 4.0 / static_cast<double>(division);
}

AmplitudeReference AmplitudeReferences::Iterator::operator*() const noexcept
{
    return {db, std::pow(10.0f, static_cast<float>(db) / 20.0f)};
}

AmplitudeReferences amplitudeReferencesAt(int gainDb, int laneHeight) noexcept
{
    const auto first = -((std::max(0, gainDb) + 2) / 3) * 3;
    const auto extent = 20.0 * std::log10(std::max(1.0, static_cast<double>(laneHeight - 1) / 2.0));
    const auto last =
        -static_cast<int>(std::floor((static_cast<double>(gainDb) + extent) / 3.0)) * 3;
    return {first, std::min(first, last) - 3};
}

} // namespace visona
