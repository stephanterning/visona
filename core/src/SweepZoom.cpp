#include "visona/SweepZoom.h"

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

constexpr double minSpan = 1.0 / maxSweepZoom;

// A span this close to 1 is the whole window, so that zooming out lands there despite rounding.
constexpr double wholeWindowTolerance = 1.0e-9;

double wrap(double position) noexcept
{
    const auto wrapped = position - std::floor(position);
    return wrapped < 1.0 ? wrapped : 0.0;
}

} // namespace

SweepZoom SweepZoom::normalized(SweepZoom zoom) noexcept
{
    if (!std::isfinite(zoom.offset) || !std::isfinite(zoom.span) ||
        zoom.span >= 1.0 - wholeWindowTolerance)
        return {};
    return {wrap(zoom.offset), std::max(zoom.span, minSpan)};
}

SweepZoom SweepZoom::zoomedAround(double anchor, double factor) const noexcept
{
    if (!std::isfinite(anchor) || !std::isfinite(factor) || !(factor > 0.0))
        return *this;
    anchor = std::clamp(anchor, 0.0, 1.0);

    const auto newSpan = std::clamp(span / factor, minSpan, 1.0);
    auto newOffset = offset + anchor * (span - newSpan);
    if (newSpan > span)
    {
        // The share of the remaining way out that this step covers, applied to the shortest turn
        // back to an offset of 0.
        const auto share = (newSpan - span) / (1.0 - span);
        newOffset = wrap(newOffset);
        const auto turn = newOffset < 0.5 ? -newOffset : 1.0 - newOffset;
        newOffset += share * turn;
    }
    return normalized({newOffset, newSpan});
}

SweepZoom SweepZoom::selected(double from, double to) const noexcept
{
    if (!std::isfinite(from) || !std::isfinite(to))
        return *this;
    const auto first = std::clamp(std::min(from, to), 0.0, 1.0);
    const auto last = std::clamp(std::max(from, to), 0.0, 1.0);

    auto newOffset = offset + first * span;
    auto newSpan = (last - first) * span;
    if (newSpan < minSpan)
    {
        newOffset -= (minSpan - newSpan) / 2.0;
        newSpan = minSpan;
    }
    return normalized({newOffset, newSpan});
}

SweepZoom SweepZoom::panned(double distance) const noexcept
{
    if (!std::isfinite(distance) || !isZoomed())
        return *this;
    return normalized({offset + distance, span});
}

SweepZoom SweepZoom::centredOn(double position) const noexcept
{
    if (!std::isfinite(position) || !isZoomed())
        return *this;
    return normalized({position - span / 2.0, span});
}

double SweepZoom::windowPositionOf(double fraction) const noexcept
{
    return wrap(offset + fraction * span);
}

} // namespace visona
