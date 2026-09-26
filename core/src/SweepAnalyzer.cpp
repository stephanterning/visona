#include "visona/SweepAnalyzer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace visona
{

namespace
{

SweepCell spanOf(const float* samples, std::size_t numFrames) noexcept
{
    if (samples == nullptr)
        return {0.0f, 0.0f};

    SweepCell span;
    for (std::size_t frame = 0; frame < numFrames; ++frame)
    {
        span.min = std::min(span.min, samples[frame]);
        span.max = std::max(span.max, samples[frame]);
    }
    return span;
}

} // namespace

std::uint64_t freeRunningWindowFrames(double sampleRate) noexcept
{
    const auto frames = std::llround(std::max(sampleRate, 0.0) * freeRunningWindowSeconds);
    return static_cast<std::uint64_t>(std::max(frames, 1LL));
}

SweepAnalyzer::SweepAnalyzer(std::size_t numChannels, std::size_t numBins)
    : buffer_(numChannels, numBins)
{
}

void SweepAnalyzer::start(std::uint64_t windowFrames) noexcept
{
    buffer_.clear();
    windowFrames_ = windowFrames;
    windowTicks_ = 0.0;
    nextSampleIndex_ = 0;
    hasProcessed_ = false;
}

void SweepAnalyzer::startMusical(double windowTicks) noexcept
{
    assert(windowTicks > 0.0);
    buffer_.clear();
    windowFrames_ = 0;
    windowTicks_ = windowTicks > 0.0 ? windowTicks : 1.0;
    windowIndex_ = 0.0;
    lastTick_ = 0.0;
    frozenSinceLastTick_ = false;
    hasProcessed_ = false;
}

void SweepAnalyzer::processMusical(std::uint64_t sampleIndex,
                                   std::span<const float* const> channels, std::size_t numFrames,
                                   const TransportSpan& span) noexcept
{
    assert(channels.size() == buffer_.numChannels());
    assert(span.kind == TransportSpan::Kind::musical && span.end > span.start);
    if (!isMusical() || numFrames == 0 || channels.size() != buffer_.numChannels() ||
        !(span.end > span.start) || !(span.endTick > span.startTick))
        return;

    const auto numBins = static_cast<double>(buffer_.numBins());
    const auto ticksPerFrame = (span.endTick - span.startTick) / (span.end - span.start);
    std::size_t done = 0;
    while (done < numFrames)
    {
        // Every position comes from the span itself, never from a running sum, so the bins do
        // not depend on where the blocks start.
        const auto frame = static_cast<double>(sampleIndex + done);
        const auto tick = span.startTick + (frame - span.start) * ticksPerFrame;
        const auto windows = tick / windowTicks_;
        const auto windowIndex = std::floor(windows);
        const auto bin = std::min(static_cast<std::size_t>((windows - windowIndex) * numBins),
                                  buffer_.numBins() - 1);
        moveMusicalHead(tick, windowIndex, bin);

        const auto boundaryTick =
            (windowIndex + static_cast<double>(bin + 1) / numBins) * windowTicks_;
        const auto boundaryFrame = span.start + (boundaryTick - span.startTick) / ticksPerFrame;
        const auto framesToBoundary = std::ceil(boundaryFrame) - frame;
        const auto run = framesToBoundary < 1.0
                             ? std::size_t{1}
                             : static_cast<std::size_t>(std::min(
                                   framesToBoundary, static_cast<double>(numFrames - done)));
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
        {
            const float* const samples = channels[channel];
            buffer_.addToHead(channel, spanOf(samples == nullptr ? nullptr : samples + done, run));
        }
        done += run;
        lastTick_ =
            span.startTick + (static_cast<double>(sampleIndex + done) - span.start) * ticksPerFrame;
    }
}

void SweepAnalyzer::freeze() noexcept
{
    frozenSinceLastTick_ = true;
}

double SweepAnalyzer::windowStartTick() const noexcept
{
    return isMusical() && buffer_.pass() > 0 ? windowIndex_ * windowTicks_ : 0.0;
}

void SweepAnalyzer::moveMusicalHead(double tick, double windowIndex, std::size_t bin) noexcept
{
    if (buffer_.pass() == 0)
    {
        buffer_.advanceHead(1, bin);
        windowIndex_ = windowIndex;
        frozenSinceLastTick_ = false;
        return;
    }

    // Carrying on means no freeze in between, or resuming within a tick of where the sweep
    // stopped, which Stop's extrapolation can fall short of.
    const bool carriesOn = !frozenSinceLastTick_ || (tick >= lastTick_ && tick <= lastTick_ + 1.0);
    frozenSinceLastTick_ = false;
    if (carriesOn && windowIndex >= windowIndex_)
    {
        const auto pass = buffer_.pass() + static_cast<std::uint64_t>(windowIndex - windowIndex_);
        windowIndex_ = windowIndex;
        buffer_.advanceHead(pass, bin);
        return;
    }
    windowIndex_ = windowIndex;
    buffer_.jumpHead(buffer_.pass() + 1, bin);
}

void SweepAnalyzer::process(std::uint64_t sampleIndex, std::span<const float* const> channels,
                            std::size_t numFrames) noexcept
{
    assert(channels.size() == buffer_.numChannels());
    if (windowFrames_ == 0 || numFrames == 0 || channels.size() != buffer_.numChannels())
        return;

    if (hasProcessed_ && sampleIndex < nextSampleIndex_)
        buffer_.clear();
    nextSampleIndex_ = sampleIndex + numFrames;
    hasProcessed_ = true;

    const auto numBins = buffer_.numBins();
    std::size_t done = 0;
    while (done < numFrames)
    {
        const auto position = sampleIndex + done;
        const auto frameInWindow = position % windowFrames_;
        const auto bin = static_cast<std::size_t>(frameInWindow * numBins / windowFrames_);
        buffer_.advanceHead(position / windowFrames_ + 1, bin);

        // Every frame up to the next bin's first frame belongs to this bin.
        const auto framesLeftInBin = binStartFrame(bin + 1) - frameInWindow;
        const auto run =
            static_cast<std::size_t>(std::min<std::uint64_t>(numFrames - done, framesLeftInBin));
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
        {
            const float* const samples = channels[channel];
            buffer_.addToHead(channel, spanOf(samples == nullptr ? nullptr : samples + done, run));
        }
        done += run;
    }
}

std::uint64_t SweepAnalyzer::binStartFrame(std::size_t bin) const noexcept
{
    // The first frame f with ⌊f · numBins / windowFrames⌋ >= bin, i.e. ⌈bin · windowFrames /
    // numBins⌉.
    const auto numBins = static_cast<std::uint64_t>(buffer_.numBins());
    return (bin * windowFrames_ + numBins - 1) / numBins;
}

} // namespace visona
