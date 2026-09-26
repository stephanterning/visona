#pragma once

#include <cstddef>

namespace visona::test
{

/**
    Counts the global operator new calls made on the calling thread since this object was created.

    The test executable replaces the global allocation functions to make this work. Catch2
    assertions allocate, so read count() before asserting anything.
*/
class AllocationCounter
{
public:
    AllocationCounter() noexcept;

    AllocationCounter(const AllocationCounter&) = delete;
    AllocationCounter& operator=(const AllocationCounter&) = delete;

    [[nodiscard]] std::size_t count() const noexcept;

private:
    std::size_t start_;
};

} // namespace visona::test
