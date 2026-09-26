#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Every colour Visona draws with. Keeping them here makes themes cheap later, without a theme UI.

    Rules (architecture.md, 3.5 and 3.6): red is reserved for errors; the grid is neutral grey,
    never blue; the write head's accent is outside the band palette, so never white, blue, orange
    or red; colour in the chrome shows state only.
*/
namespace visona::palette
{

// Chrome
inline const juce::Colour background{0xff0b0c0f};
inline const juce::Colour chrome{0xff111317};
inline const juce::Colour surface{0xff17191e};
inline const juce::Colour outline{0xff2c2f37};
inline const juce::Colour highlight{0xff3a3e48};
inline const juce::Colour text{0xffe6e6e8};
inline const juce::Colour textDim{0xff8b8e97};
inline const juce::Colour level{0xffb4b7bf};

// Scope lanes
inline const juce::Colour laneBackground{0xff0b0c0f};
inline const juce::Colour laneDivider{0xff2c2f37};
inline const juce::Colour centreLine{0xff2b2f37};
inline const juce::Colour referenceLine{0xff1d2026};
inline const juce::Colour laneLabel{0xff6e727c};

// Waveform. Mono/precise mode draws the full band in one neutral colour.
inline const juce::Colour waveform{0xffd3d7de};

/** Marks display overshoot at a lane edge. Neutral, so it is not mistaken for audio clipping. */
inline const juce::Colour clipMarker{0xff8e939d};

/** The write head. */
inline const juce::Colour head{0xff3fe08a};

// Grid
inline const juce::Colour gridBar{0xff5a5f69};
inline const juce::Colour gridBeat{0xff363a42};
inline const juce::Colour gridSixteenth{0xff23262c};
/** Thirty-seconds and sixty-fourths, shown only when zoomed in. */
inline const juce::Colour gridFine{0xff191b20};

// Frequency bands, for colouring only (D-051, D-056)
inline const juce::Colour bandLow{0xff3a7bff};
inline const juce::Colour bandMid{0xffff9a2e};
inline const juce::Colour bandHigh{0xfff4f4f4};

// State
inline const juce::Colour error{0xffc93b35};

/** Colours for JUCE's own widgets, such as combo boxes and buttons. */
inline juce::LookAndFeel_V4::ColourScheme widgetColours()
{
    return {background, surface, surface, outline, text, level, text, highlight, text};
}

} // namespace visona::palette
