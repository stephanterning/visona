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
    , requestedFreeBpm_(defaultFreeBpm)
    , window_(defaultSweepWindow)
    , freeBpm_(defaultFreeBpm)
    , channels_(numChannels, nullptr)
    , peaks_(numChannels)
{
    for (auto& peak : peaks_)
        peak.store(0.0f, std::memory_order_relaxed);
}

void AnalysisPipeline::setStream(AudioRingBuffer* ring, double sampleRate) noexcept
{
    const bool fits = ring != nullptr && (ring->numChannels() == channels_.size() ||
                                          ring->numChannels() == channels_.size() + 1);
    assert(ring == nullptr || fits);
    ring_ = fits ? ring : nullptr;
    ringHasSync_ = ring_ != nullptr && ring_->numChannels() > channels_.size();
    sync_.reset();
    syncOffset_ = 0.0;
    ++streamId_;
    sampleRate_ = ring_ != nullptr ? sampleRate : 0.0;
    nextSampleIndex_ = 0;
    analyzer_.start(0);
    analyzer_.setBandSplitting(false, 0.0);
    barPeaks_.restart();
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

void AnalysisPipeline::setFreeTempo(double bpm) noexcept
{
    requestedFreeBpm_.store(clampFreeBpm(bpm), std::memory_order_relaxed);
}

void AnalysisPipeline::runFree() noexcept
{
    freeRequests_.fetch_add(1, std::memory_order_relaxed);
}

void AnalysisPipeline::setBandSplitting(bool enabled) noexcept
{
    bandSplitting_.store(enabled, std::memory_order_relaxed);
}

void AnalysisPipeline::setMidiOffset(double frames) noexcept
{
    midiOffset_.store(frames, std::memory_order_relaxed);
}

void AnalysisPipeline::setSyncInput(bool enabled) noexcept
{
    syncInput_.store(enabled, std::memory_order_relaxed);
}

std::size_t AnalysisPipeline::poll() noexcept
{
    // A free-running sweep starts over from bar 1 on a new window or tempo; one that follows MIDI
    // Clock keeps its bar numbers.
    const bool free = transport_.state() == TransportState::freeRunning;
    if (const auto window = requestedWindow_.load(std::memory_order_relaxed); window != window_)
    {
        window_ = window;
        if (analyzer_.isMusical())
            analyzer_.startMusical(windowTicks());
        followsStart_ = followsStart_ && !free;
        changed_ = true;
    }
    if (const auto bpm = requestedFreeBpm_.load(std::memory_order_relaxed); bpm != freeBpm_)
    {
        freeBpm_ = bpm;
        followsStart_ = followsStart_ && !free;
        changed_ = true;
    }
    if (const auto requests = freeRequests_.load(std::memory_order_relaxed);
        requests != handledFreeRequests_)
    {
        handledFreeRequests_ = requests;
        if (ring_ != nullptr)
            transport_.runFree(static_cast<double>(nextSampleIndex_));
        changed_ = true;
    }

    if (const auto splitting = bandSplitting_.load(std::memory_order_relaxed) && ring_ != nullptr;
        splitting != analyzer_.splitsBands())
    {
        analyzer_.setBandSplitting(splitting, sampleRate_);
        changed_ = true;
    }

    if (const auto listens = syncInput_.load(std::memory_order_relaxed) && ringHasSync_;
        listens != (sync_.state() != SidechainSyncState::off))
    {
        sync_.setSidechainEnabled(listens);
        followSync();
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

float AnalysisPipeline::takeSyncPeak() noexcept
{
    return syncPeak_.exchange(0.0f, std::memory_order_relaxed);
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
    const auto offset = midiOffset_.load(std::memory_order_relaxed) + syncOffset_;
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
    if (ringHasSync_)
    {
        float peak = 0.0f;
        for (const auto sample : region.channel(channels_.size()).first(numFrames))
            peak = std::max(peak, std::abs(sample));
        raiseTo(syncPeak_, peak);
    }

    switch (span.kind)
    {
    case TransportSpan::Kind::freeRunning:
        followStart(span.startCount, region.sampleIndex());
        analyzeFree(region, numFrames);
        return;
    case TransportSpan::Kind::musical:
        followStart(span.startCount, region.sampleIndex());
        detectSync(region, numFrames, span);
        analyzer_.processMusical(region.sampleIndex(), channels_, numFrames, span);
        barPeaks_.process(channels_, numFrames, region.sampleIndex(), span,
                          transport_.timeSignature().ticksPerBar(), sampleRate_);
        return;
    case TransportSpan::Kind::frozen:
        followStart(span.startCount, region.sampleIndex());
        analyzer_.freeze();
        return;
    case TransportSpan::Kind::pending:
        return;
    }
}

void AnalysisPipeline::detectSync(const AudioRingBuffer::ReadRegion& region,
                                  std::size_t numFrames, const TransportSpan& span) noexcept
{
    const auto ticks = span.endTick - span.startTick;
    if (sync_.state() == SidechainSyncState::off || span.isOpen() || !(ticks > 0.0))
        return;

    // The span's ticks were placed syncOffset_ frames late. The detector measures against the
    // timeline without that, so that a correct offset keeps measuring the same.
    const auto framesPerQuarter =
        (span.end - span.start) / ticks * TimeSignature::ticksPerQuarterNote;
    const auto tick = span.tickAt(static_cast<double>(region.sampleIndex()) + syncOffset_);
    sync_.processBlock(region.channel(channels_.size()).first(numFrames),
                       tick / TimeSignature::ticksPerQuarterNote,
                       60.0 * sampleRate_ / framesPerQuarter, transport_.timeSignature(),
                       sampleRate_, true);
    followSync();
}

void AnalysisPipeline::followSync() noexcept
{
    const auto offset = sync_.state() == SidechainSyncState::locked ? sync_.offsetFrames() : 0.0;
    if (offset == syncOffset_)
        return;
    syncOffset_ = offset;
    if (analyzer_.isMusical() && transport_.state() != TransportState::freeRunning)
        analyzer_.startMusical(windowTicks());
    changed_ = true;
}

void AnalysisPipeline::followStart(std::uint64_t startCount, std::uint64_t sampleIndex) noexcept
{
    // Every Start, and entering or leaving the free-running sweep, clears the sweep and begins at
    // bar 1.
    if (followsStart_ && startCount == followedStart_)
        return;
    followsStart_ = true;
    followedStart_ = startCount;
    analyzer_.startMusical(windowTicks());
    barPeaks_.restart();
    freeOrigin_ = sampleIndex;
    freeFramesPerTick_ = 60.0 * sampleRate_ / (TimeSignature::ticksPerQuarterNote * freeBpm_);
    changed_ = true;
}

void AnalysisPipeline::analyzeFree(const AudioRingBuffer::ReadRegion& region,
                                   std::size_t numFrames) noexcept
{
    // One span for the whole free-running sweep, so that every frame's position comes from the
    // same line and the bins do not depend on how the audio is split into blocks. It is long
    // enough for years of audio.
    constexpr double spanFrames = 0x1p42;
    const auto origin = static_cast<double>(freeOrigin_);
    TransportSpan free;
    free.kind = TransportSpan::Kind::musical;
    free.start = origin;
    free.end = origin + spanFrames;
    free.endTick = spanFrames / freeFramesPerTick_;
    analyzer_.processMusical(region.sampleIndex(), channels_, numFrames, free);
    barPeaks_.process(channels_, numFrames, region.sampleIndex(), free,
                      transport_.timeSignature().ticksPerBar(), sampleRate_);
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
    snapshot.nextSampleIndex = nextSampleIndex_;
    snapshot.overruns = ring_ != nullptr ? ring_->overrunCount() : 0;
    snapshot.droppedFrames = ring_ != nullptr ? ring_->droppedFrameCount() : 0;

    snapshot.transportState = transport_.state();
    snapshot.bpm = transport_.state() == TransportState::freeRunning ? freeBpm_ : transport_.bpm();
    snapshot.nextTick = transport_.nextTick();
    snapshot.timeSignature = transport_.timeSignature();
    snapshot.musical = analyzer_.isMusical();
    snapshot.window = window_;
    snapshot.windowTicks = analyzer_.isMusical() ? analyzer_.windowTicks() : windowTicks();
    snapshot.windowStartTick = analyzer_.windowStartTick();
    snapshot.bandDelayFrames = analyzer_.bandDelayFrames();
    snapshot.barPeaks = barPeaks_.recent();
    snapshot.midiEvents = midiEvents_;
    snapshot.ignoredSpp = transport_.ignoredSppCount();
    snapshot.syncState = sync_.state();
    snapshot.syncOffsetFrames = sync_.offsetFrames();
    snapshot.syncImpulsePeak = sync_.impulsePeak();
    snapshots_.publish();

    publishedState_ = transport_.state();
    changed_ = false;
}

} // namespace visona
