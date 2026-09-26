#include "ColourSwatches.h"

#include "Palette.h"

namespace visona
{

ColourSwatches::ColourSwatches()
    : colour_(palette::defaultWaveformColour)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Waveform colour");
}

void ColourSwatches::setColour(std::size_t index)
{
    index = std::min(index, palette::waveformColours.size() - 1);
    if (index == colour_)
        return;
    colour_ = index;
    repaint();
}

void ColourSwatches::paint(juce::Graphics& g)
{
    for (std::size_t index = 0; index < palette::waveformColours.size(); ++index)
    {
        const auto bounds = swatchBounds(index);
        if (index == colour_)
        {
            g.setColour(palette::text);
            g.drawEllipse(bounds.reduced(1.0f), 2.0f);
        }
        g.setColour(palette::waveformColours[index].colour);
        g.fillEllipse(bounds.reduced(5.0f));
    }
}

void ColourSwatches::mouseDown(const juce::MouseEvent& event)
{
    const auto index = swatchAt(event.position);
    if (index >= 0 && static_cast<std::size_t>(index) != colour_ && onColourChange)
        onColourChange(static_cast<std::size_t>(index));
}

juce::String ColourSwatches::getTooltip()
{
    const auto index = swatchAt(getMouseXYRelative().toFloat());
    return index >= 0 ? juce::String(palette::waveformColours[static_cast<std::size_t>(index)].name)
                      : juce::String();
}

juce::Rectangle<float> ColourSwatches::swatchBounds(std::size_t index) const
{
    const auto count = static_cast<float>(palette::waveformColours.size());
    const auto width = static_cast<float>(getWidth()) / count;
    const auto size = std::min(width, static_cast<float>(getHeight()));
    return juce::Rectangle<float>(size, size)
        .withCentre(
            {width * (static_cast<float>(index) + 0.5f), static_cast<float>(getHeight()) / 2.0f});
}

int ColourSwatches::swatchAt(juce::Point<float> position) const
{
    for (std::size_t index = 0; index < palette::waveformColours.size(); ++index)
        if (swatchBounds(index).contains(position))
            return static_cast<int>(index);
    return -1;
}

} // namespace visona
