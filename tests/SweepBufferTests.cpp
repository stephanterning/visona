#include "support/AllocationCounter.h"

#include <visona/SweepBuffer.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>

using visona::SweepBuffer;
using visona::SweepCell;
using visona::test::AllocationCounter;

namespace
{

std::size_t countNonEmpty(const SweepBuffer& buffer, std::size_t channel)
{
    std::size_t count = 0;
    for (const auto& cell : buffer.channel(channel))
        if (!cell.isEmpty())
            ++count;
    return count;
}

} // namespace

TEST_CASE("SweepCell starts empty and widens to every value merged into it", "[sweep]")
{
    SweepCell cell;
    CHECK(cell.isEmpty());

    cell.merge({0.25f, 0.25f});
    CHECK_FALSE(cell.isEmpty());
    CHECK(cell == SweepCell{0.25f, 0.25f});

    cell.merge({-0.5f, 0.1f});
    cell.merge(SweepCell{});
    CHECK(cell == SweepCell{-0.5f, 0.25f});
}

TEST_CASE("SweepBuffer starts empty, with every bin in pass 0", "[sweep]")
{
    const SweepBuffer buffer(3, 64);
    CHECK(buffer.numChannels() == 3);
    CHECK(buffer.numBins() == 64);
    CHECK(buffer.pass() == 0);
    CHECK(buffer.generation() == 0);
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        CHECK(buffer.channel(channel).size() == 64);
        CHECK(countNonEmpty(buffer, channel) == 0);
    }
    for (const auto pass : buffer.passes())
        CHECK(pass == 0);

    CHECK(SweepBuffer::defaultBinCount == 131'072);
    CHECK_THROWS_AS(SweepBuffer(2, 0), std::invalid_argument);
}

TEST_CASE("SweepBuffer stamps only the first bin after a clear", "[sweep]")
{
    SweepBuffer buffer(1, 16);
    buffer.advanceHead(3, 5);
    CHECK(buffer.pass() == 3);
    CHECK(buffer.head() == 5);
    for (std::size_t bin = 0; bin < 16; ++bin)
        CHECK(buffer.passes()[bin] == (bin == 5 ? 3u : 0u));
}

TEST_CASE("SweepBuffer empties and stamps every bin the head enters", "[sweep]")
{
    SweepBuffer buffer(2, 8);
    buffer.advanceHead(1, 0);
    for (std::size_t bin = 0; bin < 8; ++bin)
    {
        buffer.advanceHead(1, bin);
        buffer.addToHead(0, {-1.0f, 1.0f});
        buffer.addToHead(1, {0.5f, 0.5f});
    }
    REQUIRE(countNonEmpty(buffer, 0) == 8);

    SECTION("within a pass")
    {
        buffer.advanceHead(2, 0);
        buffer.addToHead(0, {0.0f, 0.0f});
        buffer.advanceHead(2, 3);
        CHECK(buffer.head() == 3);
        CHECK(buffer.passes()[0] == 2);
        for (std::size_t bin = 1; bin <= 3; ++bin)
        {
            CHECK(buffer.passes()[bin] == 2);
            CHECK(buffer.channel(0)[bin].isEmpty());
            CHECK(buffer.channel(1)[bin].isEmpty());
        }
        for (std::size_t bin = 4; bin < 8; ++bin)
        {
            CHECK(buffer.passes()[bin] == 1);
            CHECK(buffer.channel(0)[bin] == SweepCell{-1.0f, 1.0f});
        }
    }

    SECTION("across the end of a pass")
    {
        buffer.advanceHead(2, 1);
        CHECK(buffer.pass() == 2);
        CHECK(buffer.passes()[0] == 2);
        CHECK(buffer.passes()[1] == 2);
        CHECK(buffer.channel(0)[0].isEmpty());
        CHECK(buffer.channel(0)[1].isEmpty());
        CHECK(countNonEmpty(buffer, 0) == 6);
    }

    SECTION("by a whole window or more")
    {
        buffer.advanceHead(4, 2);
        CHECK(buffer.pass() == 4);
        CHECK(countNonEmpty(buffer, 0) == 0);
        CHECK(countNonEmpty(buffer, 1) == 0);
        for (std::size_t bin = 0; bin < 8; ++bin)
            CHECK(buffer.passes()[bin] == (bin <= 2 ? 4u : 3u));
    }

    SECTION("by exactly one window")
    {
        buffer.advanceHead(2, 7);
        CHECK(countNonEmpty(buffer, 0) == 0);
        for (const auto pass : buffer.passes())
            CHECK(pass == 2);
    }

    SECTION("not at all")
    {
        buffer.addToHead(0, {-2.0f, 0.0f});
        buffer.advanceHead(1, 7);
        CHECK(buffer.channel(0)[7] == SweepCell{-2.0f, 1.0f});
    }
}

TEST_CASE("SweepBuffer jumps to a later pass without emptying the bins in between", "[sweep]")
{
    SweepBuffer buffer(1, 8);
    for (std::size_t bin = 0; bin < 6; ++bin)
    {
        buffer.advanceHead(1, bin);
        buffer.addToHead(0, {0.5f, 0.5f});
    }
    buffer.jumpHead(2, 2);
    CHECK(buffer.pass() == 2);
    CHECK(buffer.head() == 2);
    CHECK(buffer.channel(0)[2].isEmpty());
    CHECK(buffer.passes()[2] == 2);
    for (const std::size_t bin : {0u, 1u, 3u, 4u, 5u})
    {
        CAPTURE(bin);
        CHECK(buffer.channel(0)[bin] == SweepCell{0.5f, 0.5f});
        CHECK(buffer.passes()[bin] == 1);
    }
}

