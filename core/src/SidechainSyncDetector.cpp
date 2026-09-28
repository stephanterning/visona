#include "visona/SidechainSyncDetector.h"

#include <cmath>
#include <cstddef>

namespace visona
{

namespace
{

/** -20 dBFS. Visona Sync writes -6 dBFS; a host may pan, sum or split it on the way. */
constexpr float minImpulsePeak = 0.1f;

/** Measurements this close are the same offset; the impulse is placed to the nearest frame. */
constexpr double agreementFrames = 2.0;

} // namespace

void SidechainSyncDetector::reset() noexcept
{
    state_ = enabled_ ? SidechainSyncState::waiting : SidechainSyncState::off;
    offsetFrames_ = 0.0;
    hasCandidate_ = false;
    impulsePeak_ = 0.0f;
}

void SidechainSyncDetector::setSidechainEnabled(bool enabled) noexcept
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    reset();
}

void SidechainSyncDetector::processBlock(std::span<const float> sidechain, double ppqAtBlockStart,
                                         double bpm, TimeSignature timeSignature, double sampleRate,
                                         bool hostPlaying) noexcept
{
    if (!enabled_ || !hostPlaying || bpm <= 0.0 || sampleRate <= 0.0 || sidechain.empty())
        return;

    std::size_t peakIndex = 0;
    float peak = 0.0f;
    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
        if (const auto value = std::abs(sidechain[index]); value > peak)
        {
            peak = value;
            peakIndex = index;
        }
    }
    if (peak < minImpulsePeak)
        return;

    const auto framesPerQuarter = sampleRate * 60.0 / bpm;
    const auto quartersPerBar = static_cast<double>(timeSignature.ticksPerBar()) /
                                static_cast<double>(TimeSignature::ticksPerQuarterNote);
    const auto ppqAtPeak = ppqAtBlockStart + static_cast<double>(peakIndex) / framesPerQuarter;
    const auto nearestBar = std::round(ppqAtPeak / quartersPerBar) * quartersPerBar;
    const auto measured = std::round((ppqAtPeak - nearestBar) * framesPerQuarter);
    impulsePeak_ = peak;

    if (state_ != SidechainSyncState::locked)
    {
        state_ = SidechainSyncState::locked;
        offsetFrames_ = measured;
        hasCandidate_ = false;
        return;
    }
    if (std::abs(measured - offsetFrames_) <= agreementFrames)
    {
        hasCandidate_ = false;
        return;
    }
    if (hasCandidate_ && std::abs(measured - candidateFrames_) <= agreementFrames)
    {
        offsetFrames_ = measured;
        hasCandidate_ = false;
        return;
    }
    hasCandidate_ = true;
    candidateFrames_ = measured;
}

} // namespace visona
