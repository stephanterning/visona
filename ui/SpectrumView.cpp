#include "SpectrumView.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace visona
{
namespace
{
// Blend directly into opaque RGB storage. Avoiding an alpha image also makes the
// final X11 screen copy cheaper. ARM64 processes eight full-resolution pixels at once.
void blendFillRow(juce::PixelRGB* pixels, const juce::uint32* colours, const float* tops,
                  const float* fades, int count, float row, float peak,
                  const juce::PixelRGB* tail = nullptr, const float* ends = nullptr)
{
    int i = 0;
#if defined(__aarch64__) && defined(__ARM_NEON) && JUCE_LITTLE_ENDIAN && !JUCE_ANDROID
    for (; i + 8 <= count; i += 8)
    {
        if (tail != nullptr &&
            vminvq_f32(vsubq_f32(vdupq_n_f32(row + 0.5f), vld1q_f32(ends + i))) >= 0 &&
            vminvq_f32(vsubq_f32(vdupq_n_f32(row + 0.5f), vld1q_f32(ends + i + 4))) >= 0)
        {
            vst3_u8(reinterpret_cast<juce::uint8*>(pixels + i),
                    vld3_u8(reinterpret_cast<const juce::uint8*>(tail + i)));
            continue;
        }
        const auto d0 = vsubq_f32(vdupq_n_f32(row + 0.5f), vld1q_f32(tops + i));
        const auto d1 = vsubq_f32(vdupq_n_f32(row + 0.5f), vld1q_f32(tops + i + 4));
        if (std::max(vmaxvq_f32(d0), vmaxvq_f32(d1)) <= -0.5f)
            continue;
        const auto alphaFor = [peak](float32x4_t distance, const float* fade)
        {
            const auto coverage = vminq_f32(
                vdupq_n_f32(1), vmaxq_f32(vdupq_n_f32(0), vaddq_f32(distance, vdupq_n_f32(0.5f))));
            const auto alpha = vmaxq_f32(
                vdupq_n_f32(0.015f),
                vsubq_f32(vdupq_n_f32(peak),
                          vmulq_f32(vmaxq_f32(distance, vdupq_n_f32(0)), vld1q_f32(fade))));
            return vmovn_u32(vcvtq_u32_f32(vmulq_n_f32(vmulq_f32(alpha, coverage), 256)));
        };
        const auto alpha = vcombine_u16(alphaFor(d0, fades + i), alphaFor(d1, fades + i + 4));
        const auto inverse = vsubq_u16(vdupq_n_u16(256), alpha);
        const auto source = vld4_u8(reinterpret_cast<const juce::uint8*>(colours + i));
        auto destination = vld3_u8(reinterpret_cast<const juce::uint8*>(pixels + i));
        for (int channel = 0; channel < 3; ++channel)
            destination.val[channel] =
                vshrn_n_u16(vaddq_u16(vmulq_u16(vmovl_u8(source.val[channel]), alpha),
                                      vmulq_u16(vmovl_u8(destination.val[channel]), inverse)),
                            8);
        vst3_u8(reinterpret_cast<juce::uint8*>(pixels + i), destination);
    }
#endif
    for (; i < count; ++i)
    {
        if (tail != nullptr && row + 0.5f >= ends[i])
        {
            pixels[i] = tail[i];
            continue;
        }
        const auto distance = row + 0.5f - tops[i];
        const auto coverage = std::clamp(distance + 0.5f, 0.0f, 1.0f);
        const auto opacity = std::max(0.015f, peak - std::max(0.0f, distance) * fades[i]);
        const auto alpha = static_cast<juce::uint32>(opacity * coverage * 256.0f);
        const auto mix = [alpha](juce::uint32 source, juce::uint32 destination)
        { return static_cast<juce::uint8>((source * alpha + destination * (256 - alpha)) >> 8); };
        pixels[i].setARGB(255, mix((colours[i] >> 16) & 255, pixels[i].getRed()),
                          mix((colours[i] >> 8) & 255, pixels[i].getGreen()),
                          mix(colours[i] & 255, pixels[i].getBlue()));
    }
}
juce::Colour colourAt(double position, int colour)
{
    if (colour == 1)
        return juce::Colour(0xffd9dfe6);
    if (colour == 2)
        return juce::Colour(0xff70bfff);
    if (colour == 3)
        return juce::Colour(0xffffb772);
    const std::array<juce::Colour, 4> colours{juce::Colour(0xffcadb80), juce::Colour(0xff72e3ac),
                                              juce::Colour(0xff6fc9dc), juce::Colour(0xffa09ce9)};
    const auto scaled = std::clamp(position, 0.0, 1.0) * 3.0;
    const auto index = std::min(2, static_cast<int>(scaled));
    return colours[static_cast<std::size_t>(index)].interpolatedWith(
        colours[static_cast<std::size_t>(index + 1)], static_cast<float>(scaled - index));
}
juce::String frequencyText(double hz)
{
    return hz >= 1000.0 ? juce::String(hz / 1000.0, 2) + " kHz" : juce::String(hz, 1) + " Hz";
}
} // namespace
SpectrumView::SpectrumView(SpectrumAnalyzer& analyzer)
    : analyzer_(analyzer)
    , waveformColour_(palette::waveformColours[palette::defaultWaveformColour].colour)
{
    setOpaque(true);
    addChildComponent(settingsPanel_);
    settingsPanel_.addAndMakeVisible(title_);
    settingsPanel_.addAndMakeVisible(close_);
    settingsPanel_.addAndMakeVisible(clear_);
    settingsPanel_.addAndMakeVisible(viewport_);
    title_.setText("Spectrum settings", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    close_.onClick = [this] { showSettings(false); };
    clear_.onClick = [this] { analyzer_.clearHold(); };
    viewport_.setViewedComponent(&settingsContent_, false);
    viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
    const std::array<juce::String, 16> names{"Channels", "Top",       "Range",    "Slope",
                                             "Curve 1",  "Detector",  "Response", "Smoothing",
                                             "Style",    "Colour",    "Curve 2",  "Detector",
                                             "Response", "Smoothing", "Style",    "Colour"};
    for (std::size_t i = 0; i < choices_.size(); ++i)
    {
        settingsContent_.addAndMakeVisible(labels_[i]);
        settingsContent_.addAndMakeVisible(choices_[i]);
        labels_[i].setText(names[i], juce::dontSendNotification);
        labels_[i].setColour(juce::Label::textColourId, palette::textDim);
        choices_[i].setTitle(names[i]);
        choices_[i].onChange = [this] { applySettings(); };
    }
    choices_[0].addItemList({"Stereo average", "Left", "Right", "Mid (L+R)/2", "Side (L-R)/2"}, 1);
    choices_[1].addItemList(
        {"+12 dB", "+6 dB", "0 dB", "-12 dB", "-24 dB", "-36 dB", "-48 dB", "-60 dB"}, 1);
    choices_[2].addItemList({"30 dB", "60 dB", "90 dB", "120 dB"}, 1);
    choices_[3].addItemList(
        {"None", "1 dB/oct", "2 dB/oct", "3 dB/oct", "4 dB/oct", "5 dB/oct", "6 dB/oct"}, 1);
    for (std::size_t base : {4U, 10U})
    {
        choices_[base].addItemList({"Visible", "Hidden"}, 1);
        choices_[base + 1].addItemList({"Peak", "RMS"}, 1);
        choices_[base + 2].addItemList(
            {"Fast (150 ms)", "Medium (500 ms)", "Slow (1.5 s)", "Hold (Peak)"}, 1);
        choices_[base + 3].addItemList(
            {"Raw", "1/24 octave", "1/12 octave", "1/6 octave", "1/3 octave", "1 octave"}, 1);
        choices_[base + 4].addItemList({"Line", "Fill", "Solid"}, 1);
        choices_[base + 5].addItemList({"Spectrum", "White", "Blue", "Orange"}, 1);
    }
    restoreSettings({});
}
SpectrumView::~SpectrumView()
{
    stopTimer();
    analyzer_.setEnabled(false);
}
void SpectrumView::setActive(bool active)
{
    active_ = active;
    stats_ = {};
    statsStart_ = juce::Time::getMillisecondCounterHiRes();
    renderTotal_ = renderMax_ = 0;
    renderedFrames_ = paintedFrames_ = 0;
    lastPaintMs_ = paintTotal_ = paintMax_ = 0;
    analyzer_.setEnabled(active);
    setVisible(active);
    if (active)
    {
        startTimerHz(60);
        timerCallback();
    }
    else
    {
        stopTimer();
        showSettings(false);
    }
}
void SpectrumView::Panel::paint(juce::Graphics& g)
{
    g.fillAll(palette::chrome);
    g.setColour(palette::outline);
    g.drawRect(getLocalBounds(), 1);
}
void SpectrumView::setWaveformColour(juce::Colour colour)
{
    if (waveformColour_ == colour)
        return;
    waveformColour_ = colour;
    backgroundDirty_ = true;
    render();
    repaint();
}
void SpectrumView::showSettings(bool show)
{
    settingsPanel_.setVisible(show);
    if (show)
        settingsPanel_.toFront(true);
    resized();
    repaint();
}
juce::String SpectrumView::saveSettings() const
{
    juce::StringArray values;
    for (auto n : {settings_.channel, settings_.topDb, settings_.rangeDb, settings_.slope})
        values.add(juce::String(n));
    for (const auto& c : settings_.curves)
        for (auto n : {static_cast<int>(c.enabled), static_cast<int>(c.rms), c.speed, c.smoothing,
                       c.style, c.colour})
            values.add(juce::String(n));
    return values.joinIntoString(",");
}
void SpectrumView::restoreSettings(const juce::String& text)
{
    auto values = juce::StringArray::fromTokens(text, ",", "");
    if (values.size() == 16)
    {
        settings_.channel = values[0].getIntValue();
        settings_.topDb = values[1].getIntValue();
        settings_.rangeDb = values[2].getIntValue();
        settings_.slope = values[3].getIntValue();
        for (std::size_t c = 0; c < 2; ++c)
        {
            auto& s = settings_.curves[c];
            const auto b = 4 + static_cast<int>(c) * 6;
            s.enabled = values[b].getIntValue() != 0;
            s.rms = values[b + 1].getIntValue() != 0;
            s.speed = values[b + 2].getIntValue();
            s.smoothing = values[b + 3].getIntValue();
            s.style = values[b + 4].getIntValue();
            s.colour = values[b + 5].getIntValue();
        }
    }
    settings_.sanitize();
    backgroundDirty_ = true;
    choices_[0].setSelectedItemIndex(settings_.channel, juce::dontSendNotification);
    const std::array<int, 8> tops{12, 6, 0, -12, -24, -36, -48, -60};
    auto nearest = 2;
    for (std::size_t i = 0; i < tops.size(); ++i)
        if (std::abs(tops[i] - settings_.topDb) <
            std::abs(tops[static_cast<std::size_t>(nearest)] - settings_.topDb))
            nearest = static_cast<int>(i);
    choices_[1].setSelectedItemIndex(nearest, juce::dontSendNotification);
    choices_[2].setSelectedItemIndex(settings_.rangeDb / 30 - 1, juce::dontSendNotification);
    choices_[3].setSelectedItemIndex(settings_.slope, juce::dontSendNotification);
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto& s = settings_.curves[c];
        const auto b = 4 + c * 6;
        const std::array<int, 6> indices{
            !s.enabled, s.rms, s.speed, s.smoothing, s.style == 3 ? 2 : s.style, s.colour};
        auto& response = choices_[b + 2];
        response.clear(juce::dontSendNotification);
        response.addItemList({"Fast (150 ms)", "Medium (500 ms)", "Slow (1.5 s)"}, 1);
        if (!s.rms)
            response.addItem("Hold (Peak)", 4);
        auto& colour = choices_[b + 5];
        colour.clear(juce::dontSendNotification);
        if (s.style == 3)
            colour.addItem("Waveform colour", 1);
        else
            colour.addItemList({"Spectrum", "White", "Blue", "Orange"}, 1);
        for (std::size_t i = 0; i < indices.size(); ++i)
            choices_[b + i].setSelectedItemIndex(i == 5 && s.style == 3 ? 0 : indices[i],
                                                 juce::dontSendNotification);
        colour.setEnabled(s.style != 3);
    }
    if (snapshot_.analysisKey != settings_.analysisKey())
        snapshot_.valid = false;
    analyzer_.setSettings(settings_);
    render();
    repaint();
}
void SpectrumView::applySettings()
{
    const std::array<int, 8> tops{12, 6, 0, -12, -24, -36, -48, -60};
    settings_.channel = choices_[0].getSelectedItemIndex();
    settings_.topDb =
        tops[static_cast<std::size_t>(std::max(0, choices_[1].getSelectedItemIndex()))];
    settings_.rangeDb = (choices_[2].getSelectedItemIndex() + 1) * 30;
    settings_.slope = choices_[3].getSelectedItemIndex();
    for (std::size_t c = 0; c < 2; ++c)
    {
        auto& s = settings_.curves[c];
        const auto b = 4 + c * 6;
        s.enabled = choices_[b].getSelectedItemIndex() == 0;
        s.rms = choices_[b + 1].getSelectedItemIndex() == 1;
        s.speed = choices_[b + 2].getSelectedItemIndex();
        s.smoothing = choices_[b + 3].getSelectedItemIndex();
        if (s.style != 3)
            s.colour = choices_[b + 5].getSelectedItemIndex();
        const auto style = choices_[b + 4].getSelectedItemIndex();
        s.style = style == 2 ? 3 : style;
    }
    settings_.sanitize();
    restoreSettings(saveSettings());
    if (onSettingsChange)
        onSettingsChange();
}
juce::Rectangle<float> SpectrumView::plot() const
{
    return getLocalBounds()
        .toFloat()
        .withTrimmedLeft(52)
        .withTrimmedRight(18)
        .withTrimmedTop(52)
        .withTrimmedBottom(32);
}
float SpectrumView::xAt(double hz) const
{
    const auto maxHz = snapshot_.valid ? snapshot_.maxFrequency : 20000.0;
    return plot().getX() +
           plot().getWidth() * static_cast<float>(std::log(hz / 20.0) / std::log(maxHz / 20.0));
}
double SpectrumView::frequencyAt(float x) const
{
    const auto maxHz = snapshot_.valid ? snapshot_.maxFrequency : 20000.0;
    return 20.0 *
           std::pow(maxHz / 20.0,
                    static_cast<double>(std::clamp(
                        (x - plot().getX()) / std::max(1.0f, plot().getWidth()), 0.0f, 1.0f)));
}
float SpectrumView::yAt(float db) const
{
    return plot().getY() + (static_cast<float>(settings_.topDb) - db) /
                               static_cast<float>(settings_.rangeDb) * plot().getHeight();
}
float SpectrumView::shownDb(std::size_t curve, std::size_t point) const
{
    // Display points are already logarithmically spaced; avoid a pow/log round trip.
    const auto octaves = std::log2(20.0 / 1000.0) + static_cast<double>(point) /
                                                        static_cast<double>(spectrumPoints - 1) *
                                                        std::log2(snapshot_.maxFrequency / 20.0);
    return snapshot_.db[curve][point] + static_cast<float>(settings_.slope * octaves);
}
void SpectrumView::renderBackground(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    const auto area = plot();
    g.setFont(12.0f);
    for (int db = settings_.topDb; db >= settings_.topDb - settings_.rangeDb; db -= 10)
    {
        const auto y = yAt(static_cast<float>(db));
        g.setColour(juce::Colour(0xff30383f).withAlpha(db % 20 == 0 ? 0.65f : 0.35f));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
        g.setColour(palette::textDim);
        g.drawText(juce::String(db), 0, juce::roundToInt(y) - 8, 43, 16,
                   juce::Justification::centredRight);
    }
    for (int hz : {20, 30, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000, 20000})
    {
        const auto x = xAt(hz);
        if (x > area.getRight() + 1)
            continue;
        g.setColour(juce::Colour(0xff30383f).withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        // Keep labels legible on smaller windows.
        if (area.getWidth() < 800 && (hz == 30 || hz == 300 || hz == 3000))
            continue;
        g.setColour(palette::textDim);
        const auto label = hz >= 1000 ? juce::String(hz / 1000) + "k" : juce::String(hz);
        g.drawText(label, juce::roundToInt(x) - 22, juce::roundToInt(area.getBottom()) + 8, 44, 18,
                   juce::Justification::centred);
    }
    g.setColour(palette::text);
    g.setFont(16.0f);
    g.drawText("SPECTRUM (FFT)", 18, 8, 180, 24, juce::Justification::centredLeft);
    g.setColour(palette::textDim);
    g.setFont(12.0f);
    const auto heading =
        choices_[0].getText() + "  ·  " + juce::String(settings_.slope) + " dB/oct at 1 kHz";
    g.drawText(heading, 208, 8, std::max(0, getWidth() - 220), 24,
               juce::Justification::centredRight);
    juce::String legend;
    for (std::size_t c = 0; c < 2; ++c)
        if (settings_.curves[c].enabled)
            legend += (legend.isEmpty() ? "" : "    /    ") + juce::String("C") +
                      juce::String(static_cast<int>(c + 1)) + " " + choices_[5 + c * 6].getText() +
                      " · " + choices_[6 + c * 6].getText() + " · " + choices_[7 + c * 6].getText();
    g.drawText(legend, 52, 30, std::max(0, getWidth() - 70), 18, juce::Justification::centredLeft);
    if (!snapshot_.valid)
    {
        g.drawText("Waiting for audio", area.toNearestInt(), juce::Justification::centred);
        return;
    }
}
void SpectrumView::render()
{
    if (getWidth() < 80 || getHeight() < 80)
        return;
    const auto pixelWidth = juce::roundToInt(static_cast<float>(getWidth()) * renderScale_);
    const auto pixelHeight = juce::roundToInt(static_cast<float>(getHeight()) * renderScale_);
    if (cached_.getWidth() != pixelWidth || cached_.getHeight() != pixelHeight)
    {
        // Explicit software storage makes bitmap writes and Graphics drawing coherent on
        // every platform, including CoreGraphics-backed desktops.
        cached_ = juce::Image(juce::Image::RGB, pixelWidth, pixelHeight, false,
                              juce::SoftwareImageType());
        background_ = juce::Image(juce::Image::RGB, pixelWidth, pixelHeight, false,
                                  juce::SoftwareImageType());
        backgroundDirty_ = true;
    }
    const bool fullRedraw =
        backgroundDirty_ || backgroundValid_ != snapshot_.valid ||
        !juce::approximatelyEqual(backgroundMaxFrequency_, snapshot_.maxFrequency);
    if (fullRedraw)
    {
        juce::Graphics backgroundGraphics(background_);
        backgroundGraphics.addTransform(juce::AffineTransform::scale(renderScale_));
        renderBackground(backgroundGraphics);
        backgroundDirty_ = false;
        backgroundValid_ = snapshot_.valid;
        backgroundMaxFrequency_ = snapshot_.maxFrequency;
    }
    const auto area = plot();
    std::array<std::array<float, spectrumPoints>, 2> heights;
    auto curveTop = area.getBottom();
    const auto slopeStart = static_cast<float>(settings_.slope * std::log2(20.0 / 1000.0));
    const auto slopeStep =
        static_cast<float>(settings_.slope * std::log2(snapshot_.maxFrequency / 20.0) /
                           static_cast<double>(spectrumPoints - 1));
    if (snapshot_.valid)
        for (std::size_t c = 0; c < 2; ++c)
            if (settings_.curves[c].enabled)
                for (std::size_t i = 0; i < spectrumPoints; ++i)
                {
                    heights[c][i] =
                        yAt(snapshot_.db[c][i] + slopeStart + slopeStep * static_cast<float>(i));
                    curveTop = std::min(curveTop, heights[c][i] - 5.0f);
                }
    repaintRegion_ =
        fullRedraw ? getLocalBounds()
                   : area.withTop(std::max(area.getY(), std::min(previousCurveTop_, curveTop)))
                         .getSmallestIntegerContainer();
    previousCurveTop_ = curveTop;
    const auto pixelsToRestore = repaintRegion_.toFloat()
                                     .transformedBy(juce::AffineTransform::scale(renderScale_))
                                     .getSmallestIntegerContainer()
                                     .getIntersection(cached_.getBounds());
    {
        juce::Image::BitmapData source(background_, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData target(cached_, juce::Image::BitmapData::writeOnly);
        for (int y = pixelsToRestore.getY(); y < pixelsToRestore.getBottom(); ++y)
            std::memcpy(target.getPixelPointer(pixelsToRestore.getX(), y),
                        source.getPixelPointer(pixelsToRestore.getX(), y),
                        static_cast<std::size_t>(pixelsToRestore.getWidth() * source.pixelStride));
    }
    if (!snapshot_.valid)
        return;
    juce::Graphics g(cached_);
    g.addTransform(juce::AffineTransform::scale(renderScale_));
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto& config = settings_.curves[c];
        if (!config.enabled)
            continue;
        juce::Path line;
        for (std::size_t i = 0; i < spectrumPoints; ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(spectrumPoints - 1);
            const auto x = area.getX() + area.getWidth() * t;
            const auto y = std::clamp(heights[c][i], area.getY() - 4, area.getBottom() + 4);
            if (i == 0)
                line.startNewSubPath(x, y);
            else
                line.lineTo(x, y);
        }
        if (config.style == 3)
        {
            auto filled = line;
            filled.lineTo(area.getRight(), area.getBottom());
            filled.lineTo(area.getX(), area.getBottom());
            filled.closeSubPath();
            // A single flat fill avoids per-pixel fade calculations and glow strokes.
            g.setColour(waveformColour_.withAlpha(0.30f));
            g.fillPath(filled);
        }
        else if (config.style != 0)
        {
            // Rasterize the same vertical fades in contiguous rows. This replaces one
            // Graphics gradient/clip setup per column without reducing resolution.
            const auto left = juce::roundToInt(area.getX() * renderScale_);
            const auto right = juce::roundToInt(area.getRight() * renderScale_);
            const auto top = juce::roundToInt(area.getY() * renderScale_);
            const auto bottom = juce::roundToInt(area.getBottom() * renderScale_);
            const auto columns = std::max(1, right - left);
            fillTops_.resize(static_cast<std::size_t>(columns));
            fillFades_.resize(static_cast<std::size_t>(columns));
            fillEnds_.resize(static_cast<std::size_t>(columns));
            fillColours_.resize(static_cast<std::size_t>(columns));
            constexpr auto peakAlpha = 0.3f;
            for (int column = 0; column < columns; ++column)
            {
                const auto t = static_cast<float>(column) / static_cast<float>(columns);
                const auto bin = t * static_cast<float>(spectrumPoints - 1);
                const auto a = static_cast<std::size_t>(bin);
                const auto b = std::min(a + 1, spectrumPoints - 1);
                const auto index = static_cast<std::size_t>(column);
                fillTops_[index] = std::clamp(heights[c][a] + (heights[c][b] - heights[c][a]) *
                                                                  (bin - static_cast<float>(a)),
                                              area.getY() - 4, area.getBottom()) *
                                   renderScale_;
                const auto fadeHeight = std::min(static_cast<float>(bottom) - fillTops_[index],
                                                 area.getHeight() * renderScale_);
                fillEnds_[index] = fillTops_[index] + fadeHeight;
                fillFades_[index] = (peakAlpha - 0.015f) / std::max(1.0f, fadeHeight);
                fillColours_[index] = colourAt(t, config.colour).getPixelARGB().getNativeARGB();
            }
            // Curve 1's faint tail depends only on the static backdrop and colour.
            // Cache it, then copy those pixels instead of recomputing a flat blend.
            // Later curves must blend over earlier ones, so use the general path there.
            if (c == 0 && (fullRedraw || !tailBackground_.isValid()))
            {
                tailBackground_ = background_.createCopy();
                std::vector<float> flatTops(static_cast<std::size_t>(columns), -1.0f);
                std::vector<float> flatFades(static_cast<std::size_t>(columns), 0.0f);
                juce::Image::BitmapData tailPixels(tailBackground_,
                                                   juce::Image::BitmapData::readWrite);
                for (int row = top; row < bottom; ++row)
                    blendFillRow(
                        reinterpret_cast<juce::PixelRGB*>(tailPixels.getPixelPointer(left, row)),
                        fillColours_.data(), flatTops.data(), flatFades.data(), columns,
                        static_cast<float>(row), 0.015f);
            }
            juce::Image::BitmapData tailPixels(c == 0 ? tailBackground_ : background_,
                                               juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData pixels(cached_, juce::Image::BitmapData::readWrite);
            const auto firstFilledRow = std::max(top, static_cast<int>(std::floor(*std::min_element(
                                                          fillTops_.begin(), fillTops_.end()))));
            const auto tailStart =
                c == 0
                    ? std::clamp(static_cast<int>(std::ceil(
                                     *std::max_element(fillEnds_.begin(), fillEnds_.end()) - 0.5f)),
                                 firstFilledRow, bottom)
                    : bottom;
            // Below every fade, the entire row is static. Copy it in one operation
            // instead of checking fade boundaries for every eight pixels.
            for (int row = tailStart; row < bottom; ++row)
                std::memcpy(pixels.getPixelPointer(left, row),
                            tailPixels.getPixelPointer(left, row),
                            static_cast<std::size_t>(columns * pixels.pixelStride));
            for (int row = firstFilledRow; row < tailStart; ++row)
            {
                auto* pixel = reinterpret_cast<juce::PixelRGB*>(pixels.getPixelPointer(left, row));
                blendFillRow(pixel, fillColours_.data(), fillTops_.data(), fillFades_.data(),
                             columns, static_cast<float>(row), peakAlpha,
                             c == 0 ? reinterpret_cast<const juce::PixelRGB*>(
                                          tailPixels.getPixelPointer(left, row))
                                    : nullptr,
                             fillEnds_.data());
            }
        }

        juce::ColourGradient gradient(colourAt(0, config.colour), area.getX(), 0,
                                      colourAt(1, config.colour), area.getRight(), 0, false);
        gradient.addColour(1.0 / 3.0, colourAt(1.0 / 3.0, config.colour));
        gradient.addColour(2.0 / 3.0, colourAt(2.0 / 3.0, config.colour));
        if (config.style == 3)
            g.setColour(waveformColour_);
        else
            g.setGradientFill(gradient);
        g.strokePath(line, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));
    }
}
void SpectrumView::paint(juce::Graphics& g)
{
    struct Measurement
    {
        SpectrumView& view;
        double start = juce::Time::getMillisecondCounterHiRes();
        ~Measurement()
        {
            view.lastPaintMs_ = juce::Time::getMillisecondCounterHiRes() - start;
            view.paintTotal_ += view.lastPaintMs_;
            view.paintMax_ = std::max(view.paintMax_, view.lastPaintMs_);
            ++view.paintedFrames_;
        }
    } measurement{*this};
    const auto scale = std::clamp(g.getInternalContext().getPhysicalPixelScaleFactor(), 1.0f, 3.0f);
    if (!juce::approximatelyEqual(scale, renderScale_))
    {
        renderScale_ = std::clamp(scale, 1.0f, 3.0f);
        render();
    }
    if (cached_.isValid())
    {
        // The cache already matches physical display pixels; do not resample it again.
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed(cached_, juce::AffineTransform::scale(1.0f / renderScale_));
    }
    else
        g.fillAll(palette::background);
    if (cursor_ && !settingsVisible())
    {
        const auto area = plot();
        const auto x = cursorPosition_.x, y = cursorPosition_.y;
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
        const auto index =
            static_cast<std::size_t>(std::clamp((x - area.getX()) / area.getWidth(), 0.0f, 1.0f) *
                                     static_cast<float>(spectrumPoints - 1));
        auto label = frequencyText(frequencyAt(x));
        if (snapshot_.valid)
            for (std::size_t c = 0; c < 2; ++c)
                if (settings_.curves[c].enabled)
                    label += "\nC" + juce::String(static_cast<int>(c + 1)) + ": " +
                             juce::String(shownDb(c, index), 1) + " dB";
        if (settings_.slope != 0)
            label += "\nSlope applied";
        auto box = juce::Rectangle<float>(x + 16, y - 90, 154, 90).constrainedWithin(area);
        g.setColour(palette::chrome.withAlpha(0.96f));
        g.fillRoundedRectangle(box, 6);
        g.setColour(palette::text);
        g.setFont(13.0f);
        g.drawFittedText(label, box.toNearestInt().reduced(9), juce::Justification::centredLeft, 4);
    }
}
void SpectrumView::resized()
{
    backgroundDirty_ = true;
    settingsPanel_.setBounds(getLocalBounds().reduced(10));
    auto area = settingsPanel_.getLocalBounds().reduced(14);
    auto header = area.removeFromTop(44);
    close_.setBounds(header.removeFromRight(72));
    header.removeFromRight(8);
    clear_.setBounds(header.removeFromRight(112));
    title_.setBounds(header);
    area.removeFromTop(10);
    viewport_.setBounds(area);
    const auto width = std::max(100, area.getWidth() - 16);
    const bool wide = width >= 680;
    settingsContent_.setSize(width, wide ? 8 * 58 : 16 * 58);
    for (std::size_t i = 0; i < choices_.size(); ++i)
    {
        const auto column = wide ? static_cast<int>(i % 2) : 0;
        const auto row = wide ? static_cast<int>(i / 2) : static_cast<int>(i);
        const auto cellWidth = wide ? width / 2 : width;
        auto cell = juce::Rectangle<int>(column * cellWidth, row * 58, cellWidth - 12, 50);
        labels_[i].setBounds(cell.removeFromLeft(std::min(104, cellWidth / 3)));
        choices_[i].setBounds(cell);
    }
    render();
    repaint();
}
void SpectrumView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - statsStart_ >= 1000.0)
    {
        stats_ = {1000.0 * renderedFrames_ / (now - statsStart_),
                  renderedFrames_ > 0 ? renderTotal_ / renderedFrames_ : 0.0,
                  renderMax_,
                  cached_.getWidth(),
                  cached_.getHeight(),
                  renderScale_};
        stats_.paintMsAverage = paintedFrames_ > 0 ? paintTotal_ / paintedFrames_ : 0.0;
        stats_.paintMsMax = paintMax_;
        paintTotal_ = paintMax_ = 0;
        paintedFrames_ = 0;
        statsStart_ = now;
        renderTotal_ = renderMax_ = 0;
        renderedFrames_ = 0;
    }
    if (paused_ || settingsVisible())
        return;
    if (analyzer_.snapshots().fetch())
    {
        snapshot_ = analyzer_.snapshots().readBuffer();
        if (snapshot_.analysisKey != settings_.analysisKey())
            snapshot_.valid = false;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        render();
        const auto renderMs = juce::Time::getMillisecondCounterHiRes() - started;
        ++renderedFrames_;
        renderTotal_ += renderMs;
        renderMax_ = std::max(renderMax_, renderMs);
        // On ARM Linux, spend the cheaper solid fill's savings on CPU headroom rather
        // than additional frames. 25 Hz matches the Pi's previous live glow throughput.
#if JUCE_LINUX && defined(__aarch64__)
        const double targetHz = settings_.curves[0].style == 3 ? 25.0 : 30.0;
#else
        constexpr double targetHz = 30.0;
#endif
        // Budget both rendering and screen-copy time for UI responsiveness.
        // Restart after rendering so slow frames cannot queue overdue timer callbacks.
        startTimer(std::max(1, static_cast<int>(std::ceil(
                                   std::max(1000.0 / targetHz, 1.5 * (renderMs + lastPaintMs_))))));
        if (cursor_)
            repaint();
        else
            repaint(repaintRegion_);
    }
}
void SpectrumView::moveCursor(juce::Point<float> position)
{
    cursor_ = plot().contains(position);
    cursorPosition_ = position;
    repaint();
}
void SpectrumView::mouseMove(const juce::MouseEvent& e)
{
    moveCursor(e.position);
}
void SpectrumView::mouseDown(const juce::MouseEvent& e)
{
    moveCursor(e.position);
}
void SpectrumView::mouseDrag(const juce::MouseEvent& e)
{
    moveCursor(e.position);
}
void SpectrumView::mouseExit(const juce::MouseEvent& e)
{
    if (!e.source.isTouch())
    {
        cursor_ = false;
        repaint();
    }
}
bool SpectrumView::keyPressed(const juce::KeyPress& k)
{
    if (k.isKeyCode(juce::KeyPress::escapeKey))
    {
        showSettings(false);
        cursor_ = false;
        repaint();
        return true;
    }
    return false;
}
} // namespace visona
