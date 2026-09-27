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

/** The tempo range of the free-running sweep, in BPM, and its tempo on the first start (D-090). */
inline constexpr double minFreeBpm = 40.0;
inline constexpr double maxFreeBpm = 300.0;
inline constexpr double defaultFreeBpm = 120.0;

/** `bpm` rounded to 0.1 BPM and clamped to the free tempo range. Non-finite values give the
    default. */
[[nodiscard]] double clampFreeBpm(double bpm) noexcept;

} // namespace visona
