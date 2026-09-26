#pragma once

#include "ChromeLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/**
    The status bar at the top (D-046): BPM, MIDI state, sample rate, window and gain, as calm text
    with tabular digits. Colour shows state only; red means an error.

    When the values do not fit, the bar drops them from the end, keeping the BPM and the state. It
    repaints only when a value changes.
*/
class StatusBar final : public juce::Component
{
public:
    struct Values
    {
        /** Such as "126.0 BPM"; empty when the tempo is not known. */
        juce::String bpm;
        juce::String state;
        bool stateIsError = false;
        juce::String sampleRate;
        juce::String window;
        juce::String gain;

        friend bool operator==(const Values&, const Values&) = default;
    };

    StatusBar();

    void setValues(const Values& values);
    void setStep(ChromeStep step);

    [[nodiscard]] int preferredHeight() const noexcept;

    void paint(juce::Graphics& g) override;

private:
    Values values_;
    ChromeStep step_ = ChromeStep::wide;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace visona
