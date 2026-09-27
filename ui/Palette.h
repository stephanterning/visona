#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>

/**
    Every colour Visona draws with. Keeping them here makes themes cheap later, without a theme UI.

    Rules (architecture.md, 3.5 and 3.6): red is reserved for errors in the chrome; the grid is
    neutral grey, never blue; the write head's accent is outside the waveform colours; colour in
    the chrome shows state only.
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

/** A waveform colour to choose from in the settings. */
struct WaveformColour
{
    const char* name;
    juce::Colour colour;
};

/**
    The waveform colours of the standard and precise modes (D-093): calm but clear on black. There
    is no green, the head's colour. DJ colouring mixes its own colours from the bands (D-092).
*/
inline const std::array<WaveformColour, 8> waveformColours{{
    {"Teal", juce::Colour{0xff22b8a0}},
    {"Cyan", juce::Colour{0xff4fcdeb}},
    {"Blue", juce::Colour{0xff5f8dff}},
    {"Violet", juce::Colour{0xffa68cff}},
    {"Pink", juce::Colour{0xffff7cc2}},
    {"Amber", juce::Colour{0xffffb24a}},
    {"Yellow", juce::Colour{0xffe9d95c}},
    {"Grey", juce::Colour{0xffd3d7de}},
}};

/** Teal, like Oszillos Mega Scope's default. */
inline constexpr std::size_t defaultWaveformColour = 0;

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

// The measurement ruler (D-094): light and neutral, unlike the zoom selection and the head.
inline const juce::Colour rulerFill{0x2ee6e6e8};
inline const juce::Colour rulerEdge{0xb3e6e6e8};
inline const juce::Colour readoutBackground{0xeb17191e};

// State
inline const juce::Colour error{0xffc93b35};

/** Colours for JUCE's own widgets, such as combo boxes and buttons. */
inline juce::LookAndFeel_V4::ColourScheme widgetColours()
{
    return {background, surface, surface, outline, text, level, text, highlight, text};
}

} // namespace visona::palette
