#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>

namespace visona
{

/**
    WINDOW as an always-visible segmented control, `WINDOW [¼][½][1][2][4]`, in bars (D-046). Each
    segment is a touch-sized target, and the selected one stands out. Like GainControl, it shows
    what setWindow() says and asks for changes through onWindowChange.
*/
class WindowControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WindowControl();

    /** Called with the window the user picks, an index into sweepWindowBars. */
    std::function<void(std::size_t window)> onWindowChange;

    void setWindow(std::size_t window);
    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** The width that fits the control at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    /** A window for the status bar, such as "¼ BAR" or "2 BARS". */
    [[nodiscard]] static juce::String describe(std::size_t window);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Rectangle<int> segmentBounds(std::size_t segment) const;
    [[nodiscard]] int labelWidth() const;

    std::size_t window_;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;
    juce::Rectangle<int> segments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowControl)
};

} // namespace visona
