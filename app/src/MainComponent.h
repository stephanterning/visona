#pragma once

#include "IdleCursorHider.h"
#include "ui/Banner.h"
#include "ui/ControlBar.h"
#include "ui/DiagnosticsOverlay.h"
#include "ui/ScopeView.h"
#include "ui/SettingsPanel.h"
#include "ui/StatusBar.h"
#include "ui/Tooltips.h"
#include "ui/ZoomOverview.h"

#include <visona/AutoGain.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
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

    Shortcuts: Space or P presses the pause button, 1 to 5 pick the window, + and - (or the up and
    down arrows) change the gain by hand, which turns auto gain off, F toggles full screen, D the
    diagnostics overlay, Cmd+, the settings panel, and Esc closes the settings panel or, with it
    closed, resets the zoom.
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

    /** Sets the control bar's step and returns the scale to draw it at. */
    float fitControlBar();

    /** Sets the gain by hand, which turns auto gain off. */
    void setGainDb(int gainDb);
    void showGainDb(int gainDb);
    void setAutoGain(bool isOn);
    /** Lets auto gain see the bars that ended since the previous frame. */
    void followAutoGain();
    void setWindow(std::size_t window);
    void setFreeTempo(double bpm);

    /** Leaves STOPPED or MIDI CLOCK LOST for the free-running sweep, at the last MIDI tempo. */
    void runFree();

    /** Resumes the paused view, runs a frozen one free, or pauses it (D-110). */
    void pressPauseButton();
    void setPaused(bool paused);

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

    /** Dims everything under the settings panel; a click or tap on it closes the panel (D-108). */
    struct Scrim final : juce::Component
    {
        std::function<void()> onClick;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
    };
    Scrim scrim_;
    SettingsPanel settingsPanel_;
    Tooltips tooltips_{*this};

    int gainDb_ = 0;
    bool autoGainOn_ = false;
    AutoGain autoGain_;
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
