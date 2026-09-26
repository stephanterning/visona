#include "visona/MidiClockTransport.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace visona
{

namespace
{

void requireValid(TimeSignature timeSignature)
{
    if (!timeSignature.isValid())
        throw std::invalid_argument("A time signature needs a numerator of at least 1 and a "
                                    "denominator of 1, 2, 4, 8, 16 or 32");
}

} // namespace

MidiClockTransport::MidiClockTransport(double sampleRate, TimeSignature timeSignature,
                                       std::size_t tickCapacity)
    : sampleRate_(sampleRate)
    , timeSignature_(timeSignature)
    , ticks_(tickCapacity)
{
    requireValid(timeSignature);
    if (tickCapacity < 2)
        throw std::invalid_argument("MidiClockTransport must record at least 2 ticks");
}

void MidiClockTransport::reset(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    state_ = TransportState::Waiting;
    nextTick_ = 0;
    now_ = 0.0;
    runHasTick_ = false;
    runStart_ = 0.0;
    timeoutStart_ = 0.0;
    firstTick_ = 0;
    tickCount_ = 0;
    nextBpmInterval_ = 0;
    bpmIntervalsFilled_ = 0;
    lastClock_.reset();
    ignoredSppCount_ = 0;
    clockLossCount_ = 0;
}

void MidiClockTransport::setTimeSignature(TimeSignature timeSignature)
{
    requireValid(timeSignature);
    timeSignature_ = timeSignature;
}

void MidiClockTransport::setClockLossTimeout(double seconds) noexcept
{
    clockLossTimeoutSeconds_ = std::max(seconds, 0.0);
}

void MidiClockTransport::handle(const MidiClockEvent& event, double sampleTime) noexcept
{
    advanceTo(sampleTime);
    const auto time = now_;

    switch (event.type)
    {
    case MidiClockEvent::Type::Clock:
        onClock(time);
        break;

    case MidiClockEvent::Type::Start:
        endRun(time);
        startRun(time);
        nextTick_ = 0;
        nextBpmInterval_ = 0;
        bpmIntervalsFilled_ = 0;
        lastClock_.reset();
        break;

    case MidiClockEvent::Type::Continue:
        // Continue while Running changes nothing, so the tick count carries on (D-083).
        if (state_ != TransportState::Running)
        {
            startRun(time);
            // The DAW may restart its clock phase on Continue, so the interval across it is not
            // a tempo (D-082).
            lastClock_.reset();
        }
        break;

    case MidiClockEvent::Type::Stop:
        endRun(time);
        // Before the first Start there is nothing to stop; the scope keeps running free (D-083).
        if (state_ != TransportState::Waiting)
            state_ = TransportState::Stopped;
        break;

    case MidiClockEvent::Type::SongPositionPointer:
        // Moving the position while ticks are being counted would make the sweep jump (D-081).
        if (event.sppValue > MidiClockEvent::maxSppValue ||
            (state_ == TransportState::Running && runHasTick_))
        {
            ++ignoredSppCount_;
            break;
        }
        nextTick_ = static_cast<std::int64_t>(event.sppValue) * ticksPerSppUnit;
        break;
    }
}

void MidiClockTransport::advanceTo(double sampleTime) noexcept
{
    now_ = std::max(now_, sampleTime);

    const auto timeout = clockLossTimeoutSamples();
    if (state_ == TransportState::Running && now_ - timeoutStart_ > timeout)
    {
        endRun(timeoutStart_ + timeout);
        state_ = TransportState::ClockLost;
        ++clockLossCount_;
    }
}

std::optional<double> MidiClockTransport::bpm() const noexcept
{
    if (bpmIntervalsFilled_ == 0 || sampleRate_ <= 0.0)
        return std::nullopt;

    double sum = 0.0;
    for (std::size_t i = 0; i < bpmIntervalsFilled_; ++i)
        sum += bpmIntervals_[i];
    const auto meanInterval = sum / static_cast<double>(bpmIntervalsFilled_);
    return 60.0 * sampleRate_ / (ticksPerQuarterNote * meanInterval);
}

std::optional<TickSegment> MidiClockTransport::segmentAt(double sampleTime) const noexcept
{
    // The number of recorded ticks at or before sampleTime.
    std::size_t low = 0;
    std::size_t high = tickCount_;
    while (low < high)
    {
        const auto middle = low + (high - low) / 2;
        if (recorded(middle).sample <= sampleTime)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == 0)
        return std::nullopt;

    const auto index = low - 1;
    const auto& tick = recorded(index);
    if (index + 1 < tickCount_ && recorded(index + 1).followsPrevious)
    {
        const auto& next = recorded(index + 1);
        return TickSegment{tick.sample, next.sample, static_cast<double>(tick.tick),
                           1.0 / (next.sample - tick.sample)};
    }

    // The last tick of a run: its tail, if the run had ended and had an interval to extrapolate.
    if (sampleTime < tick.tailEnd && index > 0 && tick.followsPrevious)
    {
        const auto interval = tick.sample - recorded(index - 1).sample;
        return TickSegment{tick.sample, tick.tailEnd, static_cast<double>(tick.tick),
                           1.0 / interval};
    }
    return std::nullopt;
}

std::optional<double> MidiClockTransport::positionAt(double sampleTime) const noexcept
{
    if (const auto segment = segmentAt(sampleTime))
        return segment->positionAt(sampleTime);
    return std::nullopt;
}

double MidiClockTransport::horizon() const noexcept
{
    if (state_ != TransportState::Running)
        return now_;
    return runHasTick_ ? recorded(tickCount_ - 1).sample : runStart_;
}

void MidiClockTransport::onClock(double sampleTime) noexcept
{
    if (lastClock_)
    {
        const auto interval = sampleTime - *lastClock_;
        if (interval > 0.0 && interval <= clockLossTimeoutSamples())
            addBpmInterval(interval);
    }
    lastClock_ = sampleTime;

    // The clock is back: Running again, counting on from the last position (D-059).
    if (state_ == TransportState::ClockLost)
        startRun(sampleTime);

    if (state_ == TransportState::Running)
        recordTick(sampleTime);
}

void MidiClockTransport::startRun(double sampleTime) noexcept
{
    state_ = TransportState::Running;
    runHasTick_ = false;
    runStart_ = sampleTime;
    timeoutStart_ = sampleTime;
}

void MidiClockTransport::endRun(double sampleTime) noexcept
{
    if (!runHasTick_)
        return;
    runHasTick_ = false;

    auto& last = recorded(tickCount_ - 1);
    if (tickCount_ >= 2 && last.followsPrevious)
    {
        const auto interval = last.sample - recorded(tickCount_ - 2).sample;
        last.tailEnd = std::min(last.sample + interval, std::max(sampleTime, last.sample));
    }
}

void MidiClockTransport::recordTick(double sampleTime) noexcept
{
    if (tickCount_ == ticks_.size())
    {
        firstTick_ = (firstTick_ + 1) % ticks_.size();
        --tickCount_;
    }
    ++tickCount_;
    recorded(tickCount_ - 1) = {nextTick_, sampleTime, sampleTime, runHasTick_};

    ++nextTick_;
    runHasTick_ = true;
    timeoutStart_ = sampleTime;
}

void MidiClockTransport::addBpmInterval(double samples) noexcept
{
    bpmIntervals_[nextBpmInterval_] = samples;
    nextBpmInterval_ = (nextBpmInterval_ + 1) % bpmIntervals_.size();
    bpmIntervalsFilled_ = std::min(bpmIntervalsFilled_ + 1, bpmIntervals_.size());
}

double MidiClockTransport::clockLossTimeoutSamples() const noexcept
{
    return clockLossTimeoutSeconds_ * sampleRate_;
}

const MidiClockTransport::RecordedTick&
MidiClockTransport::recorded(std::size_t index) const noexcept
{
    assert(index < tickCount_);
    return ticks_[(firstTick_ + index) % ticks_.size()];
}

MidiClockTransport::RecordedTick& MidiClockTransport::recorded(std::size_t index) noexcept
{
    assert(index < tickCount_);
    return ticks_[(firstTick_ + index) % ticks_.size()];
}

} // namespace visona
