#pragma once

#include "ui/AudioSettings.h"

#include <visona/SourceLayout.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace visona
{

class Settings;

/**
    The audio device and the audio callback.

    The device is opened with all of its input channels and no outputs. The callback copies the
    input channel chosen for each source channel into the audio ring, so choosing other input
    channels takes effect at once and never restarts the device.

    Every time the device starts, a new stream begins, with its own ring, writer and reader, and
    sampleIndex from 0. The device state and the input channels are saved in Settings whenever they
    change. Public functions are for the message thread.
*/
class AudioEngine final : public AudioSettings,
                          private juce::AudioIODeviceCallback,
                          private juce::ChangeListener
{
public:
    explicit AudioEngine(Settings& settings);
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
        Opens the saved audio device, or the default input device if none has been saved. If the
        saved device is missing, no device is opened: there is no fallback to another device.
    */
    void openSavedDevice();

    /** True if a device is running with at least one input channel. */
    [[nodiscard]] bool isInputRunning() const;

    /** Why no input is running, as a sentence for the user. Empty if input is running. */
    [[nodiscard]] juce::String noInputReason() const;

    struct StreamStatus
    {
        /** Frames the device has delivered since the stream started. */
        std::uint64_t framesDelivered = 0;
        /** Frames in the latest callback. */
        std::uint32_t blockSize = 0;
        /** Whether the latest block's host time came from the device, not the fallback clock. */
        bool deviceHostTime = false;
        std::uint64_t overruns = 0;
        std::uint64_t droppedFrames = 0;
    };

    /** The running stream, or std::nullopt if no device is running. */
    [[nodiscard]] std::optional<StreamStatus> streamStatus() const;

    /** Fills `peaks` with the peak level of each source channel since the previous call. */
    void takePeaks(std::span<float> peaks);

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override;
    juce::String selectDeviceType(const juce::String& typeName) override;
    juce::String selectDevice(const juce::String& inputDeviceName) override;
    juce::String selectSampleRate(double sampleRate) override;
    juce::String selectBufferSize(int bufferSizeSamples) override;
    [[nodiscard]] const SourceLayout& layout() const noexcept override;
    [[nodiscard]] int inputChannel(std::size_t channel) const override;
    void setInputChannel(std::size_t channel, int deviceInputChannel) override;

private:
    struct Stream;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void
    audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                     float* const* outputChannelData, int numOutputChannels,
                                     int numSamples,
                                     const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceStopped() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::String applySetup(juce::AudioDeviceManager::AudioDeviceSetup setup);

    // Both need lock_. A saved channel the device does not have falls back to the default.
    [[nodiscard]] int effectiveInputChannel(std::size_t channel, int numDeviceInputs) const;
    void routeInputs();

    Settings& settings_;
    const SourceLayout layout_;
    juce::AudioDeviceManager deviceManager_;
    juce::String lastError_;

    // Guards everything below it. The audio callback never takes it.
    mutable std::mutex lock_;

    // The saved choice per source channel, which may be out of range for the current device.
    std::vector<int> inputChannels_;

    // Replaced only in audioDeviceAboutToStart() and audioDeviceStopped(), which JUCE never runs
    // while the audio callback runs, so the callback reads it without the lock.
    std::unique_ptr<Stream> stream_;

    // The reader drains a ring of about a second; App Nap's timer throttling would overflow it
    // while Visona is in the background.
    juce::ScopedLowPowerModeDisabler appNapDisabler_;
};

} // namespace visona
