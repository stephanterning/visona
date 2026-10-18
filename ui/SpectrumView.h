#pragma once
#include "ChoiceBox.h"
#include "RenderStats.h"

#include <visona/SpectrumAnalyzer.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace visona
{
/** Shared app/plugin spectrum presentation. All controls are in-window and touch accessible. */
class SpectrumView final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView(SpectrumAnalyzer& analyzer);
    ~SpectrumView() override;
    std::function<void()> onSettingsChange;
    void setActive(bool active);
    void setDisplayGainDb(int db);
    void setAfterglow(int index);
    using Stats = RenderStats;
    Stats stats() const noexcept
    {
        return stats_;
    }
    void setPaused(bool paused)
    {
        paused_ = paused;
    }
    void setWaveformColour(juce::Colour colour);
    void showSettings(bool show);
    bool settingsVisible() const
    {
        return settingsPanel_.isVisible();
    }
    juce::String saveSettings() const;
    void restoreSettings(const juce::String& text);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& k) override;

private:
    friend struct SpectrumRenderBenchmark;
    void timerCallback() override;
    void render();
    void renderBackground(juce::Graphics& g);
    void applySettings();
    juce::Rectangle<float> plot() const;
    double frequencyAt(float x) const;
    float xAt(double frequency) const;
    float yAt(float db) const;
    float shownDb(std::size_t curve, std::size_t point) const;
    void moveCursor(juce::Point<float> position);
    SpectrumAnalyzer& analyzer_;
    SpectrumSettings settings_;
    int gainDb_ = 0, afterglow_ = 2;
    juce::Colour waveformColour_;
    SpectrumSnapshot snapshot_;
    bool active_ = false, paused_ = false, cursor_ = false;
    juce::Point<float> cursorPosition_;
    juce::Image cached_, background_, tailBackground_;
    juce::Rectangle<int> repaintRegion_;
    float previousCurveTop_ = 0;
    bool backgroundDirty_ = true, backgroundValid_ = false;
    double backgroundMaxFrequency_ = 0.0;
    std::vector<float> fillTops_, fillFades_, fillEnds_;
    std::vector<juce::uint32> fillColours_;
    float renderScale_ = 1.0f;
    Stats stats_;
    double statsStart_ = 0, renderTotal_ = 0, renderMax_ = 0;
    int renderedFrames_ = 0, paintedFrames_ = 0;
    double lastPaintMs_ = 0, paintTotal_ = 0, paintMax_ = 0;
    struct Panel : juce::Component
    {
        void paint(juce::Graphics& g) override;
    } settingsPanel_;
    juce::Label title_;
    juce::TextButton close_{"Done"};
    juce::Viewport viewport_;
    juce::Component settingsContent_;
    // Channel, top, range, slope; six controls per curve.
    std::array<ChoiceBox, 16> choices_;
    std::array<juce::Label, 16> labels_;
};
} // namespace visona
