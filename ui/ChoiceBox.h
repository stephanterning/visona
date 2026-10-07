#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace visona
{

class ChoiceList;

/**
    A combo box for the settings panel whose choices open in a ChoiceList inside the window
    instead of JUCE's PopupMenu, so a tap on the kiosk touchscreen picks them (D-108). The list
    covers the box, with the current choice over it, and scrolls when it does not fit.
*/
class ChoiceBox final : public juce::ComboBox
{
public:
    ChoiceBox();
    ~ChoiceBox() override;

    void showPopup() override;
    void enablementChanged() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    void dismissList();

    std::unique_ptr<ChoiceList> list_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceBox)
};

} // namespace visona
