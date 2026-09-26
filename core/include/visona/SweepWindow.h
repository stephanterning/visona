#pragma once

#include <array>
#include <cstddef>

namespace visona
{

/** The windows the beat-synced sweep can show, in bars (D-020). Nothing else in the sweep assumes
    this range (D-058). */
inline constexpr std::array<double, 5> sweepWindowBars{0.25, 0.5, 1.0, 2.0, 4.0};

/** The window Visona starts with: 1 bar. */
inline constexpr std::size_t defaultSweepWindow = 2;

} // namespace visona
