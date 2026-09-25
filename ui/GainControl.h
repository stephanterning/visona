#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace visona
{

/**
    Display gain as `GAIN [−] +12 dB [+]`, from 0 to +36 dB in 1 dB steps (D-046, D-054). No knob:
    the buttons step it and repeat while held, dragging the value up or right raises it, the scroll
    wheel steps it, and a double-click or double-tap resets it to 0 dB.

    The control holds no state of its own beyond what it shows; it asks for changes through
    onGainChange and shows whatever setGainDb() says.
*/
class GainControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainControl();
    ~GainControl() override;

    /** Called with the gain the user asks for, already clamped. */
    std::function<void(int gainDb)> onGainChange;

    void setGainDb(int gainDb);
    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** The width that fits the control at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    /** The gain as shown, such as "+12 dB". */
    [[nodiscard]] static juce::String format(int gainDb);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    class StepButton;

    void request(int gainDb);
    [[nodiscard]] int valueWidth() const;

    std::unique_ptr<StepButton> minus_;
    std::unique_ptr<StepButton> plus_;
    juce::Rectangle<int> labelArea_;
    juce::Rectangle<int> valueArea_;

    int gainDb_ = 0;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;

    int dragStartGainDb_ = 0;
    float wheelAccumulator_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainControl)
};

} // namespace visona
