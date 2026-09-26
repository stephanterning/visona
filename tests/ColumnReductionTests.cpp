#include <visona/ColumnReduction.h>
#include <visona/SweepBuffer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <utility>
#include <vector>

using visona::ColumnMapping;
using visona::ColumnSpan;
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
