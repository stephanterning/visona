#include "SyncProcessor.h"

#include "HostPlayheadReader.h"
#include "SyncEditor.h"

#include <visona/BarImpulseScheduler.h>

#include <algorithm>
#include <array>

namespace visona
{

SyncProcessor::SyncProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              // Ableton Live rejects VST3 plugins with no input bus declared (ABLDEV-00057).
              .withInput("Input", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

SyncProcessor::~SyncProcessor() = default;

void SyncProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    playheadReader_ = {};
    sampleRate_ = sampleRate;
}

void SyncProcessor::releaseResources()
{
    sampleRate_ = 0.0;
}

void SyncProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const auto numSamples = static_cast<std::uint32_t>(buffer.getNumSamples());

    const auto playhead = readHostPlayhead(getPlayHead(), playheadReader_);
    hostValid_.store(playhead.valid, std::memory_order_relaxed);
    hostPlaying_.store(playhead.isPlaying, std::memory_order_relaxed);
    hostBpm_.store(playhead.bpm, std::memory_order_relaxed);
    if (!playhead.valid || !playhead.isPlaying || sampleRate_ <= 0.0)
        return;

    // At most one impulse per sample. Chunking bounds stack storage even if the host
    // exceeds its advertised maximum block size, without dropping any impulses.
    std::array<std::uint32_t, 128> offsets{};
    for (std::uint32_t first = 0; first < numSamples;)
    {
        const auto frames =
            std::min(numSamples - first, static_cast<std::uint32_t>(offsets.size()));
        const auto ppq =
            playhead.ppqPosition + static_cast<double>(first) / sampleRate_ * playhead.bpm / 60.0;
        const auto count = BarImpulseScheduler::impulsesInBlock(
            frames, ppq, playhead.bpm, playhead.timeSignature, sampleRate_, offsets);
        for (std::size_t i = 0; i < count; ++i)
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample(channel, static_cast<int>(first + offsets[i]),
                                 syncImpulseAmplitude);
        first += frames;
    }
}

juce::AudioProcessorEditor* SyncProcessor::createEditor()
{
    return new SyncEditor(*this);
}

bool SyncProcessor::hasEditor() const
{
    return true;
}

const juce::String SyncProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SyncProcessor::acceptsMidi() const
{
    return true;
}

bool SyncProcessor::producesMidi() const
{
    return false;
}

bool SyncProcessor::isMidiEffect() const
{
    return false;
}

double SyncProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SyncProcessor::getNumPrograms()
{
    return 1;
}

int SyncProcessor::getCurrentProgram()
{
    return 0;
}

void SyncProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String SyncProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void SyncProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void SyncProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Visona Sync has no settings, but the CLAP wrapper fails to load an empty state.
    constexpr int stateVersion = 1;
    juce::MemoryOutputStream(destData, false).writeInt(stateVersion);
}

void SyncProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

bool SyncProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 0)
    {
        const auto& input = layouts.getMainInputChannelSet();
        if (input != juce::AudioChannelSet::disabled() && input != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

} // namespace visona

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new visona::SyncProcessor();
}
