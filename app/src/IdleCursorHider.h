#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/**
    Hides the system mouse cursor after a short period without movement over a component tree.

    Used by the standalone app in fullscreen (and kiosk mode) so the pointer does not sit on top
    of the scope during appliance use. Plugins do not use this helper.
*/
class IdleCursorHider final : private juce::Timer, private juce::MouseListener
{
public:
    explicit IdleCursorHider(juce::Component& root) : root_(root) {}

    ~IdleCursorHider() override
    {
        setActive(false);
    }

    void setActive(bool shouldBeActive)
    {
        if (shouldBeActive == active_)
            return;

        active_ = shouldBeActive;
        if (active_)
        {
            root_.addMouseListener(this, true);
            showCursor();
            startTimer(idleMilliseconds);
        }
        else
        {
            stopTimer();
            root_.removeMouseListener(this);
            showCursor();
        }
    }

private:
    static constexpr int idleMilliseconds = 3000;

    void timerCallback() override
    {
        hideCursor();
    }

    void mouseMove(const juce::MouseEvent&) override
    {
        onActivity();
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (hidden_)
            hideCursor();
        else
            onActivity();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        onActivity();
    }

    void mouseDrag(const juce::MouseEvent&) override
    {
        onActivity();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override
    {
        onActivity();
    }

    void onActivity()
    {
        if (!active_)
            return;

        showCursor();
        startTimer(idleMilliseconds);
    }

    void showCursor()
    {
        hidden_ = false;
        juce::Desktop::getInstance().getMainMouseSource().revealCursor();
    }

    void hideCursor()
    {
        hidden_ = true;
        juce::Desktop::getInstance().getMainMouseSource().hideCursor();
    }

    juce::Component& root_;
    bool active_ = false;
    bool hidden_ = false;
};

} // namespace visona
