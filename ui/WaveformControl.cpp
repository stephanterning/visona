#include "WaveformControl.h"

#include "Palette.h"

#include <array>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr float labelGap = 10.0f;
constexpr int segmentPadding = 12;

constexpr std::array<WaveformMode, 3> modes{WaveformMode::standard, WaveformMode::precise,
                                            WaveformMode::dj};
constexpr std::array<const char*, 3> segmentLabels{"STD", "PRECISE", "DJ"};

juce::FontOptions labelFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

juce::FontOptions segmentFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

} // namespace

WaveformControl::WaveformControl()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Waveform");
    setTooltip("How the waveform is drawn: STD is a thin line and the lightest to draw, PRECISE "
               "fills every peak, and DJ colours it by frequency (W)");
}

void WaveformControl::setMode(WaveformMode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    repaint();
}

void WaveformControl::setShowsLabel(bool showsLabel)
{
    if (showsLabel == showsLabel_)
        return;
    showsLabel_ = showsLabel;
    resized();
    repaint();
}

void WaveformControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    resized();
    repaint();
}

int WaveformControl::preferredWidth(int height) const
{
    auto width = showsLabel_ ? labelWidth() : 0;
    for (std::size_t segment = 0; segment < modes.size(); ++segment)
        width += std::max(height, segmentWidth(segment));
    return width;
}

void WaveformControl::paint(juce::Graphics& g)
{
    if (showsLabel_)
    {
        g.setColour(palette::textDim);
        g.setFont(labelFont(fontHeight_));
        g.drawText("WAVE", getLocalBounds().withWidth(labelWidth()),
                   juce::Justification::centredLeft, false);
    }

    g.setColour(palette::surface);
    g.fillRoundedRectangle(segments_.toFloat().reduced(0.5f), cornerRadius);

    g.setFont(segmentFont(fontHeight_));
    for (std::size_t segment = 0; segment < modes.size(); ++segment)
    {
        const auto bounds = segmentBounds(segment);
        const bool selected = modes[segment] == mode_;
        if (selected)
        {
            const auto area = bounds.toFloat().reduced(2.0f);
            g.setColour(palette::highlight);
            g.fillRoundedRectangle(area, cornerRadius - 1.0f);
            g.setColour(palette::level.withAlpha(0.6f));
            g.drawRoundedRectangle(area, cornerRadius - 1.0f, 1.0f);
        }
        g.setColour(selected ? palette::text : palette::level);
        g.drawText(segmentLabels[segment], bounds, juce::Justification::centred, false);
    }
}

void WaveformControl::resized()
{
    auto area = getLocalBounds();
    if (showsLabel_)
        area.removeFromLeft(labelWidth());
    segments_ = area;
}

void WaveformControl::mouseDown(const juce::MouseEvent& event)
{
    for (std::size_t segment = 0; segment < modes.size(); ++segment)
        if (segmentBounds(segment).contains(event.getPosition()))
        {
            if (modes[segment] != mode_ && onModeChange)
                onModeChange(modes[segment]);
            return;
        }
}

juce::Rectangle<int> WaveformControl::segmentBounds(std::size_t segment) const
{
    auto x = segments_.getX();
    for (std::size_t before = 0; before < segment; ++before)
        x += std::max(getHeight(), segmentWidth(before));
    return segments_.withX(x).withWidth(std::max(getHeight(), segmentWidth(segment)));
}

int WaveformControl::segmentWidth(std::size_t segment) const
{
    return juce::GlyphArrangement::getStringWidthInt(segmentFont(fontHeight_),
                                                     segmentLabels[segment]) +
           2 * segmentPadding;
}

int WaveformControl::labelWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt(labelFont(fontHeight_), "WAVE") +
           juce::roundToInt(labelGap);
}

} // namespace visona
