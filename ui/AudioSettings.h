#pragma once

#include <visona/SourceLayout.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstddef>

namespace visona
{

/** The audio and MIDI settings that the settings panel shows and changes. */
class AudioSettings
{
public:
    virtual ~AudioSettings() = default;

    /** For reading the devices and the current setup, and for change notifications. Changes go
        through the functions below. */
    [[nodiscard]] virtual juce::AudioDeviceManager& deviceManager() noexcept = 0;

    // Each returns an error message, or an empty string on success.
    virtual juce::String selectDeviceType(const juce::String& typeName) = 0;
    virtual juce::String selectDevice(const juce::String& inputDeviceName) = 0;
    virtual juce::String selectSampleRate(double sampleRate) = 0;
    virtual juce::String selectBufferSize(int bufferSizeSamples) = 0;

    /** The source channels, each of which is fed by one device input channel. */
    [[nodiscard]] virtual const SourceLayout& layout() const noexcept = 0;

    /** The zero-based device input channel that feeds source channel `channel`, or -1 if no
        device is running. */
    [[nodiscard]] virtual int inputChannel(std::size_t channel) const = 0;
    virtual void setInputChannel(std::size_t channel, int deviceInputChannel) = 0;

    /** The MIDI input MIDI Clock comes from, by JUCE's identifier; empty for none. A saved input
        that is missing keeps its identifier and name. */
    [[nodiscard]] virtual juce::String midiInput() const = 0;
    [[nodiscard]] virtual juce::String midiInputName() const = 0;
    virtual juce::String selectMidiInput(const juce::String& identifier) = 0;

protected:
    AudioSettings() = default;
    AudioSettings(const AudioSettings&) = default;
    AudioSettings& operator=(const AudioSettings&) = default;
};

} // namespace visona
