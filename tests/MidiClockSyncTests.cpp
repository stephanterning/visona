#include "support/AllocationCounter.h"
#include "support/SyntheticClock.h"

#include <visona/AudioRingBuffer.h>
#include <visona/BlockTiming.h>
#include <visona/ClockTimeMapper.h>
#include <visona/MidiClockEvent.h>
#include <visona/MidiClockTransport.h>
#include <visona/MusicalTime.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::AudioRingBuffer;
using visona::BlockTiming;
using visona::ClockTimeMapper;
using visona::MidiClockEvent;
using visona::MidiClockQueue;
using visona::MidiClockTransport;
using visona::musicalPosition;
using visona::MusicalPosition;
using visona::TransportState;
using visona::test::AllocationCounter;
using visona::test::AudioClock;
using visona::test::blockTimings;
using visona::test::firstSampleReaching;
using visona::test::walkSweep;
using Type = MidiClockEvent::Type;

namespace
{

constexpr double sampleRate96k = 96'000.0;
constexpr std::uint64_t streamStart = 86'400'000'000'000;
constexpr std::uint64_t oneSecond = 1'000'000'000;

/** Host times of `count` Clocks at `bpm` from `firstNs` on, each moved by up to ±maxJitterNs. */
std::vector<std::uint64_t> clockHostTimes(std::uint64_t firstNs, double bpm, std::size_t count,
                                          double maxJitterNs = 0.0, std::uint32_t seed = 1)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> jitter(-maxJitterNs, maxJitterNs);
    const auto intervalNs = 60.0e9 / (visona::ticksPerQuarterNote * bpm);
    std::vector<std::uint64_t> times(count);
    for (std::size_t tick = 0; tick < count; ++tick)
    {
        const auto offset =
            static_cast<double>(tick) * intervalNs + (maxJitterNs > 0.0 ? jitter(random) : 0.0);
        times[tick] = firstNs + static_cast<std::uint64_t>(std::llround(offset));
    }
    return times;
}

void appendClocks(std::vector<MidiClockEvent>& events, const std::vector<std::uint64_t>& times)
{
    for (const auto time : times)
        events.push_back({Type::Clock, 0, time});
}

/**
    What the analysis thread will do with blocks and MIDI events: take each new block, then every
    MIDI event up to that block's host time, then advance the transport to the block.
*/
class AnalysisLoop
{
public:
    explicit AnalysisLoop(double sampleRate)
        : mapper(sampleRate)
        , transport(sampleRate)
    {
    }

    void addBlock(const BlockTiming& block) noexcept
    {
        mapper.addBlock(block);
        latestBlockNs_ = std::max(latestBlockNs_, block.hostTimeNs);
    }

    [[nodiscard]] bool isDue(const MidiClockEvent& event) const noexcept
    {
        return event.hostTimeNs <= latestBlockNs_;
    }

    void handle(const MidiClockEvent& event) noexcept
    {
        transport.handle(event, *mapper.midiSampleTime(event.hostTimeNs));
    }

    void advance() noexcept
    {
        transport.advanceTo(*mapper.midiSampleTime(latestBlockNs_));
    }

    ClockTimeMapper mapper;
    MidiClockTransport transport;

private:
    std::uint64_t latestBlockNs_ = 0;
};

void run(AnalysisLoop& loop, const std::vector<BlockTiming>& blocks,
         const std::vector<MidiClockEvent>& events)
{
    std::size_t next = 0;
    for (const auto& block : blocks)
    {
        loop.addBlock(block);
        while (next < events.size() && loop.isDue(events[next]))
            loop.handle(events[next++]);
        loop.advance();
    }
}

} // namespace

