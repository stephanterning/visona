#pragma once

#include <visona/WaveformColoring.h>

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

    /** How the waveform is coloured; bands inside the outline unless chosen otherwise. */
    [[nodiscard]] WaveformColoring waveformColoring() const;
    void setWaveformColoring(WaveformColoring coloring);

    [[nodiscard]] juce::File file() const;

private:
    juce::ApplicationProperties applicationProperties_;
    juce::PropertiesFile& properties_;
};

} // namespace visona
