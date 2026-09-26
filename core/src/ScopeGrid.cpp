#include "visona/ScopeGrid.h"

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

std::span<const AmplitudeReference> amplitudeReferencesAt(int gainDb) noexcept
{
    std::size_t count = 0;
    while (count < amplitudeReferences.size() && amplitudeReferences[count].minGainDb <= gainDb)
        ++count;
    return std::span(amplitudeReferences).first(count);
}

} // namespace visona
