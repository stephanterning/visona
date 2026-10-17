#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

namespace stereo_spike
{
inline constexpr std::array<std::string_view, 9> signalNames{
    "Mono",         "Anti-mono", "Left only", "Right only", "90-degree sine", "Independent noise",
    "Unequal mono", "Silence",   "Quiet mono"};

struct Signal
{
    std::vector<float> left, right;
};

inline Signal makeSignal(std::size_t kind, int sampleRate, std::size_t count)
{
    Signal signal{std::vector<float>(count), std::vector<float>(count)};
    std::uint32_t random = 0x7625aab1;
    auto noise = [&random]()
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return static_cast<float>(static_cast<double>(random) / 4294967295.0 - 0.5);
    };
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto phase = 2 * std::numbers::pi * 1000 * static_cast<double>(i) / sampleRate;
        const auto sine = static_cast<float>(0.7 * std::sin(phase));
        float left = sine, right = sine;
        switch (kind)
        {
        case 1:
            right = -sine;
            break;
        case 2:
            right = 0;
            break;
        case 3:
            left = 0;
            break;
        case 4:
            right = static_cast<float>(0.7 * std::cos(phase));
            break;
        case 5:
            left = noise();
            right = noise();
            break;
        case 6:
            right *= 0.5f;
            break;
        case 7:
            left = right = 0;
            break;
        case 8:
            left *= 1e-7f;
            right *= 1e-7f;
            break;
        default:
            break;
        }
        signal.left[i] = left;
        signal.right[i] = right;
    }
    return signal;
}
} // namespace stereo_spike
