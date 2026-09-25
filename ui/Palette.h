#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona::palette
{

inline const juce::Colour background{0xff0b0c0f};
inline const juce::Colour surface{0xff17191e};
inline const juce::Colour outline{0xff2c2f37};
inline const juce::Colour highlight{0xff3a3e48};
inline const juce::Colour text{0xffe6e6e8};
inline const juce::Colour textDim{0xff8b8e97};
inline const juce::Colour level{0xffb4b7bf};

/** Red is reserved for errors. */
inline const juce::Colour error{0xffc93b35};

/** Colours for JUCE's own widgets, such as combo boxes and buttons. */
inline juce::LookAndFeel_V4::ColourScheme widgetColours()
{
    return {background, surface, surface, outline, text, level, text, highlight, text};
}

} // namespace visona::palette