TEST_CASE("SweepBuffer clear empties every bin and starts a new generation", "[sweep]")
{
    SweepBuffer buffer(1, 4);
    buffer.advanceHead(7, 2);
    buffer.addToHead(0, {0.1f, 0.2f});
    buffer.clear();
    CHECK(buffer.pass() == 0);
    CHECK(buffer.generation() == 1);
    CHECK(countNonEmpty(buffer, 0) == 0);
    for (const auto pass : buffer.passes())
        CHECK(pass == 0);
}

namespace
{

/** Moves the head of `buffer` forward by `steps` bins and writes a value derived from the new
    position into every channel. */
void writeAhead(SweepBuffer& buffer, std::uint64_t& position, std::uint64_t steps)
{
    position += steps;
    const auto pass = position / buffer.numBins() + 1;
    const auto bin = static_cast<std::size_t>(position % buffer.numBins());
    buffer.advanceHead(pass, bin);
    for (std::size_t channel = 0; channel < buffer.numChannels(); ++channel)
    {
        const auto value = static_cast<float>(position % 1'000) + static_cast<float>(channel);
        buffer.addToHead(channel, {-value, value});
    }
}

} // namespace

TEST_CASE("SweepBuffer copies stay equal to their source through every kind of change",
          "[sweep][snapshot]")
{
    // Three copies refreshed in rotation, like the slots of a TripleBuffer: each copy lags the
    // source by a different amount when it is refreshed.
    SweepBuffer source(2, 64);
    std::array<SweepBuffer, 3> copies{SweepBuffer(2, 64), SweepBuffer(2, 64), SweepBuffer(2, 64)};

    std::mt19937 random(1234);
    std::uint64_t position = 0;
    for (int step = 0; step < 3'000; ++step)
    {
        const auto kind = random() % 100;
        if (kind < 2)
        {
            source.clear();
        }
        else if (kind < 4 && source.pass() > 0)
        {
            // A relocation: the next pass, anywhere in the window.
            const auto pass = source.pass() + 1;
            const auto bin = static_cast<std::size_t>(random() % source.numBins());
            position = (pass - 1) * source.numBins() + bin;
            source.jumpHead(pass, bin);
        }
        else if (kind < 6)
        {
            writeAhead(source, position, 64 + random() % 200);
        }
        else
        {
            // Mostly small steps, including 0: more samples in the head bin.
            writeAhead(source, position, random() % 5);
        }

        auto& copy = copies[static_cast<std::size_t>(step) % copies.size()];
        copy.copyFrom(source);
        REQUIRE(copy == source);
    }
}

TEST_CASE("SweepBuffer keeps each bin's start and band levels until the head enters it again",
          "[sweep]")
{
    SweepBuffer buffer(2, 8);
    CHECK(buffer.starts(0)[3] == SweepBuffer::unknownStart);
    CHECK(buffer.bands(1)[3] == visona::BandLevels{});

    buffer.advanceHead(1, 3);
    buffer.markHeadStart(0, 0.25f);
    buffer.markHeadStart(0, 0.75f); // the first value into the bin counts
    buffer.addBandsToHead(1, {0.5f, 0.1f, 0.0f});
    buffer.addBandsToHead(1, {0.2f, 0.3f, 0.05f});
    CHECK(buffer.starts(0)[3] == 0.25f);
    CHECK(buffer.starts(1)[3] == SweepBuffer::unknownStart);
    CHECK(buffer.bands(1)[3] == visona::BandLevels{0.5f, 0.3f, 0.05f});
    CHECK(buffer.bands(0)[3].isEmpty());

    buffer.advanceHead(1, 5);
    buffer.markBinStart(0, 4, -0.5f);
    buffer.addBandsToBin(0, 4, {0.0f, 0.0f, 0.4f});
    CHECK(buffer.starts(0)[4] == -0.5f);
    CHECK(buffer.bands(0)[4] == visona::BandLevels{0.0f, 0.0f, 0.4f});

    SweepBuffer copy(2, 8);
    copy.copyFrom(buffer);
    CHECK(copy == buffer);

    // The next pass empties the bins again.
    buffer.advanceHead(2, 4);
    CHECK(buffer.starts(0)[3] == SweepBuffer::unknownStart);
    CHECK(buffer.bands(1)[3].isEmpty());
    CHECK(buffer.starts(0)[4] == SweepBuffer::unknownStart);
    copy.copyFrom(buffer);
    CHECK(copy == buffer);

    buffer.clear();
    CHECK(buffer.starts(0)[4] == SweepBuffer::unknownStart);
}

TEST_CASE("SweepBuffer writing and copying do not allocate", "[sweep][snapshot][realtime]")
{
    SweepBuffer source(2, 4'096);
    SweepBuffer copy(2, 4'096);
    STATIC_REQUIRE(noexcept(copy.copyFrom(source)));
    STATIC_REQUIRE(noexcept(source.advanceHead(1, 0)));
    STATIC_REQUIRE(noexcept(source.addToHead(0, {})));
    STATIC_REQUIRE(noexcept(source.clear()));

    std::uint64_t position = 0;
    const AllocationCounter allocations;
    for (int step = 0; step < 100; ++step)
    {
        writeAhead(source, position, static_cast<std::uint64_t>(step % 7) * 300);
        copy.copyFrom(source);
    }
    source.clear();
    copy.copyFrom(source);
    const auto allocationCount = allocations.count();

    CHECK(allocationCount == 0);
    CHECK(copy == source);
}
