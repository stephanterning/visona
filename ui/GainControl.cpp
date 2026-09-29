#include "GainControl.h"

#include "Palette.h"
#include "StepButton.h"

#include <visona/LaneMapping.h>

#include <algorithm>
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

juce::FontOptions labelFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

juce::String signedDb(int gainDb)
{
    return (gainDb > 0 ? "+" : "") + juce::String(gainDb);
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

void GainControl::setAuto(bool isAuto)
{
    if (isAuto == isAuto_)
        return;
    isAuto_ = isAuto;
    setTooltip(isAuto_ ? "Auto gain. Drag, scroll or use + and - to set the gain by hand, which "
                         "turns auto gain off."
                       : "Display gain. Drag, scroll or use + and -; double-click resets to 0 dB.");
    repaint();
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
        width += labelWidth() + juce::roundToInt(labelGap);
    return width;
}

juce::String GainControl::format(int gainDb, bool isAuto)
{
    return (isAuto ? "AUTO " : "") + signedDb(gainDb) + " dB";
}

void GainControl::paint(juce::Graphics& g)
{
    if (showsLabel_)
    {
        g.setColour(isAuto_ ? palette::text : palette::textDim);
        g.setFont(labelFont(fontHeight_));
        g.drawText(isAuto_ ? "AUTO" : "GAIN", labelArea_, juce::Justification::centredLeft, false);
    }

    const auto control = valueArea_.getUnion(minus_->getBounds()).getUnion(plus_->getBounds());
    g.setColour(palette::surface);
    g.fillRoundedRectangle(control.toFloat().reduced(0.5f), StepButton::cornerRadius);

    g.setColour(palette::text);
    g.setFont(valueFont(fontHeight_ + 1.0f));
    g.drawText(valueText(), valueArea_, juce::Justification::centred, false);
}

void GainControl::resized()
{
    auto area = getLocalBounds();
    const auto height = area.getHeight();
    labelArea_ = {};
    if (showsLabel_)
        labelArea_ = area.removeFromLeft(labelWidth() + juce::roundToInt(labelGap));
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
    const auto steps = static_cast<int>(std::round((offset.x - offset.y) / dragPixelsPerStep));
    if (steps != 0 || dragStartGainDb_ != gainDb_)
        request(dragStartGainDb_ + steps);
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
    // While auto gain sets the gain, even the same value is a change by hand.
    if ((gainDb != gainDb_ || isAuto_) && onGainChange)
        onGainChange(gainDb);
}

juce::String GainControl::valueText() const
{
    return isAuto_ && !showsLabel_ ? "AUTO " + signedDb(gainDb_) : format(gainDb_);
}

int GainControl::labelWidth() const
{
    const auto font = labelFont(fontHeight_);
    return std::max(juce::GlyphArrangement::getStringWidthInt(font, "GAIN"),
                    juce::GlyphArrangement::getStringWidthInt(font, "AUTO"));
}

int GainControl::valueWidth() const
{
    const auto font = valueFont(fontHeight_ + 1.0f);
    auto width = juce::GlyphArrangement::getStringWidthInt(font, "+18 dB");
    if (!showsLabel_)
        width = std::max(width, juce::GlyphArrangement::getStringWidthInt(font, "AUTO +18"));
    return width + 16;
}

} // namespace visona
