#include "ControlBar.h"

#include "Palette.h"

namespace visona
{

namespace
{

// In the order of WaveformColoring.
std::vector<SegmentedControl::Segment> coloringSegments()
{
    return {{"Precise", "Mono/precise: the full band in one neutral colour (M)"},
            {"Blend", "One colour per column, blended from the bands (C)"},
            {"Bands", "The bands drawn inside the full-band outline (C)"}};
}

} // namespace

ControlBar::ControlBar()
    : coloring_("COLOR", coloringSegments())
{
    setOpaque(true);

    diagnostics_.setTooltip("Diagnostics (D)");
    fullScreen_.setTooltip("Full screen (F)");
    settings_.setTooltip("Settings (Cmd+,)");

    coloring_.onSelect = [this](int index)
    {
        if (onColoring)
            onColoring(static_cast<WaveformColoring>(index));
    };
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
    addAndMakeVisible(coloring_);
    addAndMakeVisible(diagnostics_);
    addAndMakeVisible(fullScreen_);
    addAndMakeVisible(settings_);
    setStep(ChromeStep::wide);
}

void ControlBar::setColoring(WaveformColoring coloring)
{
    coloring_.setSelected(static_cast<int>(coloring));
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
    coloring_.setShowsLabel(wide);
    coloring_.setFontHeight(metrics.fontHeight);
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

    gain_.setBounds(
        leftRow.removeFromLeft(std::min(gain_.preferredWidth(height), leftRow.getWidth())));
    leftRow.removeFromLeft(metrics.gap * 2);
    coloring_.setBounds(leftRow.removeFromLeft(
        std::min(coloring_.preferredWidth(height), std::max(leftRow.getWidth(), 0))));

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
    const auto metrics = ChromeMetrics::forStep(step_);
    return gain_.preferredWidth(metrics.controlHeight) + metrics.gap * 2 +
           coloring_.preferredWidth(metrics.controlHeight);
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
