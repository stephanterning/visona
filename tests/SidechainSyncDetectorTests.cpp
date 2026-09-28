#include <visona/BarImpulseScheduler.h>
#include <visona/SidechainSyncDetector.h>

#include <catch2/catch_test_macros.hpp>

using visona::SidechainSyncDetector;
using visona::SidechainSyncState;
using visona::TimeSignature;
using visona::syncImpulseAmplitude;

namespace
{

std::vector<float> blockWithImpulse(std::uint32_t numFrames, std::uint32_t offset)
{
    std::vector<float> samples(numFrames, 0.0f);
    if (offset < numFrames)
        samples[offset] = syncImpulseAmplitude;
    return samples;
}

void feedBars(SidechainSyncDetector& detector, std::uint64_t& streamSample, double& ppqAtBlockStart,
              std::uint32_t framesPerBar, double bpm, double sampleRate, int barCount = 4)
{
    const TimeSignature timeSignature{4, 4};
    for (int bar = 0; bar < barCount; ++bar)
    {
        auto samples = blockWithImpulse(framesPerBar, 0);
        streamSample += framesPerBar;
        detector.processBlock(samples, streamSample, framesPerBar, ppqAtBlockStart, bpm,
                              timeSignature, sampleRate, true);
        ppqAtBlockStart += 4.0;
    }
}

} // namespace

TEST_CASE("SidechainSyncDetector locks on the first Visona Sync impulse", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto framesPerBar = static_cast<std::uint32_t>(sampleRate * 2.0);

    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    std::uint64_t streamSample = 0;
    double ppqAtBlockStart = 0.0;
    feedBars(detector, streamSample, ppqAtBlockStart, framesPerBar, bpm, sampleRate, 1);

    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == 0.0);
}

TEST_CASE("SidechainSyncDetector locks with PDC-like sidechain delay", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto framesPerBar = static_cast<std::uint32_t>(sampleRate * 2.0);
    const TimeSignature timeSignature{4, 4};
    constexpr std::uint32_t sidechainDelay = 512;

    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    std::uint64_t streamSample = 0;
    double ppqAtBlockStart = 0.0;
    auto samples = blockWithImpulse(framesPerBar, sidechainDelay);
    streamSample += framesPerBar;
    detector.processBlock(samples, streamSample, framesPerBar, ppqAtBlockStart, bpm,
                          timeSignature, sampleRate, true);

    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == sidechainDelay);
}

TEST_CASE("SidechainSyncDetector stays locked when a bar is missed", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto framesPerBar = static_cast<std::uint32_t>(sampleRate * 2.0);
    const TimeSignature timeSignature{4, 4};

    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    std::uint64_t streamSample = 0;
    double ppqAtBlockStart = 0.0;
    feedBars(detector, streamSample, ppqAtBlockStart, framesPerBar, bpm, sampleRate, 1);
    REQUIRE(detector.state() == SidechainSyncState::locked);
    const auto lockedOffset = detector.offsetFrames();

    for (int bar = 0; bar < 4; ++bar)
    {
        const std::vector<float> silence(framesPerBar, 0.0f);
        streamSample += framesPerBar;
        detector.processBlock(silence, streamSample, framesPerBar, ppqAtBlockStart, bpm,
                              timeSignature, sampleRate, true);
        ppqAtBlockStart += 4.0;
    }

    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == lockedOffset);
}
