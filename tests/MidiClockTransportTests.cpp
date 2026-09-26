#include "support/AllocationCounter.h"
#include "support/SyntheticClock.h"

#include <visona/MidiClockEvent.h>
#include <visona/MidiClockTransport.h>
#include <visona/MusicalTime.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::MidiClockEvent;
using visona::MidiClockTransport;
using visona::musicalPosition;
using visona::MusicalPosition;
using visona::TimeSignature;
using visona::TransportState;
using visona::test::AllocationCounter;
using visona::test::firstSampleReaching;
using visona::test::samplesPerTick;
using visona::test::tickTimes;
using visona::test::walkSweep;
using visona::test::withJitter;
using Type = MidiClockEvent::Type;

namespace
{

constexpr double sampleRate96k = 96'000.0;
constexpr double tick120 = 2'000.0;  // samples per tick at 120 BPM and 96 kHz
constexpr double bar120 = 192'000.0; // samples per 4/4 bar at 120 BPM and 96 kHz

/** The scope's windows in ticks of 4/4: ¼, ½, 1, 2 and 4 bars. */
constexpr std::array<double, 5> windowTicks{24.0, 48.0, 96.0, 192.0, 384.0};
constexpr std::size_t numBins = 4'096;

void send(MidiClockTransport& transport, Type type, double sampleTime, std::uint16_t sppValue = 0)
{
    transport.handle(MidiClockEvent{type, sppValue, 0}, sampleTime);
}

void sendClocks(MidiClockTransport& transport, const std::vector<double>& times)
{
    for (const auto time : times)
        send(transport, Type::Clock, time);
}

/** Sends `count` Clocks `interval` samples apart from `first` on. Returns the next Clock's time. */
double sendClocks(MidiClockTransport& transport, double first, double interval, int count)
{
    for (int i = 0; i < count; ++i)
        send(transport, Type::Clock, first + i * interval);
    return first + count * interval;
}

double displayed(double bpm)
{
    return std::round(bpm * 10.0) / 10.0;
}

std::int64_t wholeSample(double sampleTime)
{
    return static_cast<std::int64_t>(std::floor(sampleTime));
}

MusicalPosition musicalPositionAt(const MidiClockTransport& transport, double sampleTime)
{
    const auto position = transport.positionAt(sampleTime);
    REQUIRE(position.has_value());
    return musicalPosition(*position, transport.timeSignature());
}

} // namespace

// Waiting ---------------------------------------------------------------------------------------

