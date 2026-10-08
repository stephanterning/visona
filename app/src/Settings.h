#pragma once

#include <visona/WaveformStyle.h>

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace visona
{

/**
    Visona's saved user configuration, kept with juce::ApplicationProperties. On macOS the file is
    ~/Library/Application Support/Visona/Visona.settings. Every change is written to disk at once.
*/
class Settings
{
public:
    Settings();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    /** The state from juce::AudioDeviceManager::createStateXml(), or nullptr if none is saved. */
    [[nodiscard]] std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState(const juce::XmlElement& state);

    /**
        The zero-based device input channel chosen for each of the first `count` source channels.
        A channel without a saved choice gets its own index, so the default is inputs 1, 2, ...
    */
    [[nodiscard]] std::vector<int> inputChannels(std::size_t count) const;
    void setInputChannels(std::span<const int> channels);

    /** The zero-based device input channel Visona Sync comes in on (D-109); -1, the default, is
        none. */
    [[nodiscard]] int syncInputChannel() const;
    void setSyncInputChannel(int channel);

    /** A MIDI input, by JUCE's identifier and by name for when it is missing. */
    struct MidiInputChoice
    {
        juce::String identifier;
        juce::String name;
    };

    /** The chosen MIDI input; an empty identifier is none. */
    [[nodiscard]] MidiInputChoice midiInput() const;
    void setMidiInput(const MidiInputChoice& choice);

    /** The tempo of the free-running sweep, clamped by clampFreeBpm(), 120 BPM if none is saved
        (D-090). */
    [[nodiscard]] double freeTempo() const;
    void setFreeTempo(double bpm);

    /** How the waveform is drawn (D-091); precise if nothing is saved. */
    [[nodiscard]] WaveformMode waveformMode() const;
    void setWaveformMode(WaveformMode mode);

    /** The waveform colour, an index into palette::waveformColours (D-093); `fallback` if nothing
        valid is saved. */
    [[nodiscard]] std::size_t waveformColour(std::size_t count, std::size_t fallback) const;
    void setWaveformColour(std::size_t index);

    /** Whether auto gain is on (D-100); off if nothing is saved. */
    [[nodiscard]] bool autoGain() const;
    void setAutoGain(bool isOn);

    [[nodiscard]] juce::File file() const;

private:
    juce::ApplicationProperties applicationProperties_;
    juce::PropertiesFile& properties_;
};

} // namespace visona
