#include "visona/SidechainSyncDetector.h"

#include <visona/BarImpulseScheduler.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace visona
{

namespace
{

constexpr float minPeak = 0.12f;
constexpr float maxPeak = 1.0f;
constexpr int acquisitionSearchHalfWidth = 4096;
constexpr int lockedSearchHalfWidth = 256;
constexpr int maxImpulseWidth = 12;
constexpr float minProminenceRatio = 2.5f;
constexpr int barsToLock = 3;
constexpr int missedBarsToUnlock = 4;
constexpr int missedBarsToInvalidate = 8;
constexpr int maxPeaksPerWindow = 1;
constexpr double maxOffsetJumpFrames = 8'192.0;

int countProminentPeaks(std::span<const float> sidechain, std::uint64_t blockStartSample,
                        std::int64_t windowStart, std::int64_t windowEnd) noexcept
{
    int peaks = 0;
    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
        const auto sampleIndex =
            static_cast<std::int64_t>(blockStartSample + static_cast<std::uint64_t>(index));
        if (sampleIndex < windowStart || sampleIndex > windowEnd)
            continue;

        const auto value = std::abs(sidechain[index]);
        if (value < minPeak)
            continue;
        const auto prev = index > 0 ? std::abs(sidechain[index - 1]) : 0.0f;
        const auto next =
            index + 1 < sidechain.size() ? std::abs(sidechain[index + 1]) : 0.0f;
        if (value >= prev && value >= next)
            ++peaks;
    }
    return peaks;
}

} // namespace

void SidechainSyncDetector::reset() noexcept
{
    state_ = sidechainEnabled_ ? SidechainSyncState::waiting : SidechainSyncState::off;
    offsetFrames_ = 0.0;
    consecutiveMatches_ = 0;
    missedBars_ = 0;
}

void SidechainSyncDetector::setSidechainEnabled(bool enabled) noexcept
{
    sidechainEnabled_ = enabled;
    if (!enabled)
    {
        state_ = SidechainSyncState::off;
        consecutiveMatches_ = 0;
        missedBars_ = 0;
        return;
    }
    if (state_ == SidechainSyncState::off)
        state_ = SidechainSyncState::waiting;
}

SidechainSyncDetector::PeakHit SidechainSyncDetector::findBarPeak(
    std::span<const float> sidechain, std::uint64_t blockStartSample,
    double searchCenterSample, int searchHalfWidth) const noexcept
{
    PeakHit hit;
    if (sidechain.empty() || searchHalfWidth <= 0)
        return hit;

    const auto searchCenter = static_cast<std::int64_t>(std::llround(searchCenterSample));
    const auto windowStart = searchCenter - searchHalfWidth;
    const auto windowEnd = searchCenter + searchHalfWidth;

    float secondPeak = 0.0f;
    double sumSquares = 0.0;
    std::size_t count = 0;

    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
        const auto sampleIndex =
            static_cast<std::int64_t>(blockStartSample + static_cast<std::uint64_t>(index));
        if (sampleIndex < windowStart || sampleIndex > windowEnd)
            continue;

        const auto value = std::abs(sidechain[index]);
        sumSquares += static_cast<double>(value) * static_cast<double>(value);
        ++count;
        if (value > hit.peak)
        {
            secondPeak = hit.peak;
            hit.peak = value;
            hit.sampleIndex = sampleIndex;
            hit.found = true;
        }
        else if (value > secondPeak)
        {
            secondPeak = value;
        }
    }

    if (!hit.found)
        return hit;

    const auto rms =
        count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0.0f;
    if (hit.peak < minPeak || hit.peak > maxPeak)
    {
        hit.found = false;
        return hit;
    }
    if (rms > 0.0f && hit.peak / rms < minProminenceRatio)
    {
        hit.found = false;
        return hit;
    }
    if (secondPeak > 0.0f && hit.peak < secondPeak * 1.5f)
    {
        hit.found = false;
        return hit;
    }

    return hit;
}

bool SidechainSyncDetector::peakLooksLikeImpulse(const PeakHit& hit,
                                                 std::span<const float> sidechain,
                                                 std::uint64_t blockStartSample) const noexcept
{
    if (!hit.found)
        return false;

    const auto relative =
        static_cast<std::size_t>(hit.sampleIndex - static_cast<std::int64_t>(blockStartSample));
    if (relative >= sidechain.size())
        return false;

    const auto start = relative > 0 ? relative - 1 : relative;
    const auto end = std::min(relative + maxImpulseWidth, sidechain.size());
    auto width = 0;
    for (auto index = start; index < end; ++index)
    {
        if (std::abs(sidechain[index]) >= hit.peak * 0.5f)
            ++width;
    }
    return width <= maxImpulseWidth;
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
    {
        missedBars_ = 0;
        return;
    }

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
        const auto searchHalfWidth = state_ == SidechainSyncState::locked ? lockedSearchHalfWidth
                                                                          : acquisitionSearchHalfWidth;
        const auto searchCenter =
            state_ == SidechainSyncState::locked ? expectedBoundary + offsetFrames_
                                                 : expectedBoundary;
        const auto windowStart = static_cast<std::int64_t>(std::llround(searchCenter)) -
                                 searchHalfWidth;
        const auto windowEnd = static_cast<std::int64_t>(std::llround(searchCenter)) +
                               searchHalfWidth;

        if (countProminentPeaks(sidechain, blockStartSample, windowStart, windowEnd) >
            maxPeaksPerWindow)
        {
            ++missedBars_;
            consecutiveMatches_ = 0;
            if (state_ == SidechainSyncState::locked && missedBars_ >= missedBarsToUnlock)
            {
                state_ = SidechainSyncState::invalid;
                offsetFrames_ = 0.0;
            }
            else if (state_ == SidechainSyncState::waiting &&
                     missedBars_ >= missedBarsToInvalidate)
            {
                state_ = SidechainSyncState::invalid;
            }
            continue;
        }

        const auto hit =
            findBarPeak(sidechain, blockStartSample, searchCenter, searchHalfWidth);
        if (!peakLooksLikeImpulse(hit, sidechain, blockStartSample))
        {
            ++missedBars_;
            consecutiveMatches_ = 0;
            if (state_ == SidechainSyncState::locked && missedBars_ >= missedBarsToUnlock)
            {
                state_ = SidechainSyncState::invalid;
                offsetFrames_ = 0.0;
            }
            else if (state_ == SidechainSyncState::waiting &&
                     missedBars_ >= missedBarsToInvalidate)
            {
                state_ = SidechainSyncState::invalid;
            }
            continue;
        }

        missedBars_ = 0;
        const auto measuredOffset = static_cast<double>(hit.sampleIndex) - expectedBoundary;
        if (state_ == SidechainSyncState::locked &&
            std::abs(measuredOffset - offsetFrames_) > maxOffsetJumpFrames)
        {
            state_ = SidechainSyncState::waiting;
            consecutiveMatches_ = 0;
            offsetFrames_ = 0.0;
            return;
        }

        if (consecutiveMatches_ == 0)
            offsetFrames_ = measuredOffset;
        else
            offsetFrames_ = 0.75 * offsetFrames_ + 0.25 * measuredOffset;

        ++consecutiveMatches_;
        if (consecutiveMatches_ >= barsToLock)
            state_ = SidechainSyncState::locked;
        else
            state_ = SidechainSyncState::waiting;
    }
}

} // namespace visona
