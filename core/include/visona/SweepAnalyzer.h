#pragma once

#include <visona/MidiClockTransport.h>
#include <visona/SweepBuffer.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace visona
{

/** The window of the free-running sweep, before the first MIDI Clock Start (D-060). */
inline constexpr double freeRunningWindowSeconds = 2.0;

/** The free-running window in frames at `sampleRate`, at least 1. */
[[nodiscard]] std::uint64_t freeRunningWindowFrames(double sampleRate) noexcept;

/**
    Writes the full band of every channel into a SweepBuffer, as a sweep across a window. It runs in
    one of two modes.

    Free-running (start(), process()): the window is a fixed number of frames (D-045, D-060). The
    frame at stream position s lies at phase φ = frac(s / windowFrames) and goes into bin
    ⌊φ · numBins⌋ of pass ⌊s / windowFrames⌋ + 1. Windows start at every multiple of windowFrames
    from the start of the stream. The arithmetic is exact integer arithmetic.

    Musical (startMusical(), processMusical(), freeze()): the window is a number of MIDI Clock
    ticks, and each frame's position in ticks comes from a TransportSpan (architecture.md 3.4).
    φ = frac(position / windowTicks), so windows start on multiples of the window from bar 1. Passes
    are counted from 1 at startMusical(). After a freeze, a position that does not carry on from
    where the sweep stopped is a relocation: the head jumps there and starts a new pass without
    emptying anything, so the old content becomes the previous pass.

    In both modes the result never depends on how the audio is split into blocks. A jump forward
    in sampleIndex, such as a block the audio ring dropped (D-062), empties the bins of the missing
    frames, so a gap shows as a gap and not as the previous pass (D-067). A free-running jump
    backwards clears the buffer.

    All storage is allocated in the constructor; nothing else allocates.
*/
class SweepAnalyzer
{
public:
    /** Throws std::invalid_argument if `numBins` is 0. */
    explicit SweepAnalyzer(std::size_t numChannels,
                           std::size_t numBins = SweepBuffer::defaultBinCount);

    /** Clears the buffer and starts a sweep across `windowFrames` frames. A window of 0 frames
        stops the sweep: process() then ignores its input. */
    void start(std::uint64_t windowFrames) noexcept;

    /**
        Adds `numFrames` frames from stream position `sampleIndex` on. `channels` holds one pointer
        per channel; a null pointer is silence.
    */
    void process(std::uint64_t sampleIndex, std::span<const float* const> channels,
                 std::size_t numFrames) noexcept;

    /** Clears the buffer and starts a musical sweep across `windowTicks` ticks, which must be
        positive. */
    void startMusical(double windowTicks) noexcept;

    /**
        Adds `numFrames` frames from stream position `sampleIndex` on, at the positions `span`
        gives them. The frames must lie inside `span`, which must be musical.
    */
    void processMusical(std::uint64_t sampleIndex, std::span<const float* const> channels,
                        std::size_t numFrames, const TransportSpan& span) noexcept;

    /** Notes frames that are not written, while the transport is frozen. The next musical frames
        may be a relocation. */
    void freeze() noexcept;

    [[nodiscard]] bool isMusical() const noexcept
    {
        return windowTicks_ > 0.0;
    }

    [[nodiscard]] double windowTicks() const noexcept
    {
        return windowTicks_;
    }

    /** Musical: the tick at which the head's window starts, or 0 before anything is written. */
    [[nodiscard]] double windowStartTick() const noexcept;

    [[nodiscard]] const SweepBuffer& buffer() const noexcept
    {
        return buffer_;
    }

    [[nodiscard]] std::uint64_t windowFrames() const noexcept
    {
        return windowFrames_;
    }

    /** The frame of the window at which bin `bin` starts; binStartFrame(numBins) is the window
        length. Only meaningful while a sweep is running. */
    [[nodiscard]] std::uint64_t binStartFrame(std::size_t bin) const noexcept;

private:
    void moveMusicalHead(double tick, double windowIndex, std::size_t bin) noexcept;

    SweepBuffer buffer_;
    std::uint64_t windowFrames_ = 0;
    std::uint64_t nextSampleIndex_ = 0;
    bool hasProcessed_ = false;

    // Musical mode, while windowTicks_ is positive.
    double windowTicks_ = 0.0;
    double windowIndex_ = 0.0;
    double lastTick_ = 0.0;
    bool frozenSinceLastTick_ = false;
};

} // namespace visona
