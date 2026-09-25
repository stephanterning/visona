#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <thread>

using visona::test::AllocationCounter;

namespace
{

// Storing an allocation here keeps the compiler from optimizing the allocation away.
void* volatile escapedAllocation = nullptr;

} // namespace

TEST_CASE("AllocationCounter counts allocations on its own thread", "[realtime]")
{
    const AllocationCounter allocations;
    const auto memory = std::make_unique<std::array<char, 64>>();
    escapedAllocation = memory.get();
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 1);
}

TEST_CASE("AllocationCounter ignores allocations on other threads", "[realtime]")
{
    std::atomic<bool> counting{false};
    std::thread other(
        [&]
        {
            while (!counting.load(std::memory_order_acquire))
                std::this_thread::yield();
            const auto memory = std::make_unique<std::array<char, 64>>();
            escapedAllocation = memory.get();
        });

    const AllocationCounter allocations;
    counting.store(true, std::memory_order_release);
    other.join();
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}
