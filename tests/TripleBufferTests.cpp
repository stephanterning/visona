#include "support/AllocationCounter.h"

#include <visona/TripleBuffer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

using visona::TripleBuffer;
using visona::test::AllocationCounter;

TEST_CASE("TripleBuffer starts with its initial value and nothing to fetch", "[triple-buffer]")
{
    TripleBuffer<int> buffer(7);
    CHECK(buffer.readBuffer() == 7);
    CHECK(buffer.writeBuffer() == 7);
    CHECK_FALSE(buffer.fetch());
    CHECK(buffer.readBuffer() == 7);

    TripleBuffer<int> valueInitialized;
    CHECK(valueInitialized.readBuffer() == 0);
}

TEST_CASE("TripleBuffer hands each published value to the consumer once", "[triple-buffer]")
{
    TripleBuffer<int> buffer;
    buffer.writeBuffer() = 1;
    buffer.publish();

    CHECK(buffer.fetch());
    CHECK(buffer.readBuffer() == 1);
    CHECK_FALSE(buffer.fetch());
    CHECK(buffer.readBuffer() == 1);
}

TEST_CASE("TripleBuffer gives the consumer only the latest published value", "[triple-buffer]")
{
    TripleBuffer<int> buffer;
    for (int value = 1; value <= 3; ++value)
    {
        buffer.writeBuffer() = value;
        buffer.publish();
    }

    CHECK(buffer.fetch());
    CHECK(buffer.readBuffer() == 3);
    CHECK_FALSE(buffer.fetch());
}

TEST_CASE("TripleBuffer never lets the producer and consumer share a buffer", "[triple-buffer]")
{
    TripleBuffer<int> buffer;
    for (int step = 0; step < 50; ++step)
    {
        const int* const published = &buffer.writeBuffer();
        buffer.publish();
        CHECK(&buffer.writeBuffer() != published);
        CHECK(&buffer.writeBuffer() != &buffer.readBuffer());

        // Fetch after every other publish, so both the fetched and the skipped paths run.
        if (step % 2 == 0)
        {
            CHECK(buffer.fetch());
            CHECK(&buffer.readBuffer() == published);
        }
        CHECK(&buffer.writeBuffer() != &buffer.readBuffer());
    }
}

TEST_CASE("TripleBuffer copies its initial value into every buffer", "[triple-buffer]")
{
    TripleBuffer<std::vector<float>> buffer(std::vector<float>(4'096, 0.5f));
    for (int step = 0; step < 3; ++step)
    {
        CHECK(buffer.writeBuffer().size() == 4'096);
        buffer.publish();
    }
    CHECK(buffer.readBuffer().size() == 4'096);
    CHECK(buffer.fetch());
    CHECK(buffer.readBuffer().size() == 4'096);
}

TEST_CASE("TripleBuffer does not allocate after construction", "[triple-buffer][realtime]")
{
    TripleBuffer<std::vector<float>> buffer(std::vector<float>(4'096));
    std::size_t fetches = 0;
    float lastValue = -1.0f;

    const AllocationCounter allocations;
    for (int step = 0; step < 100; ++step)
    {
        auto& snapshot = buffer.writeBuffer();
        std::fill(snapshot.begin(), snapshot.end(), static_cast<float>(step));
        buffer.publish();
        if (buffer.fetch())
        {
            ++fetches;
            lastValue = buffer.readBuffer().back();
        }
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(fetches == 100);
    CHECK(lastValue == 99.0f);
}

TEST_CASE("TripleBuffer hot paths are noexcept", "[triple-buffer][realtime]")
{
    TripleBuffer<std::vector<float>> buffer;
    STATIC_REQUIRE(noexcept(buffer.writeBuffer()));
    STATIC_REQUIRE(noexcept(buffer.publish()));
    STATIC_REQUIRE(noexcept(buffer.fetch()));
    STATIC_REQUIRE(noexcept(buffer.readBuffer()));
}

namespace
{

/** A snapshot whose payload is derived from its sequence number, so a torn read is detectable. */
struct Snapshot
{
    std::uint64_t sequence = 0;
    std::array<std::uint64_t, 64> payload{};

    void fill(std::uint64_t newSequence) noexcept
    {
        sequence = newSequence;
        std::iota(payload.begin(), payload.end(), newSequence);
    }

    [[nodiscard]] bool isConsistent() const noexcept
    {
        for (std::size_t i = 0; i < payload.size(); ++i)
            if (payload[i] != sequence + i)
                return false;
        return true;
    }
};

} // namespace

TEST_CASE("TripleBuffer delivers whole snapshots in publish order across threads",
          "[triple-buffer][stress]")
{
    constexpr std::uint64_t snapshotCount = 1'000'000;
    TripleBuffer<Snapshot> buffer;

    std::size_t producerAllocations = 0;
    std::thread producer(
        [&]
        {
            const AllocationCounter allocations;
            for (std::uint64_t sequence = 1; sequence <= snapshotCount; ++sequence)
            {
                buffer.writeBuffer().fill(sequence);
                buffer.publish();
            }
            producerAllocations = allocations.count();
        });

    std::uint64_t lastSequence = 0;
    std::size_t fetches = 0;
    std::size_t outOfOrder = 0;
    std::size_t torn = 0;
    const AllocationCounter allocations;
    while (lastSequence < snapshotCount)
    {
        if (!buffer.fetch())
        {
            std::this_thread::yield();
            continue;
        }
        ++fetches;
        const auto& snapshot = buffer.readBuffer();
        if (snapshot.sequence <= lastSequence)
            ++outOfOrder;
        // Read the snapshot twice: the producer must not touch it while the consumer holds it.
        for (int pass = 0; pass < 2; ++pass)
            if (!snapshot.isConsistent())
                ++torn;
        lastSequence = snapshot.sequence;
    }
    const auto consumerAllocations = allocations.count();
    producer.join();

    CHECK(lastSequence == snapshotCount);
    CHECK(fetches > 0);
    CHECK(outOfOrder == 0);
    CHECK(torn == 0);
    CHECK_FALSE(buffer.fetch());
    CHECK(producerAllocations == 0);
    CHECK(consumerAllocations == 0);
}
