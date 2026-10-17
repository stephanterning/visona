#include "support/AllocationCounter.h"

#include <visona/AnalysisPipeline.h>
#include <visona/AudioInputWriter.h>
#include <visona/HostAnalysisPipeline.h>
#include <visona/StereoAnalyzer.h>
#include <visona/StereoProjection.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>
#include <vector>

namespace
{
using namespace visona;
struct Signal
{
    std::vector<float> left, right;
};
Signal signal(int rate, int kind)
{
    const auto count = static_cast<std::size_t>(rate * 3 / 5);
    Signal s{std::vector<float>(count), std::vector<float>(count)};
    std::uint32_t noise = 0x78212;
    auto random = [&noise]()
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return static_cast<float>(static_cast<double>(noise) / 4294967295.0 - 0.5);
    };
    for (std::size_t i = 0; i < count; ++i)
    {
        const double phase = 2 * std::numbers::pi * 1000 * static_cast<double>(i) / rate;
        s.left[i] = s.right[i] = static_cast<float>(0.7 * std::sin(phase));
        switch (kind)
        {
        case 1:
            s.right[i] = -s.left[i];
            break;
        case 2:
            s.right[i] = 0;
            break;
        case 3:
            s.left[i] = 0;
            break;
        case 4:
            s.right[i] = static_cast<float>(0.7 * std::cos(phase));
            break;
        case 5:
            s.left[i] = random();
            s.right[i] = random();
            break;
        case 6:
            s.right[i] *= 0.5f;
            break;
        case 7:
            s.left[i] = s.right[i] = 0;
            break;
        case 8:
            s.left[i] *= 1e-7f;
            s.right[i] *= 1e-7f;
            break;
        default:
            break;
        }
    }
    return s;
}
const StereoSnapshot& latest(StereoAnalyzer& analyzer)
{
    REQUIRE(analyzer.snapshots().fetch());
    return analyzer.snapshots().readBuffer();
}
void verifyOracle(const StereoSnapshot& out, const Signal& s, int rate)
{
    const auto period = static_cast<std::size_t>(rate / 5);
    double ll = 0, rr = 0, lr = 0;
    const auto end = static_cast<std::size_t>(out.frameEnd);
    for (auto i = end - period; i < end; ++i)
    {
        const double l = static_cast<double>(s.left[i]), r = static_cast<double>(s.right[i]);
        ll += l * l;
        rr += r * r;
        lr += l * r;
    }
    if (out.correlationValid())
        REQUIRE(out.correlation == Catch::Approx(lr / std::sqrt(ll * rr)).margin(2e-4));
    if (out.signalValid())
    {
        REQUIRE(out.balance == Catch::Approx((rr - ll) / (rr + ll)).margin(2e-4));
        REQUIRE(out.count == static_cast<std::size_t>(rate / 10));
        for (std::size_t i = 0; i < out.count; ++i)
        {
            REQUIRE(out.samples[i].left == s.left[end - out.count + i]);
            REQUIRE(out.samples[i].right == s.right[end - out.count + i]);
        }
    }
    else
        REQUIRE(out.count == 0);
}
} // namespace

TEST_CASE("LSP stereo matches independent window measurements at every supported test rate",
          "[stereo]")
{
    constexpr std::array<std::size_t, 8> blocks{1, 7, 64, 127, 256, 511, 1024, 4096};
    for (const int rate : {8000, 44100, 48000, 96000, 192000, 384000})
        for (int kind = 0; kind < 9; ++kind)
        {
            CAPTURE(rate, kind);
            const auto samples = signal(rate, kind);
            StereoAnalyzer whole, split;
            whole.reset(rate);
            split.reset(rate);
            whole.setEnabled(true);
            split.setEnabled(true);
            whole.process(samples.left, samples.right, 0);
            std::size_t block = 0;
            for (std::size_t offset = 0; offset < samples.left.size(); ++block)
            {
                const auto n =
                    std::min(blocks[block % blocks.size()], samples.left.size() - offset);
                split.process(std::span(samples.left).subspan(offset, n),
                              std::span(samples.right).subspan(offset, n), offset);
                offset += n;
            }
            const auto& a = latest(whole);
            const auto& b = latest(split);
            REQUIRE(a.frameEnd == b.frameEnd);
            REQUIRE(a.validity == b.validity);
            REQUIRE(a.correlation == b.correlation);
            REQUIRE(a.balance == b.balance);
            REQUIRE(a.correlationValid() == (kind < 7 && kind != 2 && kind != 3));
            REQUIRE(a.signalValid() == (kind < 7));
            verifyOracle(a, samples, rate);
            verifyOracle(b, samples, rate);
        }
}

