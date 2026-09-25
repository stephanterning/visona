#include "visona/AudioInputWriter.h"

#include <cassert>

namespace visona
{

AudioInputWriter::AudioInputWriter(AudioRingBuffer& ring)
    : ring_(ring)
    , routes_(ring.numChannels())
    , channels_(ring.numChannels(), nullptr)
{
    for (auto& route : routes_)
        route.store(noInput, std::memory_order_relaxed);
}

void AudioInputWriter::route(std::size_t channel, int input) noexcept
{
    assert(channel < routes_.size());
    routes_[channel].store(input < 0 ? noInput : input, std::memory_order_relaxed);
}

int AudioInputWriter::routeOf(std::size_t channel) const noexcept
{
    assert(channel < routes_.size());
    return routes_[channel].load(std::memory_order_relaxed);
}

bool AudioInputWriter::write(std::span<const float* const> inputs, std::uint32_t numFrames,
                             std::uint64_t hostTimeNs) noexcept
{
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
        const auto input = routes_[channel].load(std::memory_order_relaxed);
        const bool delivered = input >= 0 && static_cast<std::size_t>(input) < inputs.size();
        channels_[channel] = delivered ? inputs[static_cast<std::size_t>(input)] : nullptr;
    }

    const auto sampleIndex = nextSampleIndex_.load(std::memory_order_relaxed);
    nextSampleIndex_.store(sampleIndex + numFrames, std::memory_order_relaxed);
    return ring_.push(channels_, {sampleIndex, hostTimeNs, numFrames});
}

} // namespace visona
