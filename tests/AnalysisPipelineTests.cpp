#include "support/AllocationCounter.h"
#include "support/Signals.h"

#include <visona/AnalysisPipeline.h>
#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/MidiClockEvent.h>
#include <visona/SweepAnalyzer.h>
#include <visona/SweepWindow.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <thread>
#include <utility>
#include <vector>

using visona::AnalysisPipeline;
using visona::AudioInputWriter;
using visona::AudioRingBuffer;
using visona::SweepAnalyzer;
using visona::SweepCell;
using visona::TransportSpan;
using visona::TransportState;
using visona::test::AllocationCounter;
using visona::test::sine;

namespace
{

/** Pushes `channels` to `writer` in blocks of `blockSize` frames. */
void pushAll(AudioInputWriter& writer, const std::vector<std::vector<float>>& channels,
             std::size_t blockSize)
{
    const auto numFrames = channels.front().size();
    std::vector<const float*> pointers(channels.size());
    for (std::size_t start = 0; start < numFrames; start += blockSize)
    {
        const auto frames = std::min(blockSize, numFrames - start);
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        writer.write(pointers, static_cast<std::uint32_t>(frames), 0);
    }
}

std::vector<std::vector<float>> stereoTestSignal(double sampleRate, std::size_t numFrames)
{
    return {sine(220.0, sampleRate, 0.5f, numFrames), sine(3'000.0, sampleRate, 0.25f, numFrames)};
}

/** The span the pipeline gives a free-running sweep that starts at frame `origin`. */
TransportSpan freeSpan(double origin, double sampleRate, double bpm)
{
    constexpr double spanFrames = 0x1p42;
    TransportSpan span;
    span.kind = TransportSpan::Kind::musical;
    span.start = origin;
    span.end = origin + spanFrames;
    span.endTick = spanFrames / (60.0 * sampleRate / (24.0 * bpm));
    return span;
}

} // namespace

TEST_CASE("AnalysisPipeline publishes an empty snapshot while it follows no stream", "[analysis]")
{
    AnalysisPipeline pipeline(2, 256);
    CHECK(pipeline.numChannels() == 2);
    CHECK(pipeline.poll() == 0);

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    const auto& snapshot = snapshots.readBuffer();
    CHECK_FALSE(snapshot.hasStream);
    CHECK(snapshot.sweep.numChannels() == 2);
    CHECK(snapshot.sweep.numBins() == 256);
    CHECK(snapshot.sweep.pass() == 0);

    // Nothing changes, so nothing more is published.
    CHECK(pipeline.poll() == 0);
    CHECK_FALSE(snapshots.fetch());
}

TEST_CASE("AnalysisPipeline analyzes a stream into the same sweep as the analyzer", "[analysis]")
{
    constexpr double sampleRate = 48'000.0;
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    AnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    // Before any Start, the sweep runs free at 120 BPM in the 1-bar window.
    const auto input = stereoTestSignal(sampleRate, 30'000);
    SweepAnalyzer reference(2);
    reference.startMusical(96.0);
    const std::array<const float*, 2> pointers{input[0].data(), input[1].data()};
    reference.processMusical(0, pointers, input[0].size(),
                             freeSpan(0.0, sampleRate, visona::defaultFreeBpm));

    auto& snapshots = pipeline.snapshots();
    std::size_t framesAnalyzed = 0;
    for (std::size_t part = 0; part < 3; ++part)
    {
        const std::vector<std::vector<float>> slice{
            {input[0].begin() + static_cast<std::ptrdiff_t>(part * 10'000),
             input[0].begin() + static_cast<std::ptrdiff_t>((part + 1) * 10'000)},
            {input[1].begin() + static_cast<std::ptrdiff_t>(part * 10'000),
             input[1].begin() + static_cast<std::ptrdiff_t>((part + 1) * 10'000)}};
        pushAll(writer, slice, 441);
        framesAnalyzed += pipeline.poll();
        REQUIRE(snapshots.fetch());
    }
    CHECK(framesAnalyzed == 30'000);

    const auto& snapshot = snapshots.readBuffer();
    CHECK(snapshot.hasStream);
    CHECK(snapshot.sampleRate == sampleRate);
    CHECK(snapshot.musical);
    CHECK(snapshot.transportState == TransportState::freeRunning);
    CHECK(snapshot.bpm == visona::defaultFreeBpm);
    CHECK(snapshot.windowTicks == 96.0);
    CHECK(snapshot.nextSampleIndex == 30'000);
    CHECK(snapshot.overruns == 0);
    // The same content; the pipeline cleared its buffer once more, when the stream started.
    const auto& expected = reference.buffer();
    CHECK(snapshot.sweep.head() == expected.head());
    CHECK(snapshot.sweep.pass() == expected.pass());
    CHECK(std::ranges::equal(snapshot.sweep.passes(), expected.passes()));
    for (std::size_t channel = 0; channel < 2; ++channel)
        CHECK(std::ranges::equal(snapshot.sweep.channel(channel), expected.channel(channel)));

    CHECK(pipeline.takePeak(0) == std::ranges::max(input[0]));
    CHECK(pipeline.takePeak(1) == std::ranges::max(input[1]));
    CHECK(pipeline.takePeak(0) == 0.0f);
}

TEST_CASE("AnalysisPipeline shows a dropped block as a gap and counts it", "[analysis]")
{
    constexpr double sampleRate = 1'000.0; // a 1-bar window at 120 BPM is 2 000 frames
    AudioRingBuffer ring(1, 256, 64);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    AnalysisPipeline pipeline(1, 200); // 10 frames per bin
    pipeline.setStream(&ring, sampleRate);

    const std::vector<std::vector<float>> block{std::vector<float>(200, 0.5f)};
    pushAll(writer, block, 200);
    pushAll(writer, block, 200); // dropped: only 56 frames are free
    pipeline.poll();
    pushAll(writer, block, 200);
    pipeline.poll();

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    const auto& snapshot = snapshots.readBuffer();
    CHECK(snapshot.overruns == 1);
    CHECK(snapshot.droppedFrames == 200);
    CHECK(snapshot.nextSampleIndex == 600);
    const auto cells = snapshot.sweep.channel(0);
    for (std::size_t bin = 0; bin < 60; ++bin)
    {
        CAPTURE(bin);
        CHECK(cells[bin].isEmpty() == (bin >= 20 && bin < 40));
    }
}

TEST_CASE("AnalysisPipeline starts a new, cleared sweep for every stream", "[analysis]")
{
    AudioRingBuffer first(2, 4'096, 64);
    AudioRingBuffer second(2, 4'096, 64);
    AudioInputWriter firstWriter(first);
    firstWriter.route(0, 0);

    AnalysisPipeline pipeline(2, 512);
    auto& snapshots = pipeline.snapshots();
    pipeline.setStream(&first, 44'100.0);
    pushAll(firstWriter, stereoTestSignal(44'100.0, 1'000), 100);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    const auto firstStream = snapshots.readBuffer().streamId;
    const auto firstGeneration = snapshots.readBuffer().sweep.generation();
    CHECK(snapshots.readBuffer().sweep.pass() == 1);

    pipeline.setStream(&second, 96'000.0);
    CHECK(pipeline.poll() == 0);
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().streamId == firstStream + 1);
    CHECK(snapshots.readBuffer().sweep.generation() > firstGeneration);
    CHECK(snapshots.readBuffer().sweep.pass() == 0);
    CHECK(snapshots.readBuffer().sampleRate == 96'000.0);
    CHECK_FALSE(snapshots.readBuffer().musical);

    pipeline.setStream(nullptr, 0.0);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    CHECK_FALSE(snapshots.readBuffer().hasStream);
    CHECK_FALSE(snapshots.readBuffer().musical);
}

TEST_CASE("AnalysisPipeline splits bands only while asked to", "[analysis]")
{
    constexpr double sampleRate = 48'000.0;
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);
    AnalysisPipeline pipeline(2, 1'024);
    pipeline.setStream(&ring, sampleRate);
    auto& snapshots = pipeline.snapshots();

    pushAll(writer, stereoTestSignal(sampleRate, 4'800), 480);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().bandDelayFrames == std::array<double, 3>{});
    const auto head = snapshots.readBuffer().sweep.head();
    CHECK(snapshots.readBuffer().sweep.bands(0)[head].isEmpty());

    pipeline.setBandSplitting(true);
    pushAll(writer, stereoTestSignal(sampleRate, 4'800), 480);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    const auto& snapshot = snapshots.readBuffer();
    CHECK(snapshot.bandDelayFrames[0] > 100.0);
    // 220 Hz on the left is mostly mid, 3 kHz on the right mostly high.
    const auto left = snapshot.sweep.bands(0)[snapshot.sweep.head()];
    const auto right = snapshot.sweep.bands(1)[snapshot.sweep.head()];
    CHECK(left.mid > left.high);
    CHECK(right.high > right.low);

    pipeline.setBandSplitting(false);
    pushAll(writer, stereoTestSignal(sampleRate, 4'800), 480);
    pipeline.poll();
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().bandDelayFrames == std::array<double, 3>{});
    CHECK(snapshots.readBuffer().sweep.bands(0)[snapshots.readBuffer().sweep.head()].isEmpty());
}

TEST_CASE("AnalysisPipeline does not allocate while it runs", "[analysis][realtime]")
{
    AudioRingBuffer ring(2, 96'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);
    AnalysisPipeline pipeline(2);
    const auto input = stereoTestSignal(96'000.0, 480);
    const std::array<const float*, 2> pointers{input[0].data(), input[1].data()};
    STATIC_REQUIRE(noexcept(pipeline.setStream(&ring, 96'000.0)));
    STATIC_REQUIRE(noexcept(pipeline.poll()));

    const AllocationCounter allocations;
    pipeline.setStream(&ring, 96'000.0);
    for (int block = 0; block < 500; ++block)
    {
        writer.write(pointers, 480, 0);
        if (block % 3 == 0)
            pipeline.poll();
    }
    pipeline.poll();
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
}

namespace
{

constexpr std::size_t stressBins = 256;
constexpr std::uint64_t stressWindowFrames = 4'800; // 2 s at 2 400 Hz
constexpr std::uint64_t stressPassCodes = 7;

/** A signal whose value names the pass and bin each frame belongs to, so the consumer can check
    that every cell of a snapshot agrees with the bin's pass stamp. */
float stressSample(std::uint64_t position)
{
    const auto pass = position / stressWindowFrames + 1;
    const auto bin = position % stressWindowFrames * stressBins / stressWindowFrames;
    return static_cast<float>((pass % stressPassCodes) * stressBins + bin + 1);
}

} // namespace

TEST_CASE("AnalysisPipeline hands consistent snapshots across threads", "[analysis][stress]")
{
    constexpr double sampleRate = 2'400.0;
    constexpr std::uint64_t totalFrames = 600'000;
    constexpr std::uint32_t blockSize = 32;

    AudioRingBuffer ring(1, 2'048, 128);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    AnalysisPipeline pipeline(1, stressBins);
    pipeline.setStream(&ring, sampleRate);

    // The ring may drop blocks if the analysis thread falls behind; the snapshots must stay
    // consistent either way.
    std::atomic<bool> producerDone{false};
    std::uint64_t endOfLastDeliveredBlock = 0;
    std::thread producer(
        [&]
        {
            std::array<float, blockSize> block{};
            for (std::uint64_t position = 0; position < totalFrames; position += blockSize)
            {
                for (std::uint32_t frame = 0; frame < blockSize; ++frame)
                    block[frame] = stressSample(position + frame);
                const float* const channel = block.data();
                if (writer.write(std::span(&channel, 1), blockSize, 0))
                    endOfLastDeliveredBlock = position + blockSize;
                std::this_thread::yield();
            }
            producerDone.store(true, std::memory_order_release);
        });

    std::atomic<bool> analysisDone{false};
    std::size_t analysisAllocations = 0;
    std::thread analysis(
        [&]
        {
            const AllocationCounter allocations;
            while (!producerDone.load(std::memory_order_acquire))
            {
                pipeline.poll();
                std::this_thread::yield();
            }
            pipeline.poll();
            analysisAllocations = allocations.count();
            analysisDone.store(true, std::memory_order_release);
        });

    std::size_t fetches = 0;
    std::size_t inconsistentCells = 0;
    std::size_t inconsistentPasses = 0;
    std::size_t backwards = 0;
    std::uint64_t lastPosition = 0;
    auto& snapshots = pipeline.snapshots();
    const auto check = [&]
    {
        ++fetches;
        const auto& sweep = snapshots.readBuffer().sweep;
        if (sweep.pass() == 0)
            return;
        const auto position = sweep.pass() * stressBins + sweep.head();
        if (position < lastPosition)
            ++backwards;
        lastPosition = position;

        const auto cells = sweep.channel(0);
        const auto passes = sweep.passes();
        for (std::size_t bin = 0; bin < stressBins; ++bin)
        {
            const auto expectedPass = bin <= sweep.head() ? sweep.pass() : sweep.pass() - 1;
            if (passes[bin] != expectedPass)
                ++inconsistentPasses;
            // A cell also reaches where the line to its neighbours crosses its edges, but always
            // holds its own frames' value.
            const auto expected =
                static_cast<float>((passes[bin] % stressPassCodes) * stressBins + bin + 1);
            if (!cells[bin].isEmpty() && (cells[bin].min > expected || cells[bin].max < expected))
                ++inconsistentCells;
        }
    };

    while (!analysisDone.load(std::memory_order_acquire))
        if (snapshots.fetch())
            check();
    if (snapshots.fetch())
        check();
    producer.join();
    analysis.join();

    CHECK(fetches > 0);
    CHECK(inconsistentCells == 0);
    CHECK(inconsistentPasses == 0);
    CHECK(backwards == 0);
    CHECK(analysisAllocations == 0);
    CHECK(snapshots.readBuffer().nextSampleIndex == endOfLastDeliveredBlock);
}

namespace
{

using visona::MidiClockEvent;
using visona::MidiClockQueue;
using visona::SweepSnapshot;

/**
    A synthetic session: stereo audio blocks stamped from one host clock, MIDI Clock messages
    stamped from the same clock, and the pipeline polled as the analysis thread would. A MIDI
    message reaches the queue once the audio block it falls in has been delivered.
*/
class MidiSession
{
public:
    static constexpr double hostStartNs = 9.0e12;

    MidiSession(double sampleRate, double blockJitterNs = 20'000.0, std::uint32_t blockFrames = 256)
        : sampleRate_(sampleRate)
        , blockFrames_(blockFrames)
        , ring_(2, static_cast<std::size_t>(sampleRate), 8'192)
        , writer_(ring_)
        , queue_(8'192)
        , pipeline_(2, 4'096)
        , block_(blockFrames)
        , jitter_(-blockJitterNs, blockJitterNs)
    {
        writer_.route(0, 0);
        writer_.route(1, 1);
        pipeline_.setStream(&ring_, sampleRate);
        pipeline_.setMidiQueue(&queue_);
    }

    AnalysisPipeline& pipeline()
    {
        return pipeline_;
    }

    AudioRingBuffer& ring()
    {
        return ring_;
    }

    [[nodiscard]] double framesPerTick(double bpm) const
    {
        return 60.0 * sampleRate_ / (bpm * 24.0);
    }

    [[nodiscard]] std::uint64_t frame() const
    {
        return frame_;
    }

    void midi(MidiClockEvent::Type type, double frame, std::uint16_t spp = 0)
    {
        events_.push_back({type, spp, hostTime(frame)});
    }

    /** Schedules `count` clocks, `interval` frames apart from `first` on. Returns the frame after
        the last one. */
    double clocks(double first, int count, double interval)
    {
        for (int clock = 0; clock < count; ++clock)
            midi(MidiClockEvent::Type::Clock, first + clock * interval);
        return first + count * interval;
    }

    /** Delivers audio up to `end`, whose samples come from `signal(frame)`, polling every few
        blocks. */
    template <typename Signal>
    void run(std::uint64_t end, Signal signal)
    {
        std::array<const float*, 2> channels{block_.data(), block_.data()};
        while (frame_ < end)
        {
            for (std::uint32_t i = 0; i < blockFrames_; ++i)
                block_[i] = signal(frame_ + i);
            const auto stamp =
                static_cast<double>(hostTime(static_cast<double>(frame_))) + jitter_(random_);
            writer_.write(channels, blockFrames_, static_cast<std::uint64_t>(stamp));
            frame_ += blockFrames_;
            while (nextEvent_ < events_.size() &&
                   events_[nextEvent_].hostTimeNs <= hostTime(static_cast<double>(frame_)))
                queue_.tryPush(events_[nextEvent_++]);
            if (++blocks_ % 4 == 0)
                pipeline_.poll();
        }
        pipeline_.poll();
    }

    void run(std::uint64_t end)
    {
        run(end, [](std::uint64_t) { return 0.25f; });
    }

    const SweepSnapshot& snapshot()
    {
        pipeline_.snapshots().fetch();
        return pipeline_.snapshots().readBuffer();
    }

private:
    [[nodiscard]] std::uint64_t hostTime(double frame) const
    {
        return static_cast<std::uint64_t>(std::llround(hostStartNs + frame / sampleRate_ * 1.0e9));
    }

    double sampleRate_;
    std::uint32_t blockFrames_;
    AudioRingBuffer ring_;
    AudioInputWriter writer_;
    MidiClockQueue queue_;
    AnalysisPipeline pipeline_;
    std::vector<float> block_;
    std::vector<MidiClockEvent> events_;
    std::size_t nextEvent_ = 0;
    std::uint64_t frame_ = 0;
    std::size_t blocks_ = 0;
    std::mt19937 random_{17};
    std::uniform_real_distribution<double> jitter_;
};

} // namespace

TEST_CASE("A click on every beat lands on the grid, end to end", "[analysis][midi]")
{
    const auto bpm = GENERATE(120.0, 126.0, 174.0);
    // Window index, and the bins between beats in that window: a beat per quarter bar.
    const auto [window, beatSpacing] = GENERATE(std::pair<std::size_t, std::size_t>{0, 4'096},
                                                std::pair<std::size_t, std::size_t>{1, 2'048},
                                                std::pair<std::size_t, std::size_t>{2, 1'024},
                                                std::pair<std::size_t, std::size_t>{3, 512},
                                                std::pair<std::size_t, std::size_t>{4, 256});
    CAPTURE(bpm, window);

    MidiSession session(96'000.0);
    session.pipeline().setWindow(window);
    const auto interval = session.framesPerTick(bpm);
    constexpr double firstClock = 50'000.0;
    const auto bars = visona::sweepWindowBars[window] + 0.5;
    session.midi(MidiClockEvent::Type::Start, firstClock - 1'000.0);
    session.clocks(firstClock, static_cast<int>((bars + 1.0) * 96.0), interval);

    // The click of each beat is on the first frame at or after the beat.
    const auto beatFrames = 24.0 * interval;
    const auto isBeatFrame = [&](std::uint64_t frame)
    {
        const auto beat = std::round((static_cast<double>(frame) - firstClock) / beatFrames);
        return beat >= 0.0 &&
               static_cast<double>(frame) == std::ceil(firstClock + beat * beatFrames);
    };
    session.run(static_cast<std::uint64_t>(firstClock + bars * 96.0 * interval),
                [&](std::uint64_t frame) { return isBeatFrame(frame) ? 1.0f : 0.0f; });

    const auto& snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(snapshot.musical);
    CHECK(std::abs(snapshot.bpm - bpm) < 0.05);
    CHECK(snapshot.window == window);
    CHECK(snapshot.windowTicks == visona::sweepWindowBars[window] * 96.0);

    const auto cells = snapshot.sweep.channel(0);
    std::size_t clickBins = 0;
    for (std::size_t bin = 0; bin < cells.size(); ++bin)
    {
        if (cells[bin].isEmpty() || cells[bin].max < 0.5f)
            continue;
        ++clickBins;
        const auto fromBeat = bin % beatSpacing;
        CAPTURE(bin);
        CHECK((fromBeat <= 1 || fromBeat >= beatSpacing - 1));
    }
    CHECK(clickBins >= 4096 / beatSpacing);
}

TEST_CASE("The sweep runs free until the first Start and follows the transport after it",
          "[analysis][midi]")
{
    MidiSession session(48'000.0);
    session.run(20'000);
    auto snapshot = session.snapshot();
    CHECK(snapshot.musical);
    CHECK(snapshot.transportState == TransportState::freeRunning);
    CHECK(snapshot.bpm == visona::defaultFreeBpm);
    CHECK(snapshot.windowStartTick == 0.0);
    const auto freeGeneration = snapshot.sweep.generation();

    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 25'000.0);
    session.clocks(26'000.0, 48, interval);
    session.run(26'000 + static_cast<std::uint64_t>(40 * interval));
    snapshot = session.snapshot();
    CHECK(snapshot.musical);
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(snapshot.sweep.generation() > freeGeneration);
    CHECK(snapshot.sweep.pass() == 1);
    // Start, and the 41 clocks up to where the audio has got.
    CHECK(snapshot.midiEvents == 42);
}

TEST_CASE("The free tempo is rounded to 0.1 BPM and kept between 40 and 300 BPM", "[free]")
{
    CHECK(visona::clampFreeBpm(126.04) == 126.0);
    CHECK(visona::clampFreeBpm(126.06) == 126.1);
    CHECK(visona::clampFreeBpm(12.0) == visona::minFreeBpm);
    CHECK(visona::clampFreeBpm(420.0) == visona::maxFreeBpm);
    CHECK(visona::clampFreeBpm(std::nan("")) == visona::defaultFreeBpm);
}

TEST_CASE("The free-running sweep is bars at the free tempo", "[analysis][free]")
{
    // 90 BPM at 48 kHz: 32 000 frames per beat and 128 000 per bar.
    MidiSession session(48'000.0);
    session.pipeline().setFreeTempo(90.0);
    constexpr std::uint64_t beatFrames = 32'000;
    session.run(beatFrames * 6,
                [](std::uint64_t frame) { return frame % beatFrames == 0 ? 1.0f : 0.0f; });

    const auto& snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::freeRunning);
    CHECK(snapshot.musical);
    CHECK(snapshot.bpm == 90.0);
    CHECK(snapshot.windowTicks == 96.0);
    CHECK(snapshot.windowStartTick == 96.0); // the second bar
    CHECK(snapshot.sweep.pass() == 2);

    const auto cells = snapshot.sweep.channel(0);
    const auto binsPerBeat = cells.size() / 4;
    std::size_t clickBins = 0;
    for (std::size_t bin = 0; bin < cells.size(); ++bin)
    {
        if (cells[bin].isEmpty() || cells[bin].max < 0.5f)
            continue;
        ++clickBins;
        const auto fromBeat = bin % binsPerBeat;
        CAPTURE(bin);
        CHECK((fromBeat <= 1 || fromBeat >= binsPerBeat - 1));
    }
    CHECK(clickBins >= 4);
}

TEST_CASE("A new tempo or window starts the free-running sweep over from bar 1", "[analysis][free]")
{
    MidiSession session(48'000.0);
    session.run(300'000);
    auto snapshot = session.snapshot();
    REQUIRE(snapshot.windowStartTick > 0.0);
    auto generation = snapshot.sweep.generation();

    session.pipeline().setFreeTempo(140.0);
    session.run(320'000);
    snapshot = session.snapshot();
    CHECK(snapshot.bpm == 140.0);
    CHECK(snapshot.sweep.generation() > generation);
    CHECK(snapshot.sweep.pass() == 1);
    CHECK(snapshot.windowStartTick == 0.0);
    generation = snapshot.sweep.generation();

    session.run(600'000);
    REQUIRE(session.snapshot().windowStartTick > 0.0);
    session.pipeline().setWindow(0);
    session.run(610'000);
    snapshot = session.snapshot();
    CHECK(snapshot.sweep.generation() > generation);
    CHECK(snapshot.windowTicks == 24.0);
    CHECK(snapshot.windowStartTick == 0.0);

    // Out of range, the tempo is clamped.
    session.pipeline().setFreeTempo(1'000.0);
    session.run(620'000);
    CHECK(session.snapshot().bpm == visona::maxFreeBpm);
}

TEST_CASE("runFree leaves Stopped for a new free-running sweep", "[analysis][free]")
{
    MidiSession session(48'000.0);
    const auto interval = session.framesPerTick(126.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    const auto afterClocks = session.clocks(2'000.0, 150, interval);
    session.midi(MidiClockEvent::Type::Stop, afterClocks);
    session.run(static_cast<std::uint64_t>(afterClocks + 4'800.0));
    auto snapshot = session.snapshot();
    REQUIRE(snapshot.transportState == TransportState::stopped);
    CHECK(std::abs(snapshot.bpm - 126.0) < 0.05);
    const auto generation = snapshot.sweep.generation();

    // The app hands over the last MIDI tempo along with the request.
    session.pipeline().setFreeTempo(126.0);
    session.pipeline().runFree();
    session.run(static_cast<std::uint64_t>(afterClocks + 60'000.0),
                [](std::uint64_t) { return 0.5f; });
    snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::freeRunning);
    CHECK(snapshot.bpm == 126.0);
    CHECK(snapshot.sweep.generation() > generation);
    CHECK(snapshot.sweep.pass() == 1);
    CHECK(snapshot.windowStartTick == 0.0);
    CHECK_FALSE(snapshot.sweep.channel(0)[snapshot.sweep.head()].isEmpty());

    // A Continue follows MIDI Clock again, from where the song stopped.
    const auto resume = afterClocks + 70'000.0;
    session.midi(MidiClockEvent::Type::Continue, resume);
    session.clocks(resume + 500.0, 12, interval);
    session.run(static_cast<std::uint64_t>(resume + 500.0 + 10 * interval));
    snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(snapshot.nextTick == 150 + 11);
    CHECK(snapshot.windowStartTick == 96.0);
}

TEST_CASE("Audio after the latest tick waits in the ring", "[analysis][midi]")
{
    MidiSession session(48'000.0);
    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    const auto afterClocks = session.clocks(2'000.0, 24, interval);
    const auto lastClock = afterClocks - interval;
    // A tenth of a second without clocks: less than the clock-loss timeout.
    session.run(static_cast<std::uint64_t>(lastClock + 4'800.0));

    const auto& snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(static_cast<double>(snapshot.nextSampleIndex) <= std::ceil(lastClock));
    CHECK(session.ring().newestFrameEnd() - snapshot.nextSampleIndex >= 4'000);
}

TEST_CASE("Stop freezes the sweep while the ring keeps draining", "[analysis][midi]")
{
    MidiSession session(96'000.0);
    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    const auto afterClocks = session.clocks(2'000.0, 150, interval);
    session.midi(MidiClockEvent::Type::Stop, afterClocks - 0.5 * interval);
    session.run(static_cast<std::uint64_t>(afterClocks + 9'600.0));

    const auto frozen = session.snapshot().sweep;
    CHECK(session.snapshot().transportState == TransportState::stopped);
    session.run(static_cast<std::uint64_t>(afterClocks + 96'000.0),
                [](std::uint64_t) { return -0.75f; });
    const auto& snapshot = session.snapshot();
    CHECK(snapshot.sweep == frozen);
    CHECK(session.ring().newestFrameEnd() - snapshot.nextSampleIndex < 2'048);
}

TEST_CASE("Clock loss freezes the sweep, and the returning clock carries on", "[analysis][midi]")
{
    MidiSession session(48'000.0);
    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    const auto afterClocks = session.clocks(2'000.0, 60, interval);
    const auto lastClock = afterClocks - interval;

    session.run(static_cast<std::uint64_t>(lastClock + 0.45 * 48'000.0));
    CHECK(session.snapshot().transportState == TransportState::running);
    session.run(static_cast<std::uint64_t>(lastClock + 0.6 * 48'000.0));
    CHECK(session.snapshot().transportState == TransportState::clockLost);
    const auto frozen = session.snapshot().sweep;
    session.run(static_cast<std::uint64_t>(lastClock + 48'000.0));
    CHECK(session.snapshot().sweep == frozen);
    CHECK(session.ring().newestFrameEnd() - session.snapshot().nextSampleIndex < 2'048);

    const auto resume = lastClock + 48'000.0 + 1'000.0;
    session.clocks(resume, 30, interval);
    session.run(static_cast<std::uint64_t>(resume + 25 * interval));
    const auto& snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(snapshot.nextTick == 60 + 26); // the 26 clocks up to where the audio has got
    CHECK(snapshot.sweep.head() > frozen.head());
    CHECK(snapshot.sweep.generation() == frozen.generation());
}

TEST_CASE("A new window clears the musical sweep", "[analysis][midi]")
{
    MidiSession session(48'000.0);
    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    session.clocks(2'000.0, 200, interval);
    session.run(static_cast<std::uint64_t>(2'000.0 + 100 * interval));
    const auto generation = session.snapshot().sweep.generation();
    CHECK(session.snapshot().windowTicks == 96.0);

    session.pipeline().setWindow(4);
    session.run(static_cast<std::uint64_t>(2'000.0 + 150 * interval));
    const auto& snapshot = session.snapshot();
    CHECK(snapshot.sweep.generation() == generation + 1);
    CHECK(snapshot.windowTicks == 384.0);
    CHECK(snapshot.window == 4);
}

TEST_CASE("Stop, SPP and Continue relocate the head without clearing", "[analysis][midi]")
{
    MidiSession session(48'000.0);
    session.pipeline().setWindow(4); // 4 bars: bars 17 to 20 are one window
    const auto interval = session.framesPerTick(120.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    auto time = session.clocks(2'000.0, 144, interval); // a bar and a half
    session.midi(MidiClockEvent::Type::Stop, time);
    session.run(static_cast<std::uint64_t>(time + 4'800.0));
    const auto before = session.snapshot().sweep;
    REQUIRE(before.pass() == 1);

    // Bar 18: 17 bars of 16 sixteenths.
    time += 48'000.0;
    session.midi(MidiClockEvent::Type::SongPositionPointer, time, 17 * 16);
    session.midi(MidiClockEvent::Type::Continue, time + 100.0);
    session.clocks(time + 1'000.0, 12, interval);
    session.run(static_cast<std::uint64_t>(time + 1'000.0 + 10 * interval));

    const auto& snapshot = session.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    CHECK(snapshot.sweep.generation() == before.generation());
    CHECK(snapshot.sweep.pass() == 2);
    CHECK(snapshot.windowStartTick == 16 * 96.0);
    // A quarter of the way into the window, plus the ten ticks since.
    CHECK(snapshot.sweep.head() >= 1'024);
    CHECK(snapshot.sweep.head() <= 1'024 + 10 * 4'096 / 384 + 1);
    // The first bar and a half of the old pass is still there.
    CHECK(snapshot.sweep.passes()[500] == 1);
    CHECK_FALSE(snapshot.sweep.channel(0)[500].isEmpty());
}

TEST_CASE("AnalysisPipeline does not allocate while following MIDI Clock",
          "[analysis][midi][realtime]")
{
    MidiSession session(96'000.0);
    const auto interval = session.framesPerTick(126.0);
    session.midi(MidiClockEvent::Type::Start, 1'000.0);
    const auto afterClocks = session.clocks(2'000.0, 400, interval);
    session.midi(MidiClockEvent::Type::Stop, afterClocks);
    session.run(4'096); // let the first allocations of Catch2 and the session happen

    const AllocationCounter allocations;
    session.run(static_cast<std::uint64_t>(afterClocks + 96'000.0));
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(session.snapshot().transportState == TransportState::stopped);
}

TEST_CASE("AnalysisPipeline follows MIDI Clock across threads", "[analysis][midi][stress]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr std::uint32_t blockFrames = 64;
    constexpr std::uint64_t totalFrames = 1'200'000;
    constexpr double interval = 1'000.0; // 120 BPM at 48 kHz
    constexpr double hostStart = 3.0e12;
    const auto hostTime = [](double frame)
    { return static_cast<std::uint64_t>(hostStart + frame / sampleRate * 1.0e9); };

    AudioRingBuffer ring(1, 48'000, 2'048);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    MidiClockQueue queue(1'024);
    AnalysisPipeline pipeline(1, 512);
    pipeline.setStream(&ring, sampleRate);
    pipeline.setMidiQueue(&queue);

    std::atomic<std::uint64_t> audioFrames{0};
    std::atomic<bool> audioDone{false};
    std::atomic<bool> started{false};
    std::thread audio(
        [&]
        {
            // The audio waits for the Start, or it might all be over before the MIDI thread runs.
            while (!started.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::array<float, blockFrames> block{};
            block.fill(0.5f);
            const float* const channel = block.data();
            for (std::uint64_t frame = 0; frame < totalFrames; frame += blockFrames)
            {
                writer.write(std::span(&channel, 1), blockFrames,
                             hostTime(static_cast<double>(frame)));
                audioFrames.store(frame + blockFrames, std::memory_order_release);
                std::this_thread::yield();
            }
            audioDone.store(true, std::memory_order_release);
        });

    // Start, then clocks, with a Stop, an SPP and a Continue in the middle.
    std::atomic<bool> midiDone{false};
    std::thread midi(
        [&]
        {
            const auto push = [&](MidiClockEvent event)
            {
                while (!queue.tryPush(event))
                    std::this_thread::yield();
            };
            double next = 10'000.0;
            push({MidiClockEvent::Type::Start, 0, hostTime(next - 100.0)});
            started.store(true, std::memory_order_release);
            for (int clock = 0; next < static_cast<double>(totalFrames); ++clock, next += interval)
            {
                while (static_cast<double>(audioFrames.load(std::memory_order_acquire)) < next)
                    std::this_thread::yield();
                if (clock == 400)
                    push({MidiClockEvent::Type::Stop, 0, hostTime(next)});
                if (clock == 450)
                {
                    push({MidiClockEvent::Type::SongPositionPointer, 64, hostTime(next - 50.0)});
                    push({MidiClockEvent::Type::Continue, 0, hostTime(next - 10.0)});
                }
                if (clock < 400 || clock >= 450)
                    push({MidiClockEvent::Type::Clock, 0, hostTime(next)});
            }
            midiDone.store(true, std::memory_order_release);
        });

    std::atomic<bool> analysisDone{false};
    std::size_t analysisAllocations = 0;
    std::thread analysis(
        [&]
        {
            pipeline.poll(); // the first poll may touch lazily initialized library state
            const AllocationCounter allocations;
            while (!audioDone.load(std::memory_order_acquire) ||
                   !midiDone.load(std::memory_order_acquire))
            {
                pipeline.poll();
                std::this_thread::yield();
            }
            pipeline.poll();
            analysisAllocations = allocations.count();
            analysisDone.store(true, std::memory_order_release);
        });

    std::size_t fetches = 0;
    std::size_t backwards = 0;
    std::uint64_t generation = 0;
    std::uint64_t position = 0;
    auto& snapshots = pipeline.snapshots();
    while (!analysisDone.load(std::memory_order_acquire))
    {
        if (!snapshots.fetch())
        {
            std::this_thread::yield();
            continue;
        }
        ++fetches;
        const auto& sweep = snapshots.readBuffer().sweep;
        const auto now = sweep.pass() * sweep.numBins() + sweep.head();
        if (sweep.generation() == generation && now < position)
            ++backwards;
        generation = sweep.generation();
        position = now;
    }
    audio.join();
    midi.join();
    analysis.join();
    snapshots.fetch();

    const auto& last = snapshots.readBuffer();
    CAPTURE(last.midiEvents, static_cast<int>(last.transportState), last.nextSampleIndex,
            last.sweep.generation(), last.overruns, last.nextTick);
    CHECK(fetches > 0);
    CHECK(backwards == 0);
    CHECK(analysisAllocations == 0);
    CHECK(snapshots.readBuffer().musical);
    CHECK(snapshots.readBuffer().transportState == TransportState::running);
}
