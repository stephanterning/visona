#include "visona/LaneMapping.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace visona
{

float DisplayGain::toLinear(int db) noexcept
{
    return std::pow(10.0f, static_cast<float>(db) / 20.0f);
}

LaneMapping::LaneMapping(int top, int height, float gain) noexcept
    : top_(top)
    , height_(std::max(height, 1))
    , gain_(gain)
{
    assert(height > 0);
}

int LaneMapping::rowOf(float value) const noexcept
{
    const auto halfHeight = static_cast<float>(height_) * 0.5f;
    const auto y = halfHeight - value * gain_ * halfHeight;
    if (std::isnan(y))
        return top_ + height_ / 2;
    const auto row = std::clamp(y, 0.0f, static_cast<float>(height_ - 1));
    return top_ + static_cast<int>(std::floor(row));
}

bool LaneMapping::isInside(float value) const noexcept
{
    return std::abs(value * gain_) <= 1.0f;
}

LaneRows LaneMapping::rowsOf(float min, float max) const noexcept
{
    assert(min <= max);
    return {rowOf(max), rowOf(min), max * gain_ > 1.0f, min * gain_ < -1.0f};
}

} // namespace visona
