#include "visona/HostAnalysisPipeline.h"

#include <visona/SweepWindow.h>

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

HostAnalysisPipeline::HostAnalysisPipeline(std::size_t numChannels, std::size_t numBins)
    : analyzer_(numChannels, numBins)
    , snapshots_(emptySnapshot(numChannels, numBins))
    , transport_(0.0)
    , requestedWindow_(defaultSweepWindow)
    , window_(defaultSweepWindow)
    , channels_(numChannels, nullptr)
    , peaks_(numChannels)
{
    for (auto& peak : peaks_)
        peak.store(0.0f, std::memory_order_relaxed);
}

void HostAnalysisPipeline::setStream(AudioRingBuffer* ring, double sampleRate) noexcept
{
    assert(ring == nullptr || ring->numChannels() == channels_.size());
    ring_ = ring != nullptr && ring->numChannels() == channels_.size() ? ring : nullptr;
    ++streamId_;
    sampleRate_ = ring_ != nullptr ? sampleRate : 0.0;
    nextSampleIndex_ = 0;
    analyzer_.start(0);
    analyzer_.setBandSplitting(false, 0.0);
    transport_.reset(sampleRate_);
    followsStart_ = false;
    playheadValid_.store(false, std::memory_order_relaxed);
    changed_ = true;
}

void HostAnalysisPipeline::setWindow(std::size_t windowIndex) noexcept
{
    requestedWindow_.store(std::min(windowIndex, sweepWindowBars.size() - 1),
                           std::memory_order_relaxed);
}

void HostAnalysisPipeline::setBandSplitting(bool enabled) noexcept
{
    bandSplitting_.store(enabled, std::memory_order_relaxed);
}

void HostAnalysisPipeline::setPlayhead(double sampleTime,
                                       const HostTransport::Playhead& playhead) noexcept
{
    playheadValid_.store(playhead.valid, std::memory_order_relaxed);
    playheadPlaying_.store(playhead.isPlaying, std::memory_order_relaxed);
    playheadSample_.store(sampleTime, std::memory_order_relaxed);
    playheadPpq_.store(playhead.ppqPosition, std::memory_order_relaxed);
    playheadBpm_.store(playhead.bpm, std::memory_order_relaxed);
    playheadNumerator_.store(playhead.timeSignature.numerator, std::memory_order_relaxed);
    playheadDenominator_.store(playhead.timeSignature.denominator, std::memory_order_relaxed);
}

std::size_t HostAnalysisPipeline::poll() noexcept
{
    if (const auto window = requestedWindow_.load(std::memory_order_relaxed); window != window_)
    {
        window_ = window;
        if (analyzer_.isMusical())
            analyzer_.startMusical(windowTicks());
        changed_ = true;
    }

    if (const auto splitting = bandSplitting_.load(std::memory_order_relaxed) && ring_ != nullptr;
        splitting != analyzer_.splitsBands())
    {
        analyzer_.setBandSplitting(splitting, sampleRate_);
        changed_ = true;
    }

    std::size_t framesAnalyzed = 0;
    if (ring_ != nullptr)
    {
        HostTransport::Playhead playhead;
        playhead.valid = playheadValid_.load(std::memory_order_relaxed);
        playhead.isPlaying = playheadPlaying_.load(std::memory_order_relaxed);
        playhead.ppqPosition = playheadPpq_.load(std::memory_order_relaxed);
        playhead.bpm = playheadBpm_.load(std::memory_order_relaxed);
        playhead.timeSignature.numerator = playheadNumerator_.load(std::memory_order_relaxed);
        playhead.timeSignature.denominator = playheadDenominator_.load(std::memory_order_relaxed);

        const auto newest = static_cast<double>(ring_->newestFrameEnd());
        transport_.syncTo(newest, playhead);
        transport_.advanceTo(newest);

        while (const auto region = ring_->peek())
        {
            const auto first = region->sampleIndex();
            const auto& span = transport_.spanAt(static_cast<double>(first));
            if (span.kind == TransportSpan::Kind::pending)
                break;
            auto numFrames = region->numFrames();
            if (!span.isOpen())
                numFrames =
                    std::min(numFrames, static_cast<std::size_t>(std::ceil(span.end) -
                                                                 static_cast<double>(first)));
            analyze(*region, numFrames, span);
            nextSampleIndex_ = first + numFrames;
            framesAnalyzed += numFrames;
            ring_->consume(numFrames);
        }
    }

    if (transport_.state() != publishedState_)
        changed_ = true;
    if (framesAnalyzed > 0 || changed_)
        publish();
    return framesAnalyzed;
}

float HostAnalysisPipeline::takePeak(std::size_t channel) noexcept
{
    assert(channel < peaks_.size());
    return peaks_[channel].exchange(0.0f, std::memory_order_relaxed);
}

void HostAnalysisPipeline::analyze(const AudioRingBuffer::ReadRegion& region,
                                   std::size_t numFrames, const TransportSpan& span) noexcept
{
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
    {
        const auto samples = region.channel(channel).first(numFrames);
        channels_[channel] = samples.data();

        float peak = 0.0f;
        for (const auto sample : samples)
            peak = std::max(peak, std::abs(sample));
        raiseTo(peaks_[channel], peak);
    }

    switch (span.kind)
    {
    case TransportSpan::Kind::musical:
        followStart(span.startCount, region.sampleIndex());
        analyzer_.processMusical(region.sampleIndex(), channels_, numFrames, span);
        return;
    case TransportSpan::Kind::frozen:
        followStart(span.startCount, region.sampleIndex());
        analyzer_.freeze();
        return;
    case TransportSpan::Kind::freeRunning:
    case TransportSpan::Kind::pending:
        return;
    }
}

void HostAnalysisPipeline::followStart(std::uint64_t startCount,
                                       std::uint64_t sampleIndex) noexcept
{
    if (followsStart_ && startCount == followedStart_)
        return;
    followsStart_ = true;
    followedStart_ = startCount;
    analyzer_.startMusical(windowTicks());
    changed_ = true;
    (void)sampleIndex;
}

double HostAnalysisPipeline::windowTicks() const noexcept
{
    return sweepWindowBars[window_] * transport_.timeSignature().ticksPerBar();
}

void HostAnalysisPipeline::publish() noexcept
{
    auto& snapshot = snapshots_.writeBuffer();
    snapshot.sweep.copyFrom(analyzer_.buffer());
    snapshot.streamId = streamId_;
    snapshot.hasStream = ring_ != nullptr;
    snapshot.sampleRate = sampleRate_;
    snapshot.nextSampleIndex = nextSampleIndex_;
    snapshot.overruns = ring_ != nullptr ? ring_->overrunCount() : 0;
    snapshot.droppedFrames = ring_ != nullptr ? ring_->droppedFrameCount() : 0;

    snapshot.transportState = transport_.state();
    snapshot.bpm = transport_.bpm();
    snapshot.nextTick = transport_.nextTick();
    snapshot.timeSignature = transport_.timeSignature();
    snapshot.musical = analyzer_.isMusical();
    snapshot.window = window_;
    snapshot.windowTicks = analyzer_.isMusical() ? analyzer_.windowTicks() : windowTicks();
    snapshot.windowStartTick = analyzer_.windowStartTick();
    snapshot.bandDelayFrames = analyzer_.bandDelayFrames();
    snapshot.midiEvents = 0;
    snapshot.ignoredSpp = 0;
    snapshots_.publish();

    publishedState_ = transport_.state();
    changed_ = false;
}

} // namespace visona
