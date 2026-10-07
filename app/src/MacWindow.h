#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/** Puts the window on the dark macOS appearance so the native title bar matches the chrome. */
void applyMacWindowChrome(juce::Component& window);

} // namespace visona
