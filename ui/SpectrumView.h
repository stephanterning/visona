#pragma once
#include "ChoiceBox.h"

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
    void setPaused(bool paused)
    {
        paused_ = paused;
    }
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
    void timerCallback() override;
    void render();
    void applySettings();
    juce::Rectangle<float> plot() const;
    double frequencyAt(float x) const;
    float xAt(double frequency) const;
    float yAt(float db) const;
    float shownDb(std::size_t curve, std::size_t point) const;
    void moveCursor(juce::Point<float> position);
    SpectrumAnalyzer& analyzer_;
    SpectrumSettings settings_;
    SpectrumSnapshot snapshot_;
    bool active_ = false, paused_ = false, cursor_ = false;
    juce::Point<float> cursorPosition_;
    juce::Image cached_;
    float renderScale_ = 1.0f;
    struct Panel : juce::Component
    {
        void paint(juce::Graphics& g) override;
    } settingsPanel_;
    juce::Label title_;
    juce::TextButton close_{"Done"}, clear_{"Reset peaks"};
    juce::Viewport viewport_;
    juce::Component settingsContent_;
    // Channel, top, range, slope; six controls per curve.
    std::array<ChoiceBox, 16> choices_;
    std::array<juce::Label, 16> labels_;
};
} // namespace visona
