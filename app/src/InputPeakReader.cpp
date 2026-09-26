#include "InputPeakReader.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace visona
{

namespace
{

// The ring holds about a second of audio, so this leaves a wide margin for scheduling delays.
constexpr int pollIntervalMs = 5;
constexpr int stopTimeoutMs = 2'000;

void raiseTo(std::atomic<float>& peak, float value) noexcept
{
    auto current = peak.load(std::memory_order_relaxed);
    while (value > current &&
           !peak.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

} // namespace

InputPeakReader::InputPeakReader(AudioRingBuffer& ring)
    : juce::Thread("Visona input reader")
    , ring_(ring)
    , peaks_(ring.numChannels())
{
    startThread();
}

InputPeakReader::~InputPeakReader()
{
    stopThread(stopTimeoutMs);
}

float InputPeakReader::takePeak(std::size_t channel) noexcept
{
    assert(channel < peaks_.size());
    return peaks_[channel].exchange(0.0f, std::memory_order_relaxed);
}

void InputPeakReader::run()
{
    while (!threadShouldExit())
    {
        drain();
        wait(pollIntervalMs);
    }
}

void InputPeakReader::drain() noexcept
{
    while (const auto region = ring_.peek())
    {
        for (std::size_t channel = 0; channel < region->numChannels(); ++channel)
        {
            float peak = 0.0f;
            for (const auto sample : region->channel(channel))
                peak = std::max(peak, std::abs(sample));
            raiseTo(peaks_[channel], peak);
        }
        ring_.consume(region->numFrames());
    }
}

} // namespace visona
