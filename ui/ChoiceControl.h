#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace visona
{

/**
    A select menu in the control bar (D-108): a caption above the current choice, such as WINDOW
    above 2 BARS, and a small arrow. A click or tap opens the choices above it, with the current one
    ticked and each item as tall as the control, so it suits a finger.

    It shows what setSelected() says; a subclass hears about the choices the user makes, other than
    the current one, through choose().
*/
class ChoiceControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceControl(const juce::String& caption, juce::StringArray choices);
    ~ChoiceControl() override;

    void setSelected(int index);
    [[nodiscard]] int selected() const noexcept
    {
        return selected_;
    }

    void setFontHeight(float fontHeight);

    /** The width that fits the caption and the widest choice at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void parentSizeChanged() override;

protected:
    /** Called with the index of the choice the user picks, when it is not the current one. */
    virtual void choose(int index) = 0;

private:
    class Menu;

    void showMenu();
    void dismissMenu();
    void pick(int index);
    [[nodiscard]] juce::Component* overlayHost() const;

    juce::String caption_;
    juce::StringArray choices_;
    int selected_ = 0;
    float fontHeight_ = 14.0f;
    bool menuOpen_ = false;
    std::unique_ptr<Menu> menu_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceControl)
};

} // namespace visona
