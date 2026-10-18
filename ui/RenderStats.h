#pragma once

#include <cstddef>

namespace visona
{

/** Common display measurements. Scope-specific counters are hidden for timer-driven views. */
struct RenderStats
{
    double framesPerSecond = 0.0, renderMsAverage = 0.0, renderMsMax = 0.0;
    int imageWidth = 0, imageHeight = 0;
    float scale = 1.0f;
    double paintMsAverage = 0.0, paintMsMax = 0.0;
    double vblanksPerSecond = 0.0, fullRedrawsPerSecond = 0.0;
    std::size_t tiles = 0;
};

} // namespace visona
