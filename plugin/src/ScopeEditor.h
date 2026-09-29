#pragma once

#include "PluginProcessor.h"
#include "PluginState.h"

#include "ui/AutoGainSettings.h"
#include "ui/Banner.h"
#include "ui/ColourSwatches.h"
#include "ui/ControlBar.h"
#include "ui/DiagnosticsOverlay.h"
#include "ui/ScopeView.h"
#include "ui/StatusBar.h"
#include "ui/ZoomOverview.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace visona
{

/**
    Slim plugin editor: scope, controls and appearance settings, including auto gain, without
    standalone audio or MIDI device panels.
*/
class ScopeEditor final : public juce::Component,
                          private juce::Timer
{
public:
    ScopeEditor(PluginProcessor& processor);
    ~ScopeEditor() override;

    std::function<void(const PluginInstanceState&)> onStateChange;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    /** Sets the gain by hand, which turns auto gain off. */
    void setGainDb(int gainDb);
    /** The gain on screen: auto gain's while it is on, otherwise the one set by hand. */
    [[nodiscard]] int shownGainDb();
    void showGainDb(int gainDb);
    void setAutoGain(bool isOn);
    void setAutoGainHold(std::size_t choice);
    /** Lets auto gain see the bars that ended since the previous frame. */
    void followAutoGain();
    void setWindow(std::size_t window);
    void setWaveformMode(WaveformMode mode);
    void setWaveformColour(std::size_t index);
    void showAppearance(bool shouldShow);
    void showDiagnostics(bool shouldShow);
    void updateStatus();
    void updateZoom();
    void updateDiagnostics();
    void notifyStateChange();

    PluginProcessor& processor_;
    PluginAnalysisThread& analysis_;
    const SourceLayout& layout_;
    PluginInstanceState& state_;
    PluginGlobalDefaults& defaults_;

    juce::LookAndFeel_V4 lookAndFeel_;
    StatusBar statusBar_;
    ZoomOverview zoomOverview_;
    ScopeView scope_;
    ControlBar controlBar_;
    Banner banner_;
    DiagnosticsOverlay diagnostics_;
    struct AppearancePanel final : juce::Component
    {
        void paint(juce::Graphics& g) override;
    };

    AppearancePanel appearancePanel_;
    juce::Label appearanceTitle_;
    juce::TextButton appearanceClose_;
    juce::Label colourLabel_;
    ColourSwatches colourSwatches_;
    juce::Label autoGainLabel_;
    AutoGainSettings autoGainSettings_;
    juce::TooltipWindow tooltips_{this};

    std::size_t window_ = 2;
    std::vector<float> peaks_;
};

} // namespace visona
