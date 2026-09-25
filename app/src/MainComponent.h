#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/** Root content component of the main window. */
class MainComponent final : public juce::Component
{
public:
    static inline const juce::Colour backgroundColour{0xff0b0c0f};

    MainComponent();

    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace visona
