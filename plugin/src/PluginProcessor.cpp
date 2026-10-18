#include "PluginProcessor.h"

#include "HostPlayheadReader.h"
#include "PluginEditor.h"
#include "ui/Palette.h"

#include <visona/AudioRingSizing.h>
#include <visona/SweepWindow.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace visona
{

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withInput("Sidechain", juce::AudioChannelSet::mono(), false)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , analysis_(layout_.totalChannelCount())
{
    instanceState_.waveformMode = globalDefaults_.waveformMode();
    instanceState_.waveformColour = globalDefaults_.waveformColour(palette::waveformColours.size(),
                                                                   palette::defaultWaveformColour);
}

PluginProcessor::~PluginProcessor()
{
    releaseResources();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    releaseResources(); // Detach before freeing a ring the analysis thread may still read.
    playheadReader_ = {};
    sampleRate_ = sampleRate;
    ring_ = std::make_unique<AudioRingBuffer>(
        layout_.totalChannelCount(), ringCapacityFrames(sampleRate, samplesPerBlock),
        ringTimingSlots(ringCapacityFrames(sampleRate, samplesPerBlock)));
    writer_ = std::make_unique<AudioInputWriter>(*ring_);
    writer_->route(0, 0);
    writer_->route(1, 1);
    channelPointers_.assign(layout_.totalChannelCount(), nullptr);
    analysis_.setWindow(instanceState_.window);
    analysis_.setBandSplitting(instanceState_.waveformMode == WaveformMode::dj);
    sidechainSync_.reset();
    analysis_.setAnalysisOffset(0.0);
    analysis_.setStream(ring_.get(), sampleRate);
}

void PluginProcessor::releaseResources()
{
    analysis_.setStream(nullptr, 0.0);
    analysis_.setAnalysisOffset(0.0);
    writer_.reset();
    ring_.reset();
    sidechainSync_.setSidechainEnabled(false);
    syncState_.store(SidechainSyncState::off, std::memory_order_relaxed);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto& mainInput = layouts.getMainInputChannelSet();
    if (mainInput != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet(true, 1);
        if (sidechain != juce::AudioChannelSet::disabled() &&
            sidechain != juce::AudioChannelSet::mono())
            return false;
    }

    return true;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    const auto mainInput = getBusBuffer(buffer, true, 0);
    const auto numSamples = static_cast<std::uint32_t>(mainInput.getNumSamples());
    for (int channel = 0; channel < mainInput.getNumChannels(); ++channel)
        channelPointers_[static_cast<std::size_t>(channel)] = mainInput.getReadPointer(channel);
    lastBlockSize_.store(numSamples, std::memory_order_relaxed);

    // The host reports the playhead at the block's first frame.
    const auto playhead = readPlayhead();
    if (writer_ != nullptr)
    {
        const auto blockStartSample = writer_->nextSampleIndex();
        writer_->write(channelPointers_, numSamples, 0);
        analysis_.pushPlayhead(blockStartSample, playhead);
    }

    updateSidechainSync(buffer, playhead);
}

void PluginProcessor::updateSidechainSync(juce::AudioBuffer<float>& buffer,
                                          const HostTransport::Playhead& playhead) noexcept
{
    const auto* bus = getBus(true, 1);
    const auto sidechain = bus != nullptr && bus->isEnabled() ? getBusBuffer(buffer, true, 1)
                                                              : juce::AudioBuffer<float>();
    const bool enabled = sidechain.getNumChannels() > 0 && sidechain.getNumSamples() > 0;
    sidechainSync_.setSidechainEnabled(enabled);
    if (enabled)
    {
        const std::span<const float> samples{sidechain.getReadPointer(0),
                                             static_cast<std::size_t>(sidechain.getNumSamples())};
        float peak = 0.0f;
        for (const auto sample : samples)
            peak = std::max(peak, std::abs(sample));
        auto held = sidechainPeak_.load(std::memory_order_relaxed);
        while (peak > held &&
               !sidechainPeak_.compare_exchange_weak(held, peak, std::memory_order_relaxed))
        {
        }

        sidechainSync_.processBlock(samples, playhead.ppqPosition, playhead.bpm,
                                    playhead.timeSignature, sampleRate_,
                                    playhead.valid && playhead.isPlaying);
    }

    const auto state = sidechainSync_.state();
    analysis_.setAnalysisOffset(state == SidechainSyncState::locked ? sidechainSync_.offsetFrames()
                                                                    : 0.0);
    syncState_.store(state, std::memory_order_relaxed);
    syncOffsetFrames_.store(sidechainSync_.offsetFrames(), std::memory_order_relaxed);
    syncImpulsePeak_.store(sidechainSync_.impulsePeak(), std::memory_order_relaxed);
}

HostTransport::Playhead PluginProcessor::readPlayhead() noexcept
{
    return readHostPlayhead(getPlayHead(), playheadReader_);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const
{
    return true;
}

const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
    return false;
}

bool PluginProcessor::producesMidi() const
{
    return false;
}

bool PluginProcessor::isMidiEffect() const
{
    return false;
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String PluginProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void PluginProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement state("VisonaPlugin");
    instanceState_.writeTo(state);
    copyXmlToBinary(state, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        instanceState_.readFrom(*xml, globalDefaults_);
}

} // namespace visona

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new visona::PluginProcessor();
}
