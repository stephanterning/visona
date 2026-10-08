#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

/** The text for a Visona Sync measurement, in the status bar and the diagnostics overlay. */
namespace visona::syncText
{

/** A signed offset in milliseconds with a real minus sign, such as "+98.7 ms". */
[[nodiscard]] inline juce::String offset(double frames, double sampleRate)
{
    const auto ms = sampleRate > 0.0 ? std::round(frames * 10'000.0 / sampleRate) / 10.0 : 0.0;
    const auto sign = ms < 0.0 ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String("+");
    return sign + juce::String(std::abs(ms), 1) + " ms";
}

/** A peak level, such as "-6.0 dBFS". */
[[nodiscard]] inline juce::String peak(float gain)
{
    if (gain <= 0.0f)
        return "-inf dBFS";
    return juce::String(20.0f * std::log10(gain), 1) + " dBFS";
}

} // namespace visona::syncText
