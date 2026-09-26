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

float sampleAt(const float* samples, std::size_t offset) noexcept
{
    return samples == nullptr ? 0.0f : samples[offset];
}

SweepCell pointAt(float value) noexcept
{
    return {value, value};
}

SweepCell between(float a, float b) noexcept
{
    return {std::min(a, b), std::max(a, b)};
}

} // namespace

SweepAnalyzer::SweepAnalyzer(std::size_t numChannels, std::size_t numBins)
    : buffer_(numChannels, numBins)
    , lastSamples_(numChannels, 0.0f)
{
}

void SweepAnalyzer::start(std::uint64_t windowFrames) noexcept
{
    buffer_.clear();
    windowFrames_ = windowFrames;
    windowTicks_ = 0.0;
    nextSampleIndex_ = 0;
    hasProcessed_ = false;
    hasLastFrame_ = false;
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
    hasLastFrame_ = false;
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

    const auto numBins = buffer_.numBins();
    const auto binsPerWindow = static_cast<double>(numBins);
    const auto ticksPerFrame = (span.endTick - span.startTick) / (span.end - span.start);
    // Positions in bins since tick 0. Every position comes from the span itself, never from a
    // running sum, so the bins do not depend on where the blocks start.
    const auto positionOf = [&](std::uint64_t frame)
    {
        const auto tick =
            span.startTick + (static_cast<double>(frame) - span.start) * ticksPerFrame;
        return tick / windowTicks_ * binsPerWindow;
    };

    std::size_t done = 0;
    while (done < numFrames)
    {
        const auto frame = sampleIndex + done;
        const auto position = positionOf(frame);
        const auto absoluteBin = static_cast<std::int64_t>(std::floor(position));
        const auto windowIndex =
            static_cast<std::int64_t>(std::floor(static_cast<double>(absoluteBin) / binsPerWindow));
        const auto bin = static_cast<std::size_t>(absoluteBin -
                                                  windowIndex * static_cast<std::int64_t>(numBins));
        const auto tick = position / binsPerWindow * windowTicks_;
        const bool consecutive = hasLastFrame_ && frame == lastFrame_ + 1 && !frozenSinceLastTick_;
        enterBin(channels, done, position, consecutive,
                 [&] { moveMusicalHead(tick, static_cast<double>(windowIndex), bin); });

        const auto boundaryTick =
            static_cast<double>(absoluteBin + 1) / binsPerWindow * windowTicks_;
        const auto boundaryFrame = span.start + (boundaryTick - span.startTick) / ticksPerFrame;
        const auto framesToBoundary = std::ceil(boundaryFrame) - static_cast<double>(frame);
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
        rememberFrame(channels, done - 1, sampleIndex + done - 1,
                      positionOf(sampleIndex + done - 1));
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
    {
        buffer_.clear();
        hasLastFrame_ = false;
    }
    nextSampleIndex_ = sampleIndex + numFrames;
    hasProcessed_ = true;

    const auto numBins = buffer_.numBins();
    // Positions in bins since the start of the stream, exact at every bin boundary.
    const auto positionOf = [&](std::uint64_t frame)
    {
        return static_cast<double>(frame / windowFrames_) * static_cast<double>(numBins) +
               static_cast<double>(frame % windowFrames_ * numBins) /
                   static_cast<double>(windowFrames_);
    };

    std::size_t done = 0;
    while (done < numFrames)
    {
        const auto frame = sampleIndex + done;
        const auto frameInWindow = frame % windowFrames_;
        const auto bin = static_cast<std::size_t>(frameInWindow * numBins / windowFrames_);
        const bool consecutive = hasLastFrame_ && frame == lastFrame_ + 1;
        enterBin(channels, done, positionOf(frame), consecutive,
                 [&] { buffer_.advanceHead(frame / windowFrames_ + 1, bin); });

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
        rememberFrame(channels, done - 1, sampleIndex + done - 1,
                      positionOf(sampleIndex + done - 1));
    }
}

template <typename MoveHead>
void SweepAnalyzer::enterBin(std::span<const float* const> channels, std::size_t offset,
                             double position, bool consecutive, MoveHead moveHead) noexcept
{
    const auto from = std::floor(lastPosition_);
    const auto to = std::floor(position);
    if (!consecutive || to <= from || !(position > lastPosition_))
    {
        moveHead();
        return;
    }

    // The line from the last frame to this one crosses the bin edges from+1 .. to. The bin the
    // head leaves reaches the first crossing, the bin it enters starts at the last one, and each
    // bin in between holds the piece of line inside it.
    const auto numBins = static_cast<double>(buffer_.numBins());
    const auto valueAt = [&](std::size_t channel, double edge)
    {
        const auto start = lastSamples_[channel];
        const auto end = sampleAt(channels[channel], offset);
        const auto fraction = (edge - lastPosition_) / (position - lastPosition_);
        return start + (end - start) * static_cast<float>(fraction);
    };
    const auto binOf = [&](double absoluteBin)
    { return static_cast<std::size_t>(absoluteBin - std::floor(absoluteBin / numBins) * numBins); };

    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        buffer_.addToHead(channel, pointAt(valueAt(channel, from + 1.0)));
    moveHead();
    for (auto edge = from + 1.0; edge < to; edge += 1.0)
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            buffer_.addToBin(channel, binOf(edge),
                             between(valueAt(channel, edge), valueAt(channel, edge + 1.0)));
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        buffer_.addToHead(channel, pointAt(valueAt(channel, to)));
}

void SweepAnalyzer::rememberFrame(std::span<const float* const> channels, std::size_t offset,
                                  std::uint64_t frame, double position) noexcept
{
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        lastSamples_[channel] = sampleAt(channels[channel], offset);
    lastFrame_ = frame;
    lastPosition_ = position;
    hasLastFrame_ = true;
}

std::uint64_t SweepAnalyzer::binStartFrame(std::size_t bin) const noexcept
{
    // The first frame f with ⌊f · numBins / windowFrames⌋ >= bin, i.e. ⌈bin · windowFrames /
    // numBins⌉.
    const auto numBins = static_cast<std::uint64_t>(buffer_.numBins());
    return (bin * windowFrames_ + numBins - 1) / numBins;
}

} // namespace visona
