#pragma once

#include <visona/TimeSignature.h>

#include <span>

namespace visona
{

enum class SidechainSyncState
{
    /** The sidechain input is disabled. */
    off,
    /** The sidechain input is enabled, but no bar impulse has arrived yet. */
    waiting,
    /** offsetFrames() holds a measured offset. */
    locked,
};

/**
    Measures how far the audio reaching a plugin lags the host playhead, from the bar impulses
    that Visona Sync puts on the plugin's sidechain.

    Visona Sync writes an impulse at every bar line of the host timeline. Hosts align a sidechain
    with the plugin's main input, so the impulse arrives exactly as late as the main audio.
    offsetFrames() is how many frames after the playhead's nearest bar line it arrived.

    There is no check that the sidechain really carries Visona Sync yet: the loudest sample of a
    block counts as an impulse when it is above -20 dBFS. The first impulse locks the offset. A
    different offset replaces it once two impulses in a row agree on it, as when a plugin with
    latency is added.

    The app measures its sync input the same way, against MIDI Clock's timeline instead of a
    playhead (D-109).
*/
class SidechainSyncDetector
{
public:
    /** Forgets the offset, as for a new stream. */
    void reset() noexcept;

    /** Whether the host feeds the sidechain input. Disabling it forgets the offset. */
    void setSidechainEnabled(bool enabled) noexcept;

    /**
        Looks for a bar impulse in one block of the sidechain. `ppqAtBlockStart` is the host
        playhead at the block's first frame.
    */
    void processBlock(std::span<const float> sidechain, double ppqAtBlockStart, double bpm,
                      TimeSignature timeSignature, double sampleRate, bool hostPlaying) noexcept;

    [[nodiscard]] SidechainSyncState state() const noexcept
    {
        return state_;
    }

    /** Frames the sidechain impulse arrives after the playhead's bar line; 0 until locked. */
    [[nodiscard]] double offsetFrames() const noexcept
    {
        return offsetFrames_;
    }

    /** The peak level of the last impulse, as a gain. */
    [[nodiscard]] float impulsePeak() const noexcept
    {
        return impulsePeak_;
    }

private:
    SidechainSyncState state_ = SidechainSyncState::off;
    bool enabled_ = false;
    double offsetFrames_ = 0.0;
    bool hasCandidate_ = false;
    double candidateFrames_ = 0.0;
    float impulsePeak_ = 0.0f;
};

} // namespace visona
