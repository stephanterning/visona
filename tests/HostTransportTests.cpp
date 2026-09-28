#include <visona/HostTransport.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

using visona::HostTransport;
using visona::TransportSpan;
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

HostTransport::Playhead stoppedAt(double ppq, double bpm = 120.0)
{
    auto playhead = playingAt(ppq, bpm);
    playhead.isPlaying = false;
    return playhead;
}

} // namespace

TEST_CASE("HostTransport starts stopped with a frozen timeline", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);
    CHECK(transport.state() == TransportState::stopped);
    CHECK(transport.numSpans() >= 1);
    CHECK(transport.span(0).kind == TransportSpan::Kind::frozen);
}

TEST_CASE("HostTransport builds musical spans while the host plays", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    transport.syncTo(0.0, playingAt(0.0));
    // One second at 120 BPM is two quarter notes.
    transport.syncTo(rate, playingAt(2.0));
    CHECK(transport.state() == TransportState::running);
    CHECK(transport.startCount() == 1);

    const auto& span = transport.spanAt(0.0);
    CHECK(span.kind == TransportSpan::Kind::musical);
    CHECK_THAT(span.startTick, WithinAbs(0.0, 1.0e-9));
    CHECK(span.endTick > span.startTick);
}

TEST_CASE("HostTransport keeps the audio after the newest playhead pending", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    // At 120 BPM a quarter note is 24000 frames, and a tick 1000.
    transport.syncTo(0.0, playingAt(0.0));
    CHECK(transport.spanAt(0.0).kind == TransportSpan::Kind::pending);

    transport.syncTo(512.0, playingAt(512.0 / 24'000.0));
    const auto& span = transport.spanAt(0.0);
    CHECK(span.kind == TransportSpan::Kind::musical);
    CHECK(span.end == 512.0);
    CHECK_THAT(span.endTick, WithinAbs(0.512, 1.0e-9));
    CHECK(transport.spanAt(512.0).kind == TransportSpan::Kind::pending);
}

TEST_CASE("HostTransport lets the audio before a jump keep going from the old position",
          "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    transport.syncTo(0.0, playingAt(8.0));
    transport.syncTo(1'000.0, playingAt(8.0 + 1'000.0 / 24'000.0));
    // A loop back to the song start.
    transport.syncTo(2'000.0, playingAt(0.0));

    const auto& beforeJump = transport.spanAt(1'000.0);
    CHECK(beforeJump.kind == TransportSpan::Kind::musical);
    CHECK(beforeJump.startCount == 1);
    CHECK_THAT(beforeJump.endTick, WithinAbs(8.0 * 24.0 + 2.0, 1.0e-9));
    const auto& afterJump = transport.spanAt(2'000.0);
    CHECK(afterJump.kind == TransportSpan::Kind::pending);
    CHECK(afterJump.startCount == 2);
    CHECK(afterJump.startTick == 0.0);
}

TEST_CASE("HostTransport freezes when the host stops", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    transport.syncTo(0.0, playingAt(0.0));
    transport.syncTo(rate, playingAt(1.0 / 4.0));
    transport.syncTo(rate * 2.0, stoppedAt(1.0 / 2.0));

    CHECK(transport.state() == TransportState::stopped);
    const auto& span = transport.spanAt(rate * 2.0);
    CHECK(span.kind == TransportSpan::Kind::frozen);
}

TEST_CASE("HostTransport bumps startCount on a host seek", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    transport.syncTo(0.0, playingAt(0.0));
    transport.syncTo(rate, playingAt(2.0));
    transport.syncTo(rate * 3.0, playingAt(20.0));

    CHECK(transport.startCount() == 2);
}

TEST_CASE("HostTransport reads the host time signature", "[transport][host]")
{
    constexpr double rate = 48'000.0;
    HostTransport transport(rate);

    auto playhead = playingAt(0.0);
    playhead.timeSignature = {3, 4};
    transport.syncTo(0.0, playhead);

    CHECK(transport.timeSignature().numerator == 3);
    CHECK(transport.timeSignature().denominator == 4);
    CHECK(transport.timeSignature().ticksPerBar() == 72);
}
