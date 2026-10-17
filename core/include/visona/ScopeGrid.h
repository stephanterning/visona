#pragma once

namespace visona
{

/**
    The finest note value the grid marks with `visibleBars` bars in view, as the denominator of
    the note: 4 for quarter notes, 8 for eighths, and so on down to 64 (D-087).

    Up to ¼ bar in view gives 64, up to ½ bar 32, up to 1 bar 16, up to 2 bars 8, and more than
    that 4. Each window shows its own value unzoomed, and zooming in makes the grid finer.
*/
[[nodiscard]] int gridDivisionFor(double visibleBars) noexcept;

/** How long one 1/`division` note lasts at `bpm` quarter notes per minute, in milliseconds, or 0
    if the tempo is not known. */
[[nodiscard]] double divisionMilliseconds(int division, double bpm) noexcept;

/** A faint amplitude reference line in input signal units. */
struct AmplitudeReference
{
    int levelDb = 0;
    float level = 1.0f;
};

/** Allocation-free sequence of visible 3 dB references, loudest first. */
struct AmplitudeReferences
{
    struct Iterator
    {
        int db;
        AmplitudeReference operator*() const noexcept;
        Iterator& operator++() noexcept
        {
            db -= 3;
            return *this;
        }
        bool operator!=(const Iterator& other) const noexcept
        {
            return db != other.db;
        }
    };
    int firstDb, endDb;
    Iterator begin() const noexcept
    {
        return {firstDb};
    }
    Iterator end() const noexcept
    {
        return {endDb};
    }
};

/** Continue in 3 dB steps down to one pixel from the lane centre. There is no
    fixed minimum dB level; the lower end follows gain and pixel resolution. */
[[nodiscard]] AmplitudeReferences amplitudeReferencesAt(int gainDb, int laneHeight = 512) noexcept;

} // namespace visona
