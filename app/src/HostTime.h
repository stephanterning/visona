#pragma once

#include <chrono>
#include <cstdint>

#if defined(__APPLE__)
#include <time.h>
#endif

namespace visona
{

/**
    The host's monotonic clock in nanoseconds. BlockTiming::hostTimeNs falls back to it when the
    audio device supplies no timestamp.

    It must share a time base with the device timestamps. CoreAudio host time is
    mach_absolute_time(), which stops while the Mac sleeps; std::chrono::steady_clock keeps counting
    during sleep on macOS, so CLOCK_UPTIME_RAW is read there instead.
*/
inline std::uint64_t monotonicHostTimeNs() noexcept
{
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count());
#endif
}

} // namespace visona
