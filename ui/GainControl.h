#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace visona
{

class StepButton;

/**
    Display gain as `GAIN [−] +12 dB [+]`, from 0 to +18 dB in 1 dB steps (D-046, D-054, D-100).
    No knob: the buttons step it and repeat while held, dragging the value up or right raises it,
    the scroll wheel steps it, and a double-click or double-tap resets it to 0 dB. While auto gain
    sets it, the label reads AUTO, or the value `AUTO +9` without a label; a change by hand is
    still asked for, and turns auto gain off.

    The control holds no state of its own beyond what it shows; it asks for changes through
    onGainChange and shows whatever setGainDb() and setAuto() say.
*/
class GainControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainControl();
    ~GainControl() override;

    /** Called with the gain the user asks for, already clamped. */
    std::function<void(int gainDb)> onGainChange;

    void setGainDb(int gainDb);

    /** Shows whether auto gain sets the gain. */
    void setAuto(bool isAuto);

    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** The width that fits the control at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    /** The gain as shown, such as "+12 dB", or "AUTO +9 dB" while auto gain sets it. */
    [[nodiscard]] static juce::String format(int gainDb, bool isAuto = false);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    void request(int gainDb);
    [[nodiscard]] juce::String valueText() const;
    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] int valueWidth() const;

    std::unique_ptr<StepButton> minus_;
    std::unique_ptr<StepButton> plus_;
    juce::Rectangle<int> labelArea_;
    juce::Rectangle<int> valueArea_;

    int gainDb_ = 0;
    bool isAuto_ = false;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;

    int dragStartGainDb_ = 0;
    float wheelAccumulator_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainControl)
};

} // namespace visona
