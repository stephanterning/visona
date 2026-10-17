#pragma once

#include <visona/StereoAnalyzer.h>

#include <array>
#include <span>

namespace visona
{
// Centres from -90 to +90 degrees in 2.5-degree steps. Endpoint bins have
// half width; mono and single-channel directions fall exactly on bin centres.
inline constexpr std::size_t polarLevelBins = 73;
struct PolarLevel
{
    std::array<double, polarLevelBins> rms{}, peak{};
    bool valid = false;
};

/** Angular energy profile over the entire supplied window, including silence.
    radius = max(abs(L), abs(R)), matching the Polar Sample radial convention.
    rms[b]^2 = sum(radius^2 in bin b) / total sample count. Thus sum(rms^2)
    equals mean peak-channel-square, not mean stereo power or conditional RMS.
    Peak is the maximum radius per bin in this same window; no hold or decay.
    No display gain, gating, temporal state or allocation. Invalid input rejects
    the complete window rather than silently biasing the denominator. */
PolarLevel polarLevel(std::span<const StereoSample> samples) noexcept;
} // namespace visona
