#include "StatusBar.h"

#include "Palette.h"

#include <array>

namespace visona
{

namespace
{

juce::FontOptions fontFor(ChromeStep step, bool bold)
{
    return juce::FontOptions(ChromeMetrics::forStep(step).fontHeight,
                             bold ? juce::Font::bold : juce::Font::plain)
        .withFeatureEnabled("tnum");
}

} // namespace

StatusBar::StatusBar()
{
    setOpaque(true);
    setInterceptsMouseClicks(true, false);
}

void StatusBar::setValues(const Values& values)
{
    if (values == values_)
        return;
    values_ = values;
    if (!values_.stateIsAction)
        stateHighlighted_ = false;
    repaint();
}

void StatusBar::setStep(ChromeStep step)
{
    if (step == step_)
        return;
    step_ = step;
    repaint();
}

int StatusBar::preferredHeight() const noexcept
{
    return ChromeMetrics::forStep(step_).statusHeight;
}

void StatusBar::paint(juce::Graphics& g)
{
    g.fillAll(palette::chrome);
    g.setColour(palette::outline);
    g.fillRect(getLocalBounds().removeFromBottom(1));

    const auto metrics = ChromeMetrics::forStep(step_);
    auto area = getLocalBounds().reduced(metrics.padding + 4, 0).withTrimmedBottom(1);
    const auto gap = metrics.gap * 3;

    // Each value gets a slot as wide as its widest possible text, so nothing moves when a value
    // changes. The BPM and the state come first and are always shown. The zoom comes last, so
    // that its changing width moves nothing.
    struct Item
    {
        const juce::String& text;
        const char* widest;
        bool bold;
        juce::Colour colour;
    };
    const std::array<Item, 6> items{{
        {values_.bpm, "999.9 BPM", true, palette::text},
        {values_.state, "", true, values_.stateIsError ? palette::error : palette::text},
        {values_.sampleRate, "192 kHz", false, palette::level},
        {values_.window, "4 BARS", false, palette::level},
        {values_.gain, "+36 dB", false, palette::level},
        {values_.zoom, "", true, palette::text},
    }};

    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        if (item.text.isEmpty())
            continue;
        const auto font = fontFor(step_, item.bold);
        const auto width = std::max(juce::GlyphArrangement::getStringWidthInt(font, item.text),
                                    juce::GlyphArrangement::getStringWidthInt(font, item.widest));
        if (index > 1 && width > area.getWidth())
            break;
        const bool button = index == 1 && values_.stateIsAction;
        const auto padding = button ? metrics.gap * 2 : 0;
        auto itemArea = area.removeFromLeft(width + 2 * padding);
        if (index == 1)
            stateArea_ = button ? itemArea.reduced(0, std::max(2, itemArea.getHeight() / 6))
                                : juce::Rectangle<int>();
        if (button)
        {
            g.setColour(stateHighlighted_ ? palette::highlight : palette::surface);
            g.fillRoundedRectangle(stateArea_.toFloat(), 4.0f);
            g.setColour(palette::outline.brighter(0.2f));
            g.drawRoundedRectangle(stateArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
            itemArea.reduce(padding, 0);
        }
        g.setFont(font);
        g.setColour(item.colour);
        g.drawText(item.text, itemArea, juce::Justification::centredLeft, true);
        area.removeFromLeft(gap);
    }
}

bool StatusBar::hitTest(int x, int y)
{
    return values_.stateIsAction && stateArea_.contains(x, y);
}

void StatusBar::mouseEnter(const juce::MouseEvent&)
{
    stateHighlighted_ = values_.stateIsAction;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint(stateArea_);
}

void StatusBar::mouseExit(const juce::MouseEvent&)
{
    stateHighlighted_ = false;
    repaint(stateArea_);
}

void StatusBar::mouseUp(const juce::MouseEvent& event)
{
    if (values_.stateIsAction && stateArea_.contains(event.getPosition()) &&
        !event.mouseWasDraggedSinceMouseDown() && onStateClick)
        onStateClick();
}

juce::String StatusBar::getTooltip()
{
    return values_.stateIsAction ? values_.stateTooltip : juce::String();
}

} // namespace visona
