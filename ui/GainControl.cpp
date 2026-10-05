#include "GainControl.h"

#include "ControlText.h"
#include "Palette.h"
#include "StepButton.h"

#include <visona/LaneMapping.h>

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

constexpr int autoGap = 6;
constexpr int autoPadding = 10;
constexpr float dragPixelsPerStep = 6.0f;
constexpr float smoothWheelPerStep = 0.06f;

juce::FontOptions autoFont(float height)
{
    return juce::FontOptions(height - 1.0f, juce::Font::bold);
}

juce::String signedDb(int gainDb)
{
    return (gainDb > 0 ? "+" : "") + juce::String(gainDb);
}

} // namespace

/** AUTO beside the gain: lit while auto gain is on. */
class GainControl::AutoButton final : public juce::Button
{
public:
    AutoButton()
        : juce::Button("Auto gain")
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip("Auto gain zooms the view in 3 dB steps, up to +18 dB, so the loudest peak "
                   "fills the top 3 dB. It zooms out as soon as a peak goes past the edge. "
                   "Setting the gain by hand turns it off.");
    }

    void setFontHeight(float fontHeight)
    {
        fontHeight_ = fontHeight;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = getToggleState();
        auto fill = on ? palette::active.withAlpha(0.16f) : palette::surface;
        if (isDown)
            fill = on ? palette::active.withAlpha(0.28f) : palette::highlight;
        else if (isHighlighted)
            fill = fill.brighter(0.08f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, StepButton::cornerRadius);
        if (on)
        {
            g.setColour(palette::active.withAlpha(0.7f));
            g.drawRoundedRectangle(bounds, StepButton::cornerRadius, 1.0f);
        }

        g.setColour(on ? palette::active : palette::level);
        g.setFont(autoFont(fontHeight_));
        g.drawText("AUTO", bounds, juce::Justification::centred, false);
    }

private:
    float fontHeight_ = 14.0f;
};

GainControl::GainControl()
    : minus_(std::make_unique<StepButton>("Decrease gain", -1))
    , plus_(std::make_unique<StepButton>("Increase gain", 1))
    , auto_(std::make_unique<AutoButton>())
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Display gain");
    setTooltip("Display gain. Drag, scroll or use + and -; double-click resets to 0 dB.");

    minus_->onClick = [this] { request(gainDb_ - 1); };
    plus_->onClick = [this] { request(gainDb_ + 1); };
    auto_->onClick = [this]
    {
        if (onAutoChange)
            onAutoChange(!isAuto_);
    };
    addAndMakeVisible(*minus_);
    addAndMakeVisible(*plus_);
    addAndMakeVisible(*auto_);
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
    auto_->setToggleState(isAuto, juce::dontSendNotification);
    setTooltip(isAuto_ ? "Auto gain. Drag, scroll or use + and - to set the gain by hand, which "
                         "turns auto gain off."
                       : "Display gain. Drag, scroll or use + and -; double-click resets to 0 dB.");
    repaint();
}

void GainControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    auto_->setFontHeight(fontHeight);
    resized();
    repaint();
}

int GainControl::preferredWidth(int height) const
{
    return 2 * height + valueWidth() + autoGap + autoWidth(height);
}

juce::String GainControl::format(int gainDb, bool isAuto)
{
    return (isAuto ? "AUTO " : "") + signedDb(gainDb) + " dB";
}

void GainControl::paint(juce::Graphics& g)
{
    const auto control = valueArea_.getUnion(minus_->getBounds()).getUnion(plus_->getBounds());
    g.setColour(palette::surface);
    g.fillRoundedRectangle(control.toFloat().reduced(0.5f), StepButton::cornerRadius);

    controlText::draw(g, valueArea_, "GAIN", format(gainDb_), palette::textDim,
                      isAuto_ ? palette::active : palette::text, fontHeight_);
}

void GainControl::resized()
{
    auto area = getLocalBounds();
    const auto height = area.getHeight();
    auto_->setBounds(area.removeFromRight(autoWidth(height)));
    area.removeFromRight(autoGap);
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

int GainControl::valueWidth() const
{
    return std::max(controlText::widestText(controlText::valueFont(fontHeight_), {"+18 dB"}),
                    controlText::widestText(controlText::captionFont(fontHeight_), {"GAIN"})) +
           16;
}

int GainControl::autoWidth(int height) const
{
    return std::max(height,
                    controlText::widestText(autoFont(fontHeight_), {"AUTO"}) + 2 * autoPadding);
}

} // namespace visona