TEST_CASE("Before the first Start the transport waits and only follows the tempo", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    CHECK(transport.state() == TransportState::Waiting);
    CHECK(transport.nextTick() == 0);
    CHECK(transport.ticksPerBar() == 96);
    CHECK_FALSE(transport.bpm().has_value());
    CHECK_FALSE(transport.positionAt(0.0).has_value());

    const auto next = sendClocks(transport, 10'000.0, tick120, 48);
    CHECK(transport.state() == TransportState::Waiting);
    CHECK(transport.nextTick() == 0);
    REQUIRE(transport.bpm().has_value());
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
    CHECK_FALSE(transport.positionAt(20'000.0).has_value());
    CHECK(transport.horizon() == next - tick120);

    // Without Start, a clock that stops is not a clock loss.
    transport.advanceTo(next + 10.0 * sampleRate96k);
    CHECK(transport.state() == TransportState::Waiting);
    CHECK(transport.clockLossCount() == 0);
    CHECK(transport.horizon() == next + 10.0 * sampleRate96k);
}

// Synthetic clock -------------------------------------------------------------------------------

TEST_CASE("A 120 BPM clock at 96 kHz gives 96 ticks = 1 bar = 192000 samples", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    constexpr double startTime = 5'000.0;
    constexpr double downbeat = 5'250.0;
    send(transport, Type::Start, startTime);
    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 0);
    CHECK(transport.horizon() == startTime);

    constexpr int bars = 4;
    const auto times = tickTimes(bars * 96 + 2, downbeat, sampleRate96k, 120.0);
    sendClocks(transport, times);

    CHECK(transport.ticksPerBar() == 96);
    CHECK(transport.nextTick() == bars * 96 + 2);
    CHECK(transport.horizon() == times.back());
    REQUIRE(transport.bpm().has_value());
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
    CHECK(displayed(*transport.bpm()) == 120.0);

    SECTION("96 ticks take exactly 192000 samples")
    {
        for (int bar = 0; bar <= bars; ++bar)
        {
            const auto position = transport.positionAt(downbeat + bar * bar120);
            CAPTURE(bar);
            REQUIRE(position.has_value());
            CHECK_THAT(*position, WithinAbs(96.0 * bar, 1.0e-9));
        }
    }

    SECTION("bar boundaries land within ±1 sample")
    {
        for (int bar = 1; bar <= bars; ++bar)
        {
            const auto expected = static_cast<std::int64_t>(std::ceil(downbeat + bar * bar120));
            const auto boundary =
                firstSampleReaching(transport, 96.0 * bar, expected - 10, expected + 10);
            CAPTURE(bar);
            REQUIRE(boundary.has_value());
            CHECK(std::abs(*boundary - expected) <= 1);
            CHECK(musicalPositionAt(transport, static_cast<double>(*boundary)) ==
                  MusicalPosition{bar + 1, 1, 0.0});
        }
    }

    SECTION("beats and sixteenths")
    {
        CHECK(musicalPositionAt(transport, downbeat) == MusicalPosition{1, 1, 0.0});
        CHECK(musicalPositionAt(transport, downbeat + 48'000.0) == MusicalPosition{1, 2, 0.0});
        CHECK(musicalPositionAt(transport, downbeat + 12'000.0) == MusicalPosition{1, 1, 6.0});
        CHECK(musicalPositionAt(transport, downbeat + bar120 + 3 * 48'000.0) ==
              MusicalPosition{2, 4, 0.0});
    }

    SECTION("the sweep writes every bin once per pass at every window length")
    {
        for (const auto window : windowTicks)
        {
            const auto walk = walkSweep(transport, wholeSample(downbeat), wholeSample(times.back()),
                                        window, numBins);
            CAPTURE(window);
            CHECK(walk.holes == 0);
            CHECK(walk.rewrites == 0);
            CHECK(walk.completePasses == static_cast<std::size_t>(bars * 96 / window));
        }
    }
}

TEST_CASE("Bar boundaries land within ±1 sample at every sample rate and tempo", "[transport]")
{
    const auto sampleRate = GENERATE(44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0);
    const auto bpm = GENERATE(120.0, 126.0, 174.0);
    CAPTURE(sampleRate, bpm);

    MidiClockTransport transport(sampleRate);
    constexpr double downbeat = 1'234.5;
    send(transport, Type::Start, 1'000.0);
    constexpr int bars = 3;
    const auto times = tickTimes(bars * 96 + 2, downbeat, sampleRate, bpm);
    sendClocks(transport, times);

    const auto samplesPerBar = 96.0 * samplesPerTick(bpm, sampleRate);
    for (int bar = 1; bar <= bars; ++bar)
    {
        const auto expected = static_cast<std::int64_t>(std::ceil(downbeat + bar * samplesPerBar));
        const auto boundary =
            firstSampleReaching(transport, 96.0 * bar, expected - 10, expected + 10);
        CAPTURE(bar);
        REQUIRE(boundary.has_value());
        CHECK(std::abs(*boundary - expected) <= 1);
    }
    REQUIRE(transport.bpm().has_value());
    CHECK_THAT(*transport.bpm(), WithinAbs(bpm, 1.0e-9));
}

TEST_CASE("Positions are interpolated between known ticks, up to the latest one", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    sendClocks(transport, {100.0, 2'100.0, 4'000.0, 6'200.0});

    CHECK_FALSE(transport.positionAt(99.0).has_value());
    CHECK(transport.positionAt(100.0) == 0.0);
    CHECK(transport.positionAt(1'100.0) == 0.5);
    CHECK_THAT(*transport.positionAt(3'050.0), WithinAbs(1.5, 1.0e-12));
    CHECK(transport.positionAt(4'000.0) == 2.0);
    CHECK_THAT(*transport.positionAt(6'199.0), WithinAbs(2.0 + 2'199.0 / 2'200.0, 1.0e-12));
    // Tick 4 is not known yet, so nothing after tick 3 has a position.
    CHECK_FALSE(transport.positionAt(6'200.0).has_value());
    CHECK(transport.horizon() == 6'200.0);

    const auto segment = transport.segmentAt(5'000.0);
    REQUIRE(segment.has_value());
    CHECK(segment->startSample == 4'000.0);
    CHECK(segment->endSample == 6'200.0);
    CHECK(segment->startTick == 2.0);
    CHECK_THAT(segment->ticksPerSample, WithinAbs(1.0 / 2'200.0, 1.0e-15));
    CHECK_THAT(segment->positionAt(5'100.0), WithinAbs(2.5, 1.0e-12));
}

// Start, Stop and Continue ----------------------------------------------------------------------

TEST_CASE("Stop keeps the position and holds it after at most one extrapolated tick", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 50); // ticks 0..49
    const auto lastTick = next - tick120;

    SECTION("a Stop within the next interval ends the extrapolation")
    {
        send(transport, Type::Stop, lastTick + 700.0);
        CHECK(transport.state() == TransportState::Stopped);
        CHECK(transport.nextTick() == 50);
        CHECK(transport.horizon() == lastTick + 700.0);
        CHECK(transport.positionAt(lastTick) == 49.0);
        CHECK_THAT(*transport.positionAt(lastTick + 699.0),
                   WithinAbs(49.0 + 699.0 / 2'000.0, 1e-12));
        CHECK_FALSE(transport.positionAt(lastTick + 700.0).has_value());
    }

    SECTION("a late Stop extrapolates one tick and no more")
    {
        send(transport, Type::Stop, lastTick + 5'000.0);
        CHECK(transport.state() == TransportState::Stopped);
        CHECK_THAT(*transport.positionAt(lastTick + 1'999.0), WithinAbs(49.9995, 1.0e-12));
        CHECK_FALSE(transport.positionAt(lastTick + 2'000.0).has_value());
        CHECK_FALSE(transport.positionAt(lastTick + 4'000.0).has_value());
    }

    SECTION("Clock while stopped updates only the BPM estimate")
    {
        send(transport, Type::Stop, lastTick + 700.0);
        const auto afterStop =
            sendClocks(transport, next, samplesPerTick(100.0, sampleRate96k), 30);
        CHECK(transport.state() == TransportState::Stopped);
        CHECK(transport.nextTick() == 50);
        CHECK_THAT(*transport.bpm(), WithinAbs(100.0, 1.0e-9));
        CHECK_FALSE(transport.positionAt(next + 10'000.0).has_value());
        CHECK(transport.horizon() == afterStop - samplesPerTick(100.0, sampleRate96k));
    }

    SECTION("no clock while stopped is not a clock loss")
    {
        send(transport, Type::Stop, lastTick + 700.0);
        transport.advanceTo(lastTick + 60.0 * sampleRate96k);
        CHECK(transport.state() == TransportState::Stopped);
        CHECK(transport.clockLossCount() == 0);
    }

    SECTION("Stop while stopped changes nothing")
    {
        send(transport, Type::Stop, lastTick + 700.0);
        send(transport, Type::Stop, lastTick + 900.0);
        CHECK(transport.state() == TransportState::Stopped);
        CHECK_FALSE(transport.positionAt(lastTick + 800.0).has_value());
    }
}

TEST_CASE("Continue resumes counting from the kept position", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 50);
    send(transport, Type::Stop, next - 1'000.0);

    constexpr double continueTime = 300'000.0;
    send(transport, Type::Continue, continueTime);
    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 50);
    CHECK(transport.horizon() == continueTime);

    sendClocks(transport, continueTime + 400.0, tick120, 3);
    CHECK(transport.nextTick() == 53);
    CHECK(transport.positionAt(continueTime + 400.0) == 50.0);
    CHECK(transport.positionAt(continueTime + 1'400.0) == 50.5);
    // Nothing between the Stop and the first Clock after Continue has a position.
    CHECK_FALSE(transport.positionAt(next).has_value());
    CHECK_FALSE(transport.positionAt(continueTime + 399.0).has_value());
    CHECK(transport.clockLossCount() == 0);
}

TEST_CASE("Start sets the position to 0 and resets the BPM estimate", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 40);
    const auto lastTick = next - tick120;

    double restart = 0.0;
    SECTION("from Stopped")
    {
        send(transport, Type::Stop, lastTick + 500.0);
        restart = 200'000.0;
        send(transport, Type::Start, restart);
    }

    SECTION("while Running, which ends the previous run at the Start")
    {
        restart = lastTick + 500.0;
        send(transport, Type::Start, restart);
        CHECK_THAT(*transport.positionAt(lastTick + 499.0), WithinAbs(39.2495, 1.0e-12));
        CHECK_FALSE(transport.positionAt(lastTick + 500.0).has_value());
    }

    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 0);
    CHECK_FALSE(transport.bpm().has_value());

    const auto interval126 = samplesPerTick(126.0, sampleRate96k);
    const auto downbeat = restart + 100.0;
    send(transport, Type::Clock, downbeat);
    CHECK_FALSE(transport.bpm().has_value());
    send(transport, Type::Clock, downbeat + interval126);
    REQUIRE(transport.bpm().has_value());
    CHECK_THAT(*transport.bpm(), WithinAbs(126.0, 1.0e-9));
    CHECK(transport.positionAt(downbeat) == 0.0);
    CHECK(musicalPositionAt(transport, downbeat) == MusicalPosition{1, 1, 0.0});
    CHECK(transport.clockLossCount() == 0);
}

TEST_CASE("Stop before the first Start is ignored, and Continue starts from the song position",
          "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Stop, 100.0);
    CHECK(transport.state() == TransportState::Waiting);

    send(transport, Type::Continue, 1'000.0);
    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 0);
    sendClocks(transport, 1'500.0, tick120, 2);
    CHECK(transport.positionAt(1'500.0) == 0.0);
}

TEST_CASE("Continue while Running changes nothing", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    sendClocks(transport, 1'000.0, tick120, 10);
    send(transport, Type::Continue, 20'500.0);
    sendClocks(transport, 21'000.0, tick120, 10);

    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 20);
    // The run goes on across the Continue.
    CHECK(transport.positionAt(20'000.0) == 9.5);
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
}

// Clock loss --------------------------------------------------------------------------------------

TEST_CASE("More than 0.5 s without Clock while Running is a clock loss", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 100); // ticks 0..99
    const auto lastTick = next - tick120;
    const auto timeout = 0.5 * sampleRate96k;

    transport.advanceTo(lastTick + timeout);
    CHECK(transport.state() == TransportState::Running);
    transport.advanceTo(lastTick + timeout + 0.5);
    CHECK(transport.state() == TransportState::ClockLost);
    CHECK(transport.clockLossCount() == 1);
    CHECK(transport.nextTick() == 100);
    CHECK(transport.horizon() == lastTick + timeout + 0.5);

    // The last interval is extrapolated by one tick, then the position is held.
    CHECK_THAT(*transport.positionAt(lastTick + 1'000.0), WithinAbs(99.5, 1.0e-12));
    CHECK_FALSE(transport.positionAt(lastTick + tick120).has_value());
    CHECK_FALSE(transport.positionAt(lastTick + timeout).has_value());

    SECTION("the clock recovers and the tick count carries on")
    {
        const auto recovery = lastTick + 2.0 * sampleRate96k;
        sendClocks(transport, recovery, tick120, 3);
        CHECK(transport.state() == TransportState::Running);
        CHECK(transport.nextTick() == 103);
        CHECK(transport.positionAt(recovery) == 100.0);
        CHECK(transport.positionAt(recovery + 1'000.0) == 100.5);
        // The gap is not interpolated.
        CHECK_FALSE(transport.positionAt(recovery - 1.0).has_value());
        CHECK_FALSE(transport.positionAt(lastTick + 3'000.0).has_value());
        // The interval across the gap is not a tempo.
        CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
        CHECK(transport.clockLossCount() == 1);
    }

    SECTION("Stop during a clock loss stops")
    {
        send(transport, Type::Stop, lastTick + timeout + 1'000.0);
        CHECK(transport.state() == TransportState::Stopped);
        send(transport, Type::Continue, lastTick + 3.0 * sampleRate96k);
        send(transport, Type::Clock, lastTick + 3.0 * sampleRate96k + 10.0);
        CHECK(transport.state() == TransportState::Running);
        CHECK(transport.nextTick() == 101);
    }
}

TEST_CASE("A Clock that comes back late first ends the run with a clock loss", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 20);
    const auto lastTick = next - tick120;

    // No advanceTo() in between: the late Clock itself reveals the loss.
    const auto late = lastTick + 0.6 * sampleRate96k;
    sendClocks(transport, late, tick120, 2);
    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.clockLossCount() == 1);
    CHECK(transport.positionAt(late) == 20.0);
    CHECK_FALSE(transport.positionAt(late - 1.0).has_value());
    CHECK_FALSE(transport.positionAt(lastTick + tick120).has_value());
}

TEST_CASE("Start without any Clock loses the clock after the timeout", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 1'000.0);
    transport.advanceTo(1'000.0 + 48'000.0);
    CHECK(transport.state() == TransportState::Running);
    transport.advanceTo(1'000.0 + 48'001.0);
    CHECK(transport.state() == TransportState::ClockLost);
    CHECK(transport.clockLossCount() == 1);
}

TEST_CASE("The clock-loss timeout can be changed", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    transport.setClockLossTimeout(0.25);
    send(transport, Type::Start, 0.0);
    send(transport, Type::Clock, 0.0);
    transport.advanceTo(24'001.0);
    CHECK(transport.state() == TransportState::ClockLost);
}

// Song Position Pointer -------------------------------------------------------------------------

TEST_CASE("SPP 16 and Continue give bar 2, beat 1", "[transport][spp]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 30);
    send(transport, Type::Stop, next - 500.0);

    send(transport, Type::SongPositionPointer, next + 1'000.0, 16);
    CHECK(transport.nextTick() == 96);
    send(transport, Type::Continue, next + 2'000.0);
    sendClocks(transport, next + 2'500.0, tick120, 2);

    CHECK(transport.positionAt(next + 2'500.0) == 96.0);
    CHECK(musicalPositionAt(transport, next + 2'500.0) == MusicalPosition{2, 1, 0.0});
    CHECK(transport.ignoredSppCount() == 0);
}

TEST_CASE("SPP 5 lands in the middle of a beat", "[transport][spp]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::SongPositionPointer, 0.0, 5);
    CHECK(transport.nextTick() == 30);
    send(transport, Type::Continue, 100.0);
    sendClocks(transport, 200.0, tick120, 2);

    // Five sixteenths: a quarter of the way into beat 2.
    const auto position = musicalPositionAt(transport, 200.0);
    CHECK(position == MusicalPosition{1, 2, 6.0});
    CHECK(position.tickInBeat > 0.0);
    CHECK(musicalPositionAt(transport, 1'200.0) == MusicalPosition{1, 2, 6.5});
}

TEST_CASE("Ableton Live's Stop, SPP, Continue relocates a running transport", "[transport][spp]")
{
    const bool clockWhileStopped = GENERATE(false, true);
    CAPTURE(clockWhileStopped);

    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    auto next = sendClocks(transport, 1'000.0, tick120, 200);

    // Relocate to bar 17: 16 bars of 16 sixteenths.
    const auto stopTime = next - 1'500.0;
    send(transport, Type::Stop, stopTime);
    if (clockWhileStopped)
        next = sendClocks(transport, next, tick120, 5);
    send(transport, Type::SongPositionPointer, next - 1'000.0, 16 * 16);
    if (clockWhileStopped)
        next = sendClocks(transport, next, tick120, 5);
    send(transport, Type::Continue, next - 800.0);
    sendClocks(transport, next, tick120, 98);

    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.ignoredSppCount() == 0);
    CHECK(transport.clockLossCount() == 0);
    CHECK(transport.nextTick() == 16 * 96 + 98);
    CHECK(musicalPositionAt(transport, next) == MusicalPosition{17, 1, 0.0});
    CHECK(musicalPositionAt(transport, next + bar120) == MusicalPosition{18, 1, 0.0});
    // The relocation is not interpolated.
    CHECK_FALSE(transport.positionAt(next - 1.0).has_value());
    CHECK_FALSE(transport.positionAt(stopTime).has_value());
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
}

TEST_CASE("SPP while Running is ignored and counted", "[transport][spp]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    const auto next = sendClocks(transport, 1'000.0, tick120, 10);
    send(transport, Type::SongPositionPointer, next - 1'000.0, 16);
    CHECK(transport.ignoredSppCount() == 1);
    CHECK(transport.nextTick() == 10);

    sendClocks(transport, next, tick120, 10);
    CHECK(transport.nextTick() == 20);
    CHECK(transport.positionAt(next - 1'000.0) == 9.5);
}

TEST_CASE("SPP is accepted whenever no ticks are being counted", "[transport][spp]")
{
    MidiClockTransport transport(sampleRate96k);
    double resume = 0.0;

    SECTION("before the first Start")
    {
        send(transport, Type::SongPositionPointer, 0.0, 256);
        resume = 100.0;
        send(transport, Type::Continue, resume);
    }

    SECTION("after Continue, until the first Clock")
    {
        send(transport, Type::Start, 0.0);
        sendClocks(transport, 50.0, tick120, 5);
        send(transport, Type::Stop, 9'000.0);
        resume = 10'000.0;
        send(transport, Type::Continue, resume);
        send(transport, Type::SongPositionPointer, resume + 50.0, 256);
    }

    SECTION("during a clock loss")
    {
        send(transport, Type::Start, 0.0);
        sendClocks(transport, 50.0, tick120, 5);
        transport.advanceTo(100'000.0);
        REQUIRE(transport.state() == TransportState::ClockLost);
        send(transport, Type::SongPositionPointer, 100'100.0, 256);
        resume = 100'200.0;
        send(transport, Type::Continue, resume);
    }

    CHECK(transport.state() == TransportState::Running);
    CHECK(transport.nextTick() == 256 * 6);
    const auto downbeat = resume + 100.0;
    sendClocks(transport, downbeat, tick120, 2);
    CHECK(musicalPositionAt(transport, downbeat) == MusicalPosition{17, 1, 0.0});
    CHECK(transport.ignoredSppCount() == 0);
}

TEST_CASE("The largest SPP, 16383, is handled", "[transport][spp]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::SongPositionPointer, 0.0, MidiClockEvent::maxSppValue);
    CHECK(transport.nextTick() == 98'298);
    send(transport, Type::Continue, 100.0);
    sendClocks(transport, 1'000.0, tick120, 200);

    CHECK(musicalPositionAt(transport, 1'000.0) == MusicalPosition{1'024, 4, 18.0});
    // Counting goes on past the largest position SPP can express.
    CHECK(musicalPositionAt(transport, 1'000.0 + 6.0 * tick120) == MusicalPosition{1'025, 1, 0.0});
    CHECK(transport.nextTick() == 98'498);
    CHECK(transport.ignoredSppCount() == 0);

    SECTION("a value above 16383 is ignored and counted")
    {
        send(transport, Type::Stop, 500'000.0);
        send(transport, Type::SongPositionPointer, 500'001.0,
             static_cast<std::uint16_t>(MidiClockEvent::maxSppValue + 1));
        CHECK(transport.ignoredSppCount() == 1);
        CHECK(transport.nextTick() == 98'498);
    }
}

// Clock timing ----------------------------------------------------------------------------------

TEST_CASE("A jittery clock keeps the sweep continuous and bars near the true bar lines",
          "[transport][timing]")
{
    const auto maxJitterMs = GENERATE(1.0, 2.0);
    CAPTURE(maxJitterMs);
    const auto maxJitter = maxJitterMs * sampleRate96k / 1'000.0;

    constexpr int bars = 8;
    constexpr double downbeat = 10'000.0;
    const auto trueTimes = tickTimes(bars * 96 + 2, downbeat, sampleRate96k, 120.0);
    const auto times = withJitter(trueTimes, maxJitter, 20'260'926);

    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);

    // After a full average, the mean interval is off by at most 2 × maxJitter / 24.
    const auto worstInterval = tick120 - 2.0 * maxJitter / 24.0;
    const auto bpmBound = 60.0 * sampleRate96k / (24.0 * worstInterval) - 120.0;
    double largestBpmError = 0.0;
    for (std::size_t tick = 0; tick < times.size(); ++tick)
    {
        send(transport, Type::Clock, times[tick]);
        if (tick > 24)
            largestBpmError = std::max(largestBpmError, std::abs(*transport.bpm() - 120.0));
    }
    WARN("Jitter ±" << maxJitterMs << " ms: the BPM estimate stays within ±" << largestBpmError
                    << " BPM of 120 (bound ±" << bpmBound << ")");
    CHECK(largestBpmError <= bpmBound + 1.0e-9);

    // Every tick lands where it arrived, so a bar line is off by that tick's jitter at most.
    for (int bar = 1; bar <= bars; ++bar)
    {
        const auto truth = trueTimes[static_cast<std::size_t>(bar * 96)];
        const auto boundary =
            firstSampleReaching(transport, 96.0 * bar, wholeSample(truth - maxJitter) - 2,
                                wholeSample(truth + maxJitter) + 2);
        CAPTURE(bar);
        REQUIRE(boundary.has_value());
        CHECK(std::abs(static_cast<double>(*boundary) - truth) <= maxJitter + 1.0);
    }

    for (const auto window : windowTicks)
    {
        const auto walk = walkSweep(transport, wholeSample(times.front()),
                                    wholeSample(times.back()), window, numBins);
        CAPTURE(window);
        CHECK(walk.holes == 0);
        CHECK(walk.rewrites == 0);
    }
}

TEST_CASE("A tempo change from 120 to 126 BPM settles within one beat", "[transport][timing]")
{
    const auto maxJitterMs = GENERATE(0.0, 1.0);
    CAPTURE(maxJitterMs);
    const auto maxJitter = maxJitterMs * sampleRate96k / 1'000.0;

    constexpr std::size_t changeTick = 4 * 96;
    constexpr double downbeat = 10'000.0;
    const auto trueTimes = tickTimes(8 * 96 + 2, downbeat, sampleRate96k, [](std::size_t tick)
                                     { return tick < changeTick ? 120.0 : 126.0; });
    const auto times = withJitter(trueTimes, maxJitter, 126);

    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);

    // Settled: from here on the estimate stays within the band. Without jitter that is 0.05 BPM,
    // so the display shows 126.0; with jitter it is the most a full average can be off.
    const auto worstInterval = samplesPerTick(126.0, sampleRate96k) - 2.0 * maxJitter / 24.0;
    const auto band =
        maxJitter == 0.0 ? 0.05 : 60.0 * sampleRate96k / (24.0 * worstInterval) - 126.0 + 1.0e-9;
    bool settled = false;
    std::size_t settledTick = 0;
    for (std::size_t tick = 0; tick < times.size(); ++tick)
    {
        send(transport, Type::Clock, times[tick]);
        if (tick <= changeTick)
            continue;
        const bool inBand = std::abs(*transport.bpm() - 126.0) <= band;
        if (inBand && !settled)
            settledTick = tick;
        settled = inBand;
    }

    REQUIRE(settled);
    const auto ticksToSettle = settledTick - changeTick;
    const auto settleMs = (trueTimes[settledTick] - trueTimes[changeTick]) / sampleRate96k * 1e3;
    WARN("120 -> 126 BPM with jitter ±" << maxJitterMs << " ms: within ±" << band << " BPM after "
                                        << ticksToSettle << " ticks (" << settleMs << " ms)");
    // The average spans 24 intervals, so a clean step settles when they are all new.
    if (maxJitter == 0.0)
    {
        CHECK(ticksToSettle == MidiClockTransport::bpmIntervalCount);
        CHECK(displayed(*transport.bpm()) == 126.0);
    }
    CHECK(ticksToSettle <= 2 * MidiClockTransport::bpmIntervalCount);

    // Positions follow the ticks, so the bar lines move with the tempo at once.
    if (maxJitter == 0.0)
    {
        for (int bar = 1; bar <= 8; ++bar)
        {
            const auto truth = trueTimes[static_cast<std::size_t>(bar * 96)];
            const auto boundary = firstSampleReaching(transport, 96.0 * bar, wholeSample(truth) - 5,
                                                      wholeSample(truth) + 5);
            CAPTURE(bar);
            REQUIRE(boundary.has_value());
            CHECK(std::abs(static_cast<double>(*boundary) - truth) <= 1.0);
        }
    }
}

TEST_CASE("A tempo ramp from 120 to 140 BPM over 4 bars leaves no holes or rewrites",
          "[transport][timing]")
{
    constexpr std::size_t rampTicks = 4 * 96;
    constexpr double downbeat = 3'000.0;
    const auto times = tickTimes(6 * 96 + 2, downbeat, sampleRate96k,
                                 [](std::size_t tick)
                                 {
                                     constexpr double ramp = 4.0 * 96.0;
                                     const auto progress =
                                         std::min(static_cast<double>(tick), ramp) / ramp;
                                     return 120.0 + 20.0 * progress;
                                 });

    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    for (std::size_t tick = 0; tick < times.size(); ++tick)
    {
        send(transport, Type::Clock, times[tick]);
        if (tick == rampTicks)
        {
            // The average of the last beat lags the ramp by half a beat.
            CHECK(*transport.bpm() > 139.0);
            CHECK(*transport.bpm() < 140.0);
        }
    }
    CHECK_THAT(*transport.bpm(), WithinAbs(140.0, 1.0e-9));

    for (std::size_t tick = 0; tick + 1 < times.size(); ++tick)
        REQUIRE(transport.positionAt(times[tick]) == static_cast<double>(tick));

    for (const auto window : windowTicks)
    {
        const auto walk =
            walkSweep(transport, wholeSample(downbeat), wholeSample(times.back()), window, numBins);
        CAPTURE(window);
        CHECK(walk.holes == 0);
        CHECK(walk.rewrites == 0);
        CHECK(walk.completePasses == static_cast<std::size_t>(6 * 96 / window));
    }
}

// Bookkeeping -----------------------------------------------------------------------------------

TEST_CASE("Events that arrive late are treated as happening at the latest time", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    transport.advanceTo(10'000.0);
    send(transport, Type::Start, 5'000.0);
    CHECK(transport.horizon() == 10'000.0);
    send(transport, Type::Clock, 9'000.0);
    send(transport, Type::Clock, 12'000.0);
    CHECK(transport.positionAt(10'000.0) == 0.0);
    CHECK(transport.positionAt(11'000.0) == 0.5);

    // Two Clocks at the same time: the empty interval between them is skipped.
    send(transport, Type::Clock, 12'000.0);
    send(transport, Type::Clock, 14'000.0);
    CHECK(transport.positionAt(12'000.0) == 2.0);
    CHECK(transport.positionAt(13'000.0) == 2.5);
}

TEST_CASE("Ticks older than the capacity have no position", "[transport]")
{
    MidiClockTransport transport(sampleRate96k, {}, 8);
    send(transport, Type::Start, 0.0);
    sendClocks(transport, 0.0, tick120, 20);
    CHECK_FALSE(transport.positionAt(1'000.0).has_value());
    CHECK_FALSE(transport.positionAt(11.0 * tick120).has_value());
    CHECK(transport.positionAt(12.5 * tick120) == 12.5);
    CHECK(transport.positionAt(18.5 * tick120) == 18.5);
}

TEST_CASE("reset() forgets everything and waits", "[transport]")
{
    MidiClockTransport transport(sampleRate96k);
    send(transport, Type::Start, 0.0);
    sendClocks(transport, 0.0, tick120, 20);
    send(transport, Type::SongPositionPointer, 50'000.0, 3);
    transport.advanceTo(200'000.0);
    REQUIRE(transport.clockLossCount() == 1);
    REQUIRE(transport.ignoredSppCount() == 1);

    transport.reset(48'000.0);
    CHECK(transport.state() == TransportState::Waiting);
    CHECK(transport.sampleRate() == 48'000.0);
    CHECK(transport.nextTick() == 0);
    CHECK_FALSE(transport.bpm().has_value());
    CHECK_FALSE(transport.positionAt(1'000.0).has_value());
    CHECK(transport.horizon() == 0.0);
    CHECK(transport.clockLossCount() == 0);
    CHECK(transport.ignoredSppCount() == 0);

    send(transport, Type::Start, 0.0);
    sendClocks(transport, 100.0, samplesPerTick(120.0, 48'000.0), 3);
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0e-9));
}

TEST_CASE("The time signature sets the ticks per bar", "[transport]")
{
    MidiClockTransport transport(sampleRate96k, {3, 4});
    CHECK(transport.ticksPerBar() == 72);
    transport.setTimeSignature({6, 8});
    CHECK(transport.ticksPerBar() == 72);
    CHECK(transport.timeSignature() == TimeSignature{6, 8});
    CHECK_THROWS_AS(transport.setTimeSignature({4, 3}), std::invalid_argument);
    CHECK(transport.timeSignature() == TimeSignature{6, 8});

    CHECK_THROWS_AS(MidiClockTransport(sampleRate96k, {0, 4}), std::invalid_argument);
    CHECK_THROWS_AS(MidiClockTransport(sampleRate96k, {}, 1), std::invalid_argument);
}

TEST_CASE("MidiClockTransport does not allocate while it runs", "[transport][realtime]")
{
    MidiClockTransport transport(sampleRate96k);
    const MidiClockEvent clock{Type::Clock, 0, 0};
    STATIC_REQUIRE(noexcept(transport.handle(clock, 0.0)));
    STATIC_REQUIRE(noexcept(transport.advanceTo(0.0)));
    STATIC_REQUIRE(noexcept(transport.positionAt(0.0)));
    STATIC_REQUIRE(noexcept(transport.segmentAt(0.0)));
    STATIC_REQUIRE(noexcept(transport.horizon()));
    STATIC_REQUIRE(noexcept(transport.bpm()));
    STATIC_REQUIRE(noexcept(transport.reset(sampleRate96k)));

    double positions = 0.0;
    const AllocationCounter allocations;
    double time = 0.0;
    for (int round = 0; round < 20; ++round)
    {
        transport.handle({Type::Start, 0, 0}, time);
        for (int tick = 0; tick < 500; ++tick, time += tick120)
        {
            transport.handle(clock, time);
            transport.advanceTo(time + 10.0);
            positions += transport.positionAt(time - 1'000.0).value_or(0.0);
        }
        transport.handle({Type::Stop, 0, 0}, time);
        transport.handle({Type::SongPositionPointer, 16, 0}, time);
        transport.handle({Type::Continue, 0, 0}, time);
        transport.advanceTo(time += sampleRate96k);
        positions += transport.bpm().value_or(0.0) + transport.horizon();
    }
    transport.reset(sampleRate96k);
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(positions > 0.0);
}
