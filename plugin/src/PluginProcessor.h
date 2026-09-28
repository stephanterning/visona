#pragma once

#include "PluginAnalysisThread.h"
#include "PluginState.h"

#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/HostTransport.h>
#include <visona/SourceLayout.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace visona
{

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] PluginAnalysisThread& analysis() noexcept
    {
        return analysis_;
    }

    [[nodiscard]] const SourceLayout& layout() const noexcept
    {
        return layout_;
    }

    [[nodiscard]] PluginInstanceState& instanceState() noexcept
    {
        return instanceState_;
    }

    [[nodiscard]] PluginGlobalDefaults& globalDefaults() noexcept
    {
        return globalDefaults_;
    }

private:
    [[nodiscard]] HostTransport::Playhead readPlayhead(int numSamples) const noexcept;

    const SourceLayout layout_{2};
    PluginGlobalDefaults globalDefaults_;
    PluginInstanceState instanceState_;
    PluginAnalysisThread analysis_;

    std::unique_ptr<AudioRingBuffer> ring_;
    std::unique_ptr<AudioInputWriter> writer_;
    std::vector<const float*> channelPointers_;
    double sampleRate_ = 0.0;
};

} // namespace visona
