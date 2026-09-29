#include "visona/BarPeaks.h"

#include <algorithm>
#include <cmath>

namespace visona
{

void BarPeakMeter::restart() noexcept
{
    inBar_ = false;
    recent_.currentPeak = 0.0f;
}

void BarPeakMeter::process(std::span<const float* const> channels, std::size_t numFrames,
                           std::uint64_t sampleIndex, const TransportSpan& span, double ticksPerBar,
                           double sampleRate) noexcept
{
    if (numFrames == 0 || !(ticksPerBar > 0.0) || !(span.end > span.start) ||
        !(span.endTick > span.startTick) || !std::isfinite(span.end))
        return;

    const auto ticksPerFrame = (span.endTick - span.startTick) / (span.end - span.start);
    std::size_t done = 0;
    while (done < numFrames)
    {
        const auto frame = static_cast<double>(sampleIndex + done);
        const auto tick = span.startTick + (frame - span.start) * ticksPerFrame;
        const auto bar = static_cast<std::int64_t>(std::floor(tick / ticksPerBar));
        if (inBar_ && bar != bar_)
            endBar(sampleRate);
        if (!inBar_)
        {
            inBar_ = true;
            bar_ = bar;
            peak_ = 0.0f;
            frames_ = 0;
        }

        const auto boundaryTick = static_cast<double>(bar + 1) * ticksPerBar;
        const auto boundaryFrame = span.start + (boundaryTick - span.startTick) / ticksPerFrame;
        const auto framesToBoundary = std::ceil(boundaryFrame) - frame;
        const auto run = framesToBoundary < 1.0
                             ? std::size_t{1}
                             : static_cast<std::size_t>(std::min(
                                   framesToBoundary, static_cast<double>(numFrames - done)));
        for (const auto* channel : channels)
            if (channel != nullptr)
                for (std::size_t offset = done; offset < done + run; ++offset)
                    peak_ = std::max(peak_, std::abs(channel[offset]));
        frames_ += run;
        done += run;
    }
    recent_.currentPeak = inBar_ ? peak_ : 0.0f;
}

void BarPeakMeter::endBar(double sampleRate) noexcept
{
    ++recent_.count;
    auto& ended = recent_.bars[(recent_.count - 1) % RecentBarPeaks::capacity];
    ended.peak = peak_;
    ended.seconds = sampleRate > 0.0 ? static_cast<double>(frames_) / sampleRate : 0.0;
    inBar_ = false;
}

} // namespace visona
