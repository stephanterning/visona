#pragma once

#include "IdleCursorHider.h"
#include "ui/Banner.h"
#include "ui/ControlBar.h"
#include "ui/DiagnosticsOverlay.h"
#include "ui/ScopeView.h"
#include "ui/SettingsPanel.h"
#include "ui/StatusBar.h"
#include "ui/ZoomOverview.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

class AudioEngine;
class Settings;

/**
    Root content component of the main window: the status bar at the top, the zoom's overview strip
    under it while zoomed in, the scope in the middle and the control bar at the bottom, with the
    banner, the diagnostics overlay and the settings panel over the scope (architecture.md 3.6).

    Shortcuts: 1 to 5 pick the window, + and - (or the up and down arrows) change the gain, F
    toggles full screen, D the diagnostics overlay, Cmd+, the settings panel, and Esc closes the
    settings panel or, with it closed, resets the zoom.
*/
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent(AudioEngine& engine, Settings& settings, bool kioskMode = false);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void setGainDb(int gainDb);
    void setWindow(std::size_t window);
    void setFreeTempo(double bpm);

    /** Leaves STOPPED or MIDI CLOCK LOST for the free-running sweep, at the last MIDI tempo. */
    void runFree();

    void setWaveformMode(WaveformMode mode);
    void setWaveformColour(std::size_t index);
    void showSettings(bool shouldShow);
    void showDiagnostics(bool shouldShow);
    void toggleFullScreen();
    [[nodiscard]] bool isFullScreen() const;
    void updateToggles();
    void updateDeviceInfo();
    void updateBanner();
    void updateStatus();
    void updateZoom();
    void updateDiagnostics();

    AudioEngine& engine_;
    Settings& settings_;
    const bool kioskMode_;
    IdleCursorHider idleCursorHider_{*this};
    juce::LookAndFeel_V4 lookAndFeel_;

    StatusBar statusBar_;
    ZoomOverview zoomOverview_;
    ScopeView scope_;
    ControlBar controlBar_;
    Banner banner_;
    DiagnosticsOverlay diagnostics_;
    SettingsPanel settingsPanel_;
    juce::TooltipWindow tooltips_{this};

    int gainDb_ = 0;
    std::size_t window_;

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
