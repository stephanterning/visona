#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/**
    A square button in the chrome that shows only an icon (D-113); its label is the tooltip and the
    accessible name. Its toggle state shows whether what it controls is on. It never takes keyboard
    focus, so shortcuts keep working.
*/
class ChromeButton final : public juce::Button
{
public:
    enum class Icon
    {
        settings,
        diagnostics,
        fullScreen,
        pause,
        play
    };

    ChromeButton(const juce::String& label, Icon icon);

    void setIcon(Icon icon);

    /** The width at `height`: the button is square. */
    [[nodiscard]] int preferredWidth(int height) const;

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

private:
    void paintIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const;

    Icon icon_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromeButton)
};

} // namespace visona
