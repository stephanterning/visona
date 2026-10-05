#pragma once

#include "ChoiceControl.h"

#include <cstddef>
#include <functional>

namespace visona
{

/**
    The window in bars as a select menu, WINDOW above `2 BARS`, with the choices ¼, ½, 1, 2 and 4
    bars (D-046, D-108). Like GainControl, it shows what setWindow() says and asks for changes
    through onWindowChange.
*/
class WindowControl final : public ChoiceControl
{
public:
    WindowControl();

    /** Called with the window the user picks, an index into sweepWindowBars. */
    std::function<void(std::size_t window)> onWindowChange;

    void setWindow(std::size_t window);

    /** A window for the status bar, such as "¼ BAR" or "2 BARS". */
    [[nodiscard]] static juce::String describe(std::size_t window);

private:
    void choose(int index) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowControl)
};

} // namespace visona
