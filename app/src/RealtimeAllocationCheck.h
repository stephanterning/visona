#pragma once

#include <cstdint>

namespace visona
{

/**
    Debug-build instrumentation that counts heap allocations made in the audio callback.

    Debug builds define VISONA_REALTIME_ALLOCATION_CHECK and replace the global operator new. While
    a Scope is alive on a thread, every operator new call on that thread is counted. That catches
    C++ allocations, such as containers, std::function and juce::String, but not code that calls
    malloc() directly. In other builds a Scope does nothing and nothing is counted.
*/
class RealtimeAllocationCheck
{
public:
#ifdef VISONA_REALTIME_ALLOCATION_CHECK
    static constexpr bool enabled = true;
#else
    static constexpr bool enabled = false;
#endif

    /** Counts the allocations the calling thread makes while this object exists. Wait-free. */
    class Scope
    {
    public:
#ifdef VISONA_REALTIME_ALLOCATION_CHECK
        Scope() noexcept;
        ~Scope();
#else
        Scope() noexcept = default;
#endif

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
#ifdef VISONA_REALTIME_ALLOCATION_CHECK
        std::uint64_t allocationsBefore_;
#endif
    };

    /** Any thread. Allocations counted in all scopes so far. */
    [[nodiscard]] static std::uint64_t allocationCount() noexcept;

    /** Checks that allocations on the calling thread really are seen. False if not enabled. */
    [[nodiscard]] static bool selfTest();
};

} // namespace visona
