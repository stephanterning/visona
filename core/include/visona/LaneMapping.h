#pragma once

namespace visona
{

/** Display gain in whole decibels, from 0 to +18 dB, set by hand or by AutoGain. It is
    presentation only: it never changes audio, analysis or buffered data (D-024, D-046, D-098). */
struct DisplayGain
{
    static constexpr int minDb = 0;
    static constexpr int maxDb = 18;

    [[nodiscard]] static constexpr int clampDb(int db) noexcept
    {
        return db < minDb ? minDb : (db > maxDb ? maxDb : db);
    }

    /** The linear factor for `db`. */
    [[nodiscard]] static float toLinear(int db) noexcept;
};

/** The pixel rows a span covers in a lane, top to bottom inclusive. */
struct LaneRows
{
    int top = 0;
    int bottom = 0;
    /** Whether display gain pushed the span past the lane's top or bottom edge. */
    bool clippedTop = false;
    bool clippedBottom = false;

    friend bool operator==(const LaneRows&, const LaneRows&) = default;
};

/**
    The vertical mapping of one lane, occupying pixel rows [top, top + height).

    A sample value v lies at y = centre - v * gain * height / 2, with the centre in the middle of
   the lane (D-024). Rows outside the lane are clamped to its edges and reported as clipped, so the
    renderer can mark display overshoot neutrally instead of mistaking it for audio clipping.
*/
class LaneMapping
{
public:
    /** `height` must be at least 1. `gain` is linear. */
    LaneMapping(int top, int height, float gain) noexcept;

    [[nodiscard]] int top() const noexcept
    {
        return top_;
    }

    [[nodiscard]] int height() const noexcept
    {
        return height_;
    }

    /** The row of value `value`, clamped to the lane. */
    [[nodiscard]] int rowOf(float value) const noexcept;

    /** Whether `value` lands inside the lane or exactly on its edge after gain. */
    [[nodiscard]] bool isInside(float value) const noexcept;

    /** The rows between `min` and `max`, at least one row. */
    [[nodiscard]] LaneRows rowsOf(float min, float max) const noexcept;

private:
    int top_;
    int height_;
    float gain_;
};

} // namespace visona
