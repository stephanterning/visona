#include "SegmentedControl.h"

#include "Palette.h"

#include <cmath>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr float labelGap = 10.0f;
constexpr float segmentPadding = 12.0f;

juce::FontOptions segmentFont(float height)
{
    return juce::FontOptions(height, juce::Font::bold);
}

juce::FontOptions labelFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

} // namespace

/** One segment. Its toggle state shows whether it is selected. */
class SegmentedControl::SegmentButton final : public juce::Button
{
public:
    SegmentButton(const Segment& segment, const float& fontHeight)
        : juce::Button(segment.text)
        , fontHeight_(fontHeight)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip(segment.tooltip);
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const bool on = getToggleState();
        if (on || isDown || isHighlighted)
        {
            g.setColour(on || isDown ? palette::highlight : palette::surface.brighter(0.08f));
            g.fillRoundedRectangle(bounds, cornerRadius - 1.0f);
        }
        g.setColour(on ? palette::text : palette::textDim);
        g.setFont(segmentFont(fontHeight_));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }

private:
    const float& fontHeight_;
};

SegmentedControl::SegmentedControl(const juce::String& label, std::vector<Segment> segments)
    : label_(label)
    , segments_(std::move(segments))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle(label);
    for (std::size_t index = 0; index < segments_.size(); ++index)
    {
        auto& button =
            *buttons_.emplace_back(std::make_unique<SegmentButton>(segments_[index], fontHeight_));
        button.onClick = [this, index]
        {
            const auto chosen = static_cast<int>(index);
            if (chosen != selected_ && onSelect)
                onSelect(chosen);
        };
        addAndMakeVisible(button);
    }
    setSelected(0);
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::setSelected(int index)
{
    selected_ = index;
    for (std::size_t button = 0; button < buttons_.size(); ++button)
        buttons_[button]->setToggleState(static_cast<int>(button) == index,
                                         juce::dontSendNotification);
}

void SegmentedControl::setShowsLabel(bool showsLabel)
{
    if (showsLabel == showsLabel_)
        return;
    showsLabel_ = showsLabel;
    resized();
    repaint();
}

void SegmentedControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    resized();
    repaint();
}

int SegmentedControl::preferredWidth(int) const
{
    auto width = showsLabel_ ? labelWidth() : 0;
    for (std::size_t index = 0; index < segments_.size(); ++index)
        width += segmentWidth(static_cast<int>(index));
    return width;
}

void SegmentedControl::paint(juce::Graphics& g)
{
    if (showsLabel_)
    {
        g.setColour(palette::textDim);
        g.setFont(labelFont(fontHeight_));
        g.drawText(label_, labelArea_, juce::Justification::centredLeft, false);
    }
    if (buttons_.empty())
        return;
    const auto area = buttons_.front()->getBounds().getUnion(buttons_.back()->getBounds());
    g.setColour(palette::surface);
    g.fillRoundedRectangle(area.toFloat().reduced(0.5f), cornerRadius);
}

void SegmentedControl::resized()
{
    auto area = getLocalBounds();
    labelArea_ = showsLabel_ ? area.removeFromLeft(labelWidth()) : juce::Rectangle<int>();
    for (std::size_t index = 0; index < buttons_.size(); ++index)
        buttons_[index]->setBounds(area.removeFromLeft(segmentWidth(static_cast<int>(index))));
}

int SegmentedControl::labelWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt(labelFont(fontHeight_), label_) +
           juce::roundToInt(labelGap);
}

int SegmentedControl::segmentWidth(int index) const
{
    const auto& text = segments_[static_cast<std::size_t>(index)].text;
    return static_cast<int>(
               std::ceil(juce::GlyphArrangement::getStringWidth(segmentFont(fontHeight_), text) +
                         2.0f * segmentPadding)) +
           2;
}

} // namespace visona
