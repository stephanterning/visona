#include "WindowControl.h"

#include "Palette.h"

#include <visona/SweepWindow.h>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr float labelGap = 10.0f;

/** The segment labels, in UTF-8. */
constexpr std::array<const char*, sweepWindowBars.size()> segmentLabels{"\xc2\xbc", "\xc2\xbd", "1",
                                                                        "2", "4"};

juce::FontOptions labelFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

} // namespace

WindowControl::WindowControl()
    : window_(defaultSweepWindow)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Window");
    setTooltip("Window in bars (keys 1 to 5)");
}

void WindowControl::setWindow(std::size_t window)
{
    window = std::min(window, sweepWindowBars.size() - 1);
    if (window == window_)
        return;
    window_ = window;
    repaint();
}

void WindowControl::setShowsLabel(bool showsLabel)
{
    if (showsLabel == showsLabel_)
        return;
    showsLabel_ = showsLabel;
    resized();
    repaint();
}

void WindowControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    resized();
    repaint();
}

int WindowControl::preferredWidth(int height) const
{
    return (showsLabel_ ? labelWidth() : 0) + static_cast<int>(sweepWindowBars.size()) * height;
}

juce::String WindowControl::describe(std::size_t window)
{
    window = std::min(window, sweepWindowBars.size() - 1);
    return juce::String::fromUTF8(segmentLabels[window]) +
           (sweepWindowBars[window] > 1.0 ? " BARS" : " BAR");
}

void WindowControl::paint(juce::Graphics& g)
{
    if (showsLabel_)
    {
        g.setColour(palette::textDim);
        g.setFont(labelFont(fontHeight_));
        g.drawText("WINDOW", getLocalBounds().withWidth(labelWidth()),
                   juce::Justification::centredLeft, false);
    }

    g.setColour(palette::surface);
    g.fillRoundedRectangle(segments_.toFloat().reduced(0.5f), cornerRadius);

    g.setFont(juce::FontOptions(fontHeight_ + 1.0f, juce::Font::bold));
    for (std::size_t segment = 0; segment < sweepWindowBars.size(); ++segment)
    {
        const auto bounds = segmentBounds(segment);
        const bool selected = segment == window_;
        if (selected)
        {
            g.setColour(palette::highlight);
            g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f), cornerRadius - 1.0f);
            g.setColour(palette::level.withAlpha(0.6f));
            g.drawRoundedRectangle(bounds.toFloat().reduced(2.0f), cornerRadius - 1.0f, 1.0f);
        }
        g.setColour(selected ? palette::text : palette::level);
        g.drawText(juce::String::fromUTF8(segmentLabels[segment]), bounds,
                   juce::Justification::centred, false);
    }
}

void WindowControl::resized()
{
    auto area = getLocalBounds();
    if (showsLabel_)
        area.removeFromLeft(labelWidth());
    segments_ = area.withWidth(static_cast<int>(sweepWindowBars.size()) * getHeight());
}

void WindowControl::mouseDown(const juce::MouseEvent& event)
{
    for (std::size_t segment = 0; segment < sweepWindowBars.size(); ++segment)
        if (segmentBounds(segment).contains(event.getPosition()))
        {
            if (segment != window_ && onWindowChange)
                onWindowChange(segment);
            return;
        }
}

juce::Rectangle<int> WindowControl::segmentBounds(std::size_t segment) const
{
    const auto width = segments_.getWidth() / static_cast<int>(sweepWindowBars.size());
    return segments_.withX(segments_.getX() + static_cast<int>(segment) * width).withWidth(width);
}

int WindowControl::labelWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt(labelFont(fontHeight_), "WINDOW") +
           juce::roundToInt(labelGap);
}

} // namespace visona
