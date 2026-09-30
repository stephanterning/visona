#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace visona::x11
{

/** The size of the X screen in pixels, or std::nullopt without an X display. X resizes the screen
    whenever a display is connected, disconnected or changes mode. */
[[nodiscard]] std::optional<juce::Rectangle<int>> screenBounds();

/** Reads the displays again. JUCE on Linux reads them at startup and when XSettings change, not
    when a display is connected; windows whose display changed then get parentSizeChanged(). */
void refreshDisplays();

/** Asks the window manager to make `window` fullscreen (_NET_WM_STATE_FULLSCREEN). JUCE's
    setFullScreen() only resizes the window, so the compositor neither hides its panel nor keeps
    the window covering a display that changes. */
void requestFullScreen(juce::Component& window);

} // namespace visona::x11
