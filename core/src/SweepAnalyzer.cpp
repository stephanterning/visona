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
        span.add(samples[frame]);
    return span;
}

} // namespace

std::uint64_t freeRunningWindowFrames(double sampleRate, double seconds) noexcept
{
    const auto frames = std::llround(std::max(sampleRate, 0.0) * std::max(seconds, 0.0));
    return static_cast<std::uint64_t>(std::max(frames, 1LL));
}

SweepAnalyzer::SweepAnalyzer(std::size_t numChannels, std::size_t numBins)
    : buffer_(numChannels, numBins)
    , splitters_(numChannels)
{
}

void SweepAnalyzer::setSampleRate(double sampleRate) noexcept
{
    const BandSplitter splitter(sampleRate);
    std::fill(splitters_.begin(), splitters_.end(), splitter);
    splitsBands_ = splitter.hasSampleRate();
    for (const auto band : splitBands)
        bandDelayFrames_[splitIndex(band)] = splitter.delayFrames(band);
}

double SweepAnalyzer::bandDelayFrames(Band band) const noexcept
{
    return band == Band::full ? 0.0 : bandDelayFrames_[splitIndex(band)];
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

    if (hasProcessed_ && sampleIndex != nextSampleIndex_)
    {
        if (sampleIndex < nextSampleIndex_)
            buffer_.clear();
        for (auto& splitter : splitters_)
            splitter.reset();
    }
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
            const float* const samples =
                channels[channel] == nullptr ? nullptr : channels[channel] + done;
            if (!splitsBands_)
            {
                buffer_.addToHead(channel, spanOf(samples, run));
                continue;
            }

            auto& splitter = splitters_[channel];
            SweepCell full;
            SweepCell low;
            SweepCell mid;
            SweepCell high;
            for (std::size_t frame = 0; frame < run; ++frame)
            {
                const auto sample = samples != nullptr ? samples[frame] : 0.0f;
                const auto bands = splitter.process(sample);
                full.add(sample);
                low.add(bands.low);
                mid.add(bands.mid);
                high.add(bands.high);
            }
            buffer_.addToHead(channel, Band::full, full);
            buffer_.addToHead(channel, Band::low, low);
            buffer_.addToHead(channel, Band::mid, mid);
            buffer_.addToHead(channel, Band::high, high);
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
