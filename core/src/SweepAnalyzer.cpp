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
    nextSampleIndex_ = 0;
    hasProcessed_ = false;
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
