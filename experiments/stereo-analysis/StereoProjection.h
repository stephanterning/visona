#pragma once

#include "SignalizerPolar.h"

namespace stereo_spike
{
// Standard M/S rotation, scaled so full-scale mono reaches the diamond's top.
// This elementary transform is Visona code, not an imported Signalizer component.
inline Point lissajous(float left, float right) noexcept
{
    return {(right - left) * 0.5f, (left + right) * 0.5f};
}
} // namespace stereo_spike
