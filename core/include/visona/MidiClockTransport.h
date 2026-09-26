#pragma once

#include <visona/MidiClockEvent.h>
#include <visona/MusicalTime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

/** The state of a transport that follows an external clock (architecture §3.3, D-045). */
enum class TransportState : std::uint8_t
{
    Waiting,  ///< No Start or Continue since the stream started: the scope runs free (D-060)
    Running,  ///< Playing: each Clock advances the position by one tick
    Stopped,  ///< Stopped by the DAW, which may still send Clock; the position is kept
    ClockLost ///< Running, but no Clock for longer than the clock-loss timeout
};

/**
    A stretch of stream time in which the musical position grows linearly: the interval between
    two ticks, or the extrapolated interval after the last tick of a run.
*/
struct TickSegment
{
    /** First sample time of the segment. */
    double startSample = 0.0;

    /** The segment ends just before this sample time. */
    double endSample = 0.0;

    /** Position in ticks at startSample. */
    double startTick = 0.0;

    double ticksPerSample = 0.0;

    [[nodiscard]] double positionAt(double sampleTime) const noexcept
    {
        return startTick + (sampleTime - startSample) * ticksPerSample;
    }
};

/**
    Follows MIDI Clock: the transport state machine, tick counting, Song Position Pointer, the BPM
    estimate and the clock-loss timeout (architecture §3.3, D-045, D-059, D-080 to D-084).

    It works in sample time: stream positions in frames, as fractional values. The analysis thread
    maps each MidiClockEvent's host time to a sample time with ClockTimeMapper, then hands the
    events over in order with handle(), and tells the transport how far audio has arrived with
    advanceTo().

    Positions count ticks at 24 PPQN. nextTick() is the tick the next Clock will mark while
    Running: Start sets it to 0, so the first Clock after Start is the downbeat of bar 1, and SPP
    sets it to sppValue × 6. Each Clock counted while Running records its sample time, and
    positionAt() interpolates between recorded ticks. A sample's position is therefore known once
    the tick after it has arrived, and the analysis processes audio only up to horizon().

    When a run of ticks ends with Stop, Start or clock loss, its last interval is extrapolated by at
    most one tick, and never past the Stop or Start. After that the position is held: samples have
    no position until the first Clock of the next run.

    All storage is allocated in the constructor. handle(), advanceTo() and the queries never
    allocate, lock or wait. The class is not thread-safe; only the analysis thread uses it.
*/
class MidiClockTransport
{
public:
    static constexpr double defaultClockLossTimeoutSeconds = 0.5;

    /** The BPM estimate averages this many tick intervals: one beat. */
    static constexpr std::size_t bpmIntervalCount = 24;

    /** Recorded ticks, enough for 14 s at 174 BPM. Older ticks have no position. */
    static constexpr std::size_t defaultTickCapacity = 1024;

    /**
        Starts in Waiting. Throws std::invalid_argument if `timeSignature` is not valid or
        `tickCapacity` is less than 2.
    */
    explicit MidiClockTransport(double sampleRate, TimeSignature timeSignature = {},
                                std::size_t tickCapacity = defaultTickCapacity);

    /** Forgets everything and returns to Waiting, for a new stream at `sampleRate`. */
    void reset(double sampleRate) noexcept;

    /** Throws std::invalid_argument if `timeSignature` is not valid. */
    void setTimeSignature(TimeSignature timeSignature);

    /** Running without a Clock for longer than this means the clock is lost. */
    void setClockLossTimeout(double seconds) noexcept;

    /**
        Handles `event`, which happened at `sampleTime`. Its hostTimeNs is ignored: the caller has
        already mapped it to `sampleTime`.

        Events must arrive in order. A sample time earlier than the latest one handled, or passed
        to advanceTo(), is treated as that latest time, so positions before horizon() never change.
    */
    void handle(const MidiClockEvent& event, double sampleTime) noexcept;

    /**
        Tells the transport that every event up to `sampleTime` has been handled, and applies the
        clock-loss timeout up to then. Call it with the latest audio time after handling the
        events that have arrived; it should trail real time by the MIDI delivery delay.
    */
    void advanceTo(double sampleTime) noexcept;

