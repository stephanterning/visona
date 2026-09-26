#include "Banner.h"

#include "Palette.h"

namespace visona
{

Banner::Banner()
{
    setInterceptsMouseClicks(false, false);
}

void Banner::setText(const juce::String& title, const juce::String& detail)
{
    if (title == title_ && detail == detail_)
        return;
    title_ = title;
    detail_ = detail;
    repaint();
}

void Banner::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(palette::error);
    g.fillRoundedRectangle(area, 8.0f);

    auto textArea = getLocalBounds().reduced(16, 10);
    g.setColour(palette::text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawFittedText(title_, textArea.removeFromTop(26), juce::Justification::centred, 1);
    g.setFont(juce::FontOptions(14.0f));
    g.drawFittedText(detail_, textArea, juce::Justification::centredTop, 2);
}

} // namespace visona
