#include "support/Signals.h"

#include <visona/ColumnReduction.h>
#include <visona/LaneMapping.h>
#include <visona/SweepAnalyzer.h>
#include <visona/WaveformColoring.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstddef>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using visona::BandLevels;
using visona::bandReadingFor;
using visona::ColumnMapping;
using visona::ColumnPaint;
using visona::ColumnSpan;
using visona::freeRunningWindowFrames;
using visona::Ink;
using visona::LaneMapping;
using visona::paintColumn;
using visona::reduceBandLevels;
using visona::reduceColumns;
using visona::SweepAnalyzer;
using visona::WaveformColoring;
using visona::test::music;

namespace
{

constexpr std::array<WaveformColoring, 3> colorings{
    WaveformColoring::precise, WaveformColoring::blended, WaveformColoring::layered};

/** The rows `paint` covers, as flags from row 0 to `height` - 1. */
std::vector<bool> coveredRows(const ColumnPaint& paint, int height)
{
    std::vector<bool> rows(static_cast<std::size_t>(height), false);
    for (std::size_t run = 0; run < paint.count; ++run)
        for (auto row = paint.runs[run].top; row <= paint.runs[run].bottom; ++row)
            rows[static_cast<std::size_t>(row)] = true;
    return rows;
}

} // namespace

TEST_CASE("Every colouring paints exactly the rows of the full-band span", "[coloring]")
{
    std::mt19937 random(2026);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int trial = 0; trial < 20'000; ++trial)
    {
        const auto height = 1 + static_cast<int>(random() % 400);
        const LaneMapping lane(0, height, 1.0f + 60.0f * unit(random) * unit(random));
        const auto a = static_cast<int>(random() % static_cast<unsigned>(height));
        const auto b = static_cast<int>(random() % static_cast<unsigned>(height));
        const auto top = std::min(a, b);
        const auto bottom = std::max(a, b);
        BandLevels levels{};
        for (auto& level : levels)
        {
            const auto kind = random() % 5;
            level = kind == 0 ? -1.0f : (kind == 1 ? 0.0f : 1.5f * unit(random));
        }

        for (const auto coloring : colorings)
        {
            const auto paint = paintColumn(coloring, top, bottom, levels, lane);
            CAPTURE(trial, static_cast<int>(coloring), height, top, bottom, levels);
            REQUIRE(paint.count >= 1);
            REQUIRE(paint.runs[0].top == top);
            REQUIRE(paint.runs[0].bottom == bottom);
            for (std::size_t run = 0; run < paint.count; ++run)
            {
                REQUIRE(paint.runs[run].top >= top);
                REQUIRE(paint.runs[run].bottom <= bottom);
                REQUIRE(paint.runs[run].top <= paint.runs[run].bottom);
            }
        }
    }
}

TEST_CASE("paintColumn paints nothing for an empty span", "[coloring]")
{
    const LaneMapping lane(0, 100, 1.0f);
    for (const auto coloring : colorings)
        CHECK(paintColumn(coloring, 10, 9, {0.5f, 0.5f, 0.5f}, lane).count == 0);
}

TEST_CASE("Without band levels, every colouring is neutral", "[coloring]")
{
    const LaneMapping lane(0, 100, 1.0f);
    for (const auto coloring : colorings)
    {
        for (const BandLevels& levels : {BandLevels{-1.0f, -1.0f, -1.0f}, BandLevels{}})
        {
            const auto paint = paintColumn(coloring, 20, 80, levels, lane);
            CHECK(paint.count == 1);
            CHECK(paint.runs[0] == ColumnPaint::Run{20, 80, Ink::neutral});
        }
    }
}

TEST_CASE("Precise paints the neutral ink only", "[coloring]")
{
    const LaneMapping lane(0, 100, 1.0f);
    const auto paint = paintColumn(WaveformColoring::precise, 5, 95, {0.9f, 0.1f, 0.3f}, lane);
    CHECK(paint.count == 1);
    CHECK(paint.runs[0] == ColumnPaint::Run{5, 95, Ink::neutral});
}

TEST_CASE("Blended weights each band by its share of the energy", "[coloring]")
{
    const LaneMapping lane(0, 100, 1.0f);
    const auto paint = paintColumn(WaveformColoring::blended, 5, 95, {0.6f, 0.3f, -1.0f}, lane);
    CHECK(paint.count == 1);
    CHECK(paint.runs[0] == ColumnPaint::Run{5, 95, Ink::blend});
    CHECK_THAT(static_cast<double>(paint.blend[0]), WithinAbs(0.8, 1.0e-6)); // 0.36 / 0.45
    CHECK_THAT(static_cast<double>(paint.blend[1]), WithinAbs(0.2, 1.0e-6));
    CHECK(paint.blend[2] == 0.0f);
}

