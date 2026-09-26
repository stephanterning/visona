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

/**
    Deterministic synthetic music at 120 BPM: a kick on every beat (a sine falling from 150 to
    50 Hz), a 55 Hz bass on the off-beats, a snare on beats 2 and 4, and a hi-hat on every eighth
    note (differentiated noise).
*/
inline std::vector<float> music(double sampleRate, std::size_t numFrames)
{
    constexpr double beatSeconds = 0.5;
    constexpr double twoPi = 2.0 * std::numbers::pi;
    std::vector<float> samples(numFrames);
    std::uint32_t noiseState = 12345;
    double previousNoise = 0.0;
    double kickPhase = 0.0;
    for (std::size_t frame = 0; frame < numFrames; ++frame)
    {
        const auto time = static_cast<double>(frame) / sampleRate;
        const auto beat = static_cast<int>(time / beatSeconds);
        const auto sinceBeat = time - beat * beatSeconds;
        const auto sinceEighth = std::fmod(time, beatSeconds / 2.0);

        noiseState = noiseState * 1'664'525u + 1'013'904'223u;
        const auto noise = static_cast<double>(noiseState >> 8) / 8'388'608.0 - 1.0;
        const auto hiss = noise - previousNoise;
        previousNoise = noise;

        kickPhase += twoPi * (50.0 + 100.0 * std::exp(-sinceBeat / 0.03)) / sampleRate;
        const auto kick = 0.7 * std::sin(kickPhase) * std::exp(-sinceBeat / 0.15);
        const auto bass = sinceBeat >= beatSeconds / 2.0
                              ? 0.25 * std::sin(twoPi * 55.0 * time) *
                                    std::exp(-(sinceBeat - beatSeconds / 2.0) / 0.2)
                              : 0.0;
        const auto snare = beat % 2 == 1 ? (0.3 * noise + 0.2 * std::sin(twoPi * 190.0 * time)) *
                                               std::exp(-sinceBeat / 0.08)
                                         : 0.0;
        const auto hat = 0.12 * hiss * std::exp(-sinceEighth / 0.03);
        samples[frame] = static_cast<float>(kick + bass + snare + hat);
    }
    return samples;
}

} // namespace visona::test
