#include "visona/MidiClockTransport.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace visona
{

namespace
{

/** One MIDI beat of a Song Position Pointer is a sixteenth note. */
constexpr std::int64_t ticksPerMidiBeat = 6;

std::size_t checkedCapacity(std::size_t spanCapacity)
{
    if (spanCapacity < 2)
        throw std::invalid_argument("MidiClockTransport needs room for at least 2 spans");
    return spanCapacity;
}

} // namespace

MidiClockTransport::MidiClockTransport(double sampleRate, TimeSignature timeSignature,
                                       std::size_t spanCapacity)
    : sampleRate_(sampleRate)
    , timeSignature_(timeSignature)
    , spans_(checkedCapacity(spanCapacity))
{
    reset(sampleRate);
}

void MidiClockTransport::reset(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    state_ = TransportState::freeRunning;
    nextTick_ = 0;
    ignoredSpp_ = 0;
    awaitingTick_ = false;
    lastInterval_ = 0.0;
    tempoCount_ = 0;
    oldestSpan_ = 0;
    spanCount_ = 0;
    pushOpen(TransportSpan::Kind::freeRunning, 0.0, 0.0);
}

void MidiClockTransport::handle(MidiClockEvent::Type type, std::uint16_t sppValue,
                                double sampleTime) noexcept
{
    sampleTime = clampToTimeline(sampleTime);
    switch (type)
    {
    case MidiClockEvent::Type::Clock:
        onClock(sampleTime);
        return;
    case MidiClockEvent::Type::Start:
        onStart(sampleTime);
        return;
    case MidiClockEvent::Type::Continue:
        onContinue(sampleTime);
        return;
    case MidiClockEvent::Type::Stop:
        onStop(sampleTime);
        return;
    case MidiClockEvent::Type::SongPositionPointer:
        // Accepted whenever the position is not advancing (D-073).
        if (state_ == TransportState::running)
            ++ignoredSpp_;
        else
            nextTick_ =
                std::min<std::int64_t>(sppValue, MidiClockEvent::maxSppValue) * ticksPerMidiBeat;
        return;
    }
}

void MidiClockTransport::runFree(double sampleTime) noexcept
{
    if (state_ != TransportState::stopped && state_ != TransportState::clockLost)
        return;
    sampleTime = clampToTimeline(sampleTime);
    ++startCount_;
    state_ = TransportState::freeRunning;
    awaitingTick_ = false;
    closeOpenAt(sampleTime);
    pushOpen(TransportSpan::Kind::freeRunning, sampleTime, 0.0);
}

void MidiClockTransport::advanceTo(double sampleTime) noexcept
{
    if (state_ != TransportState::running)
        return;
    const auto timeout = clockLossSeconds * sampleRate_;
    const auto lastSeen = awaitingTick_ ? runStart_ : lastTickTime_;
    if (sampleTime - lastSeen <= timeout)
        return;

    state_ = TransportState::clockLost;
    if (!awaitingTick_)
        endPendingAt(lastTickTime_ + framesPerTick());
    awaitingTick_ = false;
}

double MidiClockTransport::bpm() const noexcept
{
    if (tempoCount_ < 2)
        return 0.0;

    // The least-squares slope of clock time against clock number, in frames per clock.
    const auto n = static_cast<double>(tempoCount_);
    const auto oldest =
        (tempoNewest_ + tempoClocks_.size() - (tempoCount_ - 1)) % tempoClocks_.size();
    const auto origin = tempoClocks_[oldest];
    double meanTime = 0.0;
    for (std::size_t i = 0; i < tempoCount_; ++i)
        meanTime += tempoClocks_[(oldest + i) % tempoClocks_.size()] - origin;
    meanTime /= n;
    const auto meanIndex = (n - 1.0) / 2.0;

    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t i = 0; i < tempoCount_; ++i)
    {
        const auto dx = static_cast<double>(i) - meanIndex;
        covariance += dx * (tempoClocks_[(oldest + i) % tempoClocks_.size()] - origin - meanTime);
        variance += dx * dx;
    }
    const auto framesPerClock = covariance / variance;
    if (framesPerClock <= 0.0)
        return 0.0;
    return 60.0 * sampleRate_ / (TimeSignature::ticksPerQuarterNote * framesPerClock);
}

const TransportSpan& MidiClockTransport::span(std::size_t index) const noexcept
{
    assert(index < spanCount_);
    return spans_[(oldestSpan_ + index) % spans_.size()];
}

const TransportSpan& MidiClockTransport::spanAt(double sampleTime) noexcept
{
    while (spanCount_ > 1 && span(0).end <= sampleTime)
    {
        oldestSpan_ = (oldestSpan_ + 1) % spans_.size();
        --spanCount_;
    }
    return span(0);
}

TransportSpan& MidiClockTransport::openSpan() noexcept
{
    assert(spanCount_ > 0);
    return spans_[(oldestSpan_ + spanCount_ - 1) % spans_.size()];
}

