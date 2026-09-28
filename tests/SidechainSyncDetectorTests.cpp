#include <visona/BarImpulseScheduler.h>
#include <visona/SidechainSyncDetector.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using visona::SidechainSyncDetector;
using visona::SidechainSyncState;
using visona::TimeSignature;

namespace
{

// At 120 BPM and 48 kHz a quarter note is 24000 frames, and a 4/4 bar 96000.
constexpr double sampleRate = 48'000.0;
constexpr double bpm = 120.0;
constexpr double framesPerQuarter = 24'000.0;
constexpr std::uint32_t blockSize = 512;
constexpr TimeSignature fourFour{4, 4};

std::vector<float> impulseAt(std::uint32_t index, float peak = visona::syncImpulseAmplitude)
{
    std::vector<float> samples(blockSize, 0.0f);
    samples[index] = peak;
    return samples;
}

/** Feeds the block that holds the impulse of the bar at `barPpq`, arriving `lag` frames late. */
void impulseAfterBar(SidechainSyncDetector& detector, double barPpq, std::int64_t lag,
                     float peak = visona::syncImpulseAmplitude)
{
    const auto barStart = static_cast<std::int64_t>(barPpq * framesPerQuarter);
    const auto arrival = barStart + lag;
    const auto blockStart = arrival - arrival % blockSize;
    const auto ppqAtBlockStart = static_cast<double>(blockStart) / framesPerQuarter;
    detector.processBlock(impulseAt(static_cast<std::uint32_t>(arrival - blockStart), peak),
                          ppqAtBlockStart, bpm, fourFour, sampleRate, true);
}

} // namespace

TEST_CASE("SidechainSyncDetector measures no offset for an impulse on the bar line", "[sync]")
{
    SidechainSyncDetector detector;
    CHECK(detector.state() == SidechainSyncState::off);
    detector.setSidechainEnabled(true);
    CHECK(detector.state() == SidechainSyncState::waiting);

    impulseAfterBar(detector, 8.0, 0);
    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == 0.0);
    CHECK(detector.impulsePeak() == visona::syncImpulseAmplitude);
}

TEST_CASE("SidechainSyncDetector measures a lag longer than a block", "[sync]")
{
    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    impulseAfterBar(detector, 8.0, 4'736);
    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == 4'736.0);
}

TEST_CASE("SidechainSyncDetector measures an impulse before the bar line as negative", "[sync]")
{
    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    impulseAfterBar(detector, 8.0, -300);
    CHECK(detector.offsetFrames() == -300.0);
}

TEST_CASE("SidechainSyncDetector accepts an impulse summed to 0 dBFS", "[sync]")
{
    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    impulseAfterBar(detector, 4.0, 144, 1.0f);
    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == 144.0);
}

TEST_CASE("SidechainSyncDetector ignores a sidechain below -20 dBFS", "[sync]")
{
    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    impulseAfterBar(detector, 4.0, 144, 0.05f);
    CHECK(detector.state() == SidechainSyncState::waiting);
}

TEST_CASE("SidechainSyncDetector keeps its offset through a stray peak and follows a new one",
          "[sync]")
{
    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    impulseAfterBar(detector, 4.0, 144);
    impulseAfterBar(detector, 8.0, 145);
    CHECK(detector.offsetFrames() == 144.0);

    impulseAfterBar(detector, 12.0, 3'000);
    CHECK(detector.offsetFrames() == 144.0);
    impulseAfterBar(detector, 16.0, 144);

    // A plugin with 1000 frames of latency was added before the scope.
    impulseAfterBar(detector, 20.0, 1'144);
    CHECK(detector.offsetFrames() == 144.0);
    impulseAfterBar(detector, 24.0, 1'144);
    CHECK(detector.offsetFrames() == 1'144.0);
}

TEST_CASE("SidechainSyncDetector listens only while the host plays and the input is on", "[sync]")
{
    SidechainSyncDetector detector;
    detector.processBlock(impulseAt(0), 8.0, bpm, fourFour, sampleRate, true);
    CHECK(detector.state() == SidechainSyncState::off);

    detector.setSidechainEnabled(true);
    detector.processBlock(impulseAt(0), 8.0, bpm, fourFour, sampleRate, false);
    CHECK(detector.state() == SidechainSyncState::waiting);

    impulseAfterBar(detector, 8.0, 144);
    REQUIRE(detector.state() == SidechainSyncState::locked);
    detector.setSidechainEnabled(false);
    CHECK(detector.state() == SidechainSyncState::off);
    CHECK(detector.offsetFrames() == 0.0);
}
