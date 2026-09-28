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
