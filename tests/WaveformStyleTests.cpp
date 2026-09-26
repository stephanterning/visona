#include <visona/WaveformStyle.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>

using visona::BandLevels;
using visona::djColour;
using visona::Rgb;

namespace
{

float brightest(const Rgb& colour)
{
    return std::max({colour.red, colour.green, colour.blue});
}

} // namespace

TEST_CASE("One band alone gives its own colour: red, green or blue", "[colour]")
{
    const auto bass = djColour({0.8f, 0.0f, 0.0f});
    CHECK(bass.red == 1.0f);
    CHECK(bass.green < 0.2f);
    CHECK(bass.blue < 0.2f);

    const auto mids = djColour({0.0f, 0.3f, 0.0f});
    CHECK(mids.green == 1.0f);
    CHECK(mids.red < 0.5f);
    CHECK(mids.blue < 0.5f);

    const auto highs = djColour({0.0f, 0.0f, 0.05f});
    CHECK(highs.blue == 1.0f);
    CHECK(highs.red < 0.3f);
    CHECK(highs.green < 0.5f);
}

TEST_CASE("A kick with some mids is orange, and the strongest band sets the hue", "[colour]")
{
    const auto kick = djColour({1.0f, 0.4f, 0.02f});
    CHECK(kick.red == 1.0f);
    CHECK(kick.green > 0.2f);
    CHECK(kick.green < 0.8f);
    CHECK(kick.blue < kick.green);

    // A hi-hat over a quiet bass still reads blue.
    const auto hat = djColour({0.1f, 0.1f, 0.3f});
    CHECK(hat.blue == 1.0f);
    CHECK(hat.red < 0.5f);
}

TEST_CASE("The DJ colour shows the balance between the bands, never the level", "[colour]")
{
    const BandLevels levels{0.5f, 0.2f, 0.1f};
    const auto scale = GENERATE(0.001f, 0.1f, 4.0f);
    const auto colour = djColour({levels.low * scale, levels.mid * scale, levels.high * scale});
    const auto reference = djColour(levels);
    CHECK(brightest(colour) == 1.0f);
    CHECK(std::abs(colour.red - reference.red) < 1.0e-5f);
    CHECK(std::abs(colour.green - reference.green) < 1.0e-5f);
    CHECK(std::abs(colour.blue - reference.blue) < 1.0e-5f);
}

TEST_CASE("Levels without any band give black", "[colour]")
{
    CHECK(djColour({}) == Rgb{});
    CHECK(djColour({-1.0f, 0.0f, 0.0f}) == Rgb{});
}
