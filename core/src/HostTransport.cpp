#include "visona/HostTransport.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace visona
{

namespace
{

std::size_t checkedCapacity(std::size_t spanCapacity)
{
    if (spanCapacity < 2)
        throw std::invalid_argument("HostTransport needs room for at least 2 spans");
    return spanCapacity;
}

} // namespace

HostTransport::HostTransport(double sampleRate, std::size_t spanCapacity)
    : sampleRate_(sampleRate)
    , spans_(checkedCapacity(spanCapacity))
{
    reset(sampleRate);
}

void HostTransport::reset(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    timeSignature_ = {};
    hostBpm_ = 0.0;
    state_ = TransportState::stopped;
    nextTick_ = 0;
    startCount_ = 0;
    anchorSample_ = 0.0;
    anchorTick_ = 0.0;
    hasAnchor_ = false;
    oldestSpan_ = 0;
    spanCount_ = 0;
    pushOpen(TransportSpan::Kind::frozen, 0.0, 0.0);
}

void HostTransport::syncTo(double sampleTime, const Playhead& playhead) noexcept
{
    if (!playhead.valid)
        return;

    sampleTime = clampToTimeline(sampleTime);
    timeSignature_ = playhead.timeSignature;
    if (playhead.bpm > 0.0)
        hostBpm_ = playhead.bpm;

    const auto tick = tickFromPpq(playhead.ppqPosition);
    nextTick_ = static_cast<std::int64_t>(std::llround(tick));

    if (playhead.isPlaying)
    {
        if (state_ != TransportState::running)
        {
            ++startCount_;
            state_ = TransportState::running;
            if (hasAnchor_ && sampleTime > anchorSample_)
                pushMusical(anchorSample_, anchorTick_, sampleTime, tick);
            else
            {
                closeOpenAt(sampleTime);
                pushMusical(sampleTime, tick, sampleTime + 1.0, tick + 1.0);
            }
        }
        else if (hasAnchor_ && sampleTime > anchorSample_)
        {
            const auto expected =
                anchorTick_ + (sampleTime - anchorSample_) / framesPerTick();
            if (std::abs(tick - expected) > 1.0)
            {
                ++startCount_;
                pushMusical(anchorSample_, anchorTick_, sampleTime, tick);
            }
            else
                pushMusical(anchorSample_, anchorTick_, sampleTime, tick);
        }
        else if (!hasAnchor_)
            pushMusical(sampleTime, tick, sampleTime + 1.0, tick + 1.0);

        anchorSample_ = sampleTime;
        anchorTick_ = tick;
        hasAnchor_ = true;
        return;
    }

    if (state_ == TransportState::running && hasAnchor_ && sampleTime > anchorSample_)
        pushMusical(anchorSample_, anchorTick_, sampleTime, tick);

    if (state_ == TransportState::running)
    {
        state_ = TransportState::stopped;
        closeOpenAt(sampleTime);
        pushOpen(TransportSpan::Kind::frozen, sampleTime, tick);
    }

    anchorSample_ = sampleTime;
    anchorTick_ = tick;
    hasAnchor_ = true;
}

void HostTransport::advanceTo(double /*sampleTime*/) noexcept
{
}

const TransportSpan& HostTransport::span(std::size_t index) const noexcept
{
    assert(index < spanCount_);
    return spans_[(oldestSpan_ + index) % spans_.size()];
}

const TransportSpan& HostTransport::spanAt(double sampleTime) noexcept
{
    while (spanCount_ > 1 && span(0).end <= sampleTime)
    {
        oldestSpan_ = (oldestSpan_ + 1) % spans_.size();
        --spanCount_;
    }
    return span(0);
}

TransportSpan& HostTransport::openSpan() noexcept
{
    assert(spanCount_ > 0);
    return spans_[(oldestSpan_ + spanCount_ - 1) % spans_.size()];
}

void HostTransport::pushOpen(TransportSpan::Kind kind, double start, double startTick) noexcept
{
    if (spanCount_ == spans_.size())
    {
        oldestSpan_ = (oldestSpan_ + 1) % spans_.size();
        --spanCount_;
    }
    ++spanCount_;
    openSpan() = {kind, start, TransportSpan::openEnd, startTick, startTick, startCount_};
}

void HostTransport::closeOpenAt(double sampleTime) noexcept
{
    auto& open = openSpan();
    sampleTime = std::max(sampleTime, open.start);
    if (sampleTime > open.start || spanCount_ == 1)
    {
        open.end = sampleTime;
        return;
    }
    --spanCount_;
}

void HostTransport::pushMusical(double start, double startTick, double end, double endTick) noexcept
{
    if (!(end > start))
        end = start + 1.0;
    if (!(endTick > startTick))
        endTick = startTick + (end - start) / std::max(framesPerTick(), 1.0);

    closeOpenAt(start);
    if (spanCount_ == spans_.size())
    {
        oldestSpan_ = (oldestSpan_ + 1) % spans_.size();
        --spanCount_;
    }
    ++spanCount_;
    openSpan() = {TransportSpan::Kind::musical, start, end, startTick, endTick, startCount_};
}

double HostTransport::framesPerTick() const noexcept
{
    return hostBpm_ > 0.0
               ? 60.0 * sampleRate_ / (TimeSignature::ticksPerQuarterNote * hostBpm_)
               : 0.0;
}

double HostTransport::clampToTimeline(double sampleTime) const noexcept
{
    const auto& open = spans_[(oldestSpan_ + spanCount_ - 1) % spans_.size()];
    return std::max(sampleTime, open.start);
}

double HostTransport::tickFromPpq(double ppq) const noexcept
{
    return ppq * static_cast<double>(TimeSignature::ticksPerQuarterNote);
}

} // namespace visona
