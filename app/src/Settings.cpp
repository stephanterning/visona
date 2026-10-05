#include "Settings.h"

#include <visona/SweepWindow.h>

namespace visona
{

namespace
{

constexpr auto audioDeviceStateKey = "audioDeviceState";
constexpr auto inputChannelsKey = "inputChannels";
constexpr auto midiInputKey = "midiInput";
constexpr auto midiInputNameKey = "midiInputName";
constexpr auto freeTempoKey = "freeTempo";
constexpr auto waveformModeKey = "waveformMode";
constexpr auto waveformColourKey = "waveformColour";
constexpr auto autoGainKey = "autoGain";

juce::PropertiesFile::Options fileOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Visona";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = ".config/Visona";
#else
    options.folderName = "Visona";
#endif
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    return options;
}

juce::PropertiesFile& openUserSettings(juce::ApplicationProperties& applicationProperties)
{
    applicationProperties.setStorageParameters(fileOptions());
    return *applicationProperties.getUserSettings();
}

} // namespace

Settings::Settings()
    : properties_(openUserSettings(applicationProperties_))
{
}

std::unique_ptr<juce::XmlElement> Settings::audioDeviceState() const
{
    return properties_.getXmlValue(audioDeviceStateKey);
}

void Settings::setAudioDeviceState(const juce::XmlElement& state)
{
    properties_.setValue(audioDeviceStateKey, &state);
}

std::vector<int> Settings::inputChannels(std::size_t count) const
{
    const auto saved =
        juce::StringArray::fromTokens(properties_.getValue(inputChannelsKey), ",", {});

    std::vector<int> channels;
    channels.reserve(count);
    for (std::size_t channel = 0; channel < count; ++channel)
    {
        const auto index = static_cast<int>(channel);
        const auto token = saved[index].trim();
        const bool valid = token.isNotEmpty() && token.containsOnly("0123456789");
        channels.push_back(valid ? token.getIntValue() : index);
    }
    return channels;
}

void Settings::setInputChannels(std::span<const int> channels)
{
    juce::StringArray tokens;
    for (const auto channel : channels)
        tokens.add(juce::String(channel));
    properties_.setValue(inputChannelsKey, tokens.joinIntoString(","));
}

Settings::MidiInputChoice Settings::midiInput() const
{
    return {properties_.getValue(midiInputKey), properties_.getValue(midiInputNameKey)};
}

void Settings::setMidiInput(const MidiInputChoice& choice)
{
    properties_.setValue(midiInputKey, choice.identifier);
    properties_.setValue(midiInputNameKey, choice.name);
}

double Settings::freeTempo() const
{
    return clampFreeBpm(properties_.getDoubleValue(freeTempoKey, defaultFreeBpm));
}

void Settings::setFreeTempo(double bpm)
{
    properties_.setValue(freeTempoKey, clampFreeBpm(bpm));
}

WaveformMode Settings::waveformMode() const
{
    const auto value = properties_.getValue(waveformModeKey);
    if (value == "standard")
        return WaveformMode::standard;
    if (value == "dj")
        return WaveformMode::dj;
    return WaveformMode::precise;
}

void Settings::setWaveformMode(WaveformMode mode)
{
    properties_.setValue(waveformModeKey, mode == WaveformMode::standard ? "standard"
                                          : mode == WaveformMode::dj     ? "dj"
                                                                         : "precise");
}

std::size_t Settings::waveformColour(std::size_t count, std::size_t fallback) const
{
    const auto index = properties_.getIntValue(waveformColourKey, static_cast<int>(fallback));
    return index >= 0 && static_cast<std::size_t>(index) < count ? static_cast<std::size_t>(index)
                                                                 : fallback;
}

void Settings::setWaveformColour(std::size_t index)
{
    properties_.setValue(waveformColourKey, static_cast<int>(index));
}

bool Settings::autoGain() const
{
    return properties_.getBoolValue(autoGainKey, false);
}

void Settings::setAutoGain(bool isOn)
{
    properties_.setValue(autoGainKey, isOn);
}

juce::File Settings::file() const
{
    return properties_.getFile();
}

} // namespace visona
