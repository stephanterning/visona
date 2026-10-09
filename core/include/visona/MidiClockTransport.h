#pragma once

#include <visona/MidiClockEvent.h>
#include <visona/TimeSignature.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace visona
{

/** The state of the MIDI Clock transport (architecture.md 3.3). */
enum class TransportState : std::uint8_t
{
    freeRunning, ///< Before the first Start, or chosen after Stop: the sweep runs free (D-090).
    running,     ///< Started or continued; clocks advance the position.
    stopped,     ///< Stop received: the view freezes.
    clockLost    ///< Running, but no clock for more than the timeout: the view freezes.
};

/**
    What the transport says about a stretch of the audio's sample timeline, [start, end).

    - freeRunning: before the first Start, or after runFree(); the sweep keeps its own time.
    - musical: between two known clock ticks, or the at most one tick extrapolated after the last
      one on Stop or clock loss. The position in ticks runs linearly from startTick to endTick.
    - frozen: nothing may be written, as when stopped or after a Start before its first clock.
    - pending: after the last tick while running; audio here must wait for the next tick.

    Spans follow each other without gaps. The newest one is open: its end is infinite.
*/
struct TransportSpan
{
    enum class Kind : std::uint8_t
    {
        freeRunning,
        musical,
        frozen,
        pending
    };

    static constexpr double openEnd = std::numeric_limits<double>::infinity();

    Kind kind = Kind::freeRunning;
    double start = 0.0;
    double end = openEnd;
    double startTick = 0.0;
    double endTick = 0.0;

    /** Starts, runFree() calls, and Continues from freeRunning before this span. When it changes,
        the sweep starts over from bar 1. */
    std::uint64_t startCount = 0;

    [[nodiscard]] bool isOpen() const noexcept
    {
        return std::isinf(end);
    }

    /** The position in ticks at `sampleTime`. Musical spans only. */
    [[nodiscard]] double tickAt(double sampleTime) const noexcept
    {
        return startTick + (sampleTime - start) * (endTick - startTick) / (end - start);
    }
};

/**
    The MIDI Clock transport (architecture.md 3.3, D-045, D-059, D-060, D-075).

    It takes MIDI Clock, Start, Continue, Stop and Song Position Pointer messages, each placed on
   the audio's sample timeline, and tracks:
    - the state and the position in ticks, where the first clock after Start is tick 0, the
      downbeat of bar 1;
    - the tempo, from the clocks of up to the last four bars while it holds steady (D-074, D-110);
    - clock loss: running with no clock for more than half a second of audio;
    - a timeline of TransportSpans that tells the analysis how to treat each stretch of audio.

    Time is measured in audio frames, so the clock-loss timeout follows the audio, and everything
    is deterministic in tests. Events must come in order; an event stamped before the previous one
    counts as simultaneous with it.

    One thread only. Storage is allocated in the constructor; nothing else allocates.
*/
class MidiClockTransport
{
public:
    static constexpr double clockLossSeconds = 0.5;

    /** Clock intervals of the short tempo estimate, which follows tempo changes: one beat. */
    static constexpr std::size_t tempoIntervals = 24;

    /** Bars of clocks the tempo is estimated from while it holds steady. */
    static constexpr int steadyTempoBars = 4;

    /** The long estimate starts over from the short one when the two differ by more than this
        many standard errors of the short one, or by this fraction of the tempo, if more. */
    static constexpr double tempoChangeStandardErrors = 6.0;
    static constexpr double minTempoChange = 0.0025;

    /** Throws std::invalid_argument if `spanCapacity` is less than 2. */
    explicit MidiClockTransport(double sampleRate, TimeSignature timeSignature = {},
                                std::size_t spanCapacity = 1024);

    /** Back to freeRunning, with an empty timeline, for a new stream at `sampleRate`. */
    void reset(double sampleRate) noexcept;

    /** Handles one message at stream position `sampleTime`. `sppValue` is for Song Position
        Pointer only. */
    void handle(MidiClockEvent::Type type, std::uint16_t sppValue, double sampleTime) noexcept;

    /**
        Leaves Stopped or clock loss for the free-running sweep from `sampleTime` on, as before the
        first Start (D-090). The position is kept, so a Continue resumes where the song stopped,
        and Start or Continue follow MIDI Clock again. In any other state it does nothing.
    */
    void runFree(double sampleTime) noexcept;

    /** Tells the transport that audio has reached `sampleTime`, for the clock-loss timeout. */
    void advanceTo(double sampleTime) noexcept;

    [[nodiscard]] TransportState state() const noexcept
    {
        return state_;
    }

    /** Beats per minute from the recent clocks, or 0 if not known. Clocks count in every state. */
    [[nodiscard]] double bpm() const noexcept;

    /** The tick the next clock will be, while running; the position that Continue resumes from,
        while stopped. */
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

    /** Song Position Pointers ignored because they came while running. */
    [[nodiscard]] std::uint64_t ignoredSppCount() const noexcept
    {
        return ignoredSpp_;
    }

    /** Spans on the timeline, oldest first. There is always at least one. */
    [[nodiscard]] std::size_t numSpans() const noexcept
    {
        return spanCount_;
    }

    [[nodiscard]] const TransportSpan& span(std::size_t index) const noexcept;

    /** The span that holds `sampleTime`, after dropping every span that ends before it. */
    [[nodiscard]] const TransportSpan& spanAt(double sampleTime) noexcept;

private:
    [[nodiscard]] TransportSpan& openSpan() noexcept;
    void pushOpen(TransportSpan::Kind kind, double start, double startTick) noexcept;
    void closeOpenAt(double sampleTime) noexcept;
    void endPendingAt(double sampleTime) noexcept;
    [[nodiscard]] double framesPerTick() const noexcept;
    [[nodiscard]] double clampToTimeline(double sampleTime) const noexcept;

    void onClock(double sampleTime) noexcept;
    void onStart(double sampleTime) noexcept;
    void onContinue(double sampleTime) noexcept;
    void onStop(double sampleTime) noexcept;

    struct TempoFit
    {
        double framesPerClock = 0.0;
        double jitterVariance = 0.0;
    };

    /** The least-squares fit of the newest `count` tempo clocks, at least 2. */
    [[nodiscard]] TempoFit fitNewestClocks(std::size_t count) const noexcept;

    void addTempoClock(double sampleTime) noexcept;

    double sampleRate_;
    TimeSignature timeSignature_;

    TransportState state_ = TransportState::freeRunning;
    std::int64_t nextTick_ = 0;
    std::uint64_t startCount_ = 0;
    std::uint64_t ignoredSpp_ = 0;

    // While running: either waiting for the first clock since runStart_, or following ticks.
    bool awaitingTick_ = false;
    double runStart_ = 0.0;
    double lastTickTime_ = 0.0;
    double lastInterval_ = 0.0;

    // The timeline, a ring of spans; the newest is open.
    std::vector<TransportSpan> spans_;
    std::size_t oldestSpan_ = 0;
    std::size_t spanCount_ = 0;

    // The most recent clock times in any state, for the tempo: a ring of up to steadyTempoBars,
    // of which the newest tempoCount_ are in the estimate.
    std::vector<double> tempoClocks_;
    std::size_t tempoNewest_ = 0;
    std::size_t tempoCount_ = 0;
    double framesPerClock_ = 0.0;
    std::size_t tempoFollowClocks_ = 0;
};

} // namespace visona
