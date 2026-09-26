#include <visona/ColumnReduction.h>
#include <visona/SweepBuffer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::Band;
using visona::BandLevels;
using visona::bandReachBins;
using visona::BandReading;
using visona::bandReadingFor;
using visona::ColumnMapping;
using visona::ColumnSpan;
using visona::reduceBandLevels;
using visona::reduceColumns;
using visona::SweepBuffer;
using visona::SweepCell;

TEST_CASE("ColumnMapping covers every bin and every column", "[columns]")
{
    const auto [numBins, numColumns] = GENERATE(
        std::pair<std::size_t, std::size_t>{4'096, 1'000},
        std::pair<std::size_t, std::size_t>{4'096, 4'096},
        std::pair<std::size_t, std::size_t>{4'096, 5'120},
        std::pair<std::size_t, std::size_t>{4'096, 3'023},
        std::pair<std::size_t, std::size_t>{4'096, 1}, std::pair<std::size_t, std::size_t>{7, 3},
        std::pair<std::size_t, std::size_t>{3, 7}, std::pair<std::size_t, std::size_t>{1, 5});
    CAPTURE(numBins, numColumns);
    const ColumnMapping mapping(numBins, numColumns);

    std::vector<std::size_t> firstColumnOf(numBins, numColumns);
    std::vector<std::size_t> lastColumnOf(numBins, 0);
    std::size_t expectedFirst = 0;
    for (std::size_t column = 0; column < numColumns; ++column)
    {
        const auto first = mapping.firstBin(column);
        const auto end = mapping.endBin(column);
        CAPTURE(column);
        REQUIRE(first < end);
        REQUIRE(end <= numBins);
        if (numColumns <= numBins)
        {
            // Without overlap and without holes.
            REQUIRE(first == expectedFirst);
            expectedFirst = end;
        }
        else
        {
            REQUIRE(end == first + 1);
        }
        for (auto bin = first; bin < end; ++bin)
        {
            firstColumnOf[bin] = std::min(firstColumnOf[bin], column);
            lastColumnOf[bin] = std::max(lastColumnOf[bin], column);
        }
    }
    if (numColumns <= numBins)
        CHECK(expectedFirst == numBins);

    for (std::size_t bin = 0; bin < numBins; ++bin)
    {
        CAPTURE(bin);
        REQUIRE(firstColumnOf[bin] < numColumns); // every bin is shown
        REQUIRE(mapping.firstColumn(bin) == firstColumnOf[bin]);
        REQUIRE(mapping.lastColumn(bin) == lastColumnOf[bin]);
    }
}

namespace
{

/** An 8-bin sweep whose head is at bin 5 of pass 2, with distinct values in every bin. */
SweepBuffer exampleSweep()
{
    SweepBuffer sweep(1, 8);
    for (std::size_t bin = 0; bin < 8; ++bin)
    {
        sweep.advanceHead(1, bin);
        sweep.addToHead(0, {-static_cast<float>(bin) - 1.0f, static_cast<float>(bin)});
    }
    for (std::size_t bin = 0; bin <= 5; ++bin)
    {
        sweep.advanceHead(2, bin);
        if (bin != 3) // bin 3 stays empty, as after a dropped block
            sweep.addToHead(
                0, {0.5f * static_cast<float>(bin), 0.5f * static_cast<float>(bin) + 1.0f});
    }
    return sweep;
}

} // namespace

TEST_CASE("reduceColumns takes the min and max of each column's bins", "[columns]")
{
    const auto sweep = exampleSweep();

    SECTION("fewer columns than bins")
    {
        const ColumnMapping mapping(8, 4);
        std::vector<ColumnSpan> spans(4);
        reduceColumns(sweep, 0, mapping, 0, spans);

        CHECK(spans[0] == ColumnSpan{0.0f, 1.5f, ColumnSpan::Pass::current});   // bins 0, 1
        CHECK(spans[1] == ColumnSpan{1.0f, 2.0f, ColumnSpan::Pass::current});   // bin 2; 3 is empty
        CHECK(spans[2] == ColumnSpan{2.0f, 3.5f, ColumnSpan::Pass::current});   // bins 4, 5
        CHECK(spans[3] == ColumnSpan{-8.0f, 7.0f, ColumnSpan::Pass::previous}); // bins 6, 7
    }

    SECTION("the column at the head shows only the new pass")
    {
        const ColumnMapping mapping(8, 2);
        std::vector<ColumnSpan> spans(2);
        reduceColumns(sweep, 0, mapping, 0, spans);
        CHECK(spans[1] == ColumnSpan{2.0f, 3.5f, ColumnSpan::Pass::current}); // bins 4..7
    }

    SECTION("more columns than bins")
    {
        const ColumnMapping mapping(8, 16);
        std::vector<ColumnSpan> spans(4);
        reduceColumns(sweep, 0, mapping, 12, spans);
        CHECK(spans[0] == ColumnSpan{-7.0f, 6.0f, ColumnSpan::Pass::previous}); // bin 6
        CHECK(spans[1] == spans[0]);
        CHECK(spans[2] == ColumnSpan{-8.0f, 7.0f, ColumnSpan::Pass::previous}); // bin 7
        CHECK(spans[3] == spans[2]);
    }

    SECTION("an empty bin gives an empty column")
    {
        const ColumnMapping mapping(8, 8);
        std::vector<ColumnSpan> spans(1);
        reduceColumns(sweep, 0, mapping, 3, spans);
        CHECK(spans[0] == ColumnSpan{});
    }
}

TEST_CASE("reduceColumns draws nothing for a cleared sweep", "[columns]")
{
    const SweepBuffer sweep(2, 64);
    const ColumnMapping mapping(64, 10);
    std::vector<ColumnSpan> spans(10, ColumnSpan{1.0f, 2.0f, ColumnSpan::Pass::current});
    reduceColumns(sweep, 1, mapping, 0, spans);
    for (const auto& span : spans)
        CHECK(span.pass == ColumnSpan::Pass::none);
}

namespace
{

// 10 frames per bin.
constexpr std::size_t bandBins = 8;
constexpr std::uint64_t bandWindowFrames = 80;

/** The low-band level written into a bin: it names the pass and the bin. */
float lowLevel(std::uint64_t pass, std::size_t bin)
{
    return 0.01f * static_cast<float>(pass * 10 + bin);
}

/**
    An 8-bin sweep with a full pass 1 and pass 2 up to bin `head`. Every bin has a full-band span
    of ±1; the low band is ±lowLevel(), the mid band 0.5 and the high band -0.25, in bins that are
    not in `withoutBands`.
*/
SweepBuffer bandSweep(std::size_t head, std::vector<std::size_t> withoutBands = {})
{
    SweepBuffer sweep(1, bandBins);
    const auto write = [&](std::uint64_t pass, std::size_t bin)
    {
        sweep.advanceHead(pass, bin);
        sweep.addToHead(0, {-1.0f, 1.0f});
        if (pass == 2 &&
            std::find(withoutBands.begin(), withoutBands.end(), bin) != withoutBands.end())
            return;
        sweep.addToHead(0, Band::low, {-lowLevel(pass, bin), lowLevel(pass, bin)});
        sweep.addToHead(0, Band::mid, {0.5f, 0.5f});
        sweep.addToHead(0, Band::high, {-0.25f, -0.25f});
    };
    for (std::size_t bin = 0; bin < bandBins; ++bin)
        write(1, bin);
    for (std::size_t bin = 0; bin <= head; ++bin)
        write(2, bin);
    return sweep;
}

std::vector<BandLevels> levelsOf(const SweepBuffer& sweep, const BandReading& reading)
{
    const ColumnMapping mapping(bandBins, bandBins);
    std::vector<BandLevels> levels(bandBins);
    reduceBandLevels(sweep, 0, mapping, bandWindowFrames, reading, 0, levels);
    return levels;
}

BandReading lowReading(double delayFrames, double holdFrames)
{
    BandReading reading;
    reading.delayFrames[0] = delayFrames;
    reading.holdFrames[0] = holdFrames;
    return reading;
}

} // namespace

TEST_CASE("reduceBandLevels takes each band's peak over the column's bins", "[columns][bands]")
{
    const auto sweep = bandSweep(5);
    const ColumnMapping mapping(bandBins, 4);
    std::vector<BandLevels> levels(4);
    reduceBandLevels(sweep, 0, mapping, bandWindowFrames, {}, 0, levels);

    CHECK(levels[0] == BandLevels{lowLevel(2, 1), 0.5f, 0.25f}); // bins 0, 1
    CHECK(levels[2] == BandLevels{lowLevel(2, 5), 0.5f, 0.25f}); // bins 4, 5
    CHECK(levels[3] == BandLevels{lowLevel(1, 7), 0.5f, 0.25f}); // bins 6, 7 of pass 1
}

TEST_CASE("reduceBandLevels reads a band later by its delay", "[columns][bands]")
{
    const auto sweep = bandSweep(5);
    const auto levels = levelsOf(sweep, lowReading(20.0, 0.0)); // 2 bins later

    CHECK(levels[0][0] == lowLevel(2, 2));
    CHECK(levels[3][0] == lowLevel(2, 5));
    // Bins 6 and 7 still hold pass 1, so the columns 2 bins before them have no low band yet.
    CHECK(levels[4][0] == -1.0f);
    CHECK(levels[5][0] == -1.0f);
    // The end of pass 1 reads the start of pass 2.
    CHECK(levels[6][0] == lowLevel(2, 0));
    CHECK(levels[7][0] == lowLevel(2, 1));
    // The other bands are read without delay.
    CHECK(levels[4][1] == 0.5f);
}

TEST_CASE("reduceBandLevels holds a band's peak over the frames around the column",
          "[columns][bands]")
{
    const auto sweep = bandSweep(5);
    const auto levels = levelsOf(sweep, lowReading(0.0, 10.0)); // 1 bin on each side

    CHECK(levels[2][0] == lowLevel(2, 3));
    CHECK(levels[5][0] == lowLevel(2, 5)); // bin 6 is still pass 1
    CHECK(levels[6][0] == lowLevel(1, 7)); // bin 5 is already pass 2
    CHECK(levels[0][0] == lowLevel(2, 1)); // bin 7 of pass 1 is lower
}

TEST_CASE("reduceBandLevels leaves out columns without band data", "[columns][bands]")
{
    SECTION("no band data in the column's bins")
    {
        const auto sweep = bandSweep(5, {2, 3});
        const auto levels = levelsOf(sweep, {});
        CHECK(levels[2] == BandLevels{-1.0f, -1.0f, -1.0f});
        CHECK(levels[1][0] == lowLevel(2, 1));
    }

    SECTION("no full band in the column's bins")
    {
        const SweepBuffer sweep(1, bandBins);
        const auto levels = levelsOf(sweep, lowReading(20.0, 10.0));
        for (const auto& level : levels)
            CHECK(level == BandLevels{-1.0f, -1.0f, -1.0f});
    }
}

TEST_CASE("bandReachBins is the furthest any band reads past a bin", "[columns][bands]")
{
    BandReading reading;
    reading.delayFrames = {24.0, 5.0, 0.0};
    reading.holdFrames = {11.0, 2.0, 30.0};
    CHECK(bandReachBins(reading, bandBins, bandWindowFrames) == 4); // 2 + 2 for low, 3 for high
    CHECK(bandReachBins({}, bandBins, bandWindowFrames) == 0);
    CHECK(bandReachBins(reading, bandBins, 0) == 0);
}

TEST_CASE("bandReadingFor holds each band for a quarter period and compensates on request",
          "[columns][bands]")
{
    const std::array<double, 3> delays{252.6, 37.1, 2.6};
    const auto compensated = bandReadingFor(96'000.0, delays, true);
    CHECK(compensated.delayFrames == delays);
    CHECK_THAT(compensated.holdFrames[0], WithinAbs(480.0, 1.0e-9)); // 5 ms: a quarter of 50 Hz
    CHECK_THAT(compensated.holdFrames[1], WithinAbs(120.0, 1.0e-9));
    CHECK_THAT(compensated.holdFrames[2], WithinAbs(9.6, 1.0e-9));

    const auto raw = bandReadingFor(96'000.0, delays, false);
    CHECK(raw.delayFrames == std::array<double, 3>{});
    CHECK(raw.holdFrames == compensated.holdFrames);
}
