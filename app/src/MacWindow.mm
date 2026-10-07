#include "MacWindow.h"

#import <AppKit/AppKit.h>

namespace visona
{

void applyMacWindowChrome(juce::Component& window)
{
    auto* const peer = window.getPeer();
    if (peer == nullptr)
        return;
    auto* const view = static_cast<NSView*>(peer->getNativeHandle());
    if (view == nullptr)
        return;
    NSWindow* const nsWindow = view.window;
    if (nsWindow == nullptr)
        return;
    nsWindow.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
}

} // namespace visona
