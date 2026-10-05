#include "OpenGLFrameClock.h"

#include "ui/Palette.h"

#include <chrono>
#include <thread>

namespace visona
{

namespace
{

// A swap that does not wait for the display, such as one for a window that is not on screen,
// would otherwise let the render thread spin. A working swap at 60 Hz never waits here.
constexpr double minFrameIntervalMs = 1000.0 / 75.0;

// Without a context by then, OpenGL is not available, and the component would stay blank.
constexpr double contextTimeoutMs = 3000.0;
constexpr int watchdogIntervalMs = 250;

// The scope's tiles at 4K and a scale of 2, with room to spare.
constexpr std::size_t imageCacheBytes = 96 * 1024 * 1024;

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

} // namespace

OpenGLFrameClock::OpenGLFrameClock(juce::Component& component)
    : component_(component)
{
    context_.setRenderer(this);
    context_.setComponentPaintingEnabled(true);
    context_.setContinuousRepainting(true);
    context_.setImageCacheSize(imageCacheBytes);
    context_.attachTo(component);
    startTimer(watchdogIntervalMs);
}

OpenGLFrameClock::~OpenGLFrameClock()
{
    stopTimer();
    context_.detach();
    cancelPendingUpdate();
}

void OpenGLFrameClock::newOpenGLContextCreated()
{
    context_.setSwapInterval(1);
    created_ = true;
}

void OpenGLFrameClock::renderOpenGL()
{
    auto start = nowMs();
    if (const auto elapsed = start - lastFrameStartMs_;
        lastFrameStartMs_ > 0.0 && elapsed < minFrameIntervalMs)
    {
        std::this_thread::sleep_for(
            std::chrono::duration<double, std::milli>(minFrameIntervalMs - elapsed));
        start = nowMs();
    }
    lastFrameStartMs_ = start;

    juce::OpenGLHelpers::clear(palette::background);

    frameStartMs_.store(start);
    triggerAsyncUpdate();
}

void OpenGLFrameClock::openGLContextClosing() {}

void OpenGLFrameClock::handleAsyncUpdate()
{
    if (onFrame)
        onFrame(frameStartMs_.load() * 0.001);
}

void OpenGLFrameClock::timerCallback()
{
    if (created_)
    {
        stopTimer();
        return;
    }
    // The context is only created once the component is on screen.
    if (!component_.isShowing())
    {
        waitedMs_ = 0.0;
        return;
    }
    waitedMs_ += watchdogIntervalMs;
    if (waitedMs_ < contextTimeoutMs)
        return;

    stopTimer();
    context_.detach();
    failed_ = true;
    component_.repaint();
}

} // namespace visona
