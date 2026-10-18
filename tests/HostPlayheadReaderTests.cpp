#include <visona/AudioDeviceWatchdog.h>
#include <visona/AudioInputWriter.h>
#include <visona/AudioRingSizing.h>
#include <visona/HostPlayheadReader.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

TEST_CASE("Host playhead reader validates signatures and retains the last good value", "[host]")
{
    visona::HostPlayheadReader reader;
    CHECK(reader.read(true, 0, 123, visona::TimeSignature{7, 8}).timeSignature ==
          visona::TimeSignature{7, 8});
    for (const auto signature :
         {visona::TimeSignature{0, 4}, visona::TimeSignature{4, 0}, visona::TimeSignature{5, 64},
          visona::TimeSignature{65, 4}, visona::TimeSignature{4, 3}})
    {
        CHECK_FALSE(signature.isValid());
        const auto head = reader.read(true, 4, 123, signature);
        CHECK(head.valid);
        CHECK(head.timeSignature == visona::TimeSignature{7, 8});
    }
    CHECK(visona::TimeSignature{64, 32}.isValid());
}

TEST_CASE("Host playhead reader never treats a missing position as frozen valid transport",
          "[host]")
{
    visona::HostPlayheadReader reader;
    CHECK(reader.read(true, 10, 123, {}).valid);
    CHECK_FALSE(reader.read(true, {}, 123, {}).valid);
    CHECK_FALSE(reader.read(false, {}, {}, {}).valid);
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), 1.0e100})
        CHECK_FALSE(reader.read(true, bad, 123, {}).valid);
    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()})
        CHECK(reader.read(true, 11, bad, {}).bpm == 123);
}

TEST_CASE("Ring timings retain small host blocks even with a large advertised maximum", "[host]")
{
    const auto capacity = visona::ringCapacityFrames(48000, 4096);
    visona::AudioRingBuffer ring(2, capacity, visona::ringTimingSlots(capacity));
    visona::AudioInputWriter writer(ring);
    std::array<float, 64> audio{};
    std::array<const float*, 2> channels{audio.data(), audio.data()};
    writer.route(0, 0);
    writer.route(1, 1);
    // Buffer 0.8 seconds of 64-frame callbacks without any analysis-side draining.
    for (int block = 0; block < 600; ++block)
        writer.write(channels, 64, 0);
    CHECK(ring.overrunCount() == 0);
    CHECK(ring.droppedFrameCount() == 0);
}

TEST_CASE("Audio device watchdog recovers stopped, failed and stalled inputs", "[device]")
{
    visona::AudioDeviceWatchdog watchdog;
    SECTION("An open input that never starts has no stream")
    {
        for (int i = 0; i < 3; ++i)
            CHECK_FALSE(watchdog.shouldRestart(true, false, {}, false));
        CHECK(watchdog.shouldRestart(true, false, {}, false));
    }
    SECTION("An error requests recovery without waiting")
    {
        CHECK(watchdog.shouldRestart(true, true, 100, true));
    }
    SECTION("A stalled stream recovers but changing frame counts do not")
    {
        for (std::uint64_t frames = 0; frames < 10; ++frames)
            CHECK_FALSE(watchdog.shouldRestart(true, true, frames, false));
        for (int i = 0; i < 3; ++i)
            CHECK_FALSE(watchdog.shouldRestart(true, true, 9, false));
        CHECK(watchdog.shouldRestart(true, true, 9, false));
        CHECK_FALSE(watchdog.shouldRestart(true, true, 0, false));
    }
    SECTION("No selected input never triggers a reopen")
    {
        for (int i = 0; i < 10; ++i)
            CHECK_FALSE(watchdog.shouldRestart(false, false, {}, true));
    }
}
