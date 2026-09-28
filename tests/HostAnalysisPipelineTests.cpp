#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/HostAnalysisPipeline.h>
#include <visona/HostTransport.h>
#include <visona/SweepWindow.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using visona::AudioInputWriter;
using visona::AudioRingBuffer;
using visona::HostAnalysisPipeline;
using visona::HostTransport;
using visona::SweepSnapshot;
using visona::TransportState;

namespace
{

constexpr double sampleRate = 48'000.0;

/** The 1-bar sweep of the host simulation: a beat is 1024 bins, about 23 frames each. */
constexpr std::size_t binsPerBar = 4'096;
constexpr std::int64_t binsPerBeat = 1'024;

HostTransport::Playhead playingAt(double ppq, double bpm = 120.0)
{
    HostTransport::Playhead playhead;
    playhead.valid = true;
    playhead.isPlaying = true;
    playhead.ppqPosition = ppq;
    playhead.bpm = bpm;
    return playhead;
}

/** Writes `numBlocks` blocks of silence, each with the host playhead at its first frame. */
void playSilence(AudioInputWriter& writer, HostAnalysisPipeline& pipeline, std::size_t numBlocks,
                 std::uint32_t blockSize = 512, double bpm = 120.0)
{
    const std::vector<float> silence(blockSize, 0.0f);
    const std::array<const float*, 2> inputs{silence.data(), silence.data()};
    const auto framesPerQuarter = sampleRate * 60.0 / bpm;
    for (std::size_t block = 0; block < numBlocks; ++block)
    {
        const auto blockStart = writer.nextSampleIndex();
        writer.write(inputs, blockSize, 0);
        pipeline.pushPlayhead(blockStart,
                              playingAt(static_cast<double>(blockStart) / framesPerQuarter, bpm));
    }
}

/**
    A host like Ableton Live, feeding one Visona instance block by block.

    Every plugin gets the same playhead, at the first frame of each block, but the audio reaching
    Visona has passed plugins with `latency` frames of latency. The main input has a click on every
    beat of the timeline.
*/
class LiveHost
{
public:
    LiveHost(double bpm, std::uint32_t blockSize, std::uint64_t latency)
        : bpm_(bpm)
        , blockSize_(blockSize)
        , latency_(latency)
        , ring_(2, 96'000, 2'048)
        , writer_(ring_)
        , pipeline_(2, binsPerBar)
    {
        writer_.route(0, 0);
        writer_.route(1, 1);
        pipeline_.setStream(&ring_, sampleRate);
        pipeline_.setWindow(visona::defaultSweepWindow);
    }

    /** Stops for a few blocks, then plays `bars` bars from the song start. */
    void play(double bars)
    {
        constexpr std::uint64_t stoppedBlocks = 8;
        const auto playStart = stoppedBlocks * blockSize_;
        const auto end = playStart + static_cast<std::uint64_t>(bars * 4.0 * framesPerQuarter());

        std::vector<float> main(blockSize_);
        const std::array<const float*, 2> inputs{main.data(), main.data()};

        for (std::uint64_t blockStart = 0; blockStart < end; blockStart += blockSize_)
        {
            HostTransport::Playhead playhead;
            playhead.valid = true;
            playhead.isPlaying = blockStart >= playStart;
            playhead.bpm = bpm_;
            playhead.ppqPosition =
                playhead.isPlaying
                    ? static_cast<double>(blockStart - playStart) / framesPerQuarter()
                    : 0.0;

            for (std::uint32_t index = 0; index < blockSize_; ++index)
            {
                const auto frame = blockStart + index;
                main[index] =
                    frame >= latency_ && isBeatFrame(frame - latency_, playStart) ? 1.0f : 0.0f;
            }

            const auto blockStartSample = writer_.nextSampleIndex();
            writer_.write(inputs, blockSize_, 0);
            pipeline_.pushPlayhead(blockStartSample, playhead);
            while (pipeline_.poll() > 0)
            {
            }
        }
    }

    [[nodiscard]] const SweepSnapshot& snapshot()
    {
        static_cast<void>(pipeline_.snapshots().fetch());
        return pipeline_.snapshots().readBuffer();
    }

