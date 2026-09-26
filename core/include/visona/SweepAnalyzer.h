#pragma once

#include <visona/Band.h>
#include <visona/BandSplitter.h>
#include <visona/SweepBuffer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace visona
{

/** The window of the free-running sweep, before the first MIDI Clock Start (D-060). */
inline constexpr double freeRunningWindowSeconds = 2.0;

/** A free-running window of `seconds` in frames at `sampleRate`, at least 1. */
[[nodiscard]] std::uint64_t
freeRunningWindowFrames(double sampleRate, double seconds = freeRunningWindowSeconds) noexcept;

/**
    Writes every channel into a SweepBuffer, as a sweep across a window of a fixed number of frames:
    the free-running sweep (D-045, D-060). Each bin gets the signed min/max of the full band and,
    once setSampleRate() has set up the band splitters, of the low, mid and high bands (D-073).

    The frame at stream position s lies at phase φ = frac(s / windowFrames) and goes into bin
    ⌊φ · numBins⌋ of pass ⌊s / windowFrames⌋ + 1. Windows therefore start at every multiple of
    windowFrames from the start of the stream. The arithmetic is exact integer arithmetic, so the
    result never depends on how the audio is split into blocks.

    A jump forward in sampleIndex, such as a block the audio ring dropped (D-062), empties the bins
    of the missing frames, so a gap shows as a gap and not as the previous pass (D-067). A jump
    backwards clears the buffer. Either jump also resets the band splitters, since the audio before
    it does not lead into the audio after it.

    All storage is allocated in the constructor; no other function allocates.
*/
class SweepAnalyzer
{
public:
    /** Throws std::invalid_argument if `numBins` is 0. */
    explicit SweepAnalyzer(std::size_t numChannels,
                           std::size_t numBins = SweepBuffer::defaultBinCount);

    /**
        Sets up a BandSplitter per channel, with the default crossovers, for audio at `sampleRate`,
        and resets it. Until then, or with a rate of 0, only the full band is written and the split
        bands stay empty.
    */
    void setSampleRate(double sampleRate) noexcept;

    /** How many frames split band `band` lags the full band (BandSplitter::delayFrames()), or 0
        without a sample rate. */
    [[nodiscard]] double bandDelayFrames(Band band) const noexcept;

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
    std::vector<BandSplitter> splitters_;
    bool splitsBands_ = false;
    std::array<double, splitBands.size()> bandDelayFrames_{};

    std::uint64_t windowFrames_ = 0;
    std::uint64_t nextSampleIndex_ = 0;
    bool hasProcessed_ = false;
};

} // namespace visona
