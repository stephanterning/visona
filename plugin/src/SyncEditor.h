#pragma once

#include "SyncProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

class SyncEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SyncEditor(SyncProcessor& processor);
    ~SyncEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    SyncProcessor& processor_;
    juce::Label title_;
    juce::Label status_;
};

} // namespace visona
