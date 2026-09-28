#include "visona/HostAnalysisPipeline.h"

#include <visona/SweepWindow.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace visona
{

namespace
{

/** Keeps each poll() short so prepareToPlay/releaseResources do not block the host for long. */
constexpr std::size_t maxFramesPerPoll = 4'096;

/** Blocks whose playheads can wait for the analysis thread: several seconds of small blocks. */
constexpr std::size_t playheadQueueCapacity = 1'024;

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
    , playheads_(playheadQueueCapacity)
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
    // The host does not process audio while it changes the stream, so the queue holds only
    // playheads of the old stream.
    while (playheads_.tryPop())
    {
    }
    hasLastPlayhead_ = false;
    offset_ = std::round(requestedOffset_.load(std::memory_order_relaxed));
    followsStart_ = false;
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

void HostAnalysisPipeline::pushPlayhead(std::uint64_t blockStartSample,
                                        const HostTransport::Playhead& playhead) noexcept
{
    playheads_.tryPush({blockStartSample, playhead});
}

void HostAnalysisPipeline::setAnalysisOffset(double frames) noexcept
{
    requestedOffset_.store(frames, std::memory_order_relaxed);
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
        applyOffset();
        syncPlayheads();

        while (const auto region = ring_->peek())
        {
            if (framesAnalyzed >= maxFramesPerPoll)
                break;

            const auto first = region->sampleIndex();
            const auto& span = transport_.spanAt(static_cast<double>(first));
            // Audio after the newest playhead waits for the next one.
            if (span.kind == TransportSpan::Kind::pending)
                break;
            auto numFrames = std::min(region->numFrames(), maxFramesPerPoll - framesAnalyzed);
            // spanAt() only returns a closed span that ends after `first`.
            if (!span.isOpen())
                numFrames = std::min(numFrames, static_cast<std::size_t>(std::ceil(
                                                    span.end - static_cast<double>(first))));
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

void HostAnalysisPipeline::applyOffset() noexcept
{
    const auto requested = std::round(requestedOffset_.load(std::memory_order_relaxed));
    if (requested == offset_)
        return;
    offset_ = requested;
    // Moving the timeline under audio that is already drawn would draw a stretch twice or leave a
    // gap, so the timeline starts over from the next playhead.
    transport_.reset(sampleRate_);
    followsStart_ = false;
    changed_ = true;
}

void HostAnalysisPipeline::syncPlayheads() noexcept
{
    // Playheads wait in the queue rather than push spans the audio still needs out of the
    // timeline.
    while (transport_.numSpans() + 2 < transport_.spanCapacity())
    {
        const auto next = playheads_.tryPop();
        if (!next)
            return;

        auto playhead = next->playhead;
        if (playhead.valid)
        {
            hasLastPlayhead_ = true;
            lastPlayhead_ = *next;
        }
        else if (hasLastPlayhead_ && lastPlayhead_.playhead.bpm > 0.0 && sampleRate_ > 0.0)
        {
            // A block without a playhead goes on from the last one.
            playhead = lastPlayhead_.playhead;
            if (playhead.isPlaying)
                playhead.ppqPosition +=
                    static_cast<double>(next->sampleIndex - lastPlayhead_.sampleIndex) /
                    sampleRate_ * playhead.bpm / 60.0;
        }
        else
        {
            continue;
        }

        // The audio of this playhead reaches the plugin `offset_` frames later.
        transport_.syncTo(static_cast<double>(next->sampleIndex) + offset_, playhead);
    }
}

void HostAnalysisPipeline::analyze(const AudioRingBuffer::ReadRegion& region, std::size_t numFrames,
                                   const TransportSpan& span) noexcept
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
        followStart(span.startCount);
        analyzer_.processMusical(region.sampleIndex(), channels_, numFrames, span);
        return;
    case TransportSpan::Kind::frozen:
        followStart(span.startCount);
        analyzer_.freeze();
        return;
    case TransportSpan::Kind::freeRunning:
    case TransportSpan::Kind::pending:
        return;
    }
}

void HostAnalysisPipeline::followStart(std::uint64_t startCount) noexcept
{
    if (followsStart_ && startCount == followedStart_)
        return;
    followsStart_ = true;
    followedStart_ = startCount;
    analyzer_.startMusical(windowTicks());
    changed_ = true;
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
