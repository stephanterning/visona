#pragma once

#include <string_view>

namespace visona
{

/** The project version as "major.minor.patch", taken from the top-level CMake project. */
[[nodiscard]] std::string_view versionString() noexcept;

} // namespace visona