void MidiClockTransport::pushOpen(TransportSpan::Kind kind, double start, double startTick) noexcept
{
    // A full timeline forgets its oldest span; the audio for it is long gone.
    if (spanCount_ == spans_.size())
    {
        oldestSpan_ = (oldestSpan_ + 1) % spans_.size();
        --spanCount_;
    }
    ++spanCount_;
    openSpan() = {kind, start, TransportSpan::openEnd, startTick, startTick, startCount_};
}

void MidiClockTransport::closeOpenAt(double sampleTime) noexcept
{
    auto& open = openSpan();
    sampleTime = std::max(sampleTime, open.start);
    if (sampleTime > open.start || spanCount_ == 1)
    {
        open.end = sampleTime;
        return;
    }
    // A span that would cover nothing is dropped; its predecessor already ends here.
    --spanCount_;
}

void MidiClockTransport::endPendingAt(double sampleTime) noexcept
{
    // The pending span becomes the last tick's interval, extrapolated by at most one tick, and the
    // view freezes after it.
    auto& pending = openSpan();
    assert(pending.kind == TransportSpan::Kind::pending);
    const auto interval = framesPerTick();
    const auto end = std::clamp(sampleTime, pending.start, pending.start + interval);
    if (end > pending.start)
    {
        pending.kind = TransportSpan::Kind::musical;
        pending.end = end;
        pending.endTick = pending.startTick + (end - pending.start) / interval;
        pushOpen(TransportSpan::Kind::frozen, end, 0.0);
    }
    else
    {
        pending.kind = TransportSpan::Kind::frozen;
    }
}

double MidiClockTransport::framesPerTick() const noexcept
{
    if (lastInterval_ > 0.0)
        return lastInterval_;
    const auto tempo = bpm();
    return tempo > 0.0 ? 60.0 * sampleRate_ / (TimeSignature::ticksPerQuarterNote * tempo) : 0.0;
}

double MidiClockTransport::clampToTimeline(double sampleTime) const noexcept
{
    const auto& open = spans_[(oldestSpan_ + spanCount_ - 1) % spans_.size()];
    return std::max(sampleTime, open.start);
}

void MidiClockTransport::onClock(double sampleTime) noexcept
{
    addTempoClock(sampleTime);
    if (state_ == TransportState::clockLost)
    {
        // The clock is back: carry on counting from where it was lost (D-059).
        state_ = TransportState::running;
        awaitingTick_ = true;
    }
    if (state_ != TransportState::running)
        return;

    const auto tick = nextTick_++;
    if (awaitingTick_)
    {
        awaitingTick_ = false;
        lastInterval_ = 0.0;
        closeOpenAt(sampleTime);
    }
    else
    {
        auto& pending = openSpan();
        lastInterval_ = sampleTime - lastTickTime_;
        if (lastInterval_ > 0.0)
        {
            pending.kind = TransportSpan::Kind::musical;
            pending.end = sampleTime;
            pending.endTick = static_cast<double>(tick);
        }
        else
        {
            // Two clocks at the same moment: the second one takes over the pending span.
            --spanCount_;
        }
    }
    lastTickTime_ = sampleTime;
    pushOpen(TransportSpan::Kind::pending, sampleTime, static_cast<double>(tick));
}

void MidiClockTransport::onStart(double sampleTime) noexcept
{
    if (state_ == TransportState::running && !awaitingTick_)
        endPendingAt(sampleTime);

    ++startCount_;
    nextTick_ = 0;
    tempoCount_ = 0;
    state_ = TransportState::running;
    awaitingTick_ = true;
    runStart_ = sampleTime;
    closeOpenAt(sampleTime);
    pushOpen(TransportSpan::Kind::frozen, sampleTime, 0.0);
}

void MidiClockTransport::onContinue(double sampleTime) noexcept
{
    if (state_ == TransportState::running)
        return;

    // Continuing from the free-running sweep leaves it, so the sweep starts over.
    if (state_ == TransportState::freeRunning)
        ++startCount_;
    state_ = TransportState::running;
    awaitingTick_ = true;
    runStart_ = sampleTime;
    closeOpenAt(sampleTime);
    pushOpen(TransportSpan::Kind::frozen, sampleTime, 0.0);
}

void MidiClockTransport::onStop(double sampleTime) noexcept
{
    if (state_ == TransportState::running && !awaitingTick_)
        endPendingAt(sampleTime);
    if (state_ == TransportState::running || state_ == TransportState::clockLost)
        state_ = TransportState::stopped;
    awaitingTick_ = false;
}

void MidiClockTransport::addTempoClock(double sampleTime) noexcept
{
    // A gap longer than the clock-loss timeout is not a clock interval.
    if (tempoCount_ > 0 && sampleTime - tempoClocks_[tempoNewest_] > clockLossSeconds * sampleRate_)
        tempoCount_ = 0;
    tempoNewest_ = tempoCount_ == 0 ? 0 : (tempoNewest_ + 1) % tempoClocks_.size();
    tempoClocks_[tempoNewest_] = sampleTime;
    tempoCount_ = std::min(tempoCount_ + 1, tempoClocks_.size());
}

} // namespace visona
