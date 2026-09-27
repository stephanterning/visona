#include "GainControl.h"

#include "Palette.h"
#include "StepButton.h"

#include <visona/LaneMapping.h>

#include <cmath>

namespace visona
{

namespace
{

constexpr float labelGap = 10.0f;
constexpr float dragPixelsPerStep = 6.0f;
constexpr float smoothWheelPerStep = 0.06f;

juce::FontOptions valueFont(float height)
{
    return juce::FontOptions(height, juce::Font::bold).withFeatureEnabled("tnum");
}

} // namespace

GainControl::GainControl()
    : minus_(std::make_unique<StepButton>("Decrease gain", -1))
    , plus_(std::make_unique<StepButton>("Increase gain", 1))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Display gain");
    setTooltip("Display gain. Drag, scroll or use + and -; double-click resets to 0 dB.");

    minus_->onClick = [this] { request(gainDb_ - 1); };
    plus_->onClick = [this] { request(gainDb_ + 1); };
    addAndMakeVisible(*minus_);
    addAndMakeVisible(*plus_);
    setGainDb(0);
}

GainControl::~GainControl() = default;

void GainControl::setGainDb(int gainDb)
{
    gainDb = DisplayGain::clampDb(gainDb);
    minus_->setEnabled(gainDb > DisplayGain::minDb);
    plus_->setEnabled(gainDb < DisplayGain::maxDb);
    if (gainDb == gainDb_)
        return;
    gainDb_ = gainDb;
    repaint(valueArea_);
}

void GainControl::setShowsLabel(bool showsLabel)
{
    if (showsLabel == showsLabel_)
        return;
    showsLabel_ = showsLabel;
    resized();
    repaint();
}

void GainControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    resized();
    repaint();
}

int GainControl::preferredWidth(int height) const
{
    auto width = 2 * height + valueWidth();
    if (showsLabel_)
        width += juce::GlyphArrangement::getStringWidthInt(
                     juce::FontOptions(fontHeight_ - 1.0f, juce::Font::bold), "GAIN") +
                 juce::roundToInt(labelGap);
    return width;
}

juce::String GainControl::format(int gainDb)
{
    return (gainDb > 0 ? "+" : "") + juce::String(gainDb) + " dB";
}

void GainControl::paint(juce::Graphics& g)
{
    if (showsLabel_)
    {
        g.setColour(palette::textDim);
        g.setFont(juce::FontOptions(fontHeight_ - 1.0f, juce::Font::bold));
        g.drawText("GAIN", labelArea_, juce::Justification::centredLeft, false);
    }

    const auto control = valueArea_.getUnion(minus_->getBounds()).getUnion(plus_->getBounds());
    g.setColour(palette::surface);
    g.fillRoundedRectangle(control.toFloat().reduced(0.5f), StepButton::cornerRadius);

    g.setColour(palette::text);
    g.setFont(valueFont(fontHeight_ + 1.0f));
    g.drawText(format(gainDb_), valueArea_, juce::Justification::centred, false);
}

void GainControl::resized()
{
    auto area = getLocalBounds();
    const auto height = area.getHeight();
    labelArea_ = {};
    if (showsLabel_)
        labelArea_ = area.removeFromLeft(
            juce::GlyphArrangement::getStringWidthInt(
                juce::FontOptions(fontHeight_ - 1.0f, juce::Font::bold), "GAIN") +
            juce::roundToInt(labelGap));
    minus_->setBounds(area.removeFromLeft(height));
    valueArea_ = area.removeFromLeft(valueWidth());
    plus_->setBounds(area.removeFromLeft(height));
}

void GainControl::mouseDown(const juce::MouseEvent&)
{
    dragStartGainDb_ = gainDb_;
}

void GainControl::mouseDrag(const juce::MouseEvent& event)
{
    const auto offset = event.getOffsetFromDragStart().toFloat();
    const auto steps = std::round((offset.x - offset.y) / dragPixelsPerStep);
    request(dragStartGainDb_ + static_cast<int>(steps));
}

void GainControl::mouseDoubleClick(const juce::MouseEvent&)
{
    request(0);
}

void GainControl::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta =
        (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY) *
        (wheel.isReversed ? -1.0f : 1.0f);
    if (!wheel.isSmooth)
    {
        if (delta != 0.0f)
            request(gainDb_ + (delta > 0.0f ? 1 : -1));
        return;
    }

    wheelAccumulator_ += delta;
    const auto steps = static_cast<int>(wheelAccumulator_ / smoothWheelPerStep);
    if (steps != 0)
    {
        wheelAccumulator_ -= static_cast<float>(steps) * smoothWheelPerStep;
        request(gainDb_ + steps);
    }
}

void GainControl::request(int gainDb)
{
    gainDb = DisplayGain::clampDb(gainDb);
    if (gainDb != gainDb_ && onGainChange)
        onGainChange(gainDb);
}

int GainControl::valueWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt(valueFont(fontHeight_ + 1.0f), "+36 dB") + 16;
}

} // namespace visona
