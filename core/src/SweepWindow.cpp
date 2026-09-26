#include "visona/SweepWindow.h"

#include <algorithm>
#include <cmath>

namespace visona
{

double clampFreeBpm(double bpm) noexcept
{
    if (!std::isfinite(bpm))
        return defaultFreeBpm;
    return std::clamp(std::round(bpm * 10.0) / 10.0, minFreeBpm, maxFreeBpm);
}

} // namespace visona
