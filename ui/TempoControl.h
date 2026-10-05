#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace visona
{

class StepButton;

/**
    The tempo as `[− BPM 126.0 +]`, with the caption above the value (D-090, D-108). While the
    sweep runs free it sets the free tempo, from 40 to 300 BPM: the buttons step it by whole BPM
    and repeat while held, and dragging the value up or right, or scrolling over it, fine-tunes it
    in steps of 0.1 BPM. While MIDI Clock sets the tempo, it shows that tempo dimmed and cannot be
    changed.

    Like GainControl, it asks for changes through onTempoChange and shows whatever setBpm() says.
*/
class TempoControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TempoControl();
    ~TempoControl() override;

    /** Called with the tempo the user asks for, already clamped and rounded. */
    std::function<void(double bpm)> onTempoChange;

    /** The tempo to show; 0 is not known. */
    void setBpm(double bpm);

    /** Whether the tempo can be changed here, which it can only while the sweep runs free. */
    void setEditable(bool editable);

    void setFontHeight(float fontHeight);

    /** The width that fits the control at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    void request(double bpm);
    void updateButtons();
    [[nodiscard]] int valueWidth() const;

    std::unique_ptr<StepButton> minus_;
    std::unique_ptr<StepButton> plus_;
    juce::Rectangle<int> valueArea_;

    double bpm_ = 0.0;
    bool editable_ = false;
    float fontHeight_ = 14.0f;

    double dragStartBpm_ = 0.0;
    float wheelAccumulator_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoControl)
};

} // namespace visona
