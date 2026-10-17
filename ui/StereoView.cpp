#include "StereoView.h"

#include "Palette.h"

#include <visona/StereoProjection.h>

#include <bit>
#include <cmath>
#include <cstring>

namespace visona
{
namespace
{
constexpr std::array<double, 4> tails{0, 0.5, 1.0, 2.0};
constexpr std::array<int, 11> gains{-12, -6, 0, 6, 12, 18, 24, 30, 36, 42, 48};
double now()
{
    return juce::Time::getMillisecondCounterHiRes();
}
juce::String readout(float value)
{
    if (std::abs(value) < 0.005f)
        value = 0;
    return (value > 0 ? "+" : "") + juce::String(value, 2);
}
} // namespace

StereoView::StereoView(StereoAnalyzer& analyzer)
    : analyzer_(analyzer)
    , colour_(palette::waveformColours[palette::defaultWaveformColour].colour)
{
    setOpaque(true);
    // Bin edges never depend on the audio, gain or window size.
    constexpr double step =
        juce::MathConstants<double>::pi / static_cast<double>(polarLevelBins - 1);
    for (std::size_t i = 0; i < polarLevelBins; ++i)
        for (std::size_t edge = 0; edge < 2; ++edge)
        {
            const double centre =
                -juce::MathConstants<double>::halfPi + static_cast<double>(i) * step;
            const double angle = std::clamp(centre + (static_cast<double>(edge) - 0.5) * step,
                                            -juce::MathConstants<double>::halfPi,
                                            juce::MathConstants<double>::halfPi);
            levelDirections_[i][edge] = {static_cast<float>(std::sin(angle)),
                                         static_cast<float>(std::cos(angle))};
        }
    for (std::size_t i = 0; i < smoothDirections_.size(); ++i)
    {
        const double angle =
            -juce::MathConstants<double>::halfPi + static_cast<double>(i) * step / 4;
        smoothDirections_[i] = {static_cast<float>(std::sin(angle)),
                                static_cast<float>(std::cos(angle))};
    }
    addChildComponent(levelProfile_);
    levelProfile_.addItem("Directional level", 1);
    levelProfile_.addItem("Energy contribution", 2);
    levelProfile_.setSelectedItemIndex(0, juce::dontSendNotification);
    levelProfile_.setTooltip("Directional RMS with sparse-direction attenuation, or the original "
                             "whole-window energy contribution. Fill: RMS; outline: peak.");
    levelProfile_.onChange = [this]
    {
        clearHistory();
        changed();
    };
    levelFill_.preallocateSpace(static_cast<int>(smoothDirections_.size() * 3 + 8));
    levelPeak_.preallocateSpace(static_cast<int>(smoothDirections_.size() * 3 + 8));
    addAndMakeVisible(polar_);
    addAndMakeVisible(lissajous_);
    addAndMakeVisible(level_);
    addAndMakeVisible(gainLabel_);
    addAndMakeVisible(gain_);
    addAndMakeVisible(persistence_);
    addAndMakeVisible(persistenceLabel_);
    persistenceLabel_.setText("Afterglow", juce::dontSendNotification);
    persistenceLabel_.setJustificationType(juce::Justification::centredRight);
    persistence_.addItem("Off", 1);
    persistence_.addItem("0.5 s", 2);
    persistence_.addItem("1 s", 3);
    persistence_.addItem("2 s", 4);
    persistence_.setSelectedItemIndex(2, juce::dontSendNotification);
    persistence_.setTooltip("Display-only decay time to 1%. Sample brightness fades; Level radii "
                            "shrink. Lissajous is unchanged.");
    persistence_.onChange = [this]
    {
        clearHistory();
        changed();
    };
    polar_.onClick = [this] { selectMode(0); };
    lissajous_.onClick = [this] { selectMode(1); };
    level_.onClick = [this] { selectMode(2); };
    level_.setTooltip("Angular RMS (fill) and peak (outline) over 100 ms, with "
                      "adjustable display release.");
    polar_.setTooltip("Folded sample view. Mono is vertical; opposite polarity reaches the sides.");
    lissajous_.setTooltip("Signed M/S view. Mono is vertical; opposite polarity is horizontal.");
    gainLabel_.setText("Display gain", juce::dontSendNotification);
    gainLabel_.setJustificationType(juce::Justification::centredRight);
    for (std::size_t i = 0; i < gains.size(); ++i)
        gain_.addItem(juce::String(gains[i]) + " dB", static_cast<int>(i + 1));
    gain_.setSelectedItemIndex(2, juce::dontSendNotification);
    gain_.setTooltip(
        "Common gain for both display channels. Measurements and signal threshold are unchanged.");
    gain_.onChange = [this]
    {
        clearHistory();
        changed();
    };
    polar_.setToggleState(true, juce::dontSendNotification);
}
StereoView::~StereoView()
{
    analyzer_.setEnabled(false);
}
void StereoView::setActive(bool active)
{
    setVisible(active);
    analyzer_.setEnabled(active);
    if (active)
    {
        analyzer_.snapshots().fetch(); // Discard a snapshot from a previous activation.
        clearHistory();
        snapshot_->count = 0;
        snapshot_->validity = StereoValidity::waiting;
        stale_ = true;
        lastAudio_ = start_ = now();
        frames_ = paints_ = 0;
        renderTotal_ = paintTotal_ = 0;
        stats_ = {};
        render();
        startTimerHz(refreshRate());
        repaint();
    }
    else
        stopTimer();
}
void StereoView::setPaused(bool paused)
{
    paused_ = paused;
    if (!paused)
        lastRender_ = lastAudio_ = now();
    repaint();
}
void StereoView::setWaveformColour(juce::Colour colour)
{
    colour_ = colour;
    for (auto& tile : historyTiles_)
        for (auto& point : tile.pixels)
            point.lastAlpha = -1;
    render();
    repaint();
}
int StereoView::refreshRate() const noexcept
{
    // JUCE's message-thread timer is not deadline-based. A nominal 60 Hz timer
    // leaves little room for dispatch jitter. Sample polls at 240 Hz and Level
    // at 120 Hz. Only fresh 120 Hz analysis snapshots generate frames.
    return mode_ == 0 ? 240 : mode_ == 2 ? 120 : 60;
}
void StereoView::selectMode(int mode)
{
    clearHistory();
    background_ = {};
    mode_ = std::clamp(mode, 0, 2);
    if (isTimerRunning())
        startTimerHz(refreshRate());
    polar_.setToggleState(mode_ == 0, juce::dontSendNotification);
    lissajous_.setToggleState(mode_ == 1, juce::dontSendNotification);
    level_.setToggleState(mode_ == 2, juce::dontSendNotification);
    persistence_.setEnabled(mode_ != 1);
    levelProfile_.setVisible(mode_ == 2);
    changed();
}
void StereoView::clearHistory()
{
    background_ = {};
    released_ = {};
    lastRender_ = now();
}
void StereoView::changed()
{
    render();
    repaint();
    if (onSettingsChange)
        onSettingsChange();
}
void StereoView::resetView()
{
    gain_.setSelectedItemIndex(2, juce::dontSendNotification);
    persistence_.setSelectedItemIndex(2, juce::dontSendNotification);
    levelProfile_.setSelectedItemIndex(0, juce::dontSendNotification);
    selectMode(0);
}
juce::String StereoView::saveSettings() const
{
    return juce::String(mode_) + "," + juce::String(gain_.getSelectedItemIndex()) + "," +
           juce::String(persistence_.getSelectedItemIndex()) + "," +
           juce::String(levelProfile_.getSelectedItemIndex());
}
void StereoView::restoreSettings(const juce::String& text)
{
    const auto fields = juce::StringArray::fromTokens(text, ",", "");
    if ((fields.size() < 2 || fields.size() > 4) || fields[0].containsOnly("012") == false ||
        fields[0].length() != 1 || fields[1].isEmpty() || !fields[1].containsOnly("0123456789"))
        return;
    const int gain = fields[1].getIntValue();
    if (gain < 0 || gain >= static_cast<int>(gains.size()))
        return;
    if (fields.size() >= 3 && (fields[2].length() != 1 || !fields[2].containsOnly("0123")))
        return;
    if (fields.size() == 4 && (fields[3].length() != 1 || !fields[3].containsOnly("01")))
        return;
    levelProfile_.setSelectedItemIndex(fields.size() == 4 ? fields[3].getIntValue() : 0,
                                       juce::dontSendNotification);
    persistence_.setSelectedItemIndex(fields.size() >= 3 ? fields[2].getIntValue() : 2,
                                      juce::dontSendNotification);
    gain_.setSelectedItemIndex(gain, juce::dontSendNotification);
    selectMode(fields[0].getIntValue());
}
juce::Rectangle<float> StereoView::plot() const
{
    const auto available =
        getLocalBounds().toFloat().withTrimmedTop(84).withTrimmedBottom(124).reduced(24, 4);
    const float width = std::max(
        1.0f, std::min(available.getWidth(), available.getHeight() * (mode_ != 1 ? 2.0f : 1.0f)));
    return available.withSizeKeepingCentre(width, mode_ != 1 ? width * 0.5f : width);
}
void StereoView::resized()
{
    levelProfile_.setBounds(16, 54, 190, 26);
    auto row = getLocalBounds().reduced(16).removeFromTop(36);
    polar_.setBounds(row.removeFromLeft(128));
    row.removeFromLeft(6);
    level_.setBounds(row.removeFromLeft(112));
    row.removeFromLeft(6);
    lissajous_.setBounds(row.removeFromLeft(108));
    gain_.setBounds(row.removeFromRight(90));
    gainLabel_.setBounds(row.removeFromRight(100));
    persistence_.setBounds(row.removeFromRight(84));
    persistenceLabel_.setBounds(row.removeFromRight(76));
    render();
}
juce::String StereoView::status() const
{
    if (stale_)
        return "Waiting for audio";
    switch (snapshot_->validity)
    {
    case StereoValidity::waiting:
        return "Measuring a complete 200 ms window";
    case StereoValidity::silent:
        return "No measurable signal";
    case StereoValidity::leftOnly:
        return "Right channel below threshold";
    case StereoValidity::rightOnly:
        return "Left channel below threshold";
    case StereoValidity::invalidInput:
        return "Invalid audio samples";
    case StereoValidity::valid:
        return "";
    }
    return {};
}
StereoPoint StereoView::cachedPolar(const StereoSample& sample, std::uint64_t frame)
{
    const auto index = static_cast<std::size_t>(frame % stereoHistoryCapacity);
    const auto key = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(sample.left)) |
                     (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(sample.right)) << 32);
    if (projectionKeys_[index] != key)
    {
        projectionKeys_[index] = key;
        projections_[index] = signalizerPolar(sample.left, sample.right);
    }
    return projections_[index];
}
void StereoView::render()
{
    const auto start = now();
    const double tail =
        mode_ == 1 ? 0 : tails[static_cast<std::size_t>(persistence_.getSelectedItemIndex())];
    decay_ = tail > 0
                 ? std::exp(-std::log(100.0) * std::max(0.0, start - lastRender_) / (1000 * tail))
                 : 0;
    lastRender_ = start;
    // Drop history across discontinuities and invalid streams; silence may fade naturally.
    if (stale_ || snapshot_->validity == StereoValidity::waiting ||
        snapshot_->validity == StereoValidity::invalidInput)
        decay_ = 0;
    damage_ = cloudBounds_;
    cloudBounds_ = {};
    const int width = std::max(4, juce::roundToInt(plot().getWidth()));
    const int height = mode_ != 1 ? width / 2 : width;
    if (!background_.isValid() || density_.getWidth() != width || density_.getHeight() != height)
    {
        background_ =
            juce::Image(juce::Image::RGB, width, height, false, juce::SoftwareImageType());
        juce::Graphics g(background_);
        g.fillAll(palette::background);
        const auto area = background_.getBounds().toFloat();
        const auto cx = area.getCentreX();
        const auto cy = mode_ != 1 ? area.getBottom() : area.getCentreY();
        const auto radius = area.getWidth() * 0.5f;
        g.setColour(palette::outline);
        juce::Path grid;
        if (mode_ != 1)
        {
            grid.addCentredArc(cx, cy, radius, radius, 0, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
            grid.startNewSubPath(area.getX(), cy);
            grid.lineTo(area.getRight(), cy);
            constexpr float diagonal = 0.70710678f;
            grid.startNewSubPath(cx - diagonal * radius, cy - diagonal * radius);
            grid.lineTo(cx, cy);
            grid.lineTo(cx + diagonal * radius, cy - diagonal * radius);
        }
        else
        {
            grid.startNewSubPath(cx, area.getY());
            grid.lineTo(area.getRight(), cy);
            grid.lineTo(cx, area.getBottom());
            grid.lineTo(area.getX(), cy);
            grid.closeSubPath();
            grid.startNewSubPath(cx - radius * 0.5f, cy - radius * 0.5f);
            grid.lineTo(cx + radius * 0.5f, cy + radius * 0.5f);
            grid.startNewSubPath(cx + radius * 0.5f, cy - radius * 0.5f);
            grid.lineTo(cx - radius * 0.5f, cy + radius * 0.5f);
            grid.startNewSubPath(area.getX(), cy);
            grid.lineTo(area.getRight(), cy);
        }
        grid.startNewSubPath(cx, area.getY());
        grid.lineTo(cx, area.getBottom());
        g.strokePath(grid, juce::PathStrokeType(1));

        density_ = background_.createCopy();
        counts_.assign(static_cast<std::size_t>(width * height), 0);
        touched_.clear();
        historyTiles_.clear();
        historyTiles_.resize(static_cast<std::size_t>(((width + 31) / 32) * ((height + 31) / 32)));
        historySlots_.assign(counts_.size(), std::numeric_limits<unsigned>::max());
        released_ = {};
        damage_ = density_.getBounds();
    }
    if (mode_ == 2)
        renderLevel();
    else
    {
        juce::Image::BitmapData data(density_, juce::Image::BitmapData::readWrite);
        juce::Image::BitmapData backdrop(background_, juce::Image::BitmapData::readOnly);
        // Restore only pixels occupied by the previous cloud. The cached opaque grid
        // makes painting a native-size RGB copy instead of scaled alpha compositing.
        std::array<juce::PixelARGB, 256> ink;
        for (std::size_t i = 0; i < ink.size(); ++i)
            ink[i] = colour_.withAlpha(static_cast<juce::uint8>(i)).getPixelARGB();
        for (const auto index : touched_)
            counts_[index] = 0;
        touched_.clear();
        clipped_ = false;
        int minX = width, minY = height, maxX = -1, maxY = -1;
        if (snapshot_->signalValid() && !stale_)
        {
            const float gain = std::pow(
                10.0f,
                static_cast<float>(gains[static_cast<std::size_t>(gain_.getSelectedItemIndex())]) /
                    20.0f);
            const float radius = static_cast<float>(width - 4) * 0.5f;
            const float cx = static_cast<float>(width) * 0.5f;
            const float cy =
                mode_ != 1 ? static_cast<float>(height - 2) : static_cast<float>(height) * 0.5f;
            for (std::size_t i = 0; i < snapshot_->count; ++i)
            {
                const auto& sample = snapshot_->samples[i];
                auto p = mode_ != 1
                             ? cachedPolar(sample, snapshot_->frameEnd - snapshot_->count + i)
                             : stereoLissajous(sample.left, sample.right);
                p.x *= gain;
                p.y *= gain;
                if ((mode_ != 1 ? std::hypot(p.x, p.y) : std::abs(p.x) + std::abs(p.y)) > 1.00001f)
                {
                    clipped_ = true;
                    continue;
                }
                const int x = juce::roundToInt(cx + radius * p.x),
                          y = juce::roundToInt(cy - radius * p.y);
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dx = 0; dx <= 1; ++dx)
                        if (x + dx >= 0 && x + dx < width && y + dy >= 0 && y + dy < height)
                        {
                            const auto index = static_cast<unsigned>((y + dy) * width + x + dx);
                            if (counts_[index]++ == 0)
                                touched_.push_back(index);
                        }
            }
            for (const auto index : touched_)
            {
                auto& slot = historySlots_[index];
                if (slot != std::numeric_limits<unsigned>::max())
                    continue;
                const int x = static_cast<int>(index % static_cast<unsigned>(width));
                const int y = static_cast<int>(index / static_cast<unsigned>(width));
                auto& tile = historyTiles_[static_cast<std::size_t>((y / 32) * ((width + 31) / 32) +
                                                                    x / 32)];
                TrailPixel point{
                    index,
                    0,
                    x,
                    y,
                    y * data.lineStride + x * data.pixelStride,
                    *reinterpret_cast<const juce::PixelRGB*>(backdrop.getPixelPointer(x, y))};
                if (tile.free.empty())
                {
                    slot = static_cast<unsigned>(tile.pixels.size());
                    tile.pixels.push_back(point);
                }
                else
                {
                    slot = tile.free.back();
                    tile.free.pop_back();
                    tile.pixels[slot] = point;
                }
            }
        }
        // Every sample contributes. Reuse exact density values for common counts.
        const float exposure = static_cast<float>(48000.0 / std::max(1.0, snapshot_->sampleRate));
        std::array<float, 128> densityAlpha;
        for (std::size_t i = 0; i < densityAlpha.size(); ++i)
            densityAlpha[i] = 1.0f - std::exp(-0.4f * exposure * static_cast<float>(i));
        // Tile order keeps image writes and count reads local in the CPU cache.
        for (auto& tile : historyTiles_)
            for (auto& point : tile.pixels)
            {
                if (point.index == std::numeric_limits<unsigned>::max())
                    continue;
                const auto count = counts_[point.index];
                const float alpha =
                    count < densityAlpha.size()
                        ? densityAlpha[count]
                        : 1.0f - std::exp(-0.4f * exposure * static_cast<float>(count));
                // Max avoids repeatedly adding overlapping 100 ms windows.
                point.brightness = std::max(alpha, point.brightness * static_cast<float>(decay_));
                if (point.brightness < 1.0f / 255.0f)
                {
                    *reinterpret_cast<juce::PixelRGB*>(data.data + point.offset) = point.background;
                    tile.free.push_back(historySlots_[point.index]);
                    historySlots_[point.index] = std::numeric_limits<unsigned>::max();
                    point.brightness = 0;
                    point.index = std::numeric_limits<unsigned>::max();
                    continue;
                }
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
                const int alphaByte = juce::roundToInt(point.brightness * 255);
                if (alphaByte != point.lastAlpha)
                {
                    auto pixel = point.background;
                    pixel.blend(ink[static_cast<std::size_t>(alphaByte)]);
                    *reinterpret_cast<juce::PixelRGB*>(data.data + point.offset) = pixel;
                    point.lastAlpha = alphaByte;
                }
            }
        if (maxX >= minX && maxY >= minY)
            cloudBounds_ = {minX, minY, maxX - minX + 1, maxY - minY + 1};
        damage_ = damage_.getUnion(cloudBounds_);
    }
    const double duration = now() - start;
    renderTotal_ += duration;
    ++frames_;
    stats_.renderMsAverage = renderTotal_ / frames_;
    stats_.renderMsMax = std::max(stats_.renderMsMax, duration);
    stats_.imageWidth = width;
    stats_.imageHeight = height;
    stats_.scale = 1;
}
void StereoView::renderLevel()
{
    // Both images are native-size software RGB. Restore changed rows directly,
    // avoiding the generic per-pixel image renderer for a potentially wide peak.
    {
        juce::Image::BitmapData target(density_, juce::Image::BitmapData::readWrite);
        juce::Image::BitmapData source(background_, juce::Image::BitmapData::readOnly);
        const auto restore = damage_.getIntersection(density_.getBounds());
        const auto bytes = static_cast<std::size_t>(restore.getWidth() * target.pixelStride);
        if (!restore.isEmpty())
            for (int y = restore.getY(); y < restore.getBottom(); ++y)
                std::memcpy(target.getPixelPointer(restore.getX(), y),
                            source.getPixelPointer(restore.getX(), y), bytes);
    }
    clipped_ = false;
    const auto measured =
        snapshot_->signalValid() && !stale_
            ? (levelProfile_.getSelectedItemIndex() == 0
                   ? polarDirectionalLevel({snapshot_->samples.data(), snapshot_->count})
                   : polarLevel({snapshot_->samples.data(), snapshot_->count}))
            : PolarLevel{};
    bool visible = false;
    for (std::size_t i = 0; i < polarLevelBins; ++i)
    {
        released_.rms[i] = std::max(measured.rms[i], released_.rms[i] * decay_);
        released_.peak[i] = std::max(measured.peak[i], released_.peak[i] * decay_);
        visible = visible || released_.peak[i] > 1e-6;
    }
    if (!visible)
        return;
    const auto& levels = released_;
    const double gain = std::pow(
        10.0,
        static_cast<double>(gains[static_cast<std::size_t>(gain_.getSelectedItemIndex())]) / 20.0);
    const float radius = static_cast<float>(density_.getWidth() - 4) * 0.5f;
    const float cx = static_cast<float>(density_.getWidth()) * 0.5f;
    const float cy = static_cast<float>(density_.getHeight() - 2);
    auto& fill = levelFill_;
    auto& peak = levelPeak_;
    fill.clear();
    peak.clear();
    fill.startNewSubPath(cx, cy);
    peak.startNewSubPath(cx, cy);
    if (levelProfile_.getSelectedItemIndex() == 0)
    {
        // Smoothstep is bounded by adjacent measured radii: no spline overshoot.
        for (std::size_t j = 0; j < smoothDirections_.size(); ++j)
        {
            const auto i = j / 4;
            const auto next = std::min(i + 1, polarLevelBins - 1);
            const double t = static_cast<double>(j % 4) / 4.0;
            const double u = t * t * (3.0 - 2.0 * t);
            const double rms = std::lerp(levels.rms[i], levels.rms[next], u) * gain;
            const double maximum = std::lerp(levels.peak[i], levels.peak[next], u) * gain;
            clipped_ = clipped_ || maximum > 1.0;
            const auto direction = smoothDirections_[j];
            fill.lineTo(cx + direction.x * radius * static_cast<float>(std::min(1.0, rms)),
                        cy - direction.y * radius * static_cast<float>(std::min(1.0, rms)));
            peak.lineTo(cx + direction.x * radius * static_cast<float>(std::min(1.0, maximum)),
                        cy - direction.y * radius * static_cast<float>(std::min(1.0, maximum)));
        }
    }
    else
    {
        for (std::size_t i = 0; i < polarLevelBins; ++i)
        {
            const double rms = levels.rms[i] * gain, maximum = levels.peak[i] * gain;
            clipped_ = clipped_ || maximum > 1.0;
            for (const auto direction : levelDirections_[i])
            {
                const float x = direction.x * radius;
                const float y = direction.y * radius;
                fill.lineTo(cx + x * static_cast<float>(std::min(1.0, rms)),
                            cy - y * static_cast<float>(std::min(1.0, rms)));
                peak.lineTo(cx + x * static_cast<float>(std::min(1.0, maximum)),
                            cy - y * static_cast<float>(std::min(1.0, maximum)));
            }
        }
    }
    fill.closeSubPath();
    peak.closeSubPath();
    cloudBounds_ = peak.getBounds().expanded(2).getSmallestIntegerContainer().getIntersection(
        density_.getBounds());
    damage_ = damage_.getUnion(cloudBounds_);
    juce::Graphics g(density_);
    g.setColour(colour_);
    g.fillPath(fill);
    g.setColour(colour_.withAlpha(0.7f));
    // Beveled joins keep sharp bin transitions inside the two-pixel damage margin.
    g.strokePath(peak, juce::PathStrokeType(1.0f, juce::PathStrokeType::beveled));
}
void StereoView::paint(juce::Graphics& g)
{
    const auto start = now();
    // The opaque plot image covers its entire rectangle. Do not clear those
    // pixels a second time, especially when a full-width profile changes.
    {
        juce::Graphics::ScopedSaveState saved(g);
        if (density_.isValid())
            g.excludeClipRegion(density_.getBounds().translated(juce::roundToInt(plot().getX()),
                                                                juce::roundToInt(plot().getY())));
        g.fillAll(palette::background);
    }
    g.setFont(13);
    g.setColour(palette::textDim);
    g.drawText(mode_ == 2   ? "100 ms RMS / peak  |  2.5 degree centres"
               : mode_ == 0 ? "200 ms measurement  |  100 ms samples + display afterglow"
                            : "200 ms measurement  |  100 ms samples",
               mode_ == 2 ? 214 : 16, 56, getWidth() - (mode_ == 2 ? 230 : 32), 20,
               juce::Justification::centred);
    const auto area = plot();
    g.drawImageAt(density_, juce::roundToInt(area.getX()), juce::roundToInt(area.getY()));
    g.setColour(palette::textDim);
    g.drawText("L", juce::Rectangle<float>(area.getX(), area.getBottom() + 3, 24, 20),
               juce::Justification::centred);
    g.drawText("R", juce::Rectangle<float>(area.getRight() - 24, area.getBottom() + 3, 24, 20),
               juce::Justification::centred);
    g.setColour(palette::text);
    const auto message = paused_ ? juce::String("Paused") : status();
    if (message.isNotEmpty())
        g.drawText(message, 16, getHeight() - 108, getWidth() - 32, 22,
                   juce::Justification::centred);
    else if (clipped_)
        g.drawText("Display clipped - reduce display gain", 16, getHeight() - 108, getWidth() - 32,
                   22, juce::Justification::centred);
    auto correlation = juce::String("Correlation: --");
    auto balance = juce::String("Balance: --");
    if (!stale_ && snapshot_->correlationValid())
        correlation = "Correlation: " + readout(snapshot_->correlation);
    if (!stale_ && snapshot_->signalValid())
        balance = "Balance: " + readout(snapshot_->balance) + "  (L -1 / R +1)";
    g.setFont(16);
    g.drawText(correlation, 16, getHeight() - 80, getWidth() / 2 - 16, 24,
               juce::Justification::centred);
    g.drawText(balance, getWidth() / 2, getHeight() - 80, getWidth() / 2 - 16, 24,
               juce::Justification::centred);
    const auto meter = juce::Rectangle<float>(48, static_cast<float>(getHeight() - 42),
                                              static_cast<float>(getWidth() - 96), 6);
    g.setColour(palette::outline);
    g.fillRect(meter);
    g.setColour(palette::textDim);
    g.drawText("-1", 16, getHeight() - 49, 26, 20, juce::Justification::centred);
    g.drawText("+1", getWidth() - 42, getHeight() - 49, 26, 20, juce::Justification::centred);
    g.fillRect(meter.getCentreX(), meter.getY() - 3, 1.0f, 12.0f);
    if (!stale_ && snapshot_->correlationValid())
    {
        g.setColour(colour_);
        g.fillRect(meter.getX() + (snapshot_->correlation + 1) * 0.5f * meter.getWidth() - 2,
                   meter.getY() - 3, 4.0f, 12.0f);
    }
    const double duration = now() - start;
    paintTotal_ += duration;
    stats_.paintMsAverage = paintTotal_ / ++paints_;
    stats_.paintMsMax = std::max(stats_.paintMsMax, duration);
}
void StereoView::timerCallback()
{
    if (paused_)
        return;
    const auto time = now();
    bool changed = false;
    const auto previousValidity = snapshot_->validity;
    const bool previousStale = stale_, previousClipped = clipped_;
    if (analyzer_.snapshots().fetch())
    {
        *snapshot_ = analyzer_.snapshots().readBuffer();
        lastAudio_ = time;
        stale_ = false;
        render();
        changed = true;
    }
    else if (!stale_ && time - lastAudio_ > 500)
    {
        stale_ = true;
        render();
        changed = true;
    }
    if (time - start_ >= 1000)
    {
        stats_.framesPerSecond = 1000.0 * frames_ / (time - start_);
        frames_ = paints_ = 0;
        renderTotal_ = paintTotal_ = 0;
        start_ = time;
    }
    if (changed)
    {
        repaint(
            damage_.translated(juce::roundToInt(plot().getX()), juce::roundToInt(plot().getY())));
        // Numeric measurements integrate 200 ms. Redraw them at 30 Hz instead
        // of copying the full-width readout strip for every polar image. Gate,
        // stream and clipping transitions remain immediately visible.
        if (mode_ == 1 || time - lastReadout_ >= 1000.0 / 30.0 ||
            snapshot_->validity != previousValidity || stale_ != previousStale ||
            clipped_ != previousClipped)
        {
            repaint(0, getHeight() - 110, getWidth(), 110);
            lastReadout_ = time;
        }
    }
}
} // namespace visona
