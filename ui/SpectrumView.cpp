#include "SpectrumView.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace visona
{
namespace
{
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
        choices_[base + 4].addItemList({"Line", "Fill", "Glow"}, 1);
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
        const std::array<int, 6> indices{!s.enabled,  s.rms,   s.speed,
                                         s.smoothing, s.style, s.colour};
        for (std::size_t i = 0; i < indices.size(); ++i)
            choices_[b + i].setSelectedItemIndex(indices[i], juce::dontSendNotification);
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
        s.style = choices_[b + 4].getSelectedItemIndex();
        s.colour = choices_[b + 5].getSelectedItemIndex();
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
    const auto frequency =
        20.0 * std::pow(snapshot_.maxFrequency / 20.0,
                        static_cast<double>(point) / static_cast<double>(spectrumPoints - 1));
    return snapshot_.db[curve][point] +
           static_cast<float>(settings_.slope * std::log2(frequency / 1000.0));
}
void SpectrumView::render()
{
    if (getWidth() < 80 || getHeight() < 80)
        return;
    const auto pixelWidth = juce::roundToInt(static_cast<float>(getWidth()) * renderScale_);
    const auto pixelHeight = juce::roundToInt(static_cast<float>(getHeight()) * renderScale_);
    if (cached_.getWidth() != pixelWidth || cached_.getHeight() != pixelHeight)
        cached_ = juce::Image(juce::Image::RGB, pixelWidth, pixelHeight, false);
    juce::Graphics g(cached_);
    g.addTransform(juce::AffineTransform::scale(renderScale_));
    g.fillAll(juce::Colour(0xff171c20));
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
    g.drawText("SPECTRUM", 18, 8, 120, 24, juce::Justification::centredLeft);
    g.setColour(palette::textDim);
    g.setFont(12.0f);
    const auto heading =
        choices_[0].getText() + "  ·  " + juce::String(settings_.slope) + " dB/oct at 1 kHz";
    g.drawText(heading, 148, 8, std::max(0, getWidth() - 160), 24,
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
            const auto y = std::clamp(yAt(shownDb(c, i)), area.getY() - 4, area.getBottom() + 4);
            if (i == 0)
                line.startNewSubPath(x, y);
            else
                line.lineTo(x, y);
        }
        if (config.style != 0)
        {
            // Non-overlapping physical-pixel strips avoid alpha seams on Retina displays.
            const auto columns = std::max(1, juce::roundToInt(area.getWidth() * renderScale_));
            for (int column = 0; column < columns; ++column)
            {
                const auto t = static_cast<float>(column) / static_cast<float>(columns);
                const auto bin = t * static_cast<float>(spectrumPoints - 1);
                const auto a = static_cast<std::size_t>(bin);
                const auto b = std::min(a + 1, spectrumPoints - 1);
                const auto db =
                    shownDb(c, a) + (shownDb(c, b) - shownDb(c, a)) * (bin - static_cast<float>(a));
                const auto y = std::clamp(yAt(db), area.getY() - 4, area.getBottom());
                const auto x = area.getX() + static_cast<float>(column) / renderScale_;
                const auto colour = colourAt(t, config.colour);
                juce::ColourGradient fill(
                    colour.withAlpha(config.style == 2 ? 0.45f : 0.3f), x, y,
                    colour.withAlpha(0.015f), x,
                    std::min(area.getBottom(), y + (config.style == 2 ? 150.0f : area.getHeight())),
                    false);
                g.setGradientFill(fill);
                g.fillRect(x, y, 1.0f / renderScale_, area.getBottom() - y);
            }
        }

        juce::ColourGradient gradient(colourAt(0, config.colour), area.getX(), 0,
                                      colourAt(1, config.colour), area.getRight(), 0, false);
        gradient.addColour(1.0 / 3.0, colourAt(1.0 / 3.0, config.colour));
        gradient.addColour(2.0 / 3.0, colourAt(2.0 / 3.0, config.colour));
        if (config.style == 2)
        {
            auto glow = gradient;
            glow.multiplyOpacity(0.07f);
            g.setGradientFill(glow);
            g.strokePath(line, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved));
            g.strokePath(line, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved));
        }
        g.setGradientFill(gradient);
        g.strokePath(line, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));
    }
}
void SpectrumView::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!juce::approximatelyEqual(scale, renderScale_))
    {
        renderScale_ = std::clamp(scale, 1.0f, 3.0f);
        render();
    }
    if (cached_.isValid())
        g.drawImage(cached_, getLocalBounds().toFloat());
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
    if (paused_)
        return;
    if (analyzer_.snapshots().fetch())
    {
        snapshot_ = analyzer_.snapshots().readBuffer();
        if (snapshot_.analysisKey != settings_.analysisKey())
            snapshot_.valid = false;
        render();
        repaint();
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
