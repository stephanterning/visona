#pragma once

#include "ChromeLayout.h"

#include <visona/SweepSnapshot.h>
#include <visona/SweepZoom.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace visona
{

/**
    The zoom's overview strip, above the scope while it is zoomed in (D-085): the whole window as a
    track with its beats, the part in view highlighted (in two pieces when the view runs past the
    end of the window), the head as a thin accent line, and a button that resets the zoom.

    It repaints only what changes: the head's pixel, or everything on a new zoom or window.
*/
class ZoomOverview final : public juce::Component, public juce::TooltipClient
{
public:
    ZoomOverview();

    void setZoom(SweepZoom zoom);

    /** The head's position as a fraction of the window; negative hides it. */
    void setHead(double position);

    /** Takes the window's beats from `snapshot`. */
    void setTimeline(const SweepSnapshot& snapshot);

    void setStep(ChromeStep step);

    [[nodiscard]] int preferredHeight() const noexcept;

    /** The status bar's zoom text, such as "ZOOM 4.0× · 1.3–1.4": the magnification, then where
        the view starts and ends in the window, as bar.beat, bar.beat.sixteenth, or seconds while
        the sweep runs free. */
    [[nodiscard]] static juce::String describe(SweepZoom zoom, const SweepSnapshot& snapshot);

    /** Called when the reset button is pressed or the strip is double-clicked. */
    std::function<void()> onReset;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    juce::String getTooltip() override;

private:
    struct Mark
    {
        double position = 0.0;
        bool strong = false;

        friend bool operator==(const Mark& a, const Mark& b)
        {
            return juce::exactlyEqual(a.position, b.position) && a.strong == b.strong;
        }
    };

    [[nodiscard]] float xOf(double position) const noexcept;
    [[nodiscard]] juce::Rectangle<int> headArea(float x) const noexcept;
    void setResetHighlighted(bool highlighted);

    SweepZoom zoom_;
    double head_ = -1.0;
    std::vector<Mark> marks_;
    ChromeStep step_ = ChromeStep::wide;
    juce::Rectangle<float> track_;
    juce::Rectangle<int> resetButton_;
    bool resetHighlighted_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomOverview)
};

} // namespace visona
