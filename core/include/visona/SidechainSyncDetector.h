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
    Tracks Visona Sync bar impulses on the sidechain bus and estimates analysis offset.

    Permissive mode for early testing: locks on the first detected bar peak and keeps the last
    offset when a bar is missed. When locked, `offsetFrames()` is the sidechain peak minus the
    expected bar boundary.
*/
class SidechainSyncDetector
{
public:
    void reset() noexcept;

    /** Sidechain bus disabled in the host. */
    void setSidechainEnabled(bool enabled) noexcept;

    /**
        Processes one sidechain block. `blockEndSample` is the stream index after this block;
        `ppqAtBlockStart` is the host PPQ at the first frame of this block.
    */
    void processBlock(std::span<const float> sidechain, std::uint64_t blockEndSample,
                     std::uint32_t numFrames, double ppqAtBlockStart, double bpm,
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
                                      double searchCenterSample, int searchHalfWidth) const noexcept;

    SidechainSyncState state_ = SidechainSyncState::off;
    double offsetFrames_ = 0.0;
    bool sidechainEnabled_ = false;
};

} // namespace visona
