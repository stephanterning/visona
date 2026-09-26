#include "StepButton.h"

#include "Palette.h"

namespace visona
{

namespace
{

constexpr int repeatDelayMs = 400;
constexpr int repeatIntervalMs = 70;

} // namespace

StepButton::StepButton(const juce::String& name, int direction)
    : juce::Button(name)
    , direction_(direction)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setRepeatSpeed(repeatDelayMs, repeatIntervalMs);
}

void StepButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto bounds = getLocalBounds().toFloat();
    if (isEnabled() && (isDown || isHighlighted))
    {
        g.setColour(isDown ? palette::highlight : palette::surface.brighter(0.08f));
        g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);
    }

    const auto centre = bounds.getCentre();
    const auto arm = std::min(bounds.getWidth(), bounds.getHeight()) * 0.18f;
    juce::Path path;
    path.startNewSubPath(centre.x - arm, centre.y);
    path.lineTo(centre.x + arm, centre.y);
    if (direction_ > 0)
    {
        path.startNewSubPath(centre.x, centre.y - arm);
        path.lineTo(centre.x, centre.y + arm);
    }
    g.setColour(isEnabled() ? palette::text : palette::textDim);
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

} // namespace visona
