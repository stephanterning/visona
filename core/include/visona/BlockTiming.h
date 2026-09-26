#pragma once

#include <cstdint>

namespace visona
{

/** Timing of one audio block. The audio thread pushes it together with the block's samples. */
struct BlockTiming
{
    /** Stream position of the block's first frame. It is a running frame counter that starts at 0
        when the stream starts, and all musical time relates to it. */
    std::uint64_t sampleIndex = 0;

    /** Host time of the block in nanoseconds: the audio device's timestamp when available,
        otherwise a monotonic clock read at the start of the audio callback. */
    std::uint64_t hostTimeNs = 0;

    /** Frames (samples per channel) in the block. */
    std::uint32_t numFrames = 0;

    friend bool operator==(const BlockTiming&, const BlockTiming&) = default;
};

} // namespace visona
