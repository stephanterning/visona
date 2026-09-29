#include <visona/AutoGain.h>
#include <visona/BarPeaks.h>
#include <visona/LaneMapping.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using visona::AutoGain;
using visona::BarPeak;
using visona::BarPeakMeter;
using visona::DisplayGain;
using visona::RecentBarPeaks;
using visona::TransportSpan;

namespace
{

/** Adds `count` bars of `seconds` each, peaking at `peak`. Returns whether the gain changed. */
bool addBars(AutoGain& autoGain, int count, float peak, double seconds = 2.0)
{
    bool changed = false;
    for (int bar = 0; bar < count; ++bar)
        changed = autoGain.addBar({peak, seconds}) || changed;
    return changed;
}

/** A musical span of one tick per frame from frame 0 and tick 0. */
TransportSpan tickPerFrame()
{
    TransportSpan span;
    span.kind = TransportSpan::Kind::musical;
    span.start = 0.0;
    span.end = 1.0e6;
    span.startTick = 0.0;
    span.endTick = 1.0e6;
    return span;
}

} // namespace

TEST_CASE("Auto gain picks the highest 3 dB step that keeps a peak in the lane", "[autogain]")
{
    CHECK(AutoGain::gainFor(1.0f) == 0);
    CHECK(AutoGain::gainFor(2.0f) == 0);
    CHECK(AutoGain::gainFor(0.5f) == 6);  // -6.0 dBFS lands at -0.0 dB
    CHECK(AutoGain::gainFor(0.2f) == 12); // -14.0 dBFS lands at -2.0 dB
    CHECK(AutoGain::gainFor(0.1f) == 18); // -20 dBFS lands at -2 dB
    CHECK(AutoGain::gainFor(0.0f) == DisplayGain::maxDb);

    for (const auto peak : {0.9f, 0.6f, 0.45f, 0.3f, 0.2f, 0.15f})
    {
        CAPTURE(peak);
        const auto gainDb = AutoGain::gainFor(peak);
        CHECK(gainDb % AutoGain::stepDb == 0);
        CHECK(peak * DisplayGain::toLinear(gainDb) <= 1.0f);
        CHECK(peak * DisplayGain::toLinear(gainDb + AutoGain::stepDb) > 1.0f);
    }
}

