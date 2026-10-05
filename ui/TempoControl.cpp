#include "TempoControl.h"

#include "ControlText.h"
#include "Palette.h"
#include "StepButton.h"

#include <visona/SweepWindow.h>

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

constexpr double fineStep = 0.1;
constexpr float dragPixelsPerStep = 2.0f;
constexpr float smoothWheelPerStep = 0.02f;

/** Whole BPM a hair either side of a value count as that value, despite rounding. */
constexpr double wholeTolerance = 1.0e-6;

} // namespace

TempoControl::TempoControl()
    : minus_(std::make_unique<StepButton>("Decrease tempo", -1))
    , plus_(std::make_unique<StepButton>("Increase tempo", 1))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Tempo");

    minus_->onClick = [this] { request(std::ceil(bpm_ - wholeTolerance) - 1.0); };
    plus_->onClick = [this] { request(std::floor(bpm_ + wholeTolerance) + 1.0); };
    addAndMakeVisible(*minus_);
    addAndMakeVisible(*plus_);
    setEditable(true);
}

TempoControl::~TempoControl() = default;

void TempoControl::setBpm(double bpm)
{
    if (juce::exactlyEqual(bpm, bpm_))
        return;
    bpm_ = bpm;
    updateButtons();
    repaint(valueArea_);
}

void TempoControl::setEditable(bool editable)
{
    if (editable == editable_)
        return;
    editable_ = editable;
    setTooltip(editable ? "Tempo of the free-running sweep. The buttons step whole BPM; drag or "
                          "scroll to fine-tune."
                        : "The tempo from MIDI Clock. Click STOPPED to run free at it, and set it "
                          "here.");
    setMouseCursor(editable ? juce::MouseCursor::UpDownResizeCursor
                            : juce::MouseCursor::NormalCursor);
    updateButtons();
    repaint();
}

void TempoControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    resized();
    repaint();
}

int TempoControl::preferredWidth(int height) const
{
    return 2 * height + valueWidth();
}

void TempoControl::paint(juce::Graphics& g)
{
    const auto control = valueArea_.getUnion(minus_->getBounds()).getUnion(plus_->getBounds());
    g.setColour(palette::surface);
    g.fillRoundedRectangle(control.toFloat().reduced(0.5f), StepButton::cornerRadius);

    controlText::draw(g, valueArea_, "BPM",
                      bpm_ > 0.0 ? juce::String(bpm_, 1) : juce::String::fromUTF8("\xe2\x80\x93"),
                      palette::textDim, editable_ ? palette::text : palette::textDim, fontHeight_);
}

void TempoControl::resized()
{
    auto area = getLocalBounds();
    const auto height = area.getHeight();
    minus_->setBounds(area.removeFromLeft(height));
    valueArea_ = area.removeFromLeft(valueWidth());
    plus_->setBounds(area.removeFromLeft(height));
}

void TempoControl::mouseDown(const juce::MouseEvent&)
{
    dragStartBpm_ = bpm_;
}

void TempoControl::mouseDrag(const juce::MouseEvent& event)
{
    const auto offset = event.getOffsetFromDragStart().toFloat();
    const auto steps = std::round((offset.x - offset.y) / dragPixelsPerStep);
    request(dragStartBpm_ + static_cast<double>(steps) * fineStep);
}

void TempoControl::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta =
        (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY) *
        (wheel.isReversed ? -1.0f : 1.0f);
    if (!wheel.isSmooth)
    {
        if (delta != 0.0f)
            request(bpm_ + (delta > 0.0f ? fineStep : -fineStep));
        return;
    }

    wheelAccumulator_ += delta;
    const auto steps = static_cast<int>(wheelAccumulator_ / smoothWheelPerStep);
    if (steps != 0)
    {
        wheelAccumulator_ -= static_cast<float>(steps) * smoothWheelPerStep;
        request(bpm_ + steps * fineStep);
    }
}

void TempoControl::request(double bpm)
{
    if (!editable_)
        return;
    bpm = clampFreeBpm(bpm);
    if (!juce::exactlyEqual(bpm, bpm_) && onTempoChange)
        onTempoChange(bpm);
}

void TempoControl::updateButtons()
{
    minus_->setEnabled(editable_ && bpm_ > minFreeBpm);
    plus_->setEnabled(editable_ && bpm_ < maxFreeBpm);
}

int TempoControl::valueWidth() const
{
    return std::max(controlText::widestText(controlText::valueFont(fontHeight_), {"300.0"}),
                    controlText::widestText(controlText::captionFont(fontHeight_), {"BPM"})) +
           16;
}

} // namespace visona
