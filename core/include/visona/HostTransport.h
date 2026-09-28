#pragma once

#include <visona/MidiClockTransport.h>
#include <visona/TimeSignature.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace visona
{

/**
    A transport driven by a DAW host playhead (D-012 HostTransport).

    The analysis thread calls syncTo() with every playhead the host reports, in order, each with
    the sample it belongs to. While the host plays, the audio between two playheads is a musical
    span from one position to the next, and the audio after the newest playhead is pending: it
    waits for the next one. While the host is stopped the timeline is frozen. A playhead that is
    not where the previous one leads, such as at a loop or a jump, starts the sweep over; the audio
    before it keeps going from the previous position. There is no free-running or clock-loss state.

    One thread only. Storage is allocated in the constructor; nothing else allocates.
*/
class HostTransport
{
public:
    struct Playhead
    {
        bool valid = false;
        bool isPlaying = false;
        /** Quarter-note position from the host timeline. */
        double ppqPosition = 0.0;
        double bpm = 0.0;
        TimeSignature timeSignature{};
    };

    /** Throws std::invalid_argument if `spanCapacity` is less than 2. */
    explicit HostTransport(double sampleRate, std::size_t spanCapacity = 1024);

    /** Clears the timeline for a new stream at `sampleRate`. */
    void reset(double sampleRate) noexcept;

    /**
        Adds the host playhead at `sampleTime` to the timeline. Ignored when `playhead.valid` is
        false.
    */
    void syncTo(double sampleTime, const Playhead& playhead) noexcept;

    /** Tells the transport that audio has reached `sampleTime`. No clock-loss handling. */
    void advanceTo(double sampleTime) noexcept;

    [[nodiscard]] TransportState state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] double bpm() const noexcept
    {
        return hostBpm_;
    }

    [[nodiscard]] std::int64_t nextTick() const noexcept
    {
        return nextTick_;
    }

    [[nodiscard]] const TimeSignature& timeSignature() const noexcept
    {
        return timeSignature_;
    }

    [[nodiscard]] std::uint64_t startCount() const noexcept
    {
        return startCount_;
    }

    [[nodiscard]] std::uint64_t ignoredSppCount() const noexcept
    {
        return 0;
    }

    [[nodiscard]] std::size_t numSpans() const noexcept
    {
        return spanCount_;
    }

    /** The most spans the timeline holds; beyond that the oldest ones are forgotten. */
    [[nodiscard]] std::size_t spanCapacity() const noexcept
    {
        return spans_.size();
    }

    [[nodiscard]] const TransportSpan& span(std::size_t index) const noexcept;

    /** The span that holds `sampleTime`, after dropping every span that ends before it. */
    [[nodiscard]] const TransportSpan& spanAt(double sampleTime) noexcept;

private:
    [[nodiscard]] TransportSpan& openSpan() noexcept;
    void pushOpen(TransportSpan::Kind kind, double start, double startTick) noexcept;
    void closeOpenAt(double sampleTime) noexcept;
    /** Turns the pending span into a musical one that ends at `sampleTime` and `endTick`. */
    void endPendingAt(double sampleTime, double endTick) noexcept;
    [[nodiscard]] double framesPerTick() const noexcept;
    [[nodiscard]] double clampToTimeline(double sampleTime) const noexcept;
    [[nodiscard]] double tickFromPpq(double ppq) const noexcept;

    double sampleRate_;
    TimeSignature timeSignature_;
    double hostBpm_ = 0.0;

    TransportState state_ = TransportState::stopped;
    std::int64_t nextTick_ = 0;
    std::uint64_t startCount_ = 0;

    /** The newest playhead: its sample and its position in ticks. */
    double anchorSample_ = 0.0;
    double anchorTick_ = 0.0;

    std::vector<TransportSpan> spans_;
    std::size_t oldestSpan_ = 0;
    std::size_t spanCount_ = 0;
};

} // namespace visona
