#pragma once

namespace visona
{

/** The deepest zoom: 1/32 of the window (D-085). */
inline constexpr double maxSweepZoom = 32.0;

/**
    The part of the sweep window a zoomed scope shows (D-085), from `offset` for `span`, both as
    fractions of the window. It changes only what is drawn, never the window or the analysis.

    The offset is in [0, 1) and the span in [1 / maxSweepZoom, 1]. A view may run past the end of
    the window and carry on at its start, as the head does. The whole window is {0, 1}, and a span
    of 1 always has an offset of 0.
*/
struct SweepZoom
{
    double offset = 0.0;
    double span = 1.0;

    [[nodiscard]] bool isZoomed() const noexcept
    {
        return span < 1.0;
    }

    /** How many times the view is magnified: 1 / span. */
    [[nodiscard]] double factor() const noexcept
    {
        return 1.0 / span;
    }

    /**
        Zooms in by `factor`, or out if it is below 1, keeping the point `anchor` of the way across
        the view where it is. Zooming out also turns the view towards the whole window, in step
        with the span, so that it arrives there exactly when the span reaches 1.
    */
    [[nodiscard]] SweepZoom zoomedAround(double anchor, double factor) const noexcept;

    /** The part of this view from `from` to `to`, as fractions of the view, in either order. A
        part narrower than the deepest zoom is widened around its centre. */
    [[nodiscard]] SweepZoom selected(double from, double to) const noexcept;

    /** Where `fraction` of the way across the view is in the window, in [0, 1). */
    [[nodiscard]] double windowPositionOf(double fraction) const noexcept;

    /** `zoom` with its offset wrapped into the window and its span clamped. Non-finite values
        give the whole window. */
    [[nodiscard]] static SweepZoom normalized(SweepZoom zoom) noexcept;
};

} // namespace visona
