#include "SyncProcessor.h"

#include "SyncEditor.h"

#include <visona/BarImpulseScheduler.h>

namespace visona
{

SyncProcessor::SyncProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(),
                                                        true))
{
}

SyncProcessor::~SyncProcessor() = default;

void SyncProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    sampleRate_ = sampleRate;
    streamSample_ = 0;
}

void SyncProcessor::releaseResources()
{
    sampleRate_ = 0.0;
    streamSample_ = 0;
}

void SyncProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const auto numSamples = static_cast<std::uint32_t>(buffer.getNumSamples());
    const auto blockStartSample = streamSample_;
    streamSample_ += numSamples;

    const auto playhead = readPlayhead(static_cast<int>(numSamples));
    lastPlayhead_ = playhead;
    if (!playhead.valid || !playhead.isPlaying || sampleRate_ <= 0.0)
        return;

    std::vector<std::uint32_t> offsets;
    BarImpulseScheduler::impulsesInBlock(blockStartSample, numSamples, playhead.ppqPosition,
                                         playhead.bpm, playhead.timeSignature, sampleRate_,
                                         offsets);
    for (const auto offset : offsets)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, static_cast<int>(offset), syncImpulseAmplitude);
    }
}

HostTransport::Playhead SyncProcessor::readPlayhead(int numSamples) noexcept
{
    HostTransport::Playhead playhead;
    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            playhead.isPlaying = position->getIsPlaying();
            if (position->getPpqPosition())
                playhead.ppqPosition = *position->getPpqPosition();
            else
                playhead.ppqPosition = lastKnownPpq_;
            if (position->getBpm())
                playhead.bpm = *position->getBpm();
            else
                playhead.bpm = lastKnownBpm_;
            if (position->getTimeSignature())
                playhead.timeSignature = {position->getTimeSignature()->numerator,
                                          position->getTimeSignature()->denominator};
            else
                playhead.timeSignature = lastKnownTimeSignature_;

            playhead.valid = playhead.bpm > 0.0 &&
                             (position->getPpqPosition().hasValue() ||
                              position->getBpm().hasValue() || position->getIsPlaying());

            if (playhead.valid)
            {
                lastKnownBpm_ = playhead.bpm;
                lastKnownPpq_ = playhead.ppqPosition;
                lastKnownPlaying_ = playhead.isPlaying;
                lastKnownTimeSignature_ = playhead.timeSignature;
            }
        }
    }
    juce::ignoreUnused(numSamples);
    return playhead;
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
    return false;
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
    juce::ignoreUnused(destData);
}

void SyncProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

} // namespace visona

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new visona::SyncProcessor();
}
