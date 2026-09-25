#pragma once

#include <cstddef>

namespace visona
{

/**
    Alignment that keeps data written by different threads on separate cache lines.

    128 bytes covers Apple Silicon, whose cache lines are 128 bytes, and x86-64, which prefetches
    64-byte lines in pairs. std::hardware_destructive_interference_size is not used because GCC
    warns that its value depends on compiler flags, which makes it unsafe in a header.
*/
inline constexpr std::size_t cacheLineSize = 128;

} // namespace visona
