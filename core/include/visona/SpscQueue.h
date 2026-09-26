#pragma once

#include <visona/CacheLine.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace visona
{

/**
    A bounded, lock-free, single-producer single-consumer FIFO queue.

    Exactly one thread pushes and exactly one other thread pops. All slots are allocated in the
    constructor. tryPush(), tryPop() and peek() never allocate, lock or wait, so both sides can run
    on real-time threads. When the queue is full, tryPush() drops the new element and counts an
    overrun; it never overwrites an element the consumer has not read.
*/
template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "copying an element must not allocate or throw");
    static_assert(std::is_default_constructible_v<T>);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

public:
    /** Allocates room for `capacity` elements. Throws std::invalid_argument if it is 0. */
    explicit SpscQueue(std::size_t capacity)
        : slots_(capacity)
        , capacity_(capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument("SpscQueue capacity must be at least 1");
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return slots_.size();
    }

    /** Producer only. Appends a copy of `value`. If the queue is full, drops it, counts an overrun
        and returns false. */
    bool tryPush(const T& value) noexcept
    {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (write - read == capacity_)
        {
            overruns_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[slotOf(write)] = value;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    /** Consumer only. Removes and returns the oldest element, or std::nullopt if the queue is
        empty. */
    [[nodiscard]] std::optional<T> tryPop() noexcept
    {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (read == write)
            return std::nullopt;
        const T value = slots_[slotOf(read)];
        readIndex_.store(read + 1, std::memory_order_release);
        return value;
    }

    /** Consumer only. The oldest element without removing it, or nullptr if the queue is empty.
        The pointer stays valid until the consumer pops that element. */
    [[nodiscard]] const T* peek() const noexcept
    {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        return read == write ? nullptr : &slots_[slotOf(read)];
    }

    /** Any thread. Elements dropped by tryPush() because the queue was full. */
    [[nodiscard]] std::uint64_t overrunCount() const noexcept
    {
        return overruns_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] std::size_t slotOf(std::uint64_t index) const noexcept
    {
        return static_cast<std::size_t>(index % capacity_);
    }

    std::vector<T> slots_;
    const std::uint64_t capacity_;

    // Both indices count every element ever pushed or popped. At one element per nanosecond,
    // 64 bits last for more than 500 years, so they never wrap.
    alignas(cacheLineSize) std::atomic<std::uint64_t> writeIndex_{0};
    alignas(cacheLineSize) std::atomic<std::uint64_t> readIndex_{0};
    alignas(cacheLineSize) std::atomic<std::uint64_t> overruns_{0};
};

} // namespace visona
