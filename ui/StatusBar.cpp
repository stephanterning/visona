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
    setInterceptsMouseClicks(false, false);
}

void StatusBar::setValues(const Values& values)
{
    if (values == values_)
        return;
    values_ = values;
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
    // changes. The state comes first and is always shown.
    struct Item
    {
        const juce::String& text;
        const char* widest;
        bool bold;
        juce::Colour colour;
    };
    const std::array<Item, 4> items{{
        {values_.state, "", true, values_.stateIsError ? palette::error : palette::text},
        {values_.sampleRate, "192 kHz", false, palette::level},
        {values_.window, "2 s", false, palette::level},
        {values_.gain, "+36 dB", false, palette::level},
    }};

    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        if (item.text.isEmpty())
            continue;
        const auto font = fontFor(step_, item.bold);
        const auto width = std::max(juce::GlyphArrangement::getStringWidthInt(font, item.text),
                                    juce::GlyphArrangement::getStringWidthInt(font, item.widest));
        if (index > 0 && width > area.getWidth())
            break;
        g.setFont(font);
        g.setColour(item.colour);
        g.drawText(item.text, area.removeFromLeft(width), juce::Justification::centredLeft, true);
        area.removeFromLeft(gap);
    }
}

} // namespace visona
