#pragma once

namespace visona
{

/**
    The steps the chrome reflows in (architecture.md 3.6, D-069). Breakpoints are logical sizes of
    the main component's bounds.

    - wide: every control on one row, at full size.
    - narrow: smaller text and tighter gaps.
    - compact: secondary controls move into the settings panel, and the bars get slimmer.

    At every step the control bar keeps its controls on one row as long as they fit, dropping the
    buttons' labels before anything else (D-108).

    The status bar also drops values from its end, down to the state, whenever they do not fit.
*/
enum class ChromeStep
{
    wide,
    narrow,
    compact
};

struct ChromeBreakpoints
{
    static constexpr int narrowBelowWidth = 760;
    static constexpr int compactBelowWidth = 480;
    static constexpr int compactBelowHeight = 360;
};

[[nodiscard]] constexpr ChromeStep chromeStepFor(int width, int height) noexcept
{
    if (width < ChromeBreakpoints::compactBelowWidth ||
        height < ChromeBreakpoints::compactBelowHeight)
        return ChromeStep::compact;
    if (width < ChromeBreakpoints::narrowBelowWidth)
        return ChromeStep::narrow;
    return ChromeStep::wide;
}

/** Sizes of the chrome at one step, in logical pixels. */
struct ChromeMetrics
{
    int statusHeight;
    int controlHeight;
    int padding;
    int gap;
    /** Between the groups of the control bar, such as the gain and the waveform. */
    int groupGap;
    float fontHeight;

    [[nodiscard]] static constexpr ChromeMetrics forStep(ChromeStep step) noexcept
    {
        switch (step)
        {
        case ChromeStep::wide:
            return {30, 40, 8, 10, 20, 14.0f};
        case ChromeStep::narrow:
            return {28, 40, 8, 8, 10, 13.0f};
        case ChromeStep::compact:
            break;
        }
        return {24, 36, 6, 6, 8, 12.0f};
    }
};

} // namespace visona
