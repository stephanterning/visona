#pragma once

#include <visona/HostTransport.h>

#include <juce_audio_processors/juce_audio_processors.h>

namespace visona
{

class SyncProcessor final : public juce::AudioProcessor
{
public:
    SyncProcessor();
    ~SyncProcessor() override;

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

    [[nodiscard]] HostTransport::Playhead lastPlayhead() const noexcept
    {
        return lastPlayhead_;
    }

private:
    [[nodiscard]] HostTransport::Playhead readPlayhead(int numSamples) noexcept;

    double sampleRate_ = 0.0;
    std::uint64_t streamSample_ = 0;

    HostTransport::Playhead lastPlayhead_;
    double lastKnownBpm_ = 120.0;
    double lastKnownPpq_ = 0.0;
    bool lastKnownPlaying_ = false;
    TimeSignature lastKnownTimeSignature_{};
};

} // namespace visona
