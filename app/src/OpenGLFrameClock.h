#pragma once

#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <functional>

namespace visona
{

/**
    Draws a component and everything in it with OpenGL, and calls `onFrame` on the message thread
    once for each buffer swap (D-107).

    The context repaints continuously with a swap interval of 1, so each swap waits for the
    display's refresh and the render thread runs once per displayed frame. Each frame tells the
    message thread when it began, and what that thread draws in response is shown by the next
    swap: one frame later, but at the display's own pace.

    If no context has been created a few seconds after the component came on screen, OpenGL is
    not available: the context is detached and the component goes back to JUCE's software
    rendering.
*/
class OpenGLFrameClock final : private juce::OpenGLRenderer,
                               private juce::AsyncUpdater,
                               private juce::Timer
{
public:
    /** `component` must outlive the clock. */
    explicit OpenGLFrameClock(juce::Component& component);
    ~OpenGLFrameClock() override;

    /** Called on the message thread with the time the frame began, in seconds on the clock of
        juce::Time::getMillisecondCounterHiRes(). */
    std::function<void(double timestampSeconds)> onFrame;

    /** Whether OpenGL could not be used. */
    [[nodiscard]] bool hasFailed() const noexcept
    {
        return failed_;
    }

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    juce::Component& component_;
    juce::OpenGLContext context_;
    std::atomic<bool> created_{false};
    std::atomic<double> frameStartMs_{0.0};
    // Render thread only.
    double lastFrameStartMs_ = 0.0;
    double waitedMs_ = 0.0;
    bool failed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGLFrameClock)
};

} // namespace visona
