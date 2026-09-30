#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1

#include "X11Display.h"

namespace visona::x11
{

namespace
{

// forceDisplayUpdate() is protected; the class is never instantiated.
struct DisplayRefresh : juce::ComponentPeer
{
    using juce::ComponentPeer::forceDisplayUpdate;
};

::Display* xDisplay()
{
    auto* const windowSystem = juce::XWindowSystem::getInstanceWithoutCreating();
    return windowSystem != nullptr ? windowSystem->getDisplay() : nullptr;
}

} // namespace

std::optional<juce::Rectangle<int>> screenBounds()
{
    auto* const display = xDisplay();
    if (display == nullptr)
        return std::nullopt;

    auto* const x = juce::X11Symbols::getInstance();
    const juce::XWindowSystemUtilities::ScopedXLock lock;
    const auto root = x->xRootWindow(display, x->xDefaultScreen(display));
    ::Window rootReturn = 0;
    int left = 0;
    int top = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int border = 0;
    unsigned int depth = 0;
    if (x->xGetGeometry(display, root, &rootReturn, &left, &top, &width, &height, &border,
                        &depth) == 0)
        return std::nullopt;
    return juce::Rectangle<int>(left, top, static_cast<int>(width), static_cast<int>(height));
}

void refreshDisplays()
{
    DisplayRefresh::forceDisplayUpdate();
}

void requestFullScreen(juce::Component& window)
{
    auto* const display = xDisplay();
    auto* const peer = window.getPeer();
    if (display == nullptr || peer == nullptr)
        return;

    using juce::XWindowSystemUtilities::Atoms;
    const auto state = Atoms::getIfExists(display, "_NET_WM_STATE");
    const auto fullScreen = Atoms::getIfExists(display, "_NET_WM_STATE_FULLSCREEN");
    if (state == None || fullScreen == None)
        return;

    auto* const x = juce::X11Symbols::getInstance();
    const juce::XWindowSystemUtilities::ScopedXLock lock;

    XClientMessageEvent message{};
    message.type = ClientMessage;
    message.display = display;
    message.window = reinterpret_cast<::Window>(peer->getNativeHandle());
    message.message_type = state;
    message.format = 32;
    message.data.l[0] = 1; // _NET_WM_STATE_ADD
    message.data.l[1] = static_cast<long>(fullScreen);
    message.data.l[3] = 1; // A normal application

    const auto root = x->xRootWindow(display, x->xDefaultScreen(display));
    x->xSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask,
                  reinterpret_cast<XEvent*>(&message));
    x->xFlush(display);
}

} // namespace visona::x11
