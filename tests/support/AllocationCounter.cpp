#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

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

void* tryAllocate(std::size_t size) noexcept
{
    ++allocationsOnThisThread;
    return std::malloc(size == 0 ? 1 : size);
}

void* tryAllocateAligned(std::size_t size, std::size_t alignment) noexcept
{
    ++allocationsOnThisThread;
    if (size == 0)
        size = 1;
#ifdef _WIN32
    // MSVC has no aligned_alloc; memory from _aligned_malloc must be freed with _aligned_free.
    return _aligned_malloc(size, alignment);
#else
    if (alignment <= alignof(std::max_align_t))
        return std::malloc(size);
    // aligned_alloc requires the size to be a multiple of the alignment.
    return std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
}

void freeAligned(void* memory) noexcept
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void* orThrow(void* memory)
{
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}

} // namespace

// Replacements for every replaceable global allocation and deallocation function, so that no
// allocation path bypasses the counter.

void* operator new(std::size_t size)
{
    return orThrow(tryAllocate(size));
}

void* operator new[](std::size_t size)
{
    return orThrow(tryAllocate(size));
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return tryAllocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return tryAllocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return orThrow(tryAllocateAligned(size, static_cast<std::size_t>(alignment)));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return orThrow(tryAllocateAligned(size, static_cast<std::size_t>(alignment)));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return tryAllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return tryAllocateAligned(size, static_cast<std::size_t>(alignment));
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
    freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    freeAligned(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    freeAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    freeAligned(memory);
}

#endif
