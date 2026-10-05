#include "WindowControl.h"

#include <visona/SweepWindow.h>

#include <algorithm>
#include <array>

namespace visona
{

namespace
{

/** The windows' numbers, in UTF-8. */
constexpr std::array<const char*, sweepWindowBars.size()> windowNumbers{"\xc2\xbc", "\xc2\xbd", "1",
                                                                        "2", "4"};

juce::StringArray windowChoices()
{
    juce::StringArray choices;
    for (std::size_t window = 0; window < sweepWindowBars.size(); ++window)
        choices.add(WindowControl::describe(window));
    return choices;
}

} // namespace

WindowControl::WindowControl()
    : ChoiceControl("WINDOW", windowChoices())
{
    setTitle("Window");
    setTooltip("Window in bars (keys 1 to 5)");
    setSelected(static_cast<int>(defaultSweepWindow));
}

void WindowControl::setWindow(std::size_t window)
{
    setSelected(static_cast<int>(std::min(window, sweepWindowBars.size() - 1)));
}

juce::String WindowControl::describe(std::size_t window)
{
    window = std::min(window, sweepWindowBars.size() - 1);
    return juce::String::fromUTF8(windowNumbers[window]) +
           (sweepWindowBars[window] > 1.0 ? " BARS" : " BAR");
}

void WindowControl::choose(int index)
{
    if (onWindowChange)
        onWindowChange(static_cast<std::size_t>(index));
}

} // namespace visona
