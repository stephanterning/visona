#pragma once

#include <optional>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace visona
{

/** CPU time the whole process has used, user plus system, in seconds, if the platform says. */
inline std::optional<double> processCpuSeconds() noexcept
{
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return std::nullopt;
    const auto seconds = [](const timeval& time)
    { return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) * 1.0e-6; };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
#else
    return std::nullopt;
#endif
}

} // namespace visona
