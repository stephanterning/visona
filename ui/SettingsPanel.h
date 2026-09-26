#pragma once

#include "AudioSettings.h"
#include "ColourSwatches.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace visona
{

/**
    The settings overlay: audio device, sample rate, buffer size, the device input channel for each
    source channel, the MIDI input for MIDI Clock, and the waveform colour (D-093). Any input
   channel can feed any source channel. In small windows it also holds the view controls that no
   longer fit in the control bar. The panel is opaque, so it never makes the view behind it repaint.
*/
class SettingsPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit SettingsPanel(AudioSettings& settings);
    ~SettingsPanel() override;

    /** Called when the user closes the panel. */
    std::function<void()> onClose;

    /** Called when preferredHeight() may have changed, because rows or an error appeared. */
    std::function<void()> onPreferredHeightChanged;

    /** Called with the waveform colour the user picks, an index into palette::waveformColours. */
    std::function<void(std::size_t index)> onWaveformColourChange;

    void setWaveformColour(std::size_t index);

    /** Called when the user toggles the view controls. */
    std::function<void()> onDiagnostics;
    std::function<void()> onFullScreen;

    /** Shows or hides the row of view controls: diagnostics and full screen. */
    void setViewControlsVisible(bool visible);
    void setViewToggles(bool diagnostics, bool fullScreen);

    /** The height that fits every row. */
    [[nodiscard]] int preferredHeight() const;

    /** Reloads every choice from the current audio setup. */
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    struct Row
    {
        juce::Label label;
        juce::ComboBox choices;
    };

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void addRow(Row& row, const juce::String& labelText);
    void showResult(const juce::String& error);
    void notifyPreferredHeight();

    void refreshDeviceTypes();
    void refreshDevices();
    void refreshSampleRates(juce::AudioIODevice* device);
    void refreshBufferSizes(juce::AudioIODevice* device);
    void refreshInputChannels(juce::AudioIODevice* device);
    void refreshMidiInputs();

    AudioSettings& settings_;

    juce::Label title_;
    juce::TextButton closeButton_;
    Row deviceTypeRow_;
    Row deviceRow_;
    Row sampleRateRow_;
    Row bufferSizeRow_;
    std::vector<std::unique_ptr<Row>> inputRows_;
    Row midiRow_;
    juce::Label colourLabel_;
    ColourSwatches colourSwatches_;
    juce::Label viewLabel_;
    juce::TextButton diagnosticsButton_;
    juce::TextButton fullScreenButton_;
    juce::Label errorLabel_;

    juce::StringArray deviceTypes_;
    juce::StringArray devices_;
    juce::Array<double> sampleRates_;
    juce::Array<int> bufferSizes_;
    juce::StringArray midiIdentifiers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace visona
