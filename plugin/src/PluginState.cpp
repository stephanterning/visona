#include "PluginState.h"

#include "ui/Palette.h"

#include <visona/LaneMapping.h>

namespace visona
{

namespace
{

constexpr const char* propertiesName = "VisonaPluginDefaults";
constexpr const char* waveformModeKey = "waveformMode";
constexpr const char* waveformColourKey = "waveformColour";

WaveformMode modeFromString(const juce::String& value)
{
    if (value == "standard")
        return WaveformMode::standard;
    if (value == "dj")
        return WaveformMode::dj;
    return WaveformMode::precise;
}

juce::String modeToString(WaveformMode mode)
{
    switch (mode)
    {
    case WaveformMode::standard:
        return "standard";
    case WaveformMode::dj:
        return "dj";
    case WaveformMode::precise:
        return "precise";
    }
    return "precise";
}

} // namespace

static juce::PropertiesFile::Options pluginDefaultsFileOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = propertiesName;
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

PluginGlobalDefaults::PluginGlobalDefaults()
{
    applicationProperties_.setStorageParameters(pluginDefaultsFileOptions());
    properties_ = applicationProperties_.getUserSettings();
}

WaveformMode PluginGlobalDefaults::waveformMode() const
{
    return modeFromString(properties_->getValue(waveformModeKey));
}

std::size_t PluginGlobalDefaults::waveformColour(std::size_t count, std::size_t fallback) const
{
    const auto index = static_cast<int>(properties_->getIntValue(waveformColourKey, -1));
    if (index < 0 || static_cast<std::size_t>(index) >= count)
        return fallback;
    return static_cast<std::size_t>(index);
}

void PluginGlobalDefaults::setWaveformMode(WaveformMode mode)
{
    properties_->setValue(waveformModeKey, modeToString(mode));
    properties_->saveIfNeeded();
}

void PluginGlobalDefaults::setWaveformColour(std::size_t index)
{
    properties_->setValue(waveformColourKey, static_cast<int>(index));
    properties_->saveIfNeeded();
}

void PluginInstanceState::writeTo(juce::XmlElement& root) const
{
    root.setAttribute("gainDb", gainDb);
    root.setAttribute("autoGain", autoGain);
    root.setAttribute("window", static_cast<int>(window));
    root.setAttribute("waveformMode", modeToString(waveformMode));
    root.setAttribute("waveformColour", static_cast<int>(waveformColour));
    root.setAttribute("hasInstanceAppearance", hasInstanceAppearance);
}

void PluginInstanceState::readFrom(const juce::XmlElement& root, PluginGlobalDefaults& defaults)
{
    gainDb = DisplayGain::clampDb(root.getIntAttribute("gainDb", gainDb));
    autoGain = root.getBoolAttribute("autoGain", false);
    window = static_cast<std::size_t>(root.getIntAttribute("window", static_cast<int>(window)));
    waveformMode = modeFromString(
        root.getStringAttribute("waveformMode", modeToString(defaults.waveformMode())));
    waveformColour = static_cast<std::size_t>(root.getIntAttribute(
        "waveformColour", static_cast<int>(defaults.waveformColour(
                              palette::waveformColours.size(), palette::defaultWaveformColour))));
    hasInstanceAppearance = root.getBoolAttribute("hasInstanceAppearance", false);
    if (!hasInstanceAppearance)
    {
        waveformMode = defaults.waveformMode();
        waveformColour = defaults.waveformColour(palette::waveformColours.size(),
                                                 palette::defaultWaveformColour);
    }
}

} // namespace visona
