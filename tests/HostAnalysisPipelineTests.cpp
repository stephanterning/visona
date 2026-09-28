#include <visona/AudioInputWriter.h>
#include <visona/AudioRingBuffer.h>
#include <visona/HostAnalysisPipeline.h>
#include <visona/HostTransport.h>

#include <catch2/catch_test_macros.hpp>

using visona::AudioInputWriter;
using visona::AudioRingBuffer;
using visona::HostAnalysisPipeline;
using visona::HostTransport;
using visona::TransportState;

namespace
{

HostTransport::Playhead playingAt(double ppq, double bpm = 120.0)
{
    HostTransport::Playhead playhead;
    playhead.valid = true;
    playhead.isPlaying = true;
    playhead.ppqPosition = ppq;
    playhead.bpm = bpm;
    return playhead;
}

void pushSilence(AudioInputWriter& writer, std::size_t numFrames, std::size_t blockSize = 512)
{
    const std::vector<float> silence(blockSize, 0.0f);
    const std::array<const float*, 2> pointers{silence.data(), silence.data()};
    for (std::size_t pushed = 0; pushed < numFrames; pushed += blockSize)
    {
        const auto frames =
            static_cast<std::uint32_t>(std::min(blockSize, numFrames - pushed));
        writer.write(pointers, frames, 0);
    }
}

} // namespace

TEST_CASE("HostAnalysisPipeline caps work per poll", "[analysis][host]")
{
    constexpr double sampleRate = 48'000.0;
    AudioRingBuffer ring(2, 48'000, 1'024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);
    pipeline.setPlayhead(0.0, playingAt(0.0));

    pushSilence(writer, 10'000);
    const auto analyzed = pipeline.poll();
    CHECK(analyzed == 4'096);
    CHECK(ring.peek().has_value());
}

TEST_CASE("HostAnalysisPipeline keeps analyzing when the host playhead drops out briefly",
          "[analysis][host]")
{
    constexpr double sampleRate = 48'000.0;
    AudioRingBuffer ring(2, 8'192, 512);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);

    HostAnalysisPipeline pipeline(2);
    pipeline.setStream(&ring, sampleRate);

    pushSilence(writer, 2'048);
    pipeline.setPlayhead(2'048.0, playingAt(1.0));
    CHECK(pipeline.poll() > 0);

    pushSilence(writer, 2'048);
    HostTransport::Playhead invalid;
    invalid.valid = false;
    pipeline.setPlayhead(4'096.0, invalid);
    CHECK(pipeline.poll() > 0);

    auto& snapshots = pipeline.snapshots();
    REQUIRE(snapshots.fetch());
    CHECK(snapshots.readBuffer().transportState == TransportState::running);
}
