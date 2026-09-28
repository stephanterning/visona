#include "visona/SidechainSyncDetector.h"

#include <visona/BarImpulseScheduler.h>

#include <cmath>

namespace visona
{

namespace
{

constexpr float minSyncPeak = 0.30f;
constexpr float maxSyncPeak = 0.70f;

double samplesPerQuarterNote(double bpm, double sampleRate) noexcept
{
    return sampleRate * 60.0 / std::max(bpm, 1.0e-9);
}

double quarterNotesPerBar(TimeSignature timeSignature) noexcept
{
    return static_cast<double>(timeSignature.numerator) * 4.0 /
           static_cast<double>(timeSignature.denominator);
}

double maxOffsetFrames(double bpm, TimeSignature timeSignature, double sampleRate) noexcept
{
    return quarterNotesPerBar(timeSignature) * samplesPerQuarterNote(bpm, sampleRate);
}

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

SidechainSyncDetector::PeakHit SidechainSyncDetector::findStrongestPeak(
    std::span<const float> sidechain, std::uint64_t blockStartSample) const noexcept
{
    PeakHit hit;
    for (std::size_t index = 0; index < sidechain.size(); ++index)
    {
        const auto value = std::abs(sidechain[index]);
        if (value <= hit.peak)
            continue;

        hit.peak = value;
        hit.sampleIndex =
            static_cast<std::int64_t>(blockStartSample + static_cast<std::uint64_t>(index));
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
    const auto hit = findStrongestPeak(sidechain, blockStartSample);
    if (!hit.found || hit.peak < minSyncPeak || hit.peak > maxSyncPeak)
        return;

    const auto samplesPerQuarter = samplesPerQuarterNote(bpm, sampleRate);
    const auto barLength = quarterNotesPerBar(timeSignature);
    const auto peakPpq =
        ppqAtBlockStart +
        (static_cast<double>(hit.sampleIndex) - static_cast<double>(blockStartSample)) /
            samplesPerQuarter;
    const auto nearestBar =
        static_cast<std::int64_t>(std::floor(peakPpq / barLength + 1.0e-9));
    const auto barPpq = static_cast<double>(nearestBar) * barLength;
    const auto expectedBoundary = BarImpulseScheduler::sampleIndexOfBarBoundary(
        ppqAtBlockStart, blockStartSample, barPpq, bpm, sampleRate);
    const auto measuredOffset = static_cast<double>(hit.sampleIndex) - expectedBoundary;
    if (std::abs(measuredOffset) > maxOffsetFrames(bpm, timeSignature, sampleRate))
        return;

    if (state_ == SidechainSyncState::locked)
        offsetFrames_ = 0.9 * offsetFrames_ + 0.1 * measuredOffset;
    else
        offsetFrames_ = measuredOffset;

    state_ = SidechainSyncState::locked;
}

} // namespace visona