TEST_CASE("Stereo validity has per-channel hysteresis and recovers cleanly after loud audio",
          "[stereo]")
{
    StereoAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    std::vector<float> left(28800), right(28800);
    std::uint64_t frame = 0;
    auto feed = [&](float l, float r) -> const StereoSnapshot&
    {
        std::fill(left.begin(), left.end(), l);
        std::fill(right.begin(), right.end(), r);
        analyzer.process(left, right, frame);
        frame += left.size();
        return latest(analyzer);
    };
    const auto amplitude = [](float db) { return std::pow(10.0f, db / 20); };
    REQUIRE(feed(amplitude(-93), amplitude(-93)).validity == StereoValidity::silent);
    REQUIRE(feed(amplitude(-89), amplitude(-89)).correlationValid());
    REQUIRE(feed(amplitude(-93), amplitude(-93)).correlationValid());
    REQUIRE(feed(amplitude(-97), amplitude(-97)).validity == StereoValidity::silent);
    REQUIRE(feed(amplitude(-93), amplitude(-93)).validity == StereoValidity::silent);
    REQUIRE(feed(0.5f, 0).validity == StereoValidity::leftOnly);
    REQUIRE(feed(0, 0.5f).validity == StereoValidity::rightOnly);
    REQUIRE(feed(1000, 500).signalValid());
    const auto& quiet = feed(amplitude(-89), -amplitude(-89));
    REQUIRE(quiet.correlationValid());
    REQUIRE(quiet.correlation == Catch::Approx(-1).margin(2e-4));
    REQUIRE(quiet.balance == Catch::Approx(0).margin(2e-4));
    const auto& silent = feed(0, 0);
    REQUIRE(silent.validity == StereoValidity::silent);
    REQUIRE(silent.count == 0);
    REQUIRE(silent.correlation == 0.0f);
    REQUIRE(silent.balance == 0.0f);
}

TEST_CASE("Stereo requires a fresh complete window after gaps, invalid input and enable changes",
          "[stereo]")
{
    StereoAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    std::vector<float> audio(9600, 0.25f);
    analyzer.process(audio, audio, 0);
    REQUIRE(latest(analyzer).correlationValid());
    analyzer.process(std::span(audio).first(64), std::span(audio).first(64), 50000);
    REQUIRE(latest(analyzer).validity == StereoValidity::waiting);
    analyzer.process(audio, audio, 50064);
    REQUIRE(latest(analyzer).correlationValid());
    analyzer.setEnabled(false);
    analyzer.setEnabled(true); // Both changes can happen between analysis polls.
    analyzer.process(std::span(audio).first(64), std::span(audio).first(64), 59664);
    REQUIRE(latest(analyzer).validity == StereoValidity::waiting);
    analyzer.reset(96000);
    REQUIRE(latest(analyzer).validity == StereoValidity::waiting);
    analyzer.reset(48000);
    for (const float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::max()})
    {
        audio[17] = bad;
        analyzer.reset(48000);
        analyzer.process(audio, audio, 0);
        const auto& invalid = latest(analyzer);
        REQUIRE(invalid.validity == StereoValidity::invalidInput);
        REQUIRE(invalid.count == 0);
        audio[17] = 0.25f;
        analyzer.process(audio, audio, audio.size());
        REQUIRE(latest(analyzer).correlationValid());
    }
    analyzer.process(audio, std::span(audio).first(3), 19200);
    REQUIRE(latest(analyzer).validity == StereoValidity::invalidInput);
    for (const double rate : {0.0, 7999.0, 384001.0, std::numeric_limits<double>::quiet_NaN()})
    {
        analyzer.reset(rate);
        analyzer.process(audio, audio, 0);
        REQUIRE(latest(analyzer).validity == StereoValidity::waiting);
    }
}

