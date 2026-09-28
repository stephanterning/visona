#include "visona/SidechainSyncDetector.h"

#include <visona/BarImpulseScheduler.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace visona
{

namespace
{

constexpr float minPeak = 0.18f;
constexpr float maxPeak = 0.85f;
constexpr int localSearchHalfWidth = 64;
constexpr int maxImpulseWidth = 12;
constexpr float minProminenceRatio = 4.0f;
constexpr int barsToLock = 3;
constexpr int missedBarsToUnlock = 4;
constexpr int maxPeaksPerBlock = 1;
constexpr double maxOffsetJumpFrames = 8'192.0;

int countProminentPeaks(std::span<const float> sidechain) noexcept
{
    int peaks = 0;
    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
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
    double expectedBoundarySample) const noexcept
{
    PeakHit hit;
    if (sidechain.empty())
        return hit;

    const auto expected = static_cast<std::int64_t>(std::llround(expectedBoundarySample));
    const auto windowStart = expected - localSearchHalfWidth;
    const auto windowEnd = expected + localSearchHalfWidth;

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
    if (secondPeak > 0.0f && hit.peak < secondPeak * 1.8f)
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

    if (countProminentPeaks(sidechain) > maxPeaksPerBlock)
    {
        ++missedBars_;
        consecutiveMatches_ = 0;
        if (state_ == SidechainSyncState::locked && missedBars_ >= missedBarsToUnlock)
        {
            state_ = SidechainSyncState::invalid;
            offsetFrames_ = 0.0;
        }
        else if (state_ == SidechainSyncState::waiting && missedBars_ >= barsToLock)
        {
            state_ = SidechainSyncState::invalid;
        }
        return;
    }

    for (const auto offset : boundaries)
    {
        const auto expectedBoundary =
            static_cast<double>(blockStartSample + static_cast<std::uint64_t>(offset));
        const auto hit = findBarPeak(sidechain, blockStartSample, expectedBoundary);
        if (!peakLooksLikeImpulse(hit, sidechain, blockStartSample))
        {
            ++missedBars_;
            consecutiveMatches_ = 0;
            if (state_ == SidechainSyncState::locked && missedBars_ >= missedBarsToUnlock)
            {
                state_ = SidechainSyncState::invalid;
                offsetFrames_ = 0.0;
            }
            else if (state_ == SidechainSyncState::waiting && missedBars_ >= barsToLock)
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
