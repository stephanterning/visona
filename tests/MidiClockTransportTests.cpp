#include "support/AllocationCounter.h"

#include <visona/ClockTimeMapper.h>
#include <visona/MidiClockTransport.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

using visona::BlockTiming;
using visona::ClockTimeMapper;
using visona::MidiClockTransport;
using visona::TimeSignature;
using visona::TransportSpan;
using visona::TransportState;
using visona::test::AllocationCounter;
using Type = visona::MidiClockEvent::Type;
using Kind = TransportSpan::Kind;

namespace
{

constexpr double rate = 96'000.0;

/** Frames between clocks at `bpm`: 2 000 at 120 BPM and 96 kHz. */
double framesPerTick(double bpm)
{
    return 60.0 * rate / (bpm * TimeSignature::ticksPerQuarterNote);
}

/** Sends `count` clocks from `first` on, `interval` frames apart. Returns the time after the last
    one. */
double sendClocks(MidiClockTransport& transport, double first, int count, double interval)
{
    for (int clock = 0; clock < count; ++clock)
        transport.handle(Type::Clock, 0, first + clock * interval);
    return first + count * interval;
}

std::vector<TransportSpan> spans(const MidiClockTransport& transport)
{
    std::vector<TransportSpan> result;
    for (std::size_t index = 0; index < transport.numSpans(); ++index)
        result.push_back(transport.span(index));
    return result;
}

/** Checks that the spans follow each other without gaps and that the last one is open. */
void checkContiguous(const std::vector<TransportSpan>& timeline)
{
    REQUIRE_FALSE(timeline.empty());
    for (std::size_t index = 1; index < timeline.size(); ++index)
    {
        CAPTURE(index);
        REQUIRE(timeline[index].start == timeline[index - 1].end);
        REQUIRE_FALSE(timeline[index - 1].isOpen());
    }
    REQUIRE(timeline.back().isOpen());
}

/** The sample time where the position last reached `tick`, from the musical spans. A relocated
    transport may pass the same tick more than once. */
double timeOfTick(const std::vector<TransportSpan>& timeline, double tick)
{
    for (auto span = timeline.rbegin(); span != timeline.rend(); ++span)
        if (span->kind == Kind::musical && span->startTick <= tick && tick < span->endTick)
            return span->start + (tick - span->startTick) / (span->endTick - span->startTick) *
                                     (span->end - span->start);
    return -1.0;
}

} // namespace

TEST_CASE("MidiClockTransport starts running free, with a free-running timeline", "[transport]")
{
    MidiClockTransport transport(rate);
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(transport.bpm() == 0.0);
    CHECK(transport.nextTick() == 0);
    REQUIRE(transport.numSpans() == 1);
    CHECK(transport.span(0).kind == Kind::freeRunning);
    CHECK(transport.span(0).start == 0.0);
    CHECK(transport.span(0).isOpen());
    CHECK_THROWS_AS(MidiClockTransport(rate, {}, 1), std::invalid_argument);
}

TEST_CASE("Time signatures give the ticks per bar and beat", "[transport]")
{
    CHECK(TimeSignature{}.ticksPerBar() == 96);
    CHECK(TimeSignature{}.ticksPerBeat() == 24);
    CHECK(TimeSignature{3, 4}.ticksPerBar() == 72);
    CHECK(TimeSignature{6, 8}.ticksPerBar() == 72);
    CHECK(TimeSignature{6, 8}.ticksPerBeat() == 12);
}

