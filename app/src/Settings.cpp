#include "Settings.h"

namespace visona
{

namespace
{

constexpr auto audioDeviceStateKey = "audioDeviceState";
constexpr auto inputChannelsKey = "inputChannels";
constexpr auto waveformColoringKey = "waveformColoring";

// Saved by name, in the order of WaveformColoring.
const juce::StringArray& coloringNames()
{
    static const juce::StringArray names{"precise", "blended", "layered"};
    return names;
}

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

WaveformColoring Settings::waveformColoring() const
{
    const auto index = coloringNames().indexOf(properties_.getValue(waveformColoringKey));
    return index >= 0 ? static_cast<WaveformColoring>(index) : WaveformColoring::layered;
}

void Settings::setWaveformColoring(WaveformColoring coloring)
{
    properties_.setValue(waveformColoringKey, coloringNames()[static_cast<int>(coloring)]);
}

juce::File Settings::file() const
{
    return properties_.getFile();
}

} // namespace visona