TEST_CASE("Layered draws the band envelopes inside the outline, lows first", "[coloring]")
{
    // 100 rows: value v lands on row ⌊50 - 50v⌋, so an envelope of ±0.25 covers rows 37 to 62.
    const LaneMapping lane(0, 100, 1.0f);

    SECTION("lows loudest")
    {
        const auto paint =
            paintColumn(WaveformColoring::layered, 10, 70, {0.625f, 0.25f, 0.125f}, lane);
        REQUIRE(paint.count == 4);
        CHECK(paint.runs[0] == ColumnPaint::Run{10, 70, Ink::low});
        CHECK(paint.runs[1] == ColumnPaint::Run{18, 70, Ink::low}); // cut at the outline
        CHECK(paint.runs[2] == ColumnPaint::Run{37, 62, Ink::mid});
        CHECK(paint.runs[3] == ColumnPaint::Run{43, 56, Ink::high});
    }

    SECTION("mids loudest hide the lows")
    {
        const auto paint =
            paintColumn(WaveformColoring::layered, 0, 99, {0.25f, 0.75f, 0.125f}, lane);
        REQUIRE(paint.count == 4);
        CHECK(paint.runs[0] == ColumnPaint::Run{0, 99, Ink::mid});
        CHECK(paint.runs[1] == ColumnPaint::Run{37, 62, Ink::low});
        CHECK(paint.runs[2] == ColumnPaint::Run{12, 87, Ink::mid});
        CHECK(paint.runs[3] == ColumnPaint::Run{43, 56, Ink::high});
    }

    SECTION("an envelope thinner than a row is left out")
    {
        const auto paint =
            paintColumn(WaveformColoring::layered, 0, 99, {0.5f, 0.25f, 0.004f}, lane);
        REQUIRE(paint.count == 3);
        CHECK(paint.runs[2].ink == Ink::mid);
    }

    SECTION("an outline away from the centre keeps only the envelopes that reach it")
    {
        const auto paint =
            paintColumn(WaveformColoring::layered, 5, 25, {0.75f, 0.25f, 0.125f}, lane);
        REQUIRE(paint.count == 2);
        CHECK(paint.runs[0] == ColumnPaint::Run{5, 25, Ink::low});
        CHECK(paint.runs[1] == ColumnPaint::Run{12, 25, Ink::low});
    }
}

TEST_CASE("The rendered outline of music is the same in every colouring", "[coloring][bands]")
{
    // D-056 end to end: analysis with bands, reduction to columns, and painting.
    constexpr double sampleRate = 48'000.0;
    const auto windowSeconds = GENERATE(0.125, 0.5, 8.0);
    CAPTURE(windowSeconds);
    const auto windowFrames = freeRunningWindowFrames(sampleRate, windowSeconds);
    const auto input = music(sampleRate, static_cast<std::size_t>(windowFrames * 3 / 2));

    SweepAnalyzer analyzer(1);
    analyzer.setSampleRate(sampleRate);
    analyzer.start(windowFrames);
    const float* const channel = input.data();
    analyzer.process(0, std::span(&channel, 1), input.size());
    const auto& sweep = analyzer.buffer();

    constexpr std::size_t numColumns = 900;
    constexpr int height = 300;
    const ColumnMapping mapping(sweep.numBins(), numColumns);
    std::vector<ColumnSpan> spans(numColumns);
    std::vector<BandLevels> levels(numColumns);
    reduceColumns(sweep, 0, mapping, 0, spans);
    std::array<double, 3> delays{};
    for (const auto band : visona::splitBands)
        delays[visona::splitIndex(band)] = analyzer.bandDelayFrames(band);
    reduceBandLevels(sweep, 0, mapping, windowFrames, bandReadingFor(sampleRate, delays, true), 0,
                     levels);

    std::size_t colouredColumns = 0;
    for (const auto gain : {1.0f, 8.0f})
    {
        const LaneMapping lane(0, height, gain);
        for (std::size_t column = 0; column < numColumns; ++column)
        {
            const auto& span = spans[column];
            REQUIRE(span.pass != ColumnSpan::Pass::none);
            const auto rows = lane.rowsOf(span.min, span.max);
            const auto precise =
                paintColumn(WaveformColoring::precise, rows.top, rows.bottom, levels[column], lane);
            const auto expected = coveredRows(precise, height);
            for (const auto coloring : {WaveformColoring::blended, WaveformColoring::layered})
            {
                const auto paint =
                    paintColumn(coloring, rows.top, rows.bottom, levels[column], lane);
                CAPTURE(gain, column, static_cast<int>(coloring));
                REQUIRE(coveredRows(paint, height) == expected);
                if (paint.runs[0].ink != Ink::neutral)
                    ++colouredColumns;
            }
        }
    }
    // Nearly every column has band data; only the newest few behind the head wait for the delay.
    CHECK(colouredColumns > 2 * 2 * numColumns * 9 / 10);
}
