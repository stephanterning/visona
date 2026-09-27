#pragma once

#include <visona/WaveformStyle.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace visona
{

/**
    The waveform's drawing mode as a segmented control, `WAVE [STD][PRECISE][DJ]` (D-091). Like
    WindowControl, it shows what setMode() says and asks for changes through onModeChange.
*/
class WaveformControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformControl();

    std::function<void(WaveformMode mode)> onModeChange;

    void setMode(WaveformMode mode);
    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** The width that fits the control at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    [[nodiscard]] juce::Rectangle<int> segmentBounds(std::size_t segment) const;
    [[nodiscard]] int segmentWidth(std::size_t segment) const;
    [[nodiscard]] int labelWidth() const;

    WaveformMode mode_ = WaveformMode::precise;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;
    juce::Rectangle<int> segments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformControl)
};

} // namespace visona
