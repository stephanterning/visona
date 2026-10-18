#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace visona
{

/** One second of audio, or four maximum-sized blocks, whichever is larger. */
inline std::size_t ringCapacityFrames(double sampleRate, int bufferSize)
{
    const auto oneSecond = static_cast<std::size_t>(std::ceil(std::max(sampleRate, 1.0)));
    return std::max(oneSecond, static_cast<std::size_t>(std::max(bufferSize, 1)) * 4);
}

/** Independent of the maximum block size: hosts may send much smaller actual blocks. */
inline std::size_t ringTimingSlots(std::size_t capacityFrames) noexcept
{
    constexpr std::size_t framesPerTimingSlot = 16;
    return std::max<std::size_t>(capacityFrames / framesPerTimingSlot, 1);
}

} // namespace visona
