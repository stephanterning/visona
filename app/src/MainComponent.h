#pragma once

#include "ui/Banner.h"
#include "ui/DebugReadout.h"
#include "ui/SettingsPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace visona
{

class AudioEngine;

/** Root content component of the main window. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent(AudioEngine& engine);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void showSettings(bool shouldShow);
    void updateDeviceInfo();

    AudioEngine& engine_;
    juce::LookAndFeel_V4 lookAndFeel_;

    DebugReadout readout_;
    Banner banner_;
    juce::TextButton settingsButton_;
    SettingsPanel settingsPanel_;

    // Cached on device changes, because reading them from the device queries the driver.
    juce::String deviceName_;
    double sampleRate_ = 0.0;
    int bufferSize_ = 0;
    juce::StringArray inputNames_;

    std::vector<float> peaks_;
    double lastUpdateSeconds_ = 0.0;
    bool allocationCheckWorks_ = false;
    bool reportedCallbackAllocation_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace visona
