#pragma once

#include "ChoiceBox.h"
#include "SpectrumView.h"

#include <visona/PolarLevel.h>
#include <visona/StereoAnalyzer.h>
#include <visona/StereoProjection.h>

namespace visona
{
/** Shared app/plugin stereo panel. Display gain never changes measurement or gating. */
class StereoView final : public juce::Component, private juce::Timer
{
public:
    explicit StereoView(StereoAnalyzer& analyzer);
    ~StereoView() override;
    void setActive(bool active);
    void setPaused(bool paused);
    void setWaveformColour(juce::Colour colour);
    void resetView();
    juce::String saveSettings() const;
    void restoreSettings(const juce::String& text);
    std::function<void()> onSettingsChange;
    SpectrumView::Stats stats() const
    {
        return stats_;
    }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    friend struct StereoUiChecks;
    void timerCallback() override;
    void selectMode(int mode);
    void changed();
    void render();
    void renderLevel();
    StereoPoint cachedPolar(const StereoSample& sample, std::uint64_t frame);
    void clearHistory();
    int refreshRate() const noexcept;
    juce::Rectangle<float> plot() const;
    juce::Rectangle<int> logicalImageBounds(juce::Rectangle<int> pixels) const;
    juce::String status() const;
    StereoAnalyzer& analyzer_;
    std::unique_ptr<StereoSnapshot> snapshot_ = std::make_unique<StereoSnapshot>();
    juce::TextButton polar_{"Polar Sample"}, lissajous_{"Lissajous"}, level_{"Polar Level"};
    juce::Label gainLabel_;
    ChoiceBox gain_, persistence_, levelProfile_;
    juce::Label persistenceLabel_;
    struct TrailPixel
    {
        unsigned index;
        float brightness;
        int x, y, offset;
        juce::PixelRGB background;
        int lastAlpha = -1;
    };
    struct TrailTile
    {
        std::vector<TrailPixel> pixels;
        std::vector<unsigned> free;
    };
    std::vector<unsigned> historySlots_;
    std::vector<TrailTile> historyTiles_;
    // Snapshots overlap; identical sample pairs have identical projections.
    std::vector<std::uint64_t> projectionKeys_ = std::vector<std::uint64_t>(stereoHistoryCapacity);
    std::vector<StereoPoint> projections_ = std::vector<StereoPoint>(stereoHistoryCapacity);
    PolarLevel released_;
    double lastRender_ = 0, decay_ = 0;
    int mode_ = 0;
    bool paused_ = false, stale_ = true, clipped_ = false;
    juce::Colour colour_;
    juce::Image density_, background_;
    float renderScale_ = 1.0f;
    std::array<std::array<juce::Point<float>, 2>, polarLevelBins> levelDirections_{};
    std::array<juce::Point<float>, (polarLevelBins - 1) * 4 + 1> smoothDirections_{};
    juce::Path levelFill_, levelPeak_;
    juce::Rectangle<int> cloudBounds_, damage_;
    std::vector<unsigned> counts_, touched_;
    SpectrumView::Stats stats_;
    double lastReadout_ = 0;
    double lastAudio_ = 0, start_ = 0, renderTotal_ = 0, paintTotal_ = 0;
    int frames_ = 0, paints_ = 0;
};
} // namespace visona
