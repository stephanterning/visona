#pragma once

#include <visona/AudioRingBuffer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace visona
{

/**
    The audio thread's side of the audio input: for every device callback, write() copies the input
    routed to each ring channel into an AudioRingBuffer, together with the block's BlockTiming.

    The writer numbers the frames. sampleIndex is 0 for the first block written, and counts every
    frame the device delivers from then on, including the frames of blocks the ring drops. The
    consumer therefore sees a dropped block as a jump in sampleIndex (D-062). Create one writer per
    stream, so that sampleIndex starts at 0 when the stream starts.

    Routes can be changed from any thread while audio runs, and take effect from the next block.
    All storage is allocated in the constructor; write() never allocates, locks or waits.
*/
class AudioInputWriter
{
public:
    /** The route of a ring channel that stores silence. */
    static constexpr int noInput = -1;

    /** `ring` must outlive the writer. Every ring channel starts out routed to noInput. */
    explicit AudioInputWriter(AudioRingBuffer& ring);

    AudioInputWriter(const AudioInputWriter&) = delete;
    AudioInputWriter& operator=(const AudioInputWriter&) = delete;

    [[nodiscard]] std::size_t numChannels() const noexcept
    {
        return routes_.size();
    }

    /**
        Any thread. From the next block on, ring channel `channel` stores input `input` of each
       block: an index into the `inputs` passed to write(). A negative input, or one past the end of
       a block's inputs, stores silence.
    */
    void route(std::size_t channel, int input) noexcept;

    /** Any thread. The input that ring channel `channel` currently stores. */
    [[nodiscard]] int routeOf(std::size_t channel) const noexcept;

    /**
        Audio thread only. Pushes `numFrames` frames of the routed inputs to the ring, stamped with
        the next sampleIndex and `hostTimeNs`. A null input pointer stores silence. Returns false if
        the ring dropped the block.
    */
    bool write(std::span<const float* const> inputs, std::uint32_t numFrames,
               std::uint64_t hostTimeNs) noexcept;

    /** Any thread. The sampleIndex of the next block, which is every frame written so far. */
    [[nodiscard]] std::uint64_t nextSampleIndex() const noexcept
    {
        return nextSampleIndex_.load(std::memory_order_relaxed);
    }

private:
    AudioRingBuffer& ring_;
    std::vector<std::atomic<int>> routes_;

    // Audio thread only: the routed input of each ring channel for the block being written.
    std::vector<const float*> channels_;
    std::atomic<std::uint64_t> nextSampleIndex_{0};
};

} // namespace visona
