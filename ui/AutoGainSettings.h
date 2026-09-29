#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>

namespace visona
{

/**
    The auto gain settings (D-100): an on/off button and how long the gain must hold before it
    zooms in, one of AutoGain::holdChoices. Like the other controls, it asks for changes through
    its callbacks and shows whatever its setters say.
*/
class AutoGainSettings final : public juce::Component
{
public:
    AutoGainSettings();

    /** Called with whether the user turns auto gain on. */
    std::function<void(bool isOn)> onAutoGainChange;

    /** Called with the hold time the user picks, an index into AutoGain::holdChoices. */
    std::function<void(std::size_t choice)> onHoldChange;

    void setAutoGain(bool isOn);
    void setHoldChoice(std::size_t choice);

    void resized() override;

private:
    juce::TextButton toggle_;
    juce::ComboBox hold_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoGainSettings)
};

} // namespace visona
