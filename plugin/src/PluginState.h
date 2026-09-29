#pragma once

#include <visona/AutoGain.h>
#include <visona/WaveformStyle.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstddef>

namespace visona
{

/**
    Saved plugin appearance settings. Instance state in the processor wins over these global
    defaults, which live beside the standalone settings file.
*/
class PluginGlobalDefaults
{
public:
    PluginGlobalDefaults();

    [[nodiscard]] WaveformMode waveformMode() const;
    [[nodiscard]] std::size_t waveformColour(std::size_t count, std::size_t fallback) const;

    void setWaveformMode(WaveformMode mode);
    void setWaveformColour(std::size_t index);

private:
    juce::ApplicationProperties applicationProperties_;
    juce::PropertiesFile* properties_ = nullptr;
};

struct PluginInstanceState
{
    /** The gain set by hand. While auto gain is on, the processor's AutoGain sets the gain shown.
     */
    int gainDb = 0;
    bool autoGain = false;
    /** An index into AutoGain::holdChoices. */
    std::size_t autoGainHold = AutoGain::defaultHoldChoice;
    std::size_t window = 2;
    WaveformMode waveformMode = WaveformMode::precise;
    std::size_t waveformColour = 0;
    bool hasInstanceAppearance = false;

    void writeTo(juce::XmlElement& root) const;
    void readFrom(const juce::XmlElement& root, PluginGlobalDefaults& defaults);
};

} // namespace visona
