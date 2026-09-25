#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

#if defined(__SANITIZE_THREAD__)
#define VISONA_TEST_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define VISONA_TEST_TSAN 1
#endif
#endif

#ifdef VISONA_TEST_TSAN
#include <sanitizer/allocator_interface.h>
#endif

namespace
{

// Constant-initialized, so updating it from inside an allocation never allocates.
thread_local std::size_t allocationsOnThisThread = 0;

} // namespace

namespace visona::test
{

AllocationCounter::AllocationCounter() noexcept
    : start_(allocationsOnThisThread)
{
}

std::size_t AllocationCounter::count() const noexcept
{
    return allocationsOnThisThread - start_;
}

} // namespace visona::test

#ifdef VISONA_TEST_TSAN

// TSan's runtime always links its own global operator new, so it cannot be replaced. Its allocator
// hooks see every heap allocation instead, including those made by operator new.

namespace
{

void countAllocation(const volatile void*, std::size_t) noexcept
{
    ++allocationsOnThisThread;
}

void ignoreDeallocation(const volatile void*) noexcept {}

[[maybe_unused]] const int allocationHooksInstalled =
    __sanitizer_install_malloc_and_free_hooks(&countAllocation, &ignoreDeallocation);

} // namespace

#else

namespace
{

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

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
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

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

#endif
