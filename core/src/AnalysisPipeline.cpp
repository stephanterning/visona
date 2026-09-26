#include "visona/AnalysisPipeline.h"

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

AnalysisPipeline::AnalysisPipeline(std::size_t numChannels, std::size_t numBins)
    : analyzer_(numChannels, numBins)
    , snapshots_(emptySnapshot(numChannels, numBins))
    , mapper_(0.0)
    , transport_(0.0)
    , requestedWindow_(defaultSweepWindow)
    , window_(defaultSweepWindow)
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
    analyzer_.setBandSplitting(false, 0.0);
    mapper_.reset(sampleRate_);
    transport_.reset(sampleRate_);
    mappedAnyBlock_ = false;
    followsStart_ = false;
    changed_ = true;
}

void AnalysisPipeline::setMidiQueue(MidiClockQueue* queue) noexcept
{
    midi_ = queue;
}

void AnalysisPipeline::setWindow(std::size_t windowIndex) noexcept
{
    requestedWindow_.store(std::min(windowIndex, sweepWindowBars.size() - 1),
                           std::memory_order_relaxed);
}

void AnalysisPipeline::setBandSplitting(bool enabled) noexcept
{
    bandSplitting_.store(enabled, std::memory_order_relaxed);
}

void AnalysisPipeline::setMidiOffset(double frames) noexcept
{
    midiOffset_.store(frames, std::memory_order_relaxed);
}

std::size_t AnalysisPipeline::poll() noexcept
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

    // The oldest waiting block is mapped first, so that MIDI events have a timeline even before
    // any audio has been analyzed.
    if (ring_ != nullptr)
        if (const auto region = ring_->peek())
            mapBlock(region->block());
    auto midiHandled = handleMidi();

    std::size_t framesAnalyzed = 0;
    if (ring_ != nullptr)
    {
        transport_.advanceTo(static_cast<double>(ring_->newestFrameEnd()));
        while (const auto region = ring_->peek())
        {
            mapBlock(region->block());
            // Audio that arrived during this poll waits until the events that were already
            // queued are on its timeline.
            if (!midiHandled)
            {
                midiHandled = handleMidi();
                transport_.advanceTo(static_cast<double>(ring_->newestFrameEnd()));
            }
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

float AnalysisPipeline::takePeak(std::size_t channel) noexcept
{
    assert(channel < peaks_.size());
    return peaks_[channel].exchange(0.0f, std::memory_order_relaxed);
}

void AnalysisPipeline::mapBlock(const BlockTiming& block) noexcept
{
    if (mappedAnyBlock_ && block.sampleIndex == mappedBlock_)
        return;
    mapper_.addBlock(block);
    mappedAnyBlock_ = true;
    mappedBlock_ = block.sampleIndex;
}

bool AnalysisPipeline::handleMidi() noexcept
{
    if (midi_ == nullptr)
        return true;
    // Without a stream there is no timeline to place events on, so they are dropped. A new
    // stream's events wait in the queue for its first block.
    if (ring_ != nullptr && !mapper_.isReady())
        return false;
    const auto offset = midiOffset_.load(std::memory_order_relaxed);
    while (const auto event = midi_->tryPop())
    {
        ++midiEvents_;
        changed_ = true;
        if (ring_ != nullptr)
            transport_.handle(event->type, event->sppValue,
                              mapper_.sampleTimeOf(event->hostTimeNs) + offset);
    }
    return true;
}

void AnalysisPipeline::analyze(const AudioRingBuffer::ReadRegion& region, std::size_t numFrames,
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
    case TransportSpan::Kind::freeRunning:
        analyzer_.process(region.sampleIndex(), channels_, numFrames);
        return;
    case TransportSpan::Kind::musical:
        followStart(span.startCount);
        analyzer_.processMusical(region.sampleIndex(), channels_, numFrames, span);
        return;
    case TransportSpan::Kind::frozen:
        followStart(span.startCount);
        analyzer_.freeze();
        return;
    case TransportSpan::Kind::pending:
        return;
    }
}

void AnalysisPipeline::followStart(std::uint64_t startCount) noexcept
{
    // Every Start, and leaving the free-running sweep, clears the sweep and begins at bar 1.
    if (followsStart_ && startCount == followedStart_)
        return;
    followsStart_ = true;
    followedStart_ = startCount;
    analyzer_.startMusical(windowTicks());
    changed_ = true;
}

double AnalysisPipeline::windowTicks() const noexcept
{
    return sweepWindowBars[window_] * transport_.timeSignature().ticksPerBar();
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

    snapshot.transportState = transport_.state();
    snapshot.bpm = transport_.bpm();
    snapshot.nextTick = transport_.nextTick();
    snapshot.timeSignature = transport_.timeSignature();
    snapshot.musical = analyzer_.isMusical();
    snapshot.window = window_;
    snapshot.windowTicks = analyzer_.isMusical() ? analyzer_.windowTicks() : windowTicks();
    snapshot.windowStartTick = analyzer_.windowStartTick();
    snapshot.bandDelayFrames = analyzer_.bandDelayFrames();
    snapshot.midiEvents = midiEvents_;
    snapshot.ignoredSpp = transport_.ignoredSppCount();
    snapshots_.publish();

    publishedState_ = transport_.state();
    changed_ = false;
}

} // namespace visona
