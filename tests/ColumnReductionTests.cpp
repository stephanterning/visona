#include <visona/ColumnReduction.h>
#include <visona/SweepBuffer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

namespace
{

/** Which columns show each bin of the window, worked out from binsOf() alone. */
std::vector<std::vector<std::size_t>> columnsShowingEachBin(const ColumnMapping& mapping)
{
    std::vector<std::vector<std::size_t>> columns(mapping.numBins());
    std::array<ColumnMapping::BinRange, 2> ranges;
    for (std::size_t column = 0; column < mapping.numColumns(); ++column)
    {
        const auto count = mapping.binsOf(column, ranges);
        for (std::size_t range = 0; range < count; ++range)
            for (auto bin = ranges[range].first; bin < ranges[range].end; ++bin)
                columns[bin].push_back(column);
    }
    return columns;
}

/** Checks columnsOf(first, last) against the columns that show those bins. */
void checkColumnsOf(const ColumnMapping& mapping,
                    const std::vector<std::vector<std::size_t>>& columnsOfBin, std::size_t first,
                    std::size_t last)
{
    CAPTURE(first, last);
    const auto numBins = mapping.numBins();
    std::vector<bool> expected(mapping.numColumns(), false);
    for (auto bin = first;; bin = (bin + 1) % numBins)
    {
        for (const auto column : columnsOfBin[bin])
            expected[column] = true;
        if (bin == last)
            break;
    }

    std::array<ColumnMapping::ColumnRange, 2> ranges;
    const auto count = mapping.columnsOf(first, last, ranges);
    std::vector<bool> actual(mapping.numColumns(), false);
    for (std::size_t range = 0; range < count; ++range)
    {
        REQUIRE(ranges[range].first <= ranges[range].last);
        REQUIRE(ranges[range].last < mapping.numColumns());
        for (auto column = ranges[range].first; column <= ranges[range].last; ++column)
            actual[column] = true;
    }
    if (count == 2)
        REQUIRE(ranges[0].last + 1 < ranges[1].first); // in order, and merged if they touch
    REQUIRE(actual == expected);
}

} // namespace