TEST_CASE("A perfect clock puts every bar boundary on its sample", "[transport]")
{
    const auto bpm = GENERATE(120.0, 126.0, 174.0);
    CAPTURE(bpm);
    const auto interval = framesPerTick(bpm);
    constexpr double startTime = 1'234.0;
    constexpr double firstClock = 5'000.0;

    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, startTime);
    CHECK(transport.state() == TransportState::running);
    sendClocks(transport, firstClock, 4 * 96 + 1, interval);

    const auto timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline.front().kind == Kind::freeRunning);
    CHECK(timeline.front().end == startTime);
    CHECK(timeline[1].kind == Kind::frozen); // after Start, before the downbeat
    CHECK(timeline[1].end == firstClock);
    CHECK(timeline.back().kind == Kind::pending);
    CHECK(timeline.back().startTick == 4.0 * 96.0);

    // At 120 BPM and 96 kHz, a bar is 96 ticks and 192 000 frames.
    for (int bar = 0; bar < 4; ++bar)
    {
        CAPTURE(bar);
        const auto expected = firstClock + bar * 96 * interval;
        CHECK(std::abs(timeOfTick(timeline, bar * 96.0) - expected) <= 1.0);
    }
    if (bpm == 120.0)
        CHECK(timeOfTick(timeline, 96.0) - firstClock == 192'000.0);
    CHECK(std::abs(transport.bpm() - bpm) < 0.001);
    CHECK(transport.nextTick() == 4 * 96 + 1);
}

