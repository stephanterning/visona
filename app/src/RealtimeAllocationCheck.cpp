#include "RealtimeAllocationCheck.h"

#ifdef VISONA_REALTIME_ALLOCATION_CHECK

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace
{

// Constant-initialized, so updating it from inside an allocation never allocates.
thread_local std::uint64_t allocationsOnThisThread = 0;

std::atomic<std::uint64_t> allocationsInScopes{0};

// Storing an allocation here keeps the compiler from optimizing the self-test allocation away.
void* volatile escapedAllocation = nullptr;

constexpr auto defaultAlignment = alignof(std::max_align_t);

void* tryAllocate(std::size_t size, std::size_t alignment) noexcept
{
    ++allocationsOnThisThread;
    if (size == 0)
        size = 1;
    if (alignment <= defaultAlignment)
        return std::malloc(size);
    // aligned_alloc requires the size to be a multiple of the alignment.
    return std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

void* allocate(std::size_t size, std::size_t alignment)
{
    if (void* const memory = tryAllocate(size, alignment))
        return memory;
    throw std::bad_alloc();
}

} // namespace

namespace visona
{

RealtimeAllocationCheck::Scope::Scope() noexcept
    : allocationsBefore_(allocationsOnThisThread)
{
}

RealtimeAllocationCheck::Scope::~Scope()
{
    if (const auto allocations = allocationsOnThisThread - allocationsBefore_; allocations > 0)
        allocationsInScopes.fetch_add(allocations, std::memory_order_relaxed);
}

std::uint64_t RealtimeAllocationCheck::allocationCount() noexcept
{
    return allocationsInScopes.load(std::memory_order_relaxed);
}

bool RealtimeAllocationCheck::selfTest()
{
    const auto before = allocationsOnThisThread;
    auto* const memory = new int(0);
    escapedAllocation = memory;
    const auto counted = allocationsOnThisThread - before;
    delete memory;
    return counted == 1;
}

} // namespace visona

// Replacements for every replaceable global allocation and deallocation function, so that no
// allocation path bypasses the counter.

void* operator new(std::size_t size)
{
    return allocate(size, defaultAlignment);
}

void* operator new[](std::size_t size)
{
    return allocate(size, defaultAlignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return tryAllocate(size, defaultAlignment);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return tryAllocate(size, defaultAlignment);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return tryAllocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return tryAllocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

// Compilers only call the sized forms when sized deallocation is enabled.
#ifdef __cpp_sized_deallocation

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

#endif

#else

namespace visona
{

std::uint64_t RealtimeAllocationCheck::allocationCount() noexcept
{
    return 0;
}

bool RealtimeAllocationCheck::selfTest()
{
    return false;
}

} // namespace visona

#endif
