#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

/**
    The text inside the controls of the control bar: a small caption, such as GAIN, above the value
    (D-107). The caption sits inside the control rather than beside it, so it costs no width.
*/
namespace visona::controlText
{

[[nodiscard]] inline float captionHeight(float fontHeight)
{
    return std::max(9.0f, std::round(fontHeight * 0.7f));
}

[[nodiscard]] inline juce::FontOptions captionFont(float fontHeight)
{
    return juce::FontOptions(captionHeight(fontHeight), juce::Font::bold);
}

[[nodiscard]] inline juce::FontOptions valueFont(float fontHeight)
{
    return juce::FontOptions(fontHeight + 1.0f, juce::Font::bold).withFeatureEnabled("tnum");
}

/** The width of the widest of `texts` in `font`. */
[[nodiscard]] inline int widestText(const juce::FontOptions& font,
                                    std::initializer_list<juce::String> texts)
{
    auto widest = 0;
    for (const auto& text : texts)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(font, text));
    return widest;
}

/** Draws `caption` in the upper part of `area` and `value` under it, both centred. */
inline void draw(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& caption,
                 const juce::String& value, juce::Colour captionColour, juce::Colour valueColour,
                 float fontHeight)
{
    const auto captionRow = captionHeight(fontHeight);
    const auto valueRow = fontHeight + 2.0f;
    const auto bounds = area.toFloat();
    const auto top = bounds.getY() + (bounds.getHeight() - captionRow - valueRow) * 0.5f;

    g.setColour(captionColour);
    g.setFont(captionFont(fontHeight));
    g.drawText(caption, bounds.withY(top).withHeight(captionRow), juce::Justification::centred,
               false);
    g.setColour(valueColour);
    g.setFont(valueFont(fontHeight));
    g.drawText(value, bounds.withY(top + captionRow).withHeight(valueRow),
               juce::Justification::centred, false);
}

} // namespace visona::controlText
