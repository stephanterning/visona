#include "visona/WaveformStyle.h"

#include <algorithm>
#include <array>

namespace visona
{

namespace
{

/** How much each band's level counts, low, mid and high. */
constexpr std::array<float, 3> bandWeights{1.0f, 1.3f, 1.8f};

/** The colour of each band on its own, low, mid and high: not pure primaries, which look dull
    on black, but a warm red, a bright green and a light blue. */
constexpr std::array<Rgb, 3> bandColours{{
    {1.0f, 0.13f, 0.08f},
    {0.35f, 1.0f, 0.25f},
    {0.2f, 0.45f, 1.0f},
}};

} // namespace

Rgb djColour(const BandLevels& levels) noexcept
{
    const std::array<float, 3> values{levels.low, levels.mid, levels.high};
    Rgb mix;
    for (std::size_t band = 0; band < values.size(); ++band)
    {
        const auto weighted = std::max(values[band], 0.0f) * bandWeights[band];
        const auto share = weighted * weighted;
        mix.red += share * bandColours[band].red;
        mix.green += share * bandColours[band].green;
        mix.blue += share * bandColours[band].blue;
    }
    const auto brightest = std::max({mix.red, mix.green, mix.blue});
    if (!(brightest > 0.0f))
        return {};
    return {mix.red / brightest, mix.green / brightest, mix.blue / brightest};
}

} // namespace visona
