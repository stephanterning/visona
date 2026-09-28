#pragma once

#include <visona/AudioRingBuffer.h>
#include <visona/HostAnalysisPipeline.h>
#include <visona/HostTransport.h>
#include <visona/SweepSnapshot.h>
#include <visona/TripleBuffer.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace visona
{

class PluginAnalysisThread final : private juce::Thread
{
public:
    explicit PluginAnalysisThread(std::size_t numChannels);

    ~PluginAnalysisThread() override;

    void setStream(AudioRingBuffer* ring, double sampleRate);

    void setPlayhead(double sampleTime, const HostTransport::Playhead& playhead) noexcept
    {
        pipeline_.setPlayhead(sampleTime, playhead);
    }

    void setWindow(std::size_t windowIndex) noexcept
    {
        pipeline_.setWindow(windowIndex);
    }

    void setBandSplitting(bool enabled) noexcept
    {
        pipeline_.setBandSplitting(enabled);
    }

    [[nodiscard]] TripleBuffer<SweepSnapshot>& snapshots() noexcept
    {
        return pipeline_.snapshots();
    }

    [[nodiscard]] float takePeak(std::size_t channel) noexcept
    {
        return pipeline_.takePeak(channel);
    }

    [[nodiscard]] std::uint64_t busyNanoseconds() const noexcept
    {
        return busyNanoseconds_.load(std::memory_order_relaxed);
    }

private:
    void run() override;

    HostAnalysisPipeline pipeline_;
    std::mutex lock_;
    std::atomic<std::uint64_t> busyNanoseconds_{0};
};

} // namespace visona
