#pragma once

#include <visona/MidiClockEvent.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdint>
#include <memory>

namespace visona
{

class Settings;

/**
    The chosen MIDI input. On JUCE's MIDI thread it keeps only MIDI Clock, Start, Continue, Stop and
    Song Position Pointer, and pushes each as a MidiClockEvent to a lock-free queue that the
   analysis thread drains. Nothing else happens on that thread: no allocation, no lock, no wait.

    JUCE stamps a message on macOS with the CoreMIDI packet time, the driver's receive time, but
    converts it to its millisecond counter, which is the host clock in milliseconds wrapping at
    2^32. The callback turns it back into host nanoseconds in the audio's time base (D-065, D-077).

    The choice is saved in Settings. Public functions are for the message thread.
*/
class MidiClockInput final : private juce::MidiInputCallback
{
public:
    explicit MidiClockInput(Settings& settings);
    ~MidiClockInput() override;

    MidiClockInput(const MidiClockInput&) = delete;
    MidiClockInput& operator=(const MidiClockInput&) = delete;

    /** The queue the analysis thread drains. */
    [[nodiscard]] MidiClockQueue& queue() noexcept
    {
        return queue_;
    }

    /** Opens the saved input, if it is there. */
    void openSaved();

    /** Opens the input with `identifier`, or none if it is empty, and saves the choice. Returns an
        error message, or an empty string on success. */
    juce::String select(const juce::String& identifier);

    /** The saved choice: the open input, or one that is missing. Empty for none. */
    [[nodiscard]] juce::String identifier() const
    {
        return identifier_;
    }

    [[nodiscard]] juce::String name() const
    {
        return name_;
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return input_ != nullptr;
    }

    /** Messages dropped because the queue was full. */
    [[nodiscard]] std::uint64_t droppedEvents() const noexcept
    {
        return queue_.overrunCount();
    }

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    Settings& settings_;
    MidiClockQueue queue_;
    std::unique_ptr<juce::MidiInput> input_;
    juce::String identifier_;
    juce::String name_;
};

} // namespace visona