TEST_CASE("Auto gain waits the hold time before it zooms in, then zooms in at once", "[autogain]")
{
    AutoGain autoGain;
    CHECK(autoGain.holdSeconds() == 30.0);
    autoGain.reset(0);

    CHECK_FALSE(addBars(autoGain, 14, 0.2f));
    CHECK(autoGain.gainDb() == 0);
    CHECK(addBars(autoGain, 1, 0.2f));
    CHECK(autoGain.gainDb() == 12);

    autoGain.setHoldSeconds(10.0);
    CHECK_FALSE(addBars(autoGain, 4, 0.1f));
    CHECK(addBars(autoGain, 1, 0.1f));
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("Auto gain zooms out at the end of the first bar that goes past the lane", "[autogain]")
{
    AutoGain autoGain;
    autoGain.reset(18);
    CHECK_FALSE(addBars(autoGain, 3, 0.1f));
    CHECK(autoGain.gainDb() == 18);

    CHECK(addBars(autoGain, 1, 0.5f));
    CHECK(autoGain.gainDb() == 6);

    // The loud bar keeps the gain down until it is older than the hold time.
    CHECK_FALSE(addBars(autoGain, 14, 0.1f));
    CHECK(autoGain.gainDb() == 6);
    CHECK(addBars(autoGain, 1, 0.1f));
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("Auto gain zooms out as soon as the bar in progress goes past the lane", "[autogain]")
{
    AutoGain autoGain;
    autoGain.reset(18);
    CHECK_FALSE(autoGain.notePeak(0.1f));
    CHECK(autoGain.gainDb() == 18);
    CHECK(autoGain.notePeak(0.5f));
    CHECK(autoGain.gainDb() == 6);
    CHECK_FALSE(autoGain.notePeak(0.5f));

    // The bar it was in still keeps the gain down for the hold time once it ends.
    CHECK_FALSE(addBars(autoGain, 1, 0.5f));
    CHECK_FALSE(addBars(autoGain, 14, 0.1f));
    CHECK(autoGain.gainDb() == 6);
    CHECK(addBars(autoGain, 1, 0.1f));
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("A loud bar within the hold time keeps auto gain from zooming in", "[autogain]")
{
    AutoGain autoGain;
    autoGain.reset(0);
    CHECK_FALSE(addBars(autoGain, 1, 0.9f));
    CHECK_FALSE(addBars(autoGain, 14, 0.1f));
    CHECK(autoGain.gainDb() == 0);
    CHECK(addBars(autoGain, 1, 0.1f));
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("Bars at or below -50 dBFS do not count for auto gain", "[autogain]")
{
    AutoGain autoGain;
    autoGain.reset(6);
    CHECK_FALSE(addBars(autoGain, 40, 0.001f)); // -60 dBFS
    CHECK(autoGain.gainDb() == 6);

    CHECK(addBars(autoGain, 1, 0.0056f)); // -45 dBFS
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("Auto gain starts from any gain and keeps it in range", "[autogain]")
{
    AutoGain autoGain;
    autoGain.reset(40);
    CHECK(autoGain.gainDb() == DisplayGain::maxDb);
    autoGain.reset(10);
    CHECK(autoGain.gainDb() == 10);
    CHECK(addBars(autoGain, 1, 0.5f));
    CHECK(autoGain.gainDb() == 6);
}

TEST_CASE("Auto gain follows the bars that ended since it last looked", "[autogain]")
{
    RecentBarPeaks recent;
    const auto end = [&recent](float peak)
    {
        ++recent.count;
        recent.bars[(recent.count - 1) % RecentBarPeaks::capacity] = {peak, 2.0};
    };

    AutoGain autoGain;
    autoGain.reset(18);
    end(0.9f);
    // The first look only notes where the bars are.
    CHECK_FALSE(autoGain.follow(recent));
    CHECK(autoGain.gainDb() == 18);

    end(0.1f);
    CHECK_FALSE(autoGain.follow(recent));
    end(0.1f);
    end(0.5f);
    CHECK(autoGain.follow(recent));
    CHECK(autoGain.gainDb() == 6);

    // The bar in progress counts before it ends.
    recent.currentPeak = 0.9f;
    CHECK(autoGain.follow(recent));
    CHECK(autoGain.gainDb() == 0);
    CHECK_FALSE(autoGain.follow(recent));
    end(0.9f);
    recent.currentPeak = 0.1f;
    CHECK_FALSE(autoGain.follow(recent));
    CHECK(autoGain.gainDb() == 0);

    // More bars than the snapshot holds: only those still held are added, so the loud bar that
    // was missed does not keep the gain down.
    for (std::size_t bar = 0; bar < RecentBarPeaks::capacity + 4; ++bar)
        end(bar == 0 ? 1.0f : 0.1f);
    CHECK(autoGain.follow(recent));
    CHECK(autoGain.gainDb() == 18);
}

TEST_CASE("BarPeakMeter ends a bar when the next one starts", "[autogain][bars]")
{
    // 4 frames per bar at 4 ticks per bar; 2 frames per second.
    BarPeakMeter meter;
    std::vector<float> left{0.1f, 0.2f, 0.1f, 0.1f, 0.5f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f};
    std::vector<float> right{0.0f, 0.0f, -0.4f, 0.0f, 0.0f, 0.0f, -0.6f, 0.0f, 0.0f, 0.0f};
    const std::array<const float*, 2> channels{left.data(), right.data()};

    meter.process(channels, 3, 0, tickPerFrame(), 4.0, 2.0);
    CHECK(meter.recent().count == 0);
    CHECK(meter.recent().currentPeak == 0.4f);

    const std::array<const float*, 2> rest{left.data() + 3, right.data() + 3};
    meter.process(rest, 7, 3, tickPerFrame(), 4.0, 2.0);
    const auto& recent = meter.recent();
    REQUIRE(recent.count == 2);
    CHECK(recent.bar(1).peak == 0.4f);
    CHECK(recent.bar(1).seconds == 2.0);
    CHECK(recent.bar(2).peak == 0.6f);
    CHECK(recent.bar(2).seconds == 2.0);
    CHECK(recent.currentPeak == 0.3f);
}

TEST_CASE("BarPeakMeter drops the bar in progress when the sweep starts over", "[autogain][bars]")
{
    BarPeakMeter meter;
    std::vector<float> loud(4, 0.9f);
    std::vector<float> quiet(8, 0.1f);
    const std::array<const float*, 1> loudChannel{loud.data()};
    const std::array<const float*, 1> quietChannel{quiet.data()};

    meter.process(loudChannel, 2, 0, tickPerFrame(), 4.0, 2.0);
    CHECK(meter.recent().currentPeak == 0.9f);
    meter.restart();
    CHECK(meter.recent().currentPeak == 0.0f);
    meter.process(quietChannel, 5, 0, tickPerFrame(), 4.0, 2.0);
    REQUIRE(meter.recent().count == 1);
    CHECK(meter.recent().bar(1).peak == 0.1f);
}
