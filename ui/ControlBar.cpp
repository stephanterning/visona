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

    addAndMakeVisible(window_);
    addAndMakeVisible(tempo_);
    addAndMakeVisible(gain_);
    addAndMakeVisible(waveform_);
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

    window_.setFontHeight(metrics.fontHeight);
    tempo_.setFontHeight(metrics.fontHeight);
    gain_.setFontHeight(metrics.fontHeight);
    waveform_.setFontHeight(metrics.fontHeight);
    for (auto* button : {&diagnostics_, &fullScreen_, &settings_})
        button->setFontHeight(metrics.fontHeight);
    diagnostics_.setVisible(showsSecondaryControls());
    fullScreen_.setVisible(showsSecondaryControls() && fullScreenVisible_);
    resized();
    repaint();
}

void ControlBar::setTempoVisible(bool visible)
{
    if (tempoVisible_ == visible)
        return;
    tempoVisible_ = visible;
    tempo_.setVisible(visible);
    resized();
    repaint();
}

void ControlBar::setFullScreenVisible(bool visible)
{
    if (fullScreenVisible_ == visible)
        return;
    fullScreenVisible_ = visible;
    fullScreen_.setVisible(showsSecondaryControls() && fullScreenVisible_);
    resized();
    repaint();
}

int ControlBar::preferredHeight(int width) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    const auto rows = place(width).numRows;
    return rows * metrics.controlHeight + (rows - 1) * metrics.gap + 2 * metrics.padding + 1;
}

int ControlBar::minimumWidth() const
{
    return rowWidth(groupWidths(false)) + 2 * ChromeMetrics::forStep(step_).padding;
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
    const auto area = getLocalBounds().withTrimmedTop(1).reduced(metrics.padding);
    const auto height = metrics.controlHeight;
    const auto placement = place(getWidth());
    const auto widths = groupWidths(placement.buttonLabels);
    const auto rowBounds = [&](int row)
    { return area.withY(area.getY() + row * (height + metrics.gap)).withHeight(height); };

    // The window, tempo, gain and waveform flow from the left; the buttons sit at the right of
    // their row.
    std::array<juce::Rectangle<int>, 5> rows{rowBounds(0), rowBounds(1), rowBounds(2), rowBounds(3),
                                             rowBounds(4)};
    const std::array<juce::Component*, 4> controls{&window_, &tempo_, &gain_, &waveform_};
    for (std::size_t group = 0; group < controls.size(); ++group)
    {
        if (widths[group] == 0)
            continue;
        auto& row = rows[static_cast<std::size_t>(placement.rows[group])];
        controls[group]->setBounds(row.removeFromLeft(std::min(widths[group], area.getWidth())));
        row.removeFromLeft(metrics.groupGap);
    }

    auto& buttonRow = rows[static_cast<std::size_t>(placement.rows[4])];
    for (auto* button : {&settings_, &fullScreen_, &diagnostics_})
    {
        if (!button->isVisible())
            continue;
        button->setShowsLabel(placement.buttonLabels);
        button->setBounds(
            buttonRow.removeFromRight(button->preferredWidth(height, placement.buttonLabels)));
        buttonRow.removeFromRight(metrics.gap);
    }
}

std::array<int, 5> ControlBar::groupWidths(bool buttonLabels) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    auto buttons = 0;
    for (const auto* button : {&diagnostics_, &fullScreen_, &settings_})
        if (button->isVisible())
            buttons += (buttons > 0 ? metrics.gap : 0) +
                       button->preferredWidth(metrics.controlHeight, buttonLabels);
    return {window_.preferredWidth(metrics.controlHeight),
            tempoVisible_ ? tempo_.preferredWidth(metrics.controlHeight) : 0,
            gain_.preferredWidth(metrics.controlHeight),
            waveform_.preferredWidth(metrics.controlHeight), buttons};
}

int ControlBar::rowWidth(const std::array<int, 5>& widths) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    auto used = 0;
    for (const auto width : widths)
        if (width > 0)
            used += (used > 0 ? metrics.groupGap : 0) + width;
    return used;
}

ControlBar::Placement ControlBar::place(int width) const
{
    const auto metrics = ChromeMetrics::forStep(step_);
    const auto available = width - 2 * metrics.padding;
    Placement placement;
    placement.buttonLabels = rowWidth(groupWidths(true)) <= available;
    const auto widths = groupWidths(placement.buttonLabels);
    auto used = 0;
    for (std::size_t group = 0; group < widths.size(); ++group)
    {
        if (widths[group] == 0)
        {
            placement.rows[group] = 0;
            continue;
        }
        const auto needed = (used > 0 ? metrics.groupGap : 0) + widths[group];
        if (used > 0 && used + needed > available)
        {
            ++placement.numRows;
            used = widths[group];
        }
        else
        {
            used += needed;
        }
        placement.rows[group] = placement.numRows - 1;
    }
    return placement;
}

} // namespace visona
