#include "PluginEditor.h"

namespace visona
{

PluginEditor::PluginEditor(PluginProcessor& processor)
    : juce::AudioProcessorEditor(processor)
    , processor_(processor)
    , editor_(processor)
{
    editor_.onStateChange = [this](const PluginInstanceState& state)
    {
        processor_.instanceState() = state;
        processor_.analysis().setWindow(state.window);
        processor_.analysis().setBandSplitting(state.waveformMode == WaveformMode::dj);
    };

    addAndMakeVisible(editor_);
    setResizable(true, true);
    setResizeLimits(640, 480, 4096, 2160);
    setSize(960, 640);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void PluginEditor::resized()
{
    editor_.setBounds(getLocalBounds());
}

} // namespace visona
