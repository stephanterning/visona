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
    const std::array<Item, 7> items{{
        {values_.bpm, "999.9 BPM", true, palette::text},
        {values_.state, "", true, values_.stateIsError ? palette::error : palette::text},
        {values_.sidechainSync, "SC +999.9 ms", true,
         values_.sidechainSyncIsError ? palette::error : palette::level},
        {values_.sampleRate, "192 kHz", false, palette::level},
        {values_.window, "4 BARS", false, palette::level},
        {values_.gain, "AUTO +18 dB", false, palette::level},
        {values_.zoom, "", true, palette::text},
    }};

    sidechainSyncArea_ = {};
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        if (item.text.isEmpty())
            continue;
        const auto font = fontFor(step_, item.bold);
        const auto width = std::max(juce::GlyphArrangement::getStringWidthInt(font, item.text),
                                    juce::GlyphArrangement::getStringWidthInt(font, item.widest));
        if (index > 2 && width > area.getWidth())
            break;
        const auto itemArea = area.removeFromLeft(width);
        if (index == 2)
            sidechainSyncArea_ = itemArea;
        g.setFont(font);
        g.setColour(item.colour);
        g.drawText(item.text, itemArea, juce::Justification::centredLeft, true);
        area.removeFromLeft(gap);
    }
}

juce::String StatusBar::getTooltip()
{
    return sidechainSyncArea_.contains(getMouseXYRelative()) ? values_.sidechainSyncTooltip
                                                             : juce::String();
}

} // namespace visona
