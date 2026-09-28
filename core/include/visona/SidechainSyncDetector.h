#pragma once

#include <visona/TimeSignature.h>

#include <cstdint>
#include <span>

namespace visona
{

enum class SidechainSyncState
{
    off,
    waiting,
    locked,
    invalid,
};

/**
    Validates sidechain audio as Visona Sync bar impulses and estimates analysis offset.

    When locked, `offsetFrames()` is the sidechain peak minus the expected bar boundary: apply it
    by subtracting from audio sample indices when mapping to the host transport grid.
*/
class SidechainSyncDetector
{
public:
    void reset() noexcept;

    /** Sidechain bus disabled in the host. */
    void setSidechainEnabled(bool enabled) noexcept;

    /**
        Processes one sidechain block. `blockEndSample` is the stream index after this block;
        `ppqAtBlockEnd` is the host PPQ there.
    */
    void processBlock(std::span<const float> sidechain, std::uint64_t blockEndSample,
                     std::uint32_t numFrames, double ppqAtBlockEnd, double bpm,
                     TimeSignature timeSignature, double sampleRate, bool hostPlaying) noexcept;

    [[nodiscard]] SidechainSyncState state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] double offsetFrames() const noexcept
    {
        return offsetFrames_;
    }

private:
    struct PeakHit
    {
        bool found = false;
        std::int64_t sampleIndex = 0;
        float peak = 0.0f;
    };

    [[nodiscard]] PeakHit findBarPeak(std::span<const float> sidechain,
                                      std::uint64_t blockStartSample,
                                      double expectedBoundarySample) const noexcept;

    [[nodiscard]] bool peakLooksLikeImpulse(const PeakHit& hit,
                                            std::span<const float> sidechain,
                                            std::uint64_t blockStartSample) const noexcept;

    SidechainSyncState state_ = SidechainSyncState::off;
    double offsetFrames_ = 0.0;
    int consecutiveMatches_ = 0;
    int missedBars_ = 0;
    bool sidechainEnabled_ = false;
};

} // namespace visona
