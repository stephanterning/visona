#include "BandSpectrumView.h"

#include "Palette.h"

#include <cstring>
namespace visona
{
namespace
{
constexpr std::array<const char*, spectrumBands> labels{
    "20",    "25",  "31.5", "40",   "50",  "63",  "80",    "100",   "125",  "160", "200",
    "250",   "315", "400",  "500",  "630", "800", "1k",    "1.25k", "1.6k", "2k",  "2.5k",
    "3.15k", "4k",  "5k",   "6.3k", "8k",  "10k", "12.5k", "16k",   "20k"};
}
BandSpectrumView::BandSpectrumView(BandAnalyzer& analyzer)
    : analyzer_(analyzer)
    , colour_(palette::waveformColours[palette::defaultWaveformColour].colour)
{
    setOpaque(true);
}
BandSpectrumView::~BandSpectrumView()
{
    analyzer_.setEnabled(false);
}
void BandSpectrumView::setActive(bool active)
{
    setVisible(active);
    analyzer_.setEnabled(active);
    if (active)
    {
        start_ = juce::Time::getMillisecondCounterHiRes();
        frames_ = paints_ = 0;
        renderTotal_ = renderMax_ = paintTotal_ = paintMax_ = 0;
        startTimerHz(30);
    }
    else
        stopTimer();
}
juce::Rectangle<float> BandSpectrumView::plot() const
{
    return getLocalBounds()
        .toFloat()
        .withTrimmedLeft(48)
        .withTrimmedRight(16)
        .withTrimmedTop(54)
        .withTrimmedBottom(46);
}
void BandSpectrumView::resized()
{
    background_ = {};
    render();
    repaint();
}
void BandSpectrumView::render()
{
    if (getWidth() < 100 || getHeight() < 100)
        return;
    const int w = juce::roundToInt(static_cast<float>(getWidth()) * scale_),
              h = juce::roundToInt(static_cast<float>(getHeight()) * scale_);
    const auto area = plot();
    if (!background_.isValid() || background_.getWidth() != w || background_.getHeight() != h)
    {
        background_ = juce::Image(juce::Image::RGB, w, h, false, juce::SoftwareImageType());

        juce::Graphics g(background_);
        g.addTransform(juce::AffineTransform::scale(scale_));
        g.fillAll(juce::Colours::black);
        g.setColour(palette::text);
        g.setFont(16);
        g.drawText("SPECTRUM (BAND)", 18, 8, 200, 24, juce::Justification::centredLeft);
        g.setColour(palette::textDim);
        g.setFont(12);
        g.drawText("1/3 octave | Stereo energy | Peak / 150 ms release | Markers: 1 s hold", 48, 31,
                   getWidth() - 64, 18, juce::Justification::centredLeft);
        for (int db = 0; db >= -60; db -= 10)
        {
            const float y = area.getY() - static_cast<float>(db) / 60.0f * area.getHeight();
            g.setColour(juce::Colour(0xff222222));
            g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
            g.setColour(palette::textDim);
            g.drawText(juce::String(db), 0, juce::roundToInt(y) - 8, 40, 16,
                       juce::Justification::centredRight);
        }
        const float width = area.getWidth() / spectrumBands;
        g.setFont(getWidth() < 900 ? 10.0f : 12.0f);
        for (std::size_t i = 0; i < spectrumBands; ++i)
            g.drawText(labels[i],
                       juce::Rectangle<float>(
                           area.getX() + (static_cast<float>(i) + 0.5f) * width - width,
                           area.getBottom() + 6 + static_cast<float>(i % 2) * 16, width * 2, 16),
                       juce::Justification::centred);
    }
}
void BandSpectrumView::drawBands(juce::Graphics& g)
{
    const auto area = plot();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    if (!snapshot_.valid)
    {
        g.setColour(palette::textDim);
        g.drawText("Waiting for audio", area, juce::Justification::centred);
        return;
    }
    const float width = area.getWidth() / spectrumBands;
    for (std::size_t i = 0; i < spectrumBands; ++i)
    {
        if (BandAnalyzer::frequency(i) * std::pow(10.0, 0.05) >= snapshot_.sampleRate * 0.5)
            continue;
        const auto level = std::clamp((snapshot_.db[i] + 60) / 60, 0.0f, 1.0f);
        const auto peak = std::clamp((snapshot_.peakDb[i] + 60) / 60, 0.0f, 1.0f);
        const float x = area.getX() + static_cast<float>(i) * width + 2;
        g.setColour(colour_.withMultipliedBrightness(0.75f));
        g.fillRect(x, area.getBottom() - level * area.getHeight(), width - 4,
                   level * area.getHeight());
        if (peak > 0)
        {
            g.setColour(palette::text);
            g.fillRect(x, std::max(area.getY(), area.getBottom() - peak * area.getHeight() - 1),
                       width - 4, 2.0f);
        }
    }
}
void BandSpectrumView::paint(juce::Graphics& g)
{
    const auto begin = juce::Time::getMillisecondCounterHiRes();
    const auto scale = std::clamp(g.getInternalContext().getPhysicalPixelScaleFactor(), 1.0f, 3.0f);
    if (!juce::approximatelyEqual(scale, scale_))
    {
        scale_ = scale;
        background_ = {};
        render();
    }
    if (background_.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed(background_, juce::AffineTransform::scale(1 / scale_));
    }
    else
        g.fillAll(juce::Colours::black);
    drawBands(g);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - begin;
    paintTotal_ += ms;
    paintMax_ = std::max(paintMax_, ms);
    ++paints_;
}
juce::RectangleList<int> BandSpectrumView::changedRegion(const BandSnapshot& previous) const
{
    juce::RectangleList<int> dirty;
    juce::Rectangle<int> bounds;
    const auto area = plot();
    if (!previous.valid || !snapshot_.valid ||
        !juce::approximatelyEqual(previous.sampleRate, snapshot_.sampleRate))
    {
        dirty.add(area.getSmallestIntegerContainer());
        return dirty;
    }
    const float width = area.getWidth() / spectrumBands;
    const auto yAt = [area](float db)
    { return area.getBottom() - std::clamp((db + 60) / 60, 0.0f, 1.0f) * area.getHeight(); };
    for (std::size_t i = 0; i < spectrumBands; ++i)
    {
        const auto x = area.getX() + static_cast<float>(i) * width;
        const auto before = yAt(previous.db[i]), after = yAt(snapshot_.db[i]);
        if (!juce::approximatelyEqual(before, after))
            bounds = bounds.getUnion(juce::Rectangle<float>(x, std::min(before, after) - 2, width,
                                                            std::abs(before - after) + 4)
                                         .getSmallestIntegerContainer());
        const auto oldPeak = yAt(previous.peakDb[i]), newPeak = yAt(snapshot_.peakDb[i]);
        if (!juce::approximatelyEqual(oldPeak, newPeak))
            for (const auto y : {oldPeak, newPeak})
                bounds = bounds.getUnion(
                    juce::Rectangle<float>(x, y - 3, width, 6).getSmallestIntegerContainer());
    }
    dirty.add(bounds.getIntersection(area.getSmallestIntegerContainer()));
    return dirty;
}
void BandSpectrumView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - start_ >= 1000)
    {
        stats_ = {frames_ * 1000.0 / (now - start_),
                  frames_ ? renderTotal_ / frames_ : 0,
                  renderMax_,
                  background_.getWidth(),
                  background_.getHeight(),
                  scale_,
                  paints_ ? paintTotal_ / paints_ : 0,
                  paintMax_};
        start_ = now;
        frames_ = paints_ = 0;
        renderTotal_ = renderMax_ = paintTotal_ = paintMax_ = 0;
    }
    if (paused_)
        return;
    if (analyzer_.snapshots().fetch())
    {
        const auto previous = snapshot_;
        snapshot_ = analyzer_.snapshots().readBuffer();
        const auto begin = juce::Time::getMillisecondCounterHiRes();
        render();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - begin;
        renderTotal_ += ms;
        renderMax_ = std::max(renderMax_, ms);
        ++frames_;
        for (const auto& region : changedRegion(previous))
            repaint(region);
        startTimer(std::max(33, static_cast<int>(std::ceil(ms * 2))));
    }
}
} // namespace visona
