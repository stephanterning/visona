#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

namespace visona
{

/**
    The tooltips of a window, for both a mouse and a touchscreen (D-108).

    With a mouse they show on hover, as JUCE's TooltipWindow does. A finger shows a control's
    tooltip only while it is held still on the control for half a second, and hides it as soon as
    it lifts or moves; on the scope, the same hold draws the ruler instead, because the scope has no
    tooltip. Once a finger has touched the screen, hover tooltips stay off until the mouse moves
    again: a touchscreen moves the pointer too and leaves it where the finger lifted, where it
    would otherwise bring up a tooltip that stays.
*/
class Tooltips final : private juce::MouseListener
{
public:
    /** Shows the tooltips over `parent`, which must outlive this. */
    explicit Tooltips(juce::Component& parent);
    ~Tooltips() override;

private:
    class HoverTips;
    class Bubble;

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    void noteTouch(const juce::MouseEvent& event);
    void noteMouse(const juce::MouseEvent& event);
    void showHeldTip();
    void cancelHeldTip();

    juce::Component& parent_;
    bool hoverOff_ = false;
    double lastTouchMs_ = 0.0;
    juce::Point<float> lastTouchPosition_;

    // The finger held on a control, and that control's tooltip.
    std::optional<int> heldSource_;
    juce::Component::SafePointer<juce::Component> heldComponent_;
    juce::Point<float> heldPosition_;
    juce::String heldTip_;

    std::unique_ptr<HoverTips> hover_;
    std::unique_ptr<Bubble> bubble_;
    juce::TimedCallback holdTimer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tooltips)
};

} // namespace visona
