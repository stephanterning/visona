#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>

namespace visona
{

/** The waveform colours of palette::waveformColours as a row of swatches, with the chosen one
    ringed (D-093). Each swatch is a touch-sized target. */
class ColourSwatches final : public juce::Component, public juce::TooltipClient
{
public:
    ColourSwatches();

    /** Called with the index of the colour the user picks. */
    std::function<void(std::size_t index)> onColourChange;

    void setColour(std::size_t index);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    juce::String getTooltip() override;

private:
    [[nodiscard]] juce::Rectangle<float> swatchBounds(std::size_t index) const;
    [[nodiscard]] int swatchAt(juce::Point<float> position) const;

    std::size_t colour_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColourSwatches)
};

} // namespace visona
