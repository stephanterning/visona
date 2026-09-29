#include "AutoGainSettings.h"

#include <visona/AutoGain.h>

#include <algorithm>

namespace visona
{

namespace
{

constexpr int gap = 8;

} // namespace

AutoGainSettings::AutoGainSettings()
{
    setTitle("Auto gain");

    toggle_.setButtonText("Off");
    toggle_.setTooltip("Auto gain zooms the view in 3 dB steps, up to +18 dB, so the loudest peak "
                       "fills the top 3 dB. It zooms out as soon as a peak goes past the edge. "
                       "Setting the gain by hand turns it off.");
    toggle_.onClick = [this]
    {
        if (onAutoGainChange)
            onAutoGainChange(!toggle_.getToggleState());
    };
    addAndMakeVisible(toggle_);

    for (std::size_t choice = 0; choice < AutoGain::holdChoices.size(); ++choice)
        hold_.addItem("Zoom in after " + juce::String(AutoGain::holdChoices[choice]) + " s",
                      static_cast<int>(choice) + 1);
    hold_.setSelectedItemIndex(static_cast<int>(AutoGain::defaultHoldChoice),
                               juce::dontSendNotification);
    hold_.setTooltip("How long the peaks must stay low before auto gain zooms in. Bars at or below "
                     "-50 dBFS do not count.");
    hold_.onChange = [this]
    {
        if (const auto index = hold_.getSelectedItemIndex(); index >= 0 && onHoldChange)
            onHoldChange(static_cast<std::size_t>(index));
    };
    addAndMakeVisible(hold_);
}

void AutoGainSettings::setAutoGain(bool isOn)
{
    toggle_.setToggleState(isOn, juce::dontSendNotification);
    toggle_.setButtonText(isOn ? "On" : "Off");
}

void AutoGainSettings::setHoldChoice(std::size_t choice)
{
    choice = std::min(choice, AutoGain::holdChoices.size() - 1);
    hold_.setSelectedItemIndex(static_cast<int>(choice), juce::dontSendNotification);
}

void AutoGainSettings::resized()
{
    auto area = getLocalBounds();
    toggle_.setBounds(area.removeFromLeft(std::min(80, area.getWidth() / 3)));
    area.removeFromLeft(gap);
    hold_.setBounds(area);
}

} // namespace visona