TEST_CASE("A zoomed ColumnMapping shows the view's bins in order", "[columns][zoom]")
{
    struct Case
    {
        std::size_t numBins;
        std::size_t numColumns;
        double offset;
        double span;
    };
    const auto c =
        GENERATE(Case{64, 10, 0.3, 0.5}, Case{64, 64, 0.3, 0.5}, Case{64, 200, 0.3, 0.5},
                 Case{64, 10, 0.9, 0.5}, Case{64, 200, 0.9, 0.5}, Case{64, 7, 0.97, 1.0},
                 Case{64, 64, 0.5, 1.0}, Case{64, 50, 0.99, 1.0 / 32.0},
                 Case{64, 3, 0.0, 1.0 / 64.0}, Case{64, 1, 0.123, 0.2}, Case{64, 1'000, 0.95, 0.2},
                 Case{131'072, 3'456, 0.97, 1.0 / 32.0}, Case{131'072, 1'728, 0.4, 0.37});
    CAPTURE(c.numBins, c.numColumns, c.offset, c.span);
    const ColumnMapping mapping(c.numBins, c.numColumns, c.offset, c.span);
    REQUIRE(mapping.isZoomed());

    const auto bins = static_cast<double>(c.numBins);
    const auto viewFirst = static_cast<std::size_t>(std::floor(c.offset * bins));
    const auto viewEnd = mapping.endBin(c.numColumns - 1);
    CHECK(mapping.firstBin(0) == viewFirst);
    CHECK(viewEnd == std::max(static_cast<std::size_t>(std::floor((c.offset + c.span) * bins)),
                              mapping.firstBin(c.numColumns - 1) + 1));
    REQUIRE(viewEnd - viewFirst <= c.numBins);

    // The columns walk through the view without overlap and without holes, or, with more
    // columns than bins, one bin at a time.
    const auto moreBinsThanColumns = mapping.span() * bins >= static_cast<double>(c.numColumns);
    auto expectedFirst = viewFirst;
    for (std::size_t column = 0; column < c.numColumns; ++column)
    {
        CAPTURE(column);
        const auto first = mapping.firstBin(column);
        const auto end = mapping.endBin(column);
        REQUIRE(first < end);
        REQUIRE(end <= 2 * c.numBins);
        if (moreBinsThanColumns)
        {
            REQUIRE(first == expectedFirst);
            expectedFirst = end;
        }
        else
        {
            REQUIRE(end == first + 1);
            REQUIRE(first >= expectedFirst);
            REQUIRE(first <= expectedFirst + 1);
            expectedFirst = first;
        }
    }

    const auto columnsOfBin = columnsShowingEachBin(mapping);
    for (std::size_t bin = 0; bin < c.numBins; ++bin)
    {
        const auto& columns = columnsOfBin[bin];
        CAPTURE(bin);
        const auto position = bin >= viewFirst ? bin : bin + c.numBins;
        REQUIRE(columns.empty() == (position >= viewEnd));
        for (std::size_t i = 1; i < columns.size(); ++i)
            REQUIRE(columns[i] == columns[i - 1] + 1); // one run of columns
        if (!columns.empty())
        {
            REQUIRE(mapping.firstColumn(bin) == columns.front());
            REQUIRE(mapping.lastColumn(bin) == columns.back());
        }
    }

    if (c.numBins <= 64)
    {
        for (std::size_t first = 0; first < c.numBins; ++first)
            for (std::size_t last = 0; last < c.numBins; ++last)
                checkColumnsOf(mapping, columnsOfBin, first, last);
    }
    else
    {
        // Every bin of the view and its edges, and a sample of the rest.
        std::vector<std::size_t> sample;
        for (auto position = viewFirst + c.numBins - 2; position <= viewEnd + c.numBins + 1;
             position += std::max<std::size_t>((viewEnd - viewFirst) / 40, 1))
            sample.push_back(position % c.numBins);
        for (const auto position : {viewFirst, viewFirst + 1, viewEnd - 1, viewEnd})
            sample.push_back(position % c.numBins);
        for (std::size_t bin = 5; bin < c.numBins; bin += c.numBins / 9)
            sample.push_back(bin);
        for (const auto first : sample)
            for (const auto last : sample)
                checkColumnsOf(mapping, columnsOfBin, first, last);
    }
}

TEST_CASE("ColumnMapping clamps the zoom", "[columns][zoom]")
{
    SECTION("the whole window is not zoomed")
    {
        const ColumnMapping mapping(64, 10, 0.0, 1.0);
        CHECK_FALSE(mapping.isZoomed());
        CHECK_FALSE(ColumnMapping(64, 10, 1.0, 1.5).isZoomed());
    }

    SECTION("the offset is taken modulo the window")
    {
        CHECK(ColumnMapping(64, 10, 1.25, 0.5).offset() == 0.25);
        CHECK(ColumnMapping(64, 10, -0.25, 0.5).offset() == 0.75);
        CHECK(ColumnMapping(64, 10, 0.25, 1.0).isZoomed());
    }

    SECTION("the span is at most the window and at least one bin")
    {
        CHECK(ColumnMapping(64, 10, 0.0, 2.0).span() == 1.0);
        CHECK(ColumnMapping(64, 10, 0.0, 0.0).span() == 1.0 / 64.0);
        CHECK(ColumnMapping(64, 10, 0.0, -1.0).span() == 1.0 / 64.0);
    }

    SECTION("non-finite values mean the whole window")
    {
        const ColumnMapping mapping(64, 10, std::nan(""), std::numeric_limits<double>::infinity());
        CHECK(mapping.offset() == 0.0);
        CHECK(mapping.span() == 1.0);
        CHECK_FALSE(mapping.isZoomed());
    }
}

TEST_CASE("ColumnMapping converts between columns and window positions", "[columns][zoom]")
{
    const ColumnMapping mapping(64, 100, 0.875, 0.25); // from 7/8 of the window to 1/8

    CHECK(mapping.windowPositionOf(0.0) == 0.875);
    CHECK(mapping.windowPositionOf(50.0) == 0.0);
    CHECK(mapping.windowPositionOf(75.0) == 0.0625);
    CHECK(mapping.columnOf(0.875) == 0.0);
    CHECK(mapping.columnOf(0.0) == 50.0);
    CHECK(mapping.columnOf(0.0625) == 75.0);
    CHECK(mapping.columnOf(0.5) < 0.0);
    CHECK(mapping.columnOf(0.125) < 0.0); // the right edge belongs to the next view

    const ColumnMapping whole(64, 100);
    CHECK(whole.windowPositionOf(25.0) == 0.25);
    CHECK(whole.columnOf(0.25) == 25.0);
}

TEST_CASE("reduceColumns reduces a view that runs past the end of the window", "[columns][zoom]")
{
    const auto sweep = exampleSweep();

    SECTION("one bin per column")
    {
        const ColumnMapping mapping(8, 4, 0.75, 0.5); // bins 6, 7, 0, 1
        std::vector<ColumnSpan> spans(4);
        reduceColumns(sweep, 0, mapping, 0, spans);
        CHECK(spans[0] == ColumnSpan{-7.0f, 6.0f, ColumnSpan::Pass::previous});
        CHECK(spans[1] == ColumnSpan{-8.0f, 7.0f, ColumnSpan::Pass::previous});
        CHECK(spans[2] == ColumnSpan{0.0f, 1.0f, ColumnSpan::Pass::current});
        CHECK(spans[3] == ColumnSpan{0.5f, 1.5f, ColumnSpan::Pass::current});
    }

    SECTION("a column across the end of the window")
    {
        const ColumnMapping mapping(8, 2, 0.625, 0.5); // bins 5, 6 and bins 7, 0
        std::vector<ColumnSpan> spans(2);
        reduceColumns(sweep, 0, mapping, 0, spans);
        CHECK(spans[0] == ColumnSpan{2.5f, 3.5f, ColumnSpan::Pass::current});
        CHECK(spans[1] == ColumnSpan{0.0f, 1.0f, ColumnSpan::Pass::current});
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