TEST_CASE("From MIDI host times to stream positions, bar lines land within ±1 sample", "[sync]")
{
    const auto driftPpm = GENERATE(0.0, 60.0);
    const auto latencyOffset = GENERATE(0.0, 250.5);
    CAPTURE(driftPpm, latencyOffset);

    const AudioClock clock{sampleRate96k, streamStart, driftPpm};
    const auto blocks = blockTimings(clock, static_cast<std::size_t>(12 * 96'000 / 512), 512);
    const auto clocks = clockHostTimes(streamStart + oneSecond + 10'000'000, 120.0, 4 * 96 + 2);
    std::vector<MidiClockEvent> events{{Type::Start, 0, streamStart + oneSecond}};
    appendClocks(events, clocks);

    AnalysisLoop loop(sampleRate96k);
    loop.mapper.setLatencyOffset(latencyOffset);
    run(loop, blocks, events);
    const auto& transport = loop.transport;

    // The audio runs on for 3 s after the last Clock.
    CHECK(transport.state() == TransportState::ClockLost);
    CHECK(transport.clockLossCount() == 1);
    CHECK(transport.nextTick() == 4 * 96 + 2);
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0 * (1.0 + driftPpm * 1.0e-6), 1.0e-4));
    for (int bar = 0; bar <= 4; ++bar)
    {
        const auto truth =
            clock.sampleOf(clocks[static_cast<std::size_t>(bar * 96)]) - latencyOffset;
        const auto expected = static_cast<std::int64_t>(std::ceil(truth));
        const auto boundary =
            firstSampleReaching(transport, 96.0 * bar, expected - 10, expected + 10);
        CAPTURE(bar);
        REQUIRE(boundary.has_value());
        CHECK(std::abs(*boundary - expected) <= 1);
        const auto position = musicalPosition(*transport.positionAt(static_cast<double>(*boundary)),
                                              transport.timeSignature());
        CHECK(position.bar == bar + 1);
        CHECK(position.beat == 1);
        CHECK(position.tickInBeat < 0.01);
    }
}

