#pragma once

#include <visona/AudioRingBuffer.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace visona
{

/**
    A minimal consumer of the audio ring for the debug readout. On its own thread it drains the
    ring every few milliseconds and keeps the peak level of each channel. It stands in for the
    analysis thread, which will become the ring's consumer.
*/
class InputPeakReader final : private juce::Thread
{
public:
    /** Starts reading `ring`, which must outlive the reader. */
    explicit InputPeakReader(AudioRingBuffer& ring);

    /** Stops the reader thread. */
    ~InputPeakReader() override;

    /** Any thread. The highest absolute sample value of `channel` since the previous call. */
    [[nodiscard]] float takePeak(std::size_t channel) noexcept;

private:
    void run() override;
    void drain() noexcept;

    AudioRingBuffer& ring_;
    std::vector<std::atomic<float>> peaks_;
};

} // namespace visona
