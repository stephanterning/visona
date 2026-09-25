#pragma once

#include <visona/CacheLine.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace visona
{

/**
    A lock-free triple buffer that hands the latest value from one producer thread to one consumer
    thread. Used for analysis snapshots: the analysis thread publishes, the UI thread reads.

    It holds three values of T, all constructed in the constructor. The producer fills its write
    buffer in place and publishes it; the consumer fetches the most recently published value and
    reads it in place. Publishing never waits for the consumer, and a value the consumer has not
    fetched is replaced by the next one (latest wins). Neither side ever sees the other side's
    buffer, and publish() and fetch() never allocate, lock or wait.

    After publish(), the write buffer holds an older value, not the one just published. The
    producer must overwrite everything it relies on before it publishes again.
*/
template <typename T>
class TripleBuffer
{
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free);

public:
    /** All three buffers are value-initialized. */
    TripleBuffer() = default;

    /** All three buffers are copies of `initial`, for example to allocate a snapshot's storage
        up front. */
    explicit TripleBuffer(const T& initial)
        : buffers_{initial, initial, initial}
    {
    }

    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    /** Producer only. The buffer to fill before the next publish(). */
    [[nodiscard]] T& writeBuffer() noexcept
    {
        return buffers_[writeIndex_];
    }

    /** Producer only. Makes the write buffer the latest value and hands the producer a new write
        buffer. */
    void publish() noexcept
    {
        const auto previous = shared_.exchange(static_cast<std::uint8_t>(writeIndex_ | freshBit),
                                               std::memory_order_acq_rel);
        writeIndex_ = static_cast<std::uint8_t>(previous & indexMask);
    }

    /** Consumer only. Replaces the read buffer with the latest published value, if there is one
        the consumer has not fetched yet. Returns true if the read buffer changed. */
    bool fetch() noexcept
    {
        // Only the producer sets the fresh bit and only this function clears it, so it cannot
        // disappear between this check and the exchange.
        if ((shared_.load(std::memory_order_relaxed) & freshBit) == 0)
            return false;
        const auto previous = shared_.exchange(readIndex_, std::memory_order_acq_rel);
        readIndex_ = static_cast<std::uint8_t>(previous & indexMask);
        return true;
    }

    /** Consumer only. The most recently fetched value, or the initial value before the first
        fetch. */
    [[nodiscard]] const T& readBuffer() const noexcept
    {
        return buffers_[readIndex_];
    }

private:
    static constexpr std::uint8_t indexMask = 0b011;
    static constexpr std::uint8_t freshBit = 0b100;

    std::array<T, 3> buffers_{};

    // Each index names one of the three buffers. The shared index names the buffer that is neither
    // the producer's nor the consumer's; its fresh bit is set while it holds an unfetched value.
    alignas(cacheLineSize) std::uint8_t writeIndex_ = 0;
    alignas(cacheLineSize) std::atomic<std::uint8_t> shared_{1};
    alignas(cacheLineSize) std::uint8_t readIndex_ = 2;
};

} // namespace visona
