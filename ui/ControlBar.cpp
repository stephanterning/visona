#include "ControlBar.h"

#include "Palette.h"

namespace visona
{

ControlBar::ControlBar()
{
    setOpaque(true);

    diagnostics_.setTooltip("Diagnostics (D)");
    fullScreen_.setTooltip("Full screen (F)");
    settings_.setTooltip("Settings (Cmd+,)");

    diagnostics_.onClick = [this]
    {
        if (onDiagnostics)
            onDiagnostics();
    };
    fullScreen_.onClick = [this]
    {
        if (onFullScreen)
            onFullScreen();
    };
    settings_.onClick = [this]
    {
        if (onSettings)
            onSettings();
    };

    addAndMakeVisible(gain_);
    addAndMakeVisible(diagnostics_);
    addAndMakeVisible(fullScreen_);
    addAndMakeVisible(settings_);
    setStep(ChromeStep::wide);
}

void ControlBar::setToggles(bool diagnostics, bool fullScreen, bool settings)
{
    diagnostics_.setToggleState(diagnostics, juce::dontSendNotification);
    fullScreen_.setToggleState(fullScreen, juce::dontSendNotification);
    settings_.setToggleState(settings, juce::dontSendNotification);
}

void ControlBar::setStep(ChromeStep step)
{
    step_ = step;
    const auto metrics = ChromeMetrics::forStep(step);
    const bool wide = step == ChromeStep::wide;

    gain_.setShowsLabel(step != ChromeStep::compact);
    gain_.setFontHeight(metrics.fontHeight);
    for (auto* button : {&diagnostics_, &fullScreen_, &settings_})
    {
        button->setShowsLabel(wide);
        button->setFontHeight(metrics.fontHeight);
    }
    diagnostics_.setVisible(showsSecondaryControls());
    fullScreen_.setVisible(showsSecondaryControls());
    resized();
    repaint();
}

int ControlBar::preferredHeight(int width) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    const auto rows = rowsFor(width);
    return rows * metrics.controlHeight + (rows - 1) * metrics.gap + 2 * metrics.padding + 1;
}

void ControlBar::paint(juce::Graphics& g)
{
    g.fillAll(palette::chrome);
    g.setColour(palette::outline);
    g.fillRect(getLocalBounds().removeFromTop(1));
}

void ControlBar::resized()
{
    const auto metrics = ChromeMetrics::forStep(step_);
    auto area = getLocalBounds().withTrimmedTop(1).reduced(metrics.padding);
    const auto rows = rowsFor(getWidth());
    const auto height = metrics.controlHeight;

    auto leftRow = area.removeFromTop(height);
    auto rightRow = leftRow;
    if (rows > 1)
    {
        area.removeFromTop(metrics.gap);
        rightRow = area.removeFromTop(height);
    }

    gain_.setBounds(leftRow.removeFromLeft(std::min(leftWidth(), leftRow.getWidth())));

    for (auto* button : {&settings_, &fullScreen_, &diagnostics_})
    {
        if (!button->isVisible())
            continue;
        button->setBounds(rightRow.removeFromRight(button->preferredWidth(height)));
        rightRow.removeFromRight(metrics.gap);
    }
}

int ControlBar::leftWidth() const
{
    return gain_.preferredWidth(ChromeMetrics::forStep(step_).controlHeight);
}

int ControlBar::rightWidth() const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    auto width = 0;
    for (const auto* button : {&diagnostics_, &fullScreen_, &settings_})
        if (button->isVisible())
            width += button->preferredWidth(metrics.controlHeight) + metrics.gap;
    return width;
}

int ControlBar::rowsFor(int width) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    const auto needed = leftWidth() + metrics.gap * 2 + rightWidth() + 2 * metrics.padding;
    return needed > width ? 2 : 1;
}

} // namespace visona
