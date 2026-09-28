#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ui/Palette.h"

#include <visona/SweepWindow.h>

namespace visona
{

namespace
{

constexpr double ringSeconds = 1.0;

std::size_t ringCapacityFrames(double sampleRate, int bufferSize)
{
    const auto oneSecond =
        static_cast<std::size_t>(std::ceil(std::max(sampleRate, 1.0) * ringSeconds));
    return std::max(oneSecond, static_cast<std::size_t>(std::max(bufferSize, 1)) * 4);
}

} // namespace

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::mono(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , analysis_(layout_.totalChannelCount())
{
    instanceState_.waveformMode = globalDefaults_.waveformMode();
    instanceState_.waveformColour = globalDefaults_.waveformColour(
        palette::waveformColours.size(), palette::defaultWaveformColour);
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    streamSample_ = 0;
    ring_ = std::make_unique<AudioRingBuffer>(
        layout_.totalChannelCount(), ringCapacityFrames(sampleRate, samplesPerBlock),
        std::max<std::size_t>(ringCapacityFrames(sampleRate, samplesPerBlock) /
                                  static_cast<std::size_t>(std::max(samplesPerBlock, 1)),
                              1));
    writer_ = std::make_unique<AudioInputWriter>(*ring_);
    writer_->route(0, 0);
    writer_->route(1, 1);
    channelPointers_.assign(layout_.totalChannelCount(), nullptr);
    analysis_.setStream(ring_.get(), sampleRate);
    analysis_.setWindow(instanceState_.window);
    analysis_.setBandSplitting(instanceState_.waveformMode == WaveformMode::dj);
    sidechainSync_.reset();
}

void PluginProcessor::releaseResources()
{
    analysis_.setStream(nullptr, 0.0);
    analysis_.setAnalysisOffset(0.0);
    writer_.reset();
    ring_.reset();
    sidechainSync_.setSidechainEnabled(false);
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
    for (int channel = 0; channel < mainInput.getNumChannels(); ++channel)
        channelPointers_[static_cast<std::size_t>(channel)] = mainInput.getReadPointer(channel);

    if (writer_ != nullptr)
        writer_->write(channelPointers_, static_cast<std::uint32_t>(mainInput.getNumSamples()), 0);

    const auto numSamples = static_cast<std::uint32_t>(mainInput.getNumSamples());
    streamSample_ += numSamples;

    const auto playhead = readPlayhead(static_cast<int>(numSamples));
    if (writer_ != nullptr)
        analysis_.setPlayhead(static_cast<double>(writer_->nextSampleIndex()), playhead);

    sidechainInputEnabled_ = false;
    if (getBus(true, 1) != nullptr && getBus(true, 1)->isEnabled())
    {
        const auto sidechainInput = getBusBuffer(buffer, true, 1);
        if (sidechainInput.getNumChannels() > 0)
        {
            sidechainInputEnabled_ = true;
            sidechainSync_.setSidechainEnabled(true);
            sidechainSync_.processBlock(
                {sidechainInput.getReadPointer(0), sidechainInput.getNumSamples()}, streamSample_,
                numSamples, playhead.ppqPosition, playhead.bpm, playhead.timeSignature, sampleRate_,
                playhead.isPlaying);
        }
    }

    if (!sidechainInputEnabled_)
        sidechainSync_.setSidechainEnabled(false);

    if (sidechainSync_.state() == SidechainSyncState::locked)
        analysis_.setAnalysisOffset(sidechainSync_.offsetFrames());
    else
        analysis_.setAnalysisOffset(0.0);
}

HostTransport::Playhead PluginProcessor::readPlayhead(int numSamples) const noexcept
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
