#pragma once

#include <visona/HostPlayheadReader.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

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

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    [[nodiscard]] HostTransport::Playhead lastPlayhead() const noexcept
    {
        HostTransport::Playhead result;
        result.valid = hostValid_.load(std::memory_order_relaxed);
        result.isPlaying = hostPlaying_.load(std::memory_order_relaxed);
        result.bpm = hostBpm_.load(std::memory_order_relaxed);
        return result;
    }

private:
    double sampleRate_ = 0.0;
    std::uint64_t streamSample_ = 0;

    // Independent display values; the editor never reads the audio thread's mutable playhead.
    std::atomic<bool> hostValid_{false}, hostPlaying_{false};
    std::atomic<double> hostBpm_{0.0};
    HostPlayheadReader playheadReader_;
};

} // namespace visona