    [[nodiscard]] TransportState state() const noexcept
    {
        return state_;
    }

    /** The tick the next Clock will mark while Running: the song position, in ticks. */
    [[nodiscard]] std::int64_t nextTick() const noexcept
    {
        return nextTick_;
    }

    /**
        The tempo from the average of the last 24 tick intervals, or std::nullopt before the first
        interval. Clock updates it in every state. Start resets it, and intervals longer than the
        clock-loss timeout, or across a Continue, are left out (D-082).
    */
    [[nodiscard]] std::optional<double> bpm() const noexcept;

    /**
        The linear stretch of positions that contains `sampleTime`, or std::nullopt if the sample
        has no position: before the first tick of a run, while the position is held, or after the
        latest tick while Running.
    */
    [[nodiscard]] std::optional<TickSegment> segmentAt(double sampleTime) const noexcept;

    /** Position in ticks at `sampleTime`, interpolated between recorded ticks. See segmentAt(). */
    [[nodiscard]] std::optional<double> positionAt(double sampleTime) const noexcept;

    /**
        Positions of samples before this sample time are final. While Running it is the latest
        tick, or the Start or Continue before the first Clock. In every other state it is the
        latest time handled.
    */
    [[nodiscard]] double horizon() const noexcept;

    /** Song Position Pointers ignored because ticks were being counted, or out of range (D-081). */
    [[nodiscard]] std::uint64_t ignoredSppCount() const noexcept
    {
        return ignoredSppCount_;
    }

    /** Times the clock was lost while Running. */
    [[nodiscard]] std::uint64_t clockLossCount() const noexcept
    {
        return clockLossCount_;
    }

    [[nodiscard]] double sampleRate() const noexcept
    {
        return sampleRate_;
    }

    [[nodiscard]] TimeSignature timeSignature() const noexcept
    {
        return timeSignature_;
    }

    [[nodiscard]] int ticksPerBar() const noexcept
    {
        return timeSignature_.ticksPerBar();
    }

private:
    struct RecordedTick
    {
        std::int64_t tick = 0;
        double sample = 0.0;

        /** The position is extrapolated up to here after the last tick of a run. */
        double tailEnd = 0.0;

        /** Whether this tick follows the previous recorded tick in the same run. */
        bool followsPrevious = false;
    };

    void onClock(double sampleTime) noexcept;
    void startRun(double sampleTime) noexcept;
    void endRun(double sampleTime) noexcept;
    void recordTick(double sampleTime) noexcept;
    void addBpmInterval(double samples) noexcept;
    [[nodiscard]] double clockLossTimeoutSamples() const noexcept;

    /** Recorded tick `index`, where 0 is the oldest still kept. */
    [[nodiscard]] const RecordedTick& recorded(std::size_t index) const noexcept;
    [[nodiscard]] RecordedTick& recorded(std::size_t index) noexcept;

    double sampleRate_;
    TimeSignature timeSignature_;
    double clockLossTimeoutSeconds_ = defaultClockLossTimeoutSeconds;

    TransportState state_ = TransportState::Waiting;
    std::int64_t nextTick_ = 0;
    double now_ = 0.0;

    // The current run: the ticks counted since the latest Start, Continue or clock recovery.
    bool runHasTick_ = false;
    double runStart_ = 0.0;

    // The clock-loss timeout counts from the latest Clock, Start or Continue while Running.
    double timeoutStart_ = 0.0;

    std::vector<RecordedTick> ticks_;
    std::size_t firstTick_ = 0;
    std::size_t tickCount_ = 0;

    std::array<double, bpmIntervalCount> bpmIntervals_{};
    std::size_t nextBpmInterval_ = 0;
    std::size_t bpmIntervalsFilled_ = 0;
    std::optional<double> lastClock_;

    std::uint64_t ignoredSppCount_ = 0;
    std::uint64_t clockLossCount_ = 0;
};

} // namespace visona
