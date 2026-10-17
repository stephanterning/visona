/*
 * Adapted from Signalizer, Copyright (C) 2016 Janus Lynggaard Thorborg
 * (www.jthorborg.com), Source/Vectorscope/VectorscopeRendering.cpp,
 * revision e2043da622caf6dcf3ff472b8ac2b24a5ee02436, drawPolarPlot scalar path.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Visona experiment modifications (2026): scalar float function with no CPL,
 * SIMD, OpenGL, history or gain dependency; std::sin/cos instead of sincos;
 * early silence/non-finite guards and a deterministic exact anti-phase seam.
 * Peak-channel radius and folded angle retained away from the seam.
 * See THIRD_PARTY.md and licenses/Signalizer-GPL-3.0.txt.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace stereo_spike
{
struct Point
{
    float x{};
    float y{};
};

// Mathematical coordinates: positive x right, positive y up. Unit radius is
// max(abs(L), abs(R)) == 1. This is not an Ozone algorithm or an RMS measurement.
inline Point signalizerPolar(float left, float right) noexcept
{
    if (!std::isfinite(left) || !std::isfinite(right) || (left == 0 && right == 0))
        return {};
    constexpr float sine = std::numbers::sqrt2_v<float> * 0.5f;
    constexpr float cosine = -sine;
    const float length = std::max(std::abs(left), std::abs(right));
    // The upper-semicircle fold has two equivalent endpoints. Choose one
    // explicitly: fused operations can otherwise change the tiny residual's sign.
    if (right == -left)
        return {right > left ? length : -length, 0};
    const float y = left * cosine - right * sine;
    const float x = left * sine + right * cosine;
    const float angle = std::atan(x / y);
    return {std::sin(angle) * length, std::cos(angle) * length};
}
} // namespace stereo_spike
