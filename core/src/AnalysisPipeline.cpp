#include "visona/AnalysisPipeline.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace visona
{

namespace
{

SweepSnapshot emptySnapshot(std::size_t numChannels, std::size_t numBins)
{
    SweepSnapshot snapshot;
    snapshot.sweep = SweepBuffer(numChannels, numBins);
    return snapshot;
}

void raiseTo(std::atomic<float>& peak, float value) noexcept
{
    auto current = peak.load(std::memory_order_relaxed);
    while (value > current &&
           !peak.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
}

} // namespace

AnalysisPipeline::AnalysisPipeline(std::size_t numChannels, std::size_t numBins)
    : analyzer_(numChannels, numBins)
    , snapshots_(emptySnapshot(numChannels, numBins))
    , channels_(numChannels, nullptr)
    , peaks_(numChannels)
{
    for (auto& peak : peaks_)
        peak.store(0.0f, std::memory_order_relaxed);
}

void AnalysisPipeline::setStream(AudioRingBuffer* ring, double sampleRate) noexcept
{
    assert(ring == nullptr || ring->numChannels() == channels_.size());
    ring_ = ring != nullptr && ring->numChannels() == channels_.size() ? ring : nullptr;
    ++streamId_;
    sampleRate_ = ring_ != nullptr ? sampleRate : 0.0;
    nextSampleIndex_ = 0;
    analyzer_.start(ring_ != nullptr ? freeRunningWindowFrames(sampleRate) : 0);
    changed_ = true;
}

std::size_t AnalysisPipeline::poll() noexcept
{
    std::size_t framesAnalyzed = 0;
    if (ring_ != nullptr)
    {
        while (const auto region = ring_->peek())
        {
            for (std::size_t channel = 0; channel < channels_.size(); ++channel)
            {
                const auto samples = region->channel(channel);
                channels_[channel] = samples.data();

                float peak = 0.0f;
                for (const auto sample : samples)
                    peak = std::max(peak, std::abs(sample));
                raiseTo(peaks_[channel], peak);
            }
            analyzer_.process(region->sampleIndex(), channels_, region->numFrames());
            nextSampleIndex_ = region->sampleIndex() + region->numFrames();
            framesAnalyzed += region->numFrames();
            ring_->consume(region->numFrames());
        }
    }

    if (framesAnalyzed > 0 || changed_)
        publish();
    return framesAnalyzed;
}

float AnalysisPipeline::takePeak(std::size_t channel) noexcept
{
    assert(channel < peaks_.size());
    return peaks_[channel].exchange(0.0f, std::memory_order_relaxed);
}

void AnalysisPipeline::publish() noexcept
{
    auto& snapshot = snapshots_.writeBuffer();
    snapshot.sweep.copyFrom(analyzer_.buffer());
    snapshot.streamId = streamId_;
    snapshot.hasStream = ring_ != nullptr;
    snapshot.sampleRate = sampleRate_;
    snapshot.windowFrames = analyzer_.windowFrames();
    snapshot.nextSampleIndex = nextSampleIndex_;
    snapshot.overruns = ring_ != nullptr ? ring_->overrunCount() : 0;
    snapshot.droppedFrames = ring_ != nullptr ? ring_->droppedFrameCount() : 0;
    snapshots_.publish();
    changed_ = false;
}

} // namespace visona
