#pragma once

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
    Writes the full band of every channel into a SweepBuffer, as a sweep across a window of a fixed
    number of frames: the free-running sweep (D-045, D-060).

    The frame at stream position s lies at phase φ = frac(s / windowFrames) and goes into bin
    ⌊φ · numBins⌋ of pass ⌊s / windowFrames⌋ + 1. Windows therefore start at every multiple of
    windowFrames from the start of the stream. The arithmetic is exact integer arithmetic, so the
    result never depends on how the audio is split into blocks.

    A jump forward in sampleIndex, such as a block the audio ring dropped (D-062), empties the bins
    of the missing frames, so a gap shows as a gap and not as the previous pass (D-067). A jump
    backwards clears the buffer.

    All storage is allocated in the constructor; start() and process() never allocate.
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
    SweepBuffer buffer_;
    std::uint64_t windowFrames_ = 0;
    std::uint64_t nextSampleIndex_ = 0;
    bool hasProcessed_ = false;
};

} // namespace visona
