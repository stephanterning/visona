#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/**
    A button in the chrome: an icon, optionally followed by a label. Its toggle state shows whether
    what it controls is on. It never takes keyboard focus, so shortcuts keep working.
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
    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** Makes room for `label` too, so the button keeps its width when its label changes. */
    void setWidestLabel(const juce::String& label);

    /** The width that fits the icon, and the label if `withLabel`, at `height`. */
    [[nodiscard]] int preferredWidth(int height, bool withLabel) const;

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

private:
    void paintIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const;

    Icon icon_;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;
    juce::String widestLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromeButton)
};

} // namespace visona
