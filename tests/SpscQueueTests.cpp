#include "support/AllocationCounter.h"

#include <visona/MidiClockEvent.h>
#include <visona/SpscQueue.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>

using visona::MidiClockEvent;
using visona::MidiClockQueue;
using visona::SpscQueue;
using visona::test::AllocationCounter;

namespace
{

MidiClockEvent clockAt(std::uint64_t hostTimeNs)
{
    return {MidiClockEvent::Type::Clock, 0, hostTimeNs};
}

} // namespace

TEST_CASE("SpscQueue rejects a capacity of 0", "[spsc-queue]")
{
    CHECK_THROWS_AS(SpscQueue<int>(0), std::invalid_argument);
}

TEST_CASE("SpscQueue is first in first out and drops pushes when full", "[spsc-queue]")
{
    SpscQueue<int> queue(3);
    CHECK(queue.capacity() == 3);
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.peek() == nullptr);

    CHECK(queue.tryPush(1));
    CHECK(queue.tryPush(2));
    CHECK(queue.tryPush(3));
    CHECK_FALSE(queue.tryPush(4));
    CHECK(queue.overrunCount() == 1);

    REQUIRE(queue.peek() != nullptr);
    CHECK(*queue.peek() == 1);
    CHECK(queue.tryPop() == 1);
    CHECK(queue.tryPush(5));
    CHECK(queue.tryPop() == 2);
    CHECK(queue.tryPop() == 3);
    CHECK(queue.tryPop() == 5);
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.peek() == nullptr);
    CHECK(queue.overrunCount() == 1);
}

TEST_CASE("SpscQueue keeps order while its indices wrap around the storage", "[spsc-queue]")
{
    SpscQueue<std::uint64_t> queue(5);
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    for (std::uint64_t round = 0; round < 100; ++round)
    {
        const auto count = 1 + round % queue.capacity();
        for (std::uint64_t i = 0; i < count; ++i)
            REQUIRE(queue.tryPush(pushed++));
        for (std::uint64_t i = 0; i < count; ++i)
            REQUIRE(queue.tryPop() == popped++);
    }
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.overrunCount() == 0);
}

TEST_CASE("MidiClockQueue carries every MidiClockEvent field unchanged", "[spsc-queue][midi]")
{
    using Type = MidiClockEvent::Type;
    const std::array events{
        MidiClockEvent{Type::Start, 0, 1'000},
        clockAt(2'000),
        MidiClockEvent{Type::Stop, 0, 3'000},
        MidiClockEvent{Type::SongPositionPointer, MidiClockEvent::maxSppValue, 4'000},
        MidiClockEvent{Type::Continue, 0, std::numeric_limits<std::uint64_t>::max()},
    };

    MidiClockQueue queue(events.size());
    for (const auto& event : events)
        REQUIRE(queue.tryPush(event));
    for (const auto& event : events)
        CHECK(queue.tryPop() == event);
}

TEST_CASE("SpscQueue does not allocate after construction", "[spsc-queue][realtime]")
{
    MidiClockQueue queue(4);
    std::uint64_t popped = 0;

    const AllocationCounter allocations;
    for (std::uint64_t i = 0; i < 100; ++i)
    {
        static_cast<void>(queue.tryPush(clockAt(i)));
        static_cast<void>(queue.tryPush(clockAt(i)));
        if (queue.peek() != nullptr && queue.tryPop().has_value())
            ++popped;
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(popped == 100);
}

TEST_CASE("SpscQueue hot paths are noexcept", "[spsc-queue][realtime]")
{
    MidiClockQueue queue(1);
    const MidiClockEvent event;
    STATIC_REQUIRE(noexcept(queue.tryPush(event)));
    STATIC_REQUIRE(noexcept(queue.tryPop()));
    STATIC_REQUIRE(noexcept(queue.peek()));
    STATIC_REQUIRE(noexcept(queue.overrunCount()));
}

TEST_CASE("SpscQueue delivers every event in order across threads", "[spsc-queue][stress]")
{
    constexpr std::uint64_t eventCount = 1'000'000;
    MidiClockQueue queue(16);

    std::uint64_t failedPushes = 0;
    std::size_t producerAllocations = 0;
    std::thread producer(
        [&]
        {
            const AllocationCounter allocations;
            for (std::uint64_t i = 0; i < eventCount; ++i)
            {
                while (!queue.tryPush(clockAt(i)))
                {
                    ++failedPushes;
                    std::this_thread::yield();
                }
            }
            producerAllocations = allocations.count();
        });

    std::uint64_t received = 0;
    std::uint64_t outOfOrder = 0;
    const AllocationCounter allocations;
    while (received < eventCount)
    {
        if (const auto event = queue.tryPop())
        {
            if (event->hostTimeNs != received)
                ++outOfOrder;
            ++received;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    const auto consumerAllocations = allocations.count();
    producer.join();

    CHECK(outOfOrder == 0);
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.overrunCount() == failedPushes);
    CHECK(producerAllocations == 0);
    CHECK(consumerAllocations == 0);
}

TEST_CASE("SpscQueue accounts for every event it drops across threads", "[spsc-queue][stress]")
{
    constexpr std::uint64_t eventCount = 1'000'000;
    MidiClockQueue queue(4);

    // Like the MIDI thread, the producer never waits: a push that fails is dropped.
    std::atomic<bool> producerDone{false};
    std::thread producer(
        [&]
        {
            for (std::uint64_t i = 0; i < eventCount; ++i)
                static_cast<void>(queue.tryPush(clockAt(i)));
            producerDone.store(true, std::memory_order_release);
        });

    std::uint64_t received = 0;
    std::uint64_t outOfOrder = 0;
    std::optional<std::uint64_t> previous;
    for (;;)
    {
        const bool done = producerDone.load(std::memory_order_acquire);
        if (const auto event = queue.tryPop())
        {
            if (previous && event->hostTimeNs <= *previous)
                ++outOfOrder;
            previous = event->hostTimeNs;
            ++received;
        }
        else if (done)
        {
            break;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();

    CHECK(outOfOrder == 0);
    CHECK(received + queue.overrunCount() == eventCount);
}
