#include "visona/SidechainSyncDetector.h"

#include <visona/BarImpulseScheduler.h>

#include <cmath>
#include <vector>

namespace visona
{

namespace
{

constexpr float minPeak = 0.08f;
constexpr int searchHalfWidth = 4096;

} // namespace

void SidechainSyncDetector::reset() noexcept
{
    state_ = sidechainEnabled_ ? SidechainSyncState::waiting : SidechainSyncState::off;
    offsetFrames_ = 0.0;
}

void SidechainSyncDetector::setSidechainEnabled(bool enabled) noexcept
{
    sidechainEnabled_ = enabled;
    if (!enabled)
    {
        state_ = SidechainSyncState::off;
        return;
    }
    if (state_ == SidechainSyncState::off)
        state_ = SidechainSyncState::waiting;
}

SidechainSyncDetector::PeakHit SidechainSyncDetector::findBarPeak(
    std::span<const float> sidechain, std::uint64_t blockStartSample,
    double searchCenterSample, int searchHalfWidthSamples) const noexcept
{
    PeakHit hit;
    if (sidechain.empty() || searchHalfWidthSamples <= 0)
        return hit;

    const auto searchCenter = static_cast<std::int64_t>(std::llround(searchCenterSample));
    const auto windowStart = searchCenter - searchHalfWidthSamples;
    const auto windowEnd = searchCenter + searchHalfWidthSamples;

    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
        const auto sampleIndex =
            static_cast<std::int64_t>(blockStartSample + static_cast<std::uint64_t>(index));
        if (sampleIndex < windowStart || sampleIndex > windowEnd)
            continue;

        const auto value = std::abs(sidechain[index]);
        if (value < minPeak || value <= hit.peak)
            continue;

        hit.peak = value;
        hit.sampleIndex = sampleIndex;
        hit.found = true;
    }

    return hit;
}

void SidechainSyncDetector::processBlock(std::span<const float> sidechain,
                                         std::uint64_t blockEndSample, std::uint32_t numFrames,
                                         double ppqAtBlockStart, double bpm,
                                         TimeSignature timeSignature, double sampleRate,
                                         bool hostPlaying) noexcept
{
    if (!sidechainEnabled_)
        return;

    if (!hostPlaying || bpm <= 0.0 || sampleRate <= 0.0 || numFrames == 0)
        return;

    const auto blockStartSample = blockEndSample - numFrames;
    std::vector<std::uint32_t> boundaries;
    BarImpulseScheduler::impulsesInBlock(blockStartSample, numFrames, ppqAtBlockStart, bpm,
                                         timeSignature, sampleRate, boundaries);
    if (boundaries.empty())
        return;

    for (const auto offset : boundaries)
    {
        const auto expectedBoundary =
            static_cast<double>(blockStartSample + static_cast<std::uint64_t>(offset));
        const auto searchCenter = state_ == SidechainSyncState::locked
                                      ? expectedBoundary + offsetFrames_
                                      : expectedBoundary;
        const auto hit =
            findBarPeak(sidechain, blockStartSample, searchCenter, searchHalfWidth);
        if (!hit.found)
            continue;

        const auto measuredOffset = static_cast<double>(hit.sampleIndex) - expectedBoundary;
        if (state_ == SidechainSyncState::locked)
            offsetFrames_ = 0.9 * offsetFrames_ + 0.1 * measuredOffset;
        else
            offsetFrames_ = measuredOffset;

        state_ = SidechainSyncState::locked;
    }
}

} // namespace visona
