#pragma once

#include "PluginProcessor.h"
#include "ScopeEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace visona
{

class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor_;
    ScopeEditor editor_;
};

} // namespace visona
