#pragma once

#include <visona/BlockTiming.h>
#include <visona/CacheLine.h>
#include <visona/SpscQueue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace visona
{

/**
    A lock-free, single-producer single-consumer FIFO of planar multichannel audio blocks.

    The audio thread pushes each block's samples together with its BlockTiming, and the analysis
    thread reads them. A block is stored whole or not at all: if the ring lacks room for the whole
    block, push() drops it and counts an overrun. Unread audio is never overwritten and the
    producer never waits. The consumer sees a dropped block as a jump in sampleIndex.

    The consumer may read part of a block and leave the rest for later, for example to analyze
    audio only up to the latest known MIDI Clock tick.

    Sample and timing storage is allocated in the constructor. push(), peek() and consume() never
    allocate, lock or wait.
*/
class AudioRingBuffer
{
public:
    /** Unread frames that are contiguous both in memory and in stream time. */
    class ReadRegion
    {
    public:
        /** Timing of the block these frames belong to, exactly as it was pushed. */
        [[nodiscard]] const BlockTiming& block() const noexcept
        {
            return block_;
        }

        /** Stream position of the region's first frame. */
        [[nodiscard]] std::uint64_t sampleIndex() const noexcept
        {
            return sampleIndex_;
        }

        [[nodiscard]] std::size_t numFrames() const noexcept
        {
            return numFrames_;
        }

        [[nodiscard]] std::size_t numChannels() const noexcept
        {
            return numChannels_;
        }

        /** The region's samples for one channel, numFrames() long. */
        [[nodiscard]] std::span<const float> channel(std::size_t index) const noexcept;

    private:
        friend class AudioRingBuffer;

        ReadRegion(const BlockTiming& block, std::uint64_t sampleIndex, std::size_t numFrames,
                   std::size_t numChannels, const float* firstSample,
                   std::size_t channelStride) noexcept;

        BlockTiming block_;
        std::uint64_t sampleIndex_;
        std::size_t numFrames_;
        std::size_t numChannels_;
        const float* firstSample_;
        std::size_t channelStride_;
    };

    /**
        Allocates room for `capacityFrames` frames of `numChannels` channels, and for the timing of
        up to `capacityBlocks` blocks. Throws std::invalid_argument if any argument is 0.
    */
    AudioRingBuffer(std::size_t numChannels, std::size_t capacityFrames,
                    std::size_t capacityBlocks);

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    [[nodiscard]] std::size_t numChannels() const noexcept
    {
        return numChannels_;
    }

    [[nodiscard]] std::size_t capacityFrames() const noexcept
    {
        return capacityFrames_;
    }

    [[nodiscard]] std::size_t capacityBlocks() const noexcept
    {
        return blocks_.capacity();
    }

    /**
        Producer only. Stores timing.numFrames frames from each channel, together with `timing`.

        `channels` must hold exactly numChannels() pointers. A null pointer stores silence for that
        channel. Returns false and counts an overrun if the ring lacks room for the whole block or
        for its timing. A block with 0 frames stores nothing and returns true.
    */
    bool push(std::span<const float* const> channels, const BlockTiming& timing) noexcept;

    /**
        Consumer only. The oldest unread frames, or std::nullopt if there are none.

        A region ends at the end of its block or where the storage wraps around, whichever comes
        first, so one block may take two regions. The region stays valid until those frames are
        consumed.
    */
    [[nodiscard]] std::optional<ReadRegion> peek() const noexcept;

    /**
        Consumer only. Marks the oldest `numFrames` unread frames as read and frees their space for
        the producer. The frames may span several blocks, but must not exceed the unread frames.
    */
    void consume(std::size_t numFrames) noexcept;

    /** Any thread. Blocks dropped by push() because the ring was full. */
    [[nodiscard]] std::uint64_t overrunCount() const noexcept
    {
        return overruns_.load(std::memory_order_relaxed);
    }

    /** Any thread. Frames in the blocks that push() dropped. */
    [[nodiscard]] std::uint64_t droppedFrameCount() const noexcept
    {
        return droppedFrames_.load(std::memory_order_relaxed);
    }

private:
    void countOverrun(std::uint32_t numFrames) noexcept;

    const std::size_t numChannels_;
    const std::size_t capacityFrames_;

    // Planar: channel c occupies [c * capacityFrames_, (c + 1) * capacityFrames_).
    std::vector<float> samples_;

    // Timing of the stored blocks, in push order. Their frames are stored back to back, so the
    // oldest block's unread frames always start at readFrame_. Pushing a timing publishes the
    // block's samples to the consumer.
    SpscQueue<BlockTiming> blocks_;

    // Producer side. writeFrame_ counts every frame ever stored; it never wraps.
    alignas(cacheLineSize) std::uint64_t writeFrame_ = 0;
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};

    // Consumer side. readFrame_ counts every frame ever consumed; the producer reads it to find
    // free space.
    alignas(cacheLineSize) std::atomic<std::uint64_t> readFrame_{0};
    std::uint32_t framesConsumedFromOldestBlock_ = 0;
};

} // namespace visona
