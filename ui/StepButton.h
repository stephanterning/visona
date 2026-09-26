#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/** The [−] and [+] buttons of a stepped value such as the display gain. They repeat while held
    and never take keyboard focus. */
class StepButton final : public juce::Button
{
public:
    /** A plus sign for a positive `direction`, and a minus sign otherwise. */
    StepButton(const juce::String& name, int direction);

    /** The corner radius of the control the buttons sit in. */
    static constexpr float cornerRadius = 6.0f;

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

private:
    int direction_;
};

} // namespace visona