    [[nodiscard]] double framesPerQuarter() const noexcept
    {
        return sampleRate * 60.0 / bpm_;
    }

private:
    /** The click of each beat is on the first timeline frame at or after the beat. */
    [[nodiscard]] bool isBeatFrame(std::uint64_t frame, std::uint64_t playStart) const noexcept
    {
        if (frame < playStart)
            return false;
        const auto fromStart = static_cast<double>(frame - playStart);
        const auto beat = std::round(fromStart / framesPerQuarter());
        return fromStart == std::ceil(beat * framesPerQuarter());
    }

    double bpm_;
    std::uint32_t blockSize_;
    std::uint64_t latency_;
    AudioRingBuffer ring_;
    AudioInputWriter writer_;
    HostAnalysisPipeline pipeline_;
};

/** The bins of the 1-bar sweep that hold a click, as distances from the nearest beat line. */
std::vector<std::int64_t> clickDistancesFromBeats(const SweepSnapshot& snapshot)
{
    std::vector<std::int64_t> distances;
    const auto cells = snapshot.sweep.channel(0);
    for (std::size_t bin = 0; bin < cells.size(); ++bin)
    {
        if (cells[bin].isEmpty() || cells[bin].max < 0.5f)
            continue;
        auto distance = static_cast<std::int64_t>(bin) % binsPerBeat;
        if (distance > binsPerBeat / 2)
            distance -= binsPerBeat;
        distances.push_back(distance);
    }
    return distances;
}

} // namespace

TEST_CASE("HostAnalysisPipeline caps work per poll", "[analysis][host]")
{
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    playSilence(writer, pipeline, 20);
    CHECK(pipeline.poll() == 4'096);
    CHECK(ring.peek().has_value());
}

TEST_CASE("HostAnalysisPipeline analyzes a block once the playhead after it arrives",
          "[analysis][host]")
{
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    playSilence(writer, pipeline, 1);
    CHECK(pipeline.poll() == 0);
    playSilence(writer, pipeline, 1);
    CHECK(pipeline.poll() == 512);
    CHECK(ring.peek().has_value());
}

TEST_CASE("HostAnalysisPipeline keeps draining the ring with a sidechain offset",
          "[analysis][host]")
{
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);
    pipeline.setAnalysisOffset(2'048.0);

    playSilence(writer, pipeline, 16);
    while (pipeline.poll() > 0)
    {
    }
    CHECK(!ring.peek().has_value());
}

TEST_CASE("HostAnalysisPipeline keeps analyzing when the host playhead drops out briefly",
          "[analysis][host]")
{
    AudioRingBuffer ring(2, 8'192, 512);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    playSilence(writer, pipeline, 4);
    CHECK(pipeline.poll() > 0);

    const std::vector<float> silence(512, 0.0f);
    const std::array<const float*, 2> inputs{silence.data(), silence.data()};
    for (int block = 0; block < 4; ++block)
    {
        const auto blockStart = writer.nextSampleIndex();
        writer.write(inputs, 512, 0);
        pipeline.pushPlayhead(blockStart, HostTransport::Playhead{});
    }
    CHECK(pipeline.poll() > 0);

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().transportState == TransportState::running);
}

TEST_CASE("A click on every beat lands on the grid with host playheads", "[analysis][host]")
{
    const auto bpm = GENERATE(120.0, 123.0);
    const auto blockSize = GENERATE(64u, 512u, 1'000u, 1'024u);
    CAPTURE(bpm, blockSize);

    LiveHost host(bpm, blockSize, 0);
    host.play(3.0);

    const auto& snapshot = host.snapshot();
    CHECK(snapshot.transportState == TransportState::running);
    const auto distances = clickDistancesFromBeats(snapshot);
    CHECK(distances.size() >= 4);
    for (const auto distance : distances)
    {
        CAPTURE(distance);
        CHECK(std::abs(distance) <= 1);
    }
}

TEST_CASE("Audio after plugins with latency lands late without sidechain sync", "[analysis][host]")
{
    constexpr double bpm = 123.0;
    constexpr std::uint64_t latency = 4'736;
    LiveHost host(bpm, 512, latency);
    host.play(3.0);

    // 4736 frames at 123 BPM are 0.2023 beats: 207 bins.
    const auto expected = static_cast<std::int64_t>(std::round(
        static_cast<double>(latency) / host.framesPerQuarter() * static_cast<double>(binsPerBeat)));
    const auto distances = clickDistancesFromBeats(host.snapshot());
    CHECK(distances.size() >= 4);
    for (const auto distance : distances)
    {
        CAPTURE(distance);
        CHECK(std::abs(distance - expected) <= 1);
    }
}
