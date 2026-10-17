#include <visona/PolarLevel.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace visona
{
PolarLevel polarLevel(std::span<const StereoSample> samples) noexcept
{
    PolarLevel result;
    if (samples.empty())
        return result;
    constexpr double halfPi = std::numbers::pi / 2;
    constexpr double binsPerRadian = static_cast<double>(polarLevelBins - 1) / std::numbers::pi;
    for (const auto& sample : samples)
    {
        const double left = static_cast<double>(sample.left),
                     right = static_cast<double>(sample.right);
        if (!std::isfinite(left) || !std::isfinite(right) ||
            std::max(std::abs(left), std::abs(right)) > 1e6)
            return {};
        const double radius = std::max(std::abs(left), std::abs(right));
        if (radius <= 0)
            continue;
        // Same upper-semicircle direction as Polar Sample, computed in double
        // precision before angular quantization. Keep the anti-phase endpoints
        // distinct and deterministic, matching StereoProjection.h.
        const double mid = left + right, side = right - left;
        double angle = std::atan2(side, mid);
        if (angle > halfPi)
            angle -= std::numbers::pi;
        else if (angle < -halfPi)
            angle += std::numbers::pi;
        const auto bin =
            static_cast<std::size_t>(std::clamp(std::lround((angle + halfPi) * binsPerRadian), 0L,
                                                static_cast<long>(polarLevelBins - 1)));
        result.rms[bin] += radius * radius;
        result.peak[bin] = std::max(result.peak[bin], radius);
    }
    for (auto& value : result.rms)
        value = std::sqrt(value / static_cast<double>(samples.size()));
    result.valid = true;
    return result;
}
PolarLevel polarDirectionalLevel(std::span<const StereoSample> samples) noexcept
{
    PolarLevel result;
    std::array<double, polarLevelBins> support{};
    if (samples.empty())
        return result;
    constexpr double halfPi = std::numbers::pi / 2;
    for (const auto& sample : samples)
    {
        const double left = static_cast<double>(sample.left),
                     right = static_cast<double>(sample.right);
        const double radius = std::max(std::abs(left), std::abs(right));
        if (!std::isfinite(left) || !std::isfinite(right) || radius > 1e6)
            return {};
        if (radius == 0)
            continue;
        double angle = std::atan2(right - left, left + right);
        if (angle > halfPi)
            angle -= std::numbers::pi;
        else if (angle < -halfPi)
            angle += std::numbers::pi;
        const double position = std::clamp(
            (angle + halfPi) * static_cast<double>(polarLevelBins - 1) / std::numbers::pi, 0.0,
            static_cast<double>(polarLevelBins - 1));
        const auto lower = static_cast<std::size_t>(position);
        const double fraction = position - static_cast<double>(lower);
        const auto add = [&](std::size_t bin, double weight)
        {
            if (weight <= 0)
                return;
            support[bin] += weight;
            result.rms[bin] += weight * radius * radius;
            result.peak[bin] = std::max(result.peak[bin], radius);
        };
        add(lower, 1.0 - fraction);
        if (lower + 1 < polarLevelBins)
            add(lower + 1, fraction);
    }
    const double minimumSupport = 0.01 * static_cast<double>(samples.size());
    for (std::size_t i = 0; i < polarLevelBins; ++i)
        result.rms[i] = std::sqrt(result.rms[i] / std::max(support[i], minimumSupport));
    result.valid = true;
    return result;
}
} // namespace visona
