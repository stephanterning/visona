#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace visona::test
{

/** `numFrames` frames of a sine at `frequency` Hz and peak `amplitude`, starting at phase 0. */
inline std::vector<float> sine(double frequency, double sampleRate, float amplitude,
                               std::size_t numFrames)
{
    std::vector<float> samples(numFrames);
    for (std::size_t frame = 0; frame < numFrames; ++frame)
    {
        const auto phase =
            2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
        samples[frame] = amplitude * static_cast<float>(std::sin(phase));
    }
    return samples;
}

/** Silence with a one-frame click of `amplitude` at each of `clickFrames`. */
inline std::vector<float> clicks(std::size_t numFrames,
                                 const std::vector<std::uint64_t>& clickFrames, float amplitude)
{
    std::vector<float> samples(numFrames, 0.0f);
    for (const auto frame : clickFrames)
        if (frame < numFrames)
            samples[static_cast<std::size_t>(frame)] = amplitude;
    return samples;
}

} // namespace visona::test
