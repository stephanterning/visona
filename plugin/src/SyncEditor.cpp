#include "SyncEditor.h"

#include "ui/Palette.h"

namespace visona
{

SyncEditor::SyncEditor(SyncProcessor& syncProcessor)
    : juce::AudioProcessorEditor(syncProcessor)
    , processor_(syncProcessor)
{
    title_.setText("Visona Sync", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    title_.setColour(juce::Label::textColourId, palette::text);
    status_.setFont(juce::Font(juce::FontOptions(16.0f)));
    status_.setColour(juce::Label::textColourId, palette::level);
    addAndMakeVisible(title_);
    addAndMakeVisible(status_);
    setSize(360, 160);
    startTimerHz(10);
}

SyncEditor::~SyncEditor()
{
    stopTimer();
}

void SyncEditor::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
}

void SyncEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    title_.setBounds(area.removeFromTop(32));
    status_.setBounds(area.removeFromTop(28));
}

void SyncEditor::timerCallback()
{
    const auto playhead = processor_.lastPlayhead();
    juce::String text = "Waiting for host transport";
    if (playhead.valid)
    {
        text = playhead.isPlaying ? "HOST RUN - bar impulses at -6 dBFS"
                                  : "STOPPED";
        if (playhead.bpm > 0.0)
            text += " | " + juce::String(playhead.bpm, 1) + " BPM";
    }
    status_.setText(text, juce::dontSendNotification);
}

} // namespace visona
