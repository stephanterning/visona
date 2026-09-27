#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace visona
{

/**
    The bands of the sweep (architecture.md 3.4). `full` is the unfiltered signal and always defines
    the waveform's shape. `low`, `mid` and `high` come from the BandSplitter and drive the frequency
    colouring only (D-056).
*/
enum class Band : std::uint8_t
{
    full,
    low,
    mid,
    high
};

inline constexpr std::size_t bandCount = 4;

/** The bands the BandSplitter produces, lowest first. */
inline constexpr std::array<Band, 3> splitBands{Band::low, Band::mid, Band::high};

/** The index of a split band in arrays that hold only the split bands: low 0, mid 1, high 2. */
[[nodiscard]] constexpr std::size_t splitIndex(Band band) noexcept
{
    return static_cast<std::size_t>(band) - 1;
}

} // namespace visona
