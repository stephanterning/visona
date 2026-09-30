#include "AudioEngine.h"

#include "HostTime.h"
#include "RealtimeAllocationCheck.h"
#include "Settings.h"

#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace visona
{

namespace
{

// JUCE opens the first N input channels by default. N is above any real device's channel count,
// so every input channel is opened.
constexpr int allInputChannels = 1024;

constexpr double ringSeconds = 1.0;

// The frames fill up before the timings unless blocks average fewer frames than this.
constexpr std::size_t framesPerTimingSlot = 16;

constexpr int devicePollMs = 500;

// A running device that delivers nothing for this long is lost. JUCE's ALSA thread ends without
// telling anyone when its device is unplugged or fails, and the device still reports that it plays.
constexpr int stallTicks = 4;

// How often opening the chosen device is tried while it is listed but does not open.
constexpr juce::uint32 openRetryMs = 2'000;

/** The sound cards there are, as text that changes whenever one is added or removed. */
juce::String soundCards()
{
#if JUCE_LINUX
    return juce::File("/proc/asound/cards").loadFileAsString();
#else
    return {};
#endif
}

std::size_t ringCapacityFrames(double sampleRate, int bufferSize)
{
    const auto oneSecond =
        static_cast<std::size_t>(std::ceil(std::max(sampleRate, 1.0) * ringSeconds));
    return std::max(oneSecond, static_cast<std::size_t>(std::max(bufferSize, 1)) * 4);
}

/** The index of a device input channel among the channels the callback delivers, or noInput. The
    callback delivers the active channels in ascending order, without gaps. */
int callbackInputIndex(const juce::BigInteger& activeInputs, int deviceChannel)
{
    if (deviceChannel < 0 || !activeInputs[deviceChannel])
        return AudioInputWriter::noInput;
    return activeInputs.getBitRange(0, deviceChannel).countNumberOfSetBits();
}

} // namespace

struct AudioEngine::Stream
{
    Stream(std::size_t numChannels, std::size_t capacityFrames, juce::BigInteger activeInputsIn,
           int numDeviceInputsIn)
        : ring(numChannels, capacityFrames,
               std::max<std::size_t>(capacityFrames / framesPerTimingSlot, 1))
        , writer(ring)
        , activeInputs(std::move(activeInputsIn))
        , numDeviceInputs(numDeviceInputsIn)
    {
    }

    AudioRingBuffer ring;
    AudioInputWriter writer;
    const juce::BigInteger activeInputs;
    const int numDeviceInputs;

    std::atomic<std::uint32_t> blockSize{0};
    std::atomic<bool> deviceHostTime{false};
};

AudioEngine::AudioEngine(Settings& settings)
    : settings_(settings)
    , layout_{2} // MVP 1.0 has exactly one stereo source (D-052).
    , midi_(settings)
    , analysis_(layout_.totalChannelCount())
    , inputChannels_(settings.inputChannels(layout_.totalChannelCount()))
{
    analysis_.setMidiQueue(&midi_.queue());
    analysis_.setFreeTempo(settings_.freeTempo());
    deviceManager_.addAudioCallback(this);
    deviceManager_.addChangeListener(this);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
}

void AudioEngine::openSavedDevice()
{
    soundCards_ = soundCards();
    const auto savedState = settings_.audioDeviceState();
    lastError_ = deviceManager_.initialise(allInputChannels, 0, savedState.get(), false);

    // JUCE does not count the default device it opens without a saved state as chosen, so it
    // would not be saved. Reopening it as a choice saves it, and the next start restores this
    // device instead of whatever the default is then.
    if (savedState == nullptr && deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        const auto setup = deviceManager_.getAudioDeviceSetup();
        deviceManager_.closeAudioDevice();
        applySetup(setup);
    }
    waitingForChosen_ = deviceManager_.getCurrentAudioDevice() == nullptr;
    nextOpenAttemptMs_ = juce::Time::getMillisecondCounter() + openRetryMs;

    midi_.reconnect();
    midiDevicesChanged_ = juce::MidiDeviceListConnection::make(
        [this]
        {
            if (midi_.reconnect())
                deviceManager_.sendChangeMessage();
        });
    startTimer(devicePollMs);
}

bool AudioEngine::isInputRunning() const
{
    auto* const device = deviceManager_.getCurrentAudioDevice();
    return device != nullptr && device->isPlaying() && !device->getActiveInputChannels().isZero();
}

juce::String AudioEngine::noInputReason() const
{
    if (isInputRunning())
        return {};

    auto* const type = deviceManager_.getCurrentDeviceTypeObject();
    const auto availableDevices =
        type != nullptr ? type->getDeviceNames(true) : juce::StringArray{};

    if (const auto state = deviceManager_.createStateXml())
    {
        const auto savedDevice = state->getStringAttribute("audioInputDeviceName");
        if (savedDevice.isNotEmpty() &&
            !isListed(state->getStringAttribute("deviceType"), savedDevice))
            return "The audio device \"" + savedDevice + "\" is not connected." +
                   (waitingForChosen_ ? " Visona opens it when it is plugged in." : "");
    }
    if (lastError_.isNotEmpty())
        return lastError_;
    if (availableDevices.isEmpty())
        return "No audio input device was found.";
    return "No audio input device is selected.";
}

std::optional<AudioEngine::StreamStatus> AudioEngine::streamStatus() const
{
    const std::scoped_lock lock(lock_);
    if (stream_ == nullptr)
        return std::nullopt;

    return StreamStatus{stream_->writer.nextSampleIndex(),
                        stream_->blockSize.load(std::memory_order_relaxed),
                        stream_->deviceHostTime.load(std::memory_order_relaxed),
                        stream_->ring.overrunCount(), stream_->ring.droppedFrameCount()};
}

void AudioEngine::takePeaks(std::span<float> peaks)
{
    for (std::size_t channel = 0; channel < peaks.size(); ++channel)
        peaks[channel] = channel < layout_.totalChannelCount() ? analysis_.takePeak(channel) : 0.0f;
}

TripleBuffer<SweepSnapshot>& AudioEngine::snapshots() noexcept
{
    return analysis_.snapshots();
}

std::uint64_t AudioEngine::analysisBusyNanoseconds() const noexcept
{
    return analysis_.busyNanoseconds();
}

void AudioEngine::setWindow(std::size_t windowIndex) noexcept
{
    analysis_.setWindow(windowIndex);
}

double AudioEngine::freeTempo() const
{
    return settings_.freeTempo();
}

void AudioEngine::setFreeTempo(double bpm)
{
    settings_.setFreeTempo(bpm);
    analysis_.setFreeTempo(settings_.freeTempo());
}

void AudioEngine::runFree() noexcept
{
    analysis_.runFree();
}

void AudioEngine::setBandSplitting(bool enabled) noexcept
{
    analysis_.setBandSplitting(enabled);
}

juce::String AudioEngine::midiInput() const
{
    return midi_.identifier();
}

juce::String AudioEngine::midiInputName() const
{
    return midi_.name();
}

juce::String AudioEngine::selectMidiInput(const juce::String& identifier)
{
    return midi_.select(identifier);
}

juce::AudioDeviceManager& AudioEngine::deviceManager() noexcept
{
    return deviceManager_;
}

juce::String AudioEngine::selectDeviceType(const juce::String& typeName)
{
    // A new type starts from its default setup, which uses the default (all) input channels.
    deviceManager_.setCurrentAudioDeviceType(typeName, true);
    lastError_.clear();
    waitingForChosen_ = false;
    return {};
}

juce::String AudioEngine::selectDevice(const juce::String& inputDeviceName)
{
    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.inputDeviceName = inputDeviceName;
    // Keep the new device at its current rate instead of forcing the previous device's rate on it,
    // since a device synced to a digital input must run at the incoming rate.
    setup.sampleRate = 0.0;
    return applySetup(setup);
}

juce::String AudioEngine::selectSampleRate(double sampleRate)
{
    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.sampleRate = sampleRate;
    return applySetup(setup);
}

juce::String AudioEngine::selectBufferSize(int bufferSizeSamples)
{
    auto setup = deviceManager_.getAudioDeviceSetup();
    setup.bufferSize = bufferSizeSamples;
    return applySetup(setup);
}

const SourceLayout& AudioEngine::layout() const noexcept
{
    return layout_;
}

int AudioEngine::inputChannel(std::size_t channel) const
{
    const std::scoped_lock lock(lock_);
    return effectiveInputChannel(channel, stream_ != nullptr ? stream_->numDeviceInputs : 0);
}

void AudioEngine::setInputChannel(std::size_t channel, int deviceInputChannel)
{
    jassert(channel < inputChannels_.size() && deviceInputChannel >= 0);
    std::vector<int> chosen;
    {
        const std::scoped_lock lock(lock_);
        inputChannels_[channel] = deviceInputChannel;
        routeInputs();
        chosen = inputChannels_;
    }
    settings_.setInputChannels(chosen);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    auto stream = std::make_unique<Stream>(
        layout_.totalChannelCount(),
        ringCapacityFrames(sampleRate, device->getCurrentBufferSizeSamples()),
        device->getActiveInputChannels(), device->getInputChannelNames().size());

    // Audio captured at some moment appears in the stream the input latency later than a MIDI
    // event stamped at that moment (D-078). CoreAudio's input timestamp already marks the start
    // of the buffer; the fallback clock is read after the buffer has filled.
    const bool deviceTimestamps = device->getTypeName() == "CoreAudio";
    const auto latency =
        std::max(device->getInputLatencyInSamples() -
                     (deviceTimestamps ? device->getCurrentBufferSizeSamples() : 0),
                 0);
    midiOffsetFrames_.store(static_cast<double>(latency), std::memory_order_relaxed);
    analysis_.setMidiOffset(static_cast<double>(latency));
    analysis_.setStream(&stream->ring, sampleRate);

    const std::scoped_lock lock(lock_);
    stream_ = std::move(stream);
    routeInputs();
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
    int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    const bool deviceHostTime = context.hostTimeNs != nullptr;
    const auto hostTimeNs = deviceHostTime ? *context.hostTimeNs : monotonicHostTimeNs();
    [[maybe_unused]] const RealtimeAllocationCheck::Scope allocationCheck;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    auto* const stream = stream_.get();
    if (stream == nullptr || numSamples <= 0)
        return;

    const auto numFrames = static_cast<std::uint32_t>(numSamples);
    const std::span<const float* const> inputs(
        inputChannelData, static_cast<std::size_t>(std::max(numInputChannels, 0)));
    stream->writer.write(inputs, numFrames, hostTimeNs);
    stream->blockSize.store(numFrames, std::memory_order_relaxed);
    stream->deviceHostTime.store(deviceHostTime, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped()
{
    // The analysis thread lets go of the ring before the ring is destroyed.
    analysis_.setStream(nullptr, 0.0);
    const std::scoped_lock lock(lock_);
    stream_.reset();
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // When the chosen device disappears, JUCE opens the default device in its place; this closes
    // it again before the other listeners see it.
    if (isTimerRunning() && followChosenDevice())
        deviceManager_.sendChangeMessage();

    // The state is the last device the user chose. JUCE keeps it when the saved device is missing,
    // so a missing device stays saved for the next start.
    if (const auto state = deviceManager_.createStateXml())
        settings_.setAudioDeviceState(*state);
}

void AudioEngine::timerCallback()
{
    bool changed = false;
    if (const auto cards = soundCards(); cards != soundCards_)
    {
        soundCards_ = cards;
        rescanDevices();
        changed = true;
    }
    changed = closeStalledDevice() || changed;
    changed = followChosenDevice() || changed;
    changed = midi_.reconnect() || changed;
    if (changed)
        deviceManager_.sendChangeMessage();
}

juce::String AudioEngine::applySetup(juce::AudioDeviceManager::AudioDeviceSetup setup)
{
    setup.outputDeviceName.clear();
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    lastError_ = deviceManager_.setAudioDeviceSetup(setup, true);
    waitingForChosen_ = false;
    return lastError_;
}

bool AudioEngine::followChosenDevice()
{
    const auto chosen = deviceManager_.createStateXml();
    if (chosen == nullptr)
        return false;
    const auto typeName = chosen->getStringAttribute("deviceType");
    const auto deviceName = chosen->getStringAttribute("audioInputDeviceName");
    if (deviceName.isEmpty())
        return false;

    bool changed = false;
    if (auto* const device = deviceManager_.getCurrentAudioDevice())
    {
        const bool isChosen = device->getTypeName() == typeName &&
                              deviceManager_.getAudioDeviceSetup().inputDeviceName == deviceName;
        if (isChosen && isListed(typeName, deviceName))
        {
            waitingForChosen_ = false;
            return false;
        }
        deviceManager_.closeAudioDevice();
        waitingForChosen_ = true;
        lastError_.clear();
        changed = true;
    }

    const auto now = juce::Time::getMillisecondCounter();
    if (!waitingForChosen_ || !isListed(typeName, deviceName) ||
        static_cast<std::int32_t>(now - nextOpenAttemptMs_) < 0)
        return changed;

    nextOpenAttemptMs_ = now + openRetryMs;
    lastError_ = deviceManager_.initialise(allInputChannels, 0, chosen.get(), false);
    if (deviceManager_.getCurrentAudioDevice() != nullptr)
        waitingForChosen_ = false;
    return true;
}

bool AudioEngine::closeStalledDevice()
{
    const auto status = streamStatus();
    if (!status.has_value() || !isInputRunning() || status->framesDelivered != lastFramesDelivered_)
    {
        lastFramesDelivered_ = status.has_value() ? status->framesDelivered : 0;
        stalledTicks_ = 0;
        return false;
    }
    if (++stalledTicks_ < stallTicks)
        return false;

    stalledTicks_ = 0;
    deviceManager_.closeAudioDevice();
    waitingForChosen_ = true;
    lastError_ = "The audio device stopped delivering audio.";
    // It may be back already, so it is opened again at once if it is listed.
    nextOpenAttemptMs_ = juce::Time::getMillisecondCounter();
    return true;
}

void AudioEngine::rescanDevices()
{
    // An open device does not depend on its type, so it keeps running if it is still there. The
    // old types go first: JUCE's ALSA type resets the ALSA error handler that a new one sets.
    juce::Array<juce::AudioIODeviceType*> oldTypes;
    for (auto* const type : deviceManager_.getAvailableDeviceTypes())
        oldTypes.add(type);
    for (auto* const type : oldTypes)
        deviceManager_.removeAudioDeviceType(type);

    juce::OwnedArray<juce::AudioIODeviceType> newTypes;
    deviceManager_.createAudioDeviceTypes(newTypes);
    while (!newTypes.isEmpty())
    {
        std::unique_ptr<juce::AudioIODeviceType> type(newTypes.removeAndReturn(0));
        type->scanForDevices();
        deviceManager_.addAudioDeviceType(std::move(type));
    }
}

bool AudioEngine::isListed(const juce::String& typeName, const juce::String& inputDeviceName) const
{
    // Not const in JUCE only because it creates the types on first use.
    auto& deviceManager = const_cast<juce::AudioDeviceManager&>(deviceManager_);
    for (auto* const type : deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() == typeName)
            for (const auto& name : type->getDeviceNames(true))
                if (name.trim().equalsIgnoreCase(inputDeviceName.trim()))
                    return true;
    return false;
}

int AudioEngine::effectiveInputChannel(std::size_t channel, int numDeviceInputs) const
{
    if (numDeviceInputs <= 0)
        return -1;
    const auto saved = inputChannels_[channel];
    if (saved >= 0 && saved < numDeviceInputs)
        return saved;
    return std::min(static_cast<int>(channel), numDeviceInputs - 1);
}

void AudioEngine::routeInputs()
{
    if (stream_ == nullptr)
        return;
    for (std::size_t channel = 0; channel < layout_.totalChannelCount(); ++channel)
    {
        const auto deviceChannel = effectiveInputChannel(channel, stream_->numDeviceInputs);
        stream_->writer.route(channel, callbackInputIndex(stream_->activeInputs, deviceChannel));
    }
}

} // namespace visona
