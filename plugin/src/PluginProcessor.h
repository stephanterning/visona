#pragma once

#include "PluginAnalysisThread.h"
#include "PluginState.h"

#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/HostTransport.h>
#include <visona/SidechainSyncDetector.h>
#include <visona/SourceLayout.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
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
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

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

    /** Any thread. The sidechain sync state after the latest block. */
    [[nodiscard]] SidechainSyncState sidechainSyncState() const noexcept
    {
        return syncState_.load(std::memory_order_relaxed);
    }

    /** Any thread. Frames the audio here lags the host playhead, as Visona Sync measured it. */
    [[nodiscard]] double sidechainSyncOffsetFrames() const noexcept
    {
        return syncOffsetFrames_.load(std::memory_order_relaxed);
    }

    /** Any thread. The peak level of the latest bar impulse, as a gain. */
    [[nodiscard]] float sidechainImpulsePeak() const noexcept
    {
        return syncImpulsePeak_.load(std::memory_order_relaxed);
    }

    /** Any thread. The highest sidechain sample since the previous call. */
    [[nodiscard]] float takeSidechainPeak() noexcept
    {
        return sidechainPeak_.exchange(0.0f, std::memory_order_relaxed);
    }

    /** Any thread. Frames in the latest block the host processed. */
    [[nodiscard]] std::uint32_t lastBlockSize() const noexcept
    {
        return lastBlockSize_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] HostTransport::Playhead readPlayhead() const noexcept;
    void updateSidechainSync(juce::AudioBuffer<float>& buffer,
                             const HostTransport::Playhead& playhead) noexcept;

    const SourceLayout layout_{2};
    PluginGlobalDefaults globalDefaults_;
    PluginInstanceState instanceState_;
    PluginAnalysisThread analysis_;
    SidechainSyncDetector sidechainSync_;

    std::unique_ptr<AudioRingBuffer> ring_;
    std::unique_ptr<AudioInputWriter> writer_;
    std::vector<const float*> channelPointers_;
    double sampleRate_ = 0.0;

    std::atomic<SidechainSyncState> syncState_{SidechainSyncState::off};
    std::atomic<double> syncOffsetFrames_{0.0};
    std::atomic<float> syncImpulsePeak_{0.0f};
    std::atomic<float> sidechainPeak_{0.0f};
    std::atomic<std::uint32_t> lastBlockSize_{0};

    mutable double lastKnownBpm_ = 120.0;
    mutable double lastKnownPpq_ = 0.0;
    mutable bool lastKnownPlaying_ = false;
    mutable TimeSignature lastKnownTimeSignature_{};
};

} // namespace visona