TEST_CASE("A jittery clock keeps the position moving forward and the tempo steady", "[transport]")
{
    const auto interval = framesPerTick(120.0);
    const auto jitterMs = GENERATE(1.0, 2.0);
    CAPTURE(jitterMs);
    std::mt19937 random(static_cast<unsigned>(jitterMs * 10));
    std::uniform_real_distribution<double> jitter(-jitterMs * rate / 1000.0,
                                                  jitterMs * rate / 1000.0);

    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    double worstTempoError = 0.0;
    for (int clock = 0; clock < 8 * 96; ++clock)
    {
        transport.handle(Type::Clock, 0, 10'000.0 + clock * interval + jitter(random));
        if (clock > static_cast<int>(MidiClockTransport::tempoIntervals))
            worstTempoError = std::max(worstTempoError, std::abs(transport.bpm() - 120.0));
    }

    const auto timeline = spans(transport);
    checkContiguous(timeline);
    double lastTick = -1.0;
    for (const auto& span : timeline)
    {
        if (span.kind != Kind::musical)
            continue;
        REQUIRE(span.endTick == span.startTick + 1.0);
        REQUIRE(span.startTick > lastTick);
        REQUIRE(span.end > span.start);
        lastTick = span.startTick;
    }
    for (int bar = 1; bar < 8; ++bar)
        CHECK(std::abs(timeOfTick(timeline, bar * 96.0) - (10'000.0 + bar * 96 * interval)) <=
              jitterMs * rate / 1000.0);
    // The worst case of a least-squares fit over 25 clocks, each off by at most the jitter, is an
    // error of 0.12 jitter per clock interval: 0.69 BPM per millisecond of jitter at 120 BPM.
    CAPTURE(worstTempoError);
    CHECK(worstTempoError < 0.7 * jitterMs);
}

TEST_CASE("The tempo follows a change from 120 to 126 BPM within a beat", "[transport]")
{
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    auto time = sendClocks(transport, 1'000.0, 96, framesPerTick(120.0));
    REQUIRE(std::abs(transport.bpm() - 120.0) < 0.001);

    int settleClocks = -1;
    for (int clock = 0; clock < 96; ++clock)
    {
        transport.handle(Type::Clock, 0, time);
        time += framesPerTick(126.0);
        if (settleClocks < 0 && std::abs(transport.bpm() - 126.0) < 0.05)
            settleClocks = clock + 1;
    }
    INFO("settled after " << settleClocks << " clocks");
    CHECK(settleClocks > 0);
    CHECK(settleClocks <= static_cast<int>(MidiClockTransport::tempoIntervals) + 1);
    CHECK(std::abs(transport.bpm() - 126.0) < 0.001);
}

TEST_CASE("Stop freezes after at most one extrapolated tick, and Continue carries on",
          "[transport]")
{
    const auto interval = framesPerTick(120.0);
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    const auto afterClocks = sendClocks(transport, 1'000.0, 10, interval); // ticks 0..9
    const auto lastClock = afterClocks - interval;

    SECTION("Stop between two clocks")
    {
        transport.handle(Type::Stop, 0, lastClock + 0.25 * interval);
        CHECK(transport.state() == TransportState::stopped);
        CHECK(transport.nextTick() == 10);

        const auto timeline = spans(transport);
        checkContiguous(timeline);
        const auto& extrapolated = timeline[timeline.size() - 2];
        CHECK(extrapolated.kind == Kind::musical);
        CHECK(extrapolated.start == lastClock);
        CHECK(extrapolated.end == lastClock + 0.25 * interval);
        CHECK(extrapolated.endTick == 9.25);
        CHECK(timeline.back().kind == Kind::frozen);
    }

    SECTION("Stop long after the last clock")
    {
        transport.handle(Type::Stop, 0, lastClock + 3.0 * interval);
        const auto timeline = spans(transport);
        const auto& extrapolated = timeline[timeline.size() - 2];
        CHECK(extrapolated.end == lastClock + interval);
        CHECK(extrapolated.endTick == 10.0);
        CHECK(timeline.back().kind == Kind::frozen);
        CHECK(timeline.back().start == lastClock + interval);
    }

    SECTION("Continue resumes the tick count")
    {
        const auto startCount = transport.startCount();
        transport.handle(Type::Stop, 0, lastClock + interval);
        transport.handle(Type::Continue, 0, 100'000.0);
        CHECK(transport.state() == TransportState::running);
        sendClocks(transport, 101'000.0, 3, interval);
        CHECK(transport.nextTick() == 13);
        CHECK(transport.startCount() == startCount);

        const auto timeline = spans(transport);
        checkContiguous(timeline);
        CHECK(timeline.back().kind == Kind::pending);
        CHECK(timeline.back().startTick == 12.0);
        CHECK(timeOfTick(timeline, 10.0) == 101'000.0);
    }

    SECTION("Clocks while stopped update the tempo only")
    {
        transport.handle(Type::Stop, 0, lastClock + interval);
        const auto numSpans = transport.numSpans();
        sendClocks(transport, 50'000.0, 30, framesPerTick(90.0));
        CHECK(transport.state() == TransportState::stopped);
        CHECK(transport.nextTick() == 10);
        CHECK(transport.numSpans() == numSpans);
        CHECK(std::abs(transport.bpm() - 90.0) < 0.001);
    }
}

TEST_CASE("Clocks while running free update the tempo only", "[transport]")
{
    MidiClockTransport transport(rate);
    sendClocks(transport, 0.0, 50, framesPerTick(128.0));
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(transport.nextTick() == 0);
    CHECK(transport.numSpans() == 1);
    CHECK(std::abs(transport.bpm() - 128.0) < 0.001);
}

TEST_CASE("Half a second without clocks while running is clock loss", "[transport]")
{
    const auto interval = framesPerTick(120.0);
    const auto timeout = MidiClockTransport::clockLossSeconds * rate;
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    const auto lastClock = sendClocks(transport, 1'000.0, 48, interval) - interval; // ticks 0..47

    transport.advanceTo(lastClock + timeout);
    CHECK(transport.state() == TransportState::running);
    CHECK(transport.span(transport.numSpans() - 1).kind == Kind::pending);

    transport.advanceTo(lastClock + timeout + 1.0);
    CHECK(transport.state() == TransportState::clockLost);
    auto timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline[timeline.size() - 2].kind == Kind::musical);
    CHECK(timeline[timeline.size() - 2].end == lastClock + interval);
    CHECK(timeline[timeline.size() - 2].endTick == 48.0);
    CHECK(timeline.back().kind == Kind::frozen);

    SECTION("the clock comes back and counting continues")
    {
        const auto resume = lastClock + 2.0 * timeout;
        sendClocks(transport, resume, 2, interval);
        CHECK(transport.state() == TransportState::running);
        CHECK(transport.nextTick() == 50);
        timeline = spans(transport);
        checkContiguous(timeline);
        CHECK(timeOfTick(timeline, 48.0) == resume);
        // The gap is not a clock interval, so the tempo stays right.
        CHECK(std::abs(transport.bpm() - 120.0) < 0.001);
    }

    SECTION("Stop while the clock is lost")
    {
        transport.handle(Type::Stop, 0, lastClock + 2.0 * timeout);
        CHECK(transport.state() == TransportState::stopped);
    }
}

TEST_CASE("A Start with no clock after it is clock loss too", "[transport]")
{
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 1'000.0);
    transport.advanceTo(1'000.0 + MidiClockTransport::clockLossSeconds * rate + 1.0);
    CHECK(transport.state() == TransportState::clockLost);
    CHECK(transport.span(transport.numSpans() - 1).kind == Kind::frozen);
}

TEST_CASE("Clock loss is only checked while running", "[transport]")
{
    MidiClockTransport transport(rate);
    transport.advanceTo(10.0 * rate);
    CHECK(transport.state() == TransportState::freeRunning);
    transport.handle(Type::Start, 0, 0.0);
    sendClocks(transport, 100.0, 5, framesPerTick(120.0));
    transport.handle(Type::Stop, 0, 20'000.0);
    transport.advanceTo(10.0 * rate);
    CHECK(transport.state() == TransportState::stopped);
}

TEST_CASE("Song Position Pointer sets where Continue resumes", "[transport][spp]")
{
    const auto interval = framesPerTick(120.0);
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    sendClocks(transport, 1'000.0, 200, interval);
    transport.handle(Type::Stop, 0, 1'000.0 + 200 * interval);

    SECTION("SPP 16 is bar 2, beat 1")
    {
        transport.handle(Type::SongPositionPointer, 16, 500'000.0);
        CHECK(transport.nextTick() == 96);
        transport.handle(Type::Continue, 0, 500'100.0);
        sendClocks(transport, 501'000.0, 2, interval);
        const auto timeline = spans(transport);
        checkContiguous(timeline);
        // The first clock after Continue is the position the pointer set.
        CHECK(timeOfTick(timeline, 96.0) == 501'000.0);
        CHECK(96 / TimeSignature{}.ticksPerBar() + 1 == 2);
    }

    SECTION("SPP 5 is in the middle of a beat")
    {
        transport.handle(Type::SongPositionPointer, 5, 500'000.0);
        CHECK(transport.nextTick() == 30);
        CHECK(transport.nextTick() % TimeSignature{}.ticksPerBeat() != 0);
    }

    SECTION("Ableton Live's Stop, SPP, Continue relocates the position")
    {
        transport.handle(Type::SongPositionPointer, 64, 500'000.0); // bar 17
        transport.handle(Type::Continue, 0, 500'010.0);
        sendClocks(transport, 501'000.0, 3, interval);
        const auto timeline = spans(transport);
        checkContiguous(timeline);
        CHECK(timeOfTick(timeline, 384.0) == 501'000.0);
        CHECK(384 / TimeSignature{}.ticksPerBar() + 1 == 5);
        CHECK(transport.nextTick() == 387);
    }

    SECTION("the largest SPP value")
    {
        transport.handle(Type::SongPositionPointer, 16'383, 500'000.0);
        CHECK(transport.nextTick() == 98'298);
    }

    SECTION("SPP while clock is lost")
    {
        transport.handle(Type::Continue, 0, 600'000.0);
        transport.advanceTo(600'000.0 + rate);
        REQUIRE(transport.state() == TransportState::clockLost);
        transport.handle(Type::SongPositionPointer, 8, 700'000.0);
        CHECK(transport.nextTick() == 48);
    }
}

TEST_CASE("Song Position Pointer is ignored while running", "[transport][spp]")
{
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    sendClocks(transport, 1'000.0, 10, framesPerTick(120.0));
    transport.handle(Type::SongPositionPointer, 64, 30'000.0);
    CHECK(transport.nextTick() == 10);
    CHECK(transport.ignoredSppCount() == 1);
}

TEST_CASE("Continue without a Start leaves the free-running sweep", "[transport][spp]")
{
    MidiClockTransport transport(rate);
    transport.handle(Type::SongPositionPointer, 32, 1'000.0);
    transport.handle(Type::Continue, 0, 1'100.0);
    CHECK(transport.state() == TransportState::running);
    CHECK(transport.startCount() == 1);
    transport.handle(Type::Clock, 0, 2'000.0);
    const auto timeline = spans(transport);
    CHECK(timeline.front().kind == Kind::freeRunning);
    CHECK(timeline.back().startTick == 192.0);
    CHECK(timeline.back().startCount == 1);
}

TEST_CASE("runFree leaves Stopped for the free-running sweep, and Continue follows MIDI again",
          "[transport]")
{
    MidiClockTransport transport(rate);
    const auto interval = framesPerTick(120.0);
    transport.handle(Type::Start, 0, 1'000.0);
    auto time = sendClocks(transport, 2'000.0, 48, interval);
    transport.handle(Type::Stop, 0, time);
    REQUIRE(transport.state() == TransportState::stopped);
    const auto starts = transport.startCount();
    const auto stoppedAt = transport.nextTick();
    const auto tempo = transport.bpm();

    transport.runFree(time + 10'000.0);
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(transport.startCount() == starts + 1);
    CHECK(transport.nextTick() == stoppedAt);
    CHECK(transport.bpm() == tempo);
    auto timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline.back().kind == Kind::freeRunning);
    CHECK(timeline.back().start == time + 10'000.0);
    CHECK(timeline.back().startCount == starts + 1);
    CHECK(timeline[timeline.size() - 2].kind == Kind::frozen);

    // Continue resumes where the song stopped, as a new sweep.
    time += 50'000.0;
    transport.handle(Type::Continue, 0, time);
    sendClocks(transport, time + 500.0, 3, interval);
    CHECK(transport.state() == TransportState::running);
    CHECK(transport.startCount() == starts + 2);
    CHECK(transport.nextTick() == stoppedAt + 3);
    timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline.back().startCount == starts + 2);
}

TEST_CASE("runFree leaves clock loss too, and nothing else", "[transport]")
{
    MidiClockTransport transport(rate);
    transport.runFree(100.0);
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(transport.startCount() == 0);
    CHECK(transport.numSpans() == 1);

    const auto interval = framesPerTick(120.0);
    transport.handle(Type::Start, 0, 1'000.0);
    const auto time = sendClocks(transport, 2'000.0, 30, interval);
    transport.runFree(time);
    CHECK(transport.state() == TransportState::running);
    CHECK(transport.startCount() == 1);

    transport.advanceTo(time + rate);
    REQUIRE(transport.state() == TransportState::clockLost);
    transport.runFree(time + rate);
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(transport.startCount() == 2);
    const auto timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline.back().kind == Kind::freeRunning);

    // Clocks while running free only feed the tempo.
    sendClocks(transport, time + 2.0 * rate, 30, interval);
    CHECK(transport.state() == TransportState::freeRunning);
    CHECK(std::abs(transport.bpm() - 120.0) < 0.01);
}

TEST_CASE("Start restarts from bar 1, also while running", "[transport]")
{
    const auto interval = framesPerTick(120.0);
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 0.0);
    sendClocks(transport, 1'000.0, 50, interval);
    const auto firstCount = transport.startCount();

    const auto restart = 1'000.0 + 49.5 * interval;
    transport.handle(Type::Start, 0, restart);
    CHECK(transport.startCount() == firstCount + 1);
    CHECK(transport.nextTick() == 0);
    sendClocks(transport, restart + 700.0, 2, interval);

    const auto timeline = spans(transport);
    checkContiguous(timeline);
    // The interrupted tick is extrapolated up to the Start.
    const auto& beforeRestart = timeline[timeline.size() - 4];
    CHECK(beforeRestart.kind == Kind::musical);
    CHECK(beforeRestart.end == restart);
    CHECK(beforeRestart.endTick == 49.5);
    CHECK(timeline[timeline.size() - 3].kind == Kind::frozen);
    CHECK(timeline[timeline.size() - 3].startCount == firstCount + 1);
    CHECK(timeline.back().startTick == 1.0);
}

TEST_CASE("Events stamped before earlier ones count as simultaneous", "[transport]")
{
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 10'000.0);
    transport.handle(Type::Clock, 0, 12'000.0);
    transport.handle(Type::Clock, 0, 11'000.0);
    const auto timeline = spans(transport);
    checkContiguous(timeline);
    CHECK(timeline.back().start == 12'000.0);
    CHECK(timeline.back().startTick == 1.0);
}

TEST_CASE("spanAt drops the spans the audio has passed", "[transport]")
{
    const auto interval = framesPerTick(120.0);
    MidiClockTransport transport(rate);
    transport.handle(Type::Start, 0, 500.0);
    sendClocks(transport, 1'000.0, 5, interval);
    REQUIRE(transport.numSpans() == 7);

    CHECK(transport.spanAt(0.0).kind == Kind::freeRunning);
    CHECK(transport.spanAt(700.0).kind == Kind::frozen);
    const auto& musical = transport.spanAt(1'000.0 + 1.5 * interval);
    CHECK(musical.kind == Kind::musical);
    CHECK(musical.startTick == 1.0);
    CHECK(musical.tickAt(1'000.0 + 1.5 * interval) == 1.5);
    CHECK(transport.numSpans() == 4);
    CHECK(transport.spanAt(1.0e9).kind == Kind::pending);
    CHECK(transport.numSpans() == 1);
}

TEST_CASE("A full timeline forgets its oldest spans", "[transport]")
{
    MidiClockTransport transport(rate, {}, 8);
    transport.handle(Type::Start, 0, 0.0);
    sendClocks(transport, 100.0, 50, framesPerTick(120.0));
    const auto timeline = spans(transport);
    CHECK(timeline.size() == 8);
    checkContiguous(timeline);
    CHECK(timeline.back().startTick == 49.0);
}

TEST_CASE("Clock mapping and transport put bar boundaries on their samples", "[transport][clock]")
{
    // Audio blocks and MIDI clocks from one host clock, with a little timestamp jitter on the
    // blocks, as from the audio device.
    constexpr double hostStart = 7.0e12;
    constexpr std::uint32_t blockFrames = 256;
    const auto hostTimeOf = [](double sampleTime, double jitterNs = 0.0)
    {
        return static_cast<std::uint64_t>(
            std::llround(hostStart + sampleTime / rate * 1.0e9 + jitterNs));
    };

    std::mt19937 random(11);
    std::uniform_real_distribution<double> blockJitterNs(-20'000.0, 20'000.0);
    ClockTimeMapper mapper(rate);
    MidiClockTransport transport(rate);
    const auto interval = framesPerTick(120.0);
    const double firstClock = 1.25 * rate;

    std::uint64_t sampleIndex = 0;
    int nextClock = 0;
    bool started = false;
    while (nextClock <= 2 * 96 + 1)
    {
        mapper.addBlock({sampleIndex,
                         hostTimeOf(static_cast<double>(sampleIndex), blockJitterNs(random)),
                         blockFrames});
        sampleIndex += blockFrames;
        // MIDI arrives while the audio up to it is still being delivered.
        const auto clockTime = firstClock + nextClock * interval;
        if (clockTime < static_cast<double>(sampleIndex))
        {
            if (!started)
            {
                transport.handle(Type::Start, 0,
                                 mapper.sampleTimeOf(hostTimeOf(clockTime - 300.0)));
                started = true;
            }
            transport.handle(Type::Clock, 0, mapper.sampleTimeOf(hostTimeOf(clockTime)));
            ++nextClock;
        }
    }

    const auto timeline = spans(transport);
    checkContiguous(timeline);
    for (int bar = 0; bar <= 2; ++bar)
    {
        CAPTURE(bar);
        CHECK(std::abs(timeOfTick(timeline, bar * 96.0) - (firstClock + bar * 192'000.0)) <= 1.0);
    }
}

TEST_CASE("MidiClockTransport does not allocate", "[transport][realtime]")
{
    MidiClockTransport transport(rate);
    STATIC_REQUIRE(noexcept(transport.handle(Type::Clock, 0, 0.0)));
    STATIC_REQUIRE(noexcept(transport.advanceTo(0.0)));
    STATIC_REQUIRE(noexcept(transport.spanAt(0.0)));

    const AllocationCounter allocations;
    double time = 0.0;
    for (int round = 0; round < 20; ++round)
    {
        transport.handle(Type::Start, 0, time);
        time = sendClocks(transport, time + 100.0, 100, 2'000.0);
        transport.handle(Type::Stop, 0, time);
        transport.handle(Type::SongPositionPointer, 16, time + 10.0);
        transport.advanceTo(time);
        static_cast<void>(transport.spanAt(time));
        time += rate;
    }
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}