TEST_CASE("MIDI jitter, callback jitter and drift together keep the sweep continuous", "[sync]")
{
    const AudioClock clock{sampleRate96k, streamStart, 80.0};
    const auto blocks =
        blockTimings(clock, static_cast<std::size_t>(20 * 96'000 / 256), 256, 100'000.0, 5);
    constexpr double maxMidiJitterNs = 1'000'000.0;
    const auto clocks =
        clockHostTimes(streamStart + oneSecond, 126.0, 8 * 96 + 2, maxMidiJitterNs, 9);
    std::vector<MidiClockEvent> events{{Type::Start, 0, streamStart + oneSecond - 5'000'000}};
    appendClocks(events, clocks);

    AnalysisLoop loop(sampleRate96k);
    run(loop, blocks, events);
    const auto& transport = loop.transport;

    const auto maxJitter = maxMidiJitterNs * 1.0e-9 * sampleRate96k;
    for (int bar = 1; bar <= 8; ++bar)
    {
        // The true bar line, without MIDI jitter.
        const auto truth = clock.sampleOf(streamStart + oneSecond) +
                           bar * 96 * visona::test::samplesPerTick(126.0, sampleRate96k) /
                               (1.0 + clock.driftPpm * 1.0e-6);
        const auto from = static_cast<std::int64_t>(truth - maxJitter) - 5;
        const auto boundary = firstSampleReaching(transport, 96.0 * bar, from,
                                                  static_cast<std::int64_t>(truth + maxJitter) + 5);
        CAPTURE(bar);
        REQUIRE(boundary.has_value());
        CHECK(std::abs(static_cast<double>(*boundary) - truth) <= maxJitter + 2.0);
    }

    const auto first = static_cast<std::int64_t>(*loop.mapper.midiSampleTime(clocks.front()));
    const auto last = static_cast<std::int64_t>(*loop.mapper.midiSampleTime(clocks.back()));
    for (const auto window : {24.0, 96.0, 384.0})
    {
        const auto walk = walkSweep(transport, first + 1, last - 1, window, 4'096);
        CAPTURE(window);
        CHECK(walk.holes == 0);
        CHECK(walk.rewrites == 0);
    }
}

TEST_CASE("The analysis thread follows audio and MIDI from their own threads without races",
          "[sync][stress]")
{
    // A session: Start, 8 bars, then Ableton's Stop, SPP, Continue to bar 17, 4 more bars, Stop.
    const AudioClock clock{sampleRate96k, streamStart, 40.0};
    const auto blocks =
        blockTimings(clock, static_cast<std::size_t>(30 * 96'000 / 512), 512, 150'000.0, 3);

    std::vector<MidiClockEvent> events{{Type::Start, 0, streamStart + oneSecond}};
    const auto firstRun =
        clockHostTimes(streamStart + oneSecond + 5'000'000, 120.0, 8 * 96, 1'000'000.0, 21);
    appendClocks(events, firstRun);
    const auto stop = firstRun.back() + 5'000'000;
    events.push_back({Type::Stop, 0, stop});
    events.push_back({Type::SongPositionPointer, 16 * 16, stop + 100'000'000});
    events.push_back({Type::Continue, 0, stop + 200'000'000});
    const auto secondRun = clockHostTimes(stop + 205'000'000, 120.0, 4 * 96 + 2, 1'000'000.0, 22);
    appendClocks(events, secondRun);
    events.push_back({Type::Stop, 0, secondRun.back() + 5'000'000});

    AnalysisLoop reference(sampleRate96k);
    run(reference, blocks, events);

    AudioRingBuffer ring(1, 8'192, 64);
    MidiClockQueue midiQueue(64);
    std::atomic<bool> audioDone{false};
    std::atomic<bool> midiDone{false};
    std::atomic<std::uint64_t> midiPushedUpTo{0};

    std::size_t audioAllocations = 0;
    std::thread audio(
        [&]
        {
            const AllocationCounter allocations;
            const std::array<const float*, 1> silence{nullptr};
            for (const auto& block : blocks)
                while (!ring.push(silence, block))
                    std::this_thread::yield();
            audioAllocations = allocations.count();
            audioDone.store(true, std::memory_order_release);
        });

    std::size_t midiAllocations = 0;
    std::thread midi(
        [&]
        {
            const AllocationCounter allocations;
            for (const auto& event : events)
            {
                while (!midiQueue.tryPush(event))
                    std::this_thread::yield();
                midiPushedUpTo.store(event.hostTimeNs, std::memory_order_release);
            }
            midiAllocations = allocations.count();
            midiDone.store(true, std::memory_order_release);
        });

    // This thread is the analysis thread. It reads the ring in random parts, so it sees most
    // blocks' timings more than once.
    AnalysisLoop loop(sampleRate96k);
    std::mt19937 random(5);
    std::uniform_int_distribution<std::size_t> readSize(1, 700);
    const AllocationCounter allocations;
    for (;;)
    {
        const auto region = ring.peek();
        if (!region)
        {
            if (audioDone.load(std::memory_order_acquire) && !ring.peek())
                break;
            std::this_thread::yield();
            continue;
        }

        // MIDI up to this block's time must have arrived before the block is analyzed.
        const auto& block = region->block();
        while (!midiDone.load(std::memory_order_acquire) &&
               midiPushedUpTo.load(std::memory_order_acquire) <= block.hostTimeNs)
            std::this_thread::yield();

        loop.addBlock(block);
        while (const auto* event = midiQueue.peek())
        {
            if (!loop.isDue(*event))
                break;
            loop.handle(*event);
            static_cast<void>(midiQueue.tryPop());
        }
        loop.advance();
        ring.consume(std::min(region->numFrames(), readSize(random)));
    }
    const auto analysisAllocations = allocations.count();
    audio.join();
    midi.join();

    CHECK(audioAllocations == 0);
    CHECK(midiAllocations == 0);
    CHECK(analysisAllocations == 0);

    // The threads change nothing: the result is the same as handing everything over in order.
    const auto& transport = loop.transport;
    CHECK(transport.state() == reference.transport.state());
    CHECK(transport.nextTick() == reference.transport.nextTick());
    CHECK(transport.bpm() == reference.transport.bpm());
    CHECK(transport.horizon() == reference.transport.horizon());
    std::size_t differences = 0;
    const auto end = static_cast<std::int64_t>(transport.horizon());
    for (std::int64_t sample = 0; sample < end; sample += 97)
        if (transport.positionAt(static_cast<double>(sample)) !=
            reference.transport.positionAt(static_cast<double>(sample)))
            ++differences;
    CHECK(differences == 0);

    CHECK(transport.state() == TransportState::Stopped);
    CHECK(transport.ignoredSppCount() == 0);
    CHECK(transport.clockLossCount() == 0);
    CHECK(transport.nextTick() == 16 * 96 + 4 * 96 + 2);
    const auto relocated = transport.positionAt(*loop.mapper.midiSampleTime(secondRun.front()));
    REQUIRE(relocated.has_value());
    CHECK_THAT(*relocated, WithinAbs(16.0 * 96.0, 0.01));
    CHECK_THAT(*transport.bpm(), WithinAbs(120.0, 1.0));
}
