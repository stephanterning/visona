#pragma once

#include "ui/Banner.h"
#include "ui/ControlBar.h"
#include "ui/DiagnosticsOverlay.h"
#include "ui/ScopeView.h"
#include "ui/SettingsPanel.h"
#include "ui/StatusBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

class AudioEngine;
class Settings;

/**
    Root content component of the main window: the status bar at the top, the scope in the middle
    and the control bar at the bottom, with the banner, the diagnostics overlay and the settings
    panel over the scope (architecture.md 3.6).

    Shortcuts: + and - (or the up and down arrows) change the gain, F toggles full screen, D the
    diagnostics overlay, Cmd+, the settings panel, and Esc closes the settings panel. M toggles
    mono/precise, and C switches between the two colouring methods.

    Temporary debug shortcuts (D-094): [ and ] (or , and .) step the free-running window, B
    toggles the band delay compensation, and G cycles the debug grid.
*/
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent(AudioEngine& engine, Settings& settings);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void setGainDb(int gainDb);
    void setColoring(WaveformColoring coloring);
    void setDebugValues(const SettingsPanel::DebugValues& values);
    void showSettings(bool shouldShow);
    void showDiagnostics(bool shouldShow);
    void toggleFullScreen();
    [[nodiscard]] bool isFullScreen() const;
    void updateToggles();
    void updateDeviceInfo();
    void updateStatus();
    void updateDiagnostics();

    AudioEngine& engine_;
    Settings& settings_;
    juce::LookAndFeel_V4 lookAndFeel_;

    StatusBar statusBar_;
    ScopeView scope_;
    ControlBar controlBar_;
    Banner banner_;
    DiagnosticsOverlay diagnostics_;
    SettingsPanel settingsPanel_;
    juce::TooltipWindow tooltips_{this};

    int gainDb_ = 0;
    WaveformColoring coloring_ = WaveformColoring::layered;
    // The method M returns to from mono/precise.
    WaveformColoring colouredMethod_ = WaveformColoring::layered;
    SettingsPanel::DebugValues debug_;

    // Cached on device changes, because reading them from the device queries the driver.
    juce::String deviceName_;
    double sampleRate_ = 0.0;
    int bufferSize_ = 0;
    juce::StringArray inputNames_;
    bool inputRunning_ = false;

    std::vector<float> peaks_;
    double lastUpdateSeconds_ = 0.0;
    std::uint64_t lastAnalysisBusyNs_ = 0;
    std::optional<double> lastCpuSeconds_;
    bool allocationCheckWorks_ = false;
    bool reportedCallbackAllocation_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace visona
