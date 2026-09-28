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
              std::uint32_t framesPerBar, double bpm, double sampleRate)
{
    const TimeSignature timeSignature{4, 4};
    for (int bar = 0; bar < 4; ++bar)
    {
        auto samples = blockWithImpulse(framesPerBar, 0);
        streamSample += framesPerBar;
        detector.processBlock(samples, streamSample, framesPerBar, ppqAtBlockStart, bpm,
                              timeSignature, sampleRate, true);
        ppqAtBlockStart += 4.0;
    }
}

} // namespace

TEST_CASE("SidechainSyncDetector locks on Visona Sync impulses", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto framesPerBar = static_cast<std::uint32_t>(sampleRate * 2.0);

    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    std::uint64_t streamSample = 0;
    double ppqAtBlockStart = 0.0;
    feedBars(detector, streamSample, ppqAtBlockStart, framesPerBar, bpm, sampleRate);

    CHECK(detector.state() == SidechainSyncState::locked);
    CHECK(detector.offsetFrames() == 0.0);
}

TEST_CASE("SidechainSyncDetector rejects dense sidechain peaks", "[sync]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr double bpm = 120.0;
    const auto framesPerBar = static_cast<std::uint32_t>(sampleRate * 2.0);
    const TimeSignature timeSignature{4, 4};

    SidechainSyncDetector detector;
    detector.setSidechainEnabled(true);

    std::uint64_t streamSample = 0;
    double ppqAtBlockStart = 0.0;
    for (int bar = 0; bar < 4; ++bar)
    {
        std::vector<float> samples(framesPerBar, 0.0f);
        for (std::uint32_t index = 0; index < framesPerBar; index += 512)
            samples[index] = syncImpulseAmplitude;
        streamSample += framesPerBar;
        detector.processBlock(samples, streamSample, framesPerBar, ppqAtBlockStart, bpm,
                              timeSignature, sampleRate, true);
        ppqAtBlockStart += 4.0;
    }

    CHECK(detector.state() == SidechainSyncState::invalid);
}
