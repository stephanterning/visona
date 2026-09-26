#include "MidiClockInput.h"

#include "HostTime.h"
#include "Settings.h"

#include <cmath>

namespace visona
{

namespace
{

// At 24 clocks per quarter note, several seconds of clock even at a high tempo.
constexpr std::size_t queueCapacity = 4'096;

constexpr double millisecondCounterWrap = 4'294'967'296.0;

/**
    Host nanoseconds for a JUCE MIDI timestamp in seconds of its millisecond counter, which is the
    host clock in milliseconds modulo 2^32. The timestamp lies shortly before now, so the wrap is
    undone relative to the current host time.
*/
std::uint64_t hostTimeOf(double timeStampSeconds) noexcept
{
    const auto nowNs = monotonicHostTimeNs();
    auto ageMs = std::fmod(static_cast<double>(nowNs) * 1.0e-6 - timeStampSeconds * 1'000.0,
                           millisecondCounterWrap);
    if (ageMs > millisecondCounterWrap / 2.0)
        ageMs -= millisecondCounterWrap;
    else if (ageMs < -millisecondCounterWrap / 2.0)
        ageMs += millisecondCounterWrap;
    const auto ageNs = static_cast<std::int64_t>(std::llround(ageMs * 1.0e6));
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(nowNs) - ageNs);
}

} // namespace

MidiClockInput::MidiClockInput(Settings& settings)
    : settings_(settings)
    , queue_(queueCapacity)
{
    const auto saved = settings_.midiInput();
    identifier_ = saved.identifier;
    name_ = saved.name;
}

MidiClockInput::~MidiClockInput()
{
    if (input_ != nullptr)
        input_->stop();
}

void MidiClockInput::openSaved()
{
    if (identifier_.isEmpty())
        return;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (device.identifier == identifier_)
        {
            select(identifier_);
            return;
        }
}

juce::String MidiClockInput::select(const juce::String& identifier)
{
    // The old input stops calling back before the new one starts, so the queue always has a
    // single producer.
    if (input_ != nullptr)
    {
        input_->stop();
        input_.reset();
    }

    identifier_ = identifier;
    name_.clear();
    if (identifier.isNotEmpty())
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (device.identifier == identifier)
                name_ = device.name;
        input_ = juce::MidiInput::openDevice(identifier, this);
    }
    settings_.setMidiInput({identifier_, name_});

    if (identifier.isNotEmpty() && input_ == nullptr)
        return "The MIDI input \"" + (name_.isNotEmpty() ? name_ : identifier) +
               "\" could not be opened.";
    if (input_ != nullptr)
        input_->start();
    return {};
}

void MidiClockInput::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    MidiClockEvent event;
    if (message.isMidiClock())
        event.type = MidiClockEvent::Type::Clock;
    else if (message.isMidiStart())
        event.type = MidiClockEvent::Type::Start;
    else if (message.isMidiContinue())
        event.type = MidiClockEvent::Type::Continue;
    else if (message.isMidiStop())
        event.type = MidiClockEvent::Type::Stop;
    else if (message.isSongPositionPointer())
    {
        event.type = MidiClockEvent::Type::SongPositionPointer;
        event.sppValue = static_cast<std::uint16_t>(message.getSongPositionPointerMidiBeat());
    }
    else
        return;

    event.hostTimeNs = hostTimeOf(message.getTimeStamp());
    queue_.tryPush(event);
}

} // namespace visona
