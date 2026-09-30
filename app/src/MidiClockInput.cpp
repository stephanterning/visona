#include "MidiClockInput.h"

#include "HostTime.h"
#include "Settings.h"

#include <algorithm>
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
    close();
}

bool MidiClockInput::reconnect()
{
    if (identifier_.isEmpty())
        return false;

    const auto devices = juce::MidiInput::getAvailableDevices();
    const auto withIdentifier = [&devices](const juce::String& identifier)
    {
        return std::find_if(devices.begin(), devices.end(), [&identifier](const auto& device)
                            { return device.identifier == identifier; });
    };

    if (input_ != nullptr)
    {
        if (withIdentifier(identifier_) != devices.end())
            return false;
        close();
        return true;
    }

    auto device = withIdentifier(identifier_);
    if (device == devices.end() && name_.isNotEmpty())
        device = std::find_if(devices.begin(), devices.end(),
                              [this](const auto& candidate) { return candidate.name == name_; });
    if (device == devices.end())
        return false;

    const auto identifier = device->identifier;
    const auto name = device->name;
    if (!open(identifier))
        return false;
    if (identifier != identifier_ || name != name_)
    {
        identifier_ = identifier;
        name_ = name;
        settings_.setMidiInput({identifier_, name_});
    }
    return true;
}

juce::String MidiClockInput::select(const juce::String& identifier)
{
    close();

    identifier_ = identifier;
    name_.clear();
    if (identifier.isNotEmpty())
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (device.identifier == identifier)
                name_ = device.name;
    settings_.setMidiInput({identifier_, name_});

    if (identifier.isNotEmpty() && !open(identifier))
        return "The MIDI input \"" + (name_.isNotEmpty() ? name_ : identifier) +
               "\" could not be opened.";
    return {};
}

void MidiClockInput::close()
{
    if (input_ == nullptr)
        return;
    input_->stop();
    input_.reset();
}

bool MidiClockInput::open(const juce::String& identifier)
{
    close();
    input_ = juce::MidiInput::openDevice(identifier, this);
    if (input_ == nullptr)
        return false;
    input_->start();
    return true;
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