TEST_CASE("Stereo processing, stream reset and snapshot publication allocate no storage",
          "[stereo][realtime]")
{
    StereoAnalyzer analyzer;
    const auto samples = signal(384000, 5);
    std::size_t allocations = 0;
    {
        test::AllocationCounter counter;
        analyzer.reset(384000);
        analyzer.setEnabled(true);
        analyzer.process(samples.left, samples.right, 0);
        analyzer.snapshots().fetch();
        analyzer.reset(44100);
        analyzer.process(samples.left, samples.right, 0);
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}

TEST_CASE("Stereo projections preserve channel orientation, scale and exact anti-phase", "[stereo]")
{
    for (int l = -50; l <= 50; ++l)
        for (int r = -50; r <= 50; ++r)
        {
            const float left = static_cast<float>(l) / 50, right = static_cast<float>(r) / 50;
            const auto cartesian = stereoLissajous(left, right);
            REQUIRE(cartesian.y - cartesian.x == Catch::Approx(left).margin(1e-6));
            REQUIRE(cartesian.y + cartesian.x == Catch::Approx(right).margin(1e-6));
            const auto polar = signalizerPolar(left, right);
            REQUIRE(std::hypot(polar.x, polar.y) ==
                    Catch::Approx(std::max(std::abs(left), std::abs(right))).margin(1e-6));
            REQUIRE(polar.y >= -1e-6f);
            const auto swap = signalizerPolar(right, left);
            REQUIRE(swap.x == Catch::Approx(-polar.x).margin(1e-6));
            REQUIRE(swap.y == Catch::Approx(polar.y).margin(1e-6));
        }
    REQUIRE(signalizerPolar(1, 1).y == Catch::Approx(1));
    REQUIRE(signalizerPolar(1, 0).x < 0.0f);
    REQUIRE(signalizerPolar(0, 1).x > 0.0f);
    REQUIRE(signalizerPolar(1, -1).y == 0.0f);
    REQUIRE(signalizerPolar(1, -1).x == -1.0f);
}

TEST_CASE("Stereo snapshots stay coherent with concurrent UI reads and enable changes",
          "[stereo][stress]")
{
    StereoAnalyzer analyzer;
    analyzer.reset(48000);
    analyzer.setEnabled(true);
    const std::vector<float> left(256, 0.5f), right(256, -0.5f);
    std::atomic<bool> done{false};
    std::thread producer(
        [&]
        {
            for (std::uint64_t frame = 0; frame < 256000; frame += 256)
                analyzer.process(left, right, frame);
            done.store(true, std::memory_order_release);
        });
    bool coherent = true;
    std::size_t reads = 0;
    do
    {
        if (analyzer.snapshots().fetch())
        {
            const auto& snapshot = analyzer.snapshots().readBuffer();
            if (snapshot.correlationValid())
                coherent = coherent && snapshot.correlation < -0.999f && snapshot.count == 4800 &&
                           snapshot.samples[0].left == 0.5f && snapshot.samples[0].right == -0.5f;
            if (++reads == 3)
            {
                analyzer.setEnabled(false);
                analyzer.setEnabled(true);
            }
        }
        std::this_thread::yield();
    } while (!done.load(std::memory_order_acquire));
    producer.join();
    REQUIRE(coherent);
    REQUIRE(reads > 0);
}

TEST_CASE("Both pipelines deliver stereo without transport playback and exclude the sync input",
          "[stereo][analysis]")
{
    const std::vector<float> left(512, 0.5f), right(512, -0.25f), sync(512, 1.0f);
    const std::array<const float*, 3> input{left.data(), right.data(), sync.data()};
    AudioRingBuffer ring(3, 96000, 1024);
    AudioInputWriter writer(ring);
    writer.route(0, 0);
    writer.route(1, 1);
    writer.route(2, 2);
    SECTION("Standalone pause and resume")
    {
        AnalysisPipeline pipeline(2, 256);
        pipeline.setStream(&ring, 48000);
        pipeline.stereo().setEnabled(true);
        for (int i = 0; i < 64; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE(latest(pipeline.stereo()).correlation == Catch::Approx(-1).margin(2e-4));
        pipeline.setPaused(true);
        for (int i = 0; i < 32; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE_FALSE(pipeline.stereo().snapshots().fetch());
        pipeline.setPaused(false);
        writer.write(input, 512, 0);
        pipeline.poll();
        REQUIRE(latest(pipeline.stereo()).validity == StereoValidity::waiting);
        for (int i = 0; i < 32; ++i)
        {
            writer.write(input, 512, 0);
            pipeline.poll();
        }
        REQUIRE(latest(pipeline.stereo()).correlationValid());
    }
    SECTION("Plugin while host transport is stopped")
    {
        AudioRingBuffer hostRing(2, 96000, 1024);
        AudioInputWriter hostWriter(hostRing);
        hostWriter.route(0, 0);
        hostWriter.route(1, 1);
        HostAnalysisPipeline pipeline(2, 256);
        pipeline.setStream(&hostRing, 48000);
        pipeline.stereo().setEnabled(true);
        HostTransport::Playhead playhead;
        playhead.valid = true;
        playhead.isPlaying = false;
        playhead.bpm = 120;
        for (int i = 0; i < 64; ++i)
        {
            const auto first = hostWriter.nextSampleIndex();
            hostWriter.write(input, 512, 0);
            pipeline.pushPlayhead(first, playhead);
            pipeline.poll();
        }
        const auto& out = latest(pipeline.stereo());
        REQUIRE(out.correlation == Catch::Approx(-1).margin(2e-4));
        REQUIRE(out.balance == Catch::Approx(-0.6f).margin(2e-4));
    }
}
