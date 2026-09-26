#include <visona/MusicalTime.h>

#include <catch2/catch_test_macros.hpp>

using visona::musicalPosition;
using visona::MusicalPosition;
using visona::TimeSignature;

TEST_CASE("MIDI Clock counts 24 ticks per quarter note and 6 per SPP unit", "[musical-time]")
{
    STATIC_REQUIRE(visona::ticksPerQuarterNote == 24);
    STATIC_REQUIRE(visona::ticksPerSppUnit == 6);
}

TEST_CASE("ticksPerBar is 24 × numerator × 4 / denominator", "[musical-time]")
{
    STATIC_REQUIRE(TimeSignature{}.ticksPerBar() == 96);
    STATIC_REQUIRE(TimeSignature{} == TimeSignature{4, 4});
    CHECK(TimeSignature{4, 4}.ticksPerBeat() == 24);
    CHECK(TimeSignature{3, 4}.ticksPerBar() == 72);
    CHECK(TimeSignature{5, 4}.ticksPerBar() == 120);
    CHECK(TimeSignature{2, 2}.ticksPerBeat() == 48);
    CHECK(TimeSignature{2, 2}.ticksPerBar() == 96);
    CHECK(TimeSignature{6, 8}.ticksPerBeat() == 12);
    CHECK(TimeSignature{6, 8}.ticksPerBar() == 72);
    CHECK(TimeSignature{7, 8}.ticksPerBar() == 84);
    CHECK(TimeSignature{1, 32}.ticksPerBar() == 3);
}

TEST_CASE("A time signature needs a whole number of ticks per beat", "[musical-time]")
{
    CHECK(TimeSignature{4, 4}.isValid());
    CHECK(TimeSignature{1, 1}.isValid());
    CHECK(TimeSignature{13, 16}.isValid());
    CHECK(TimeSignature{4, 32}.isValid());

    CHECK_FALSE(TimeSignature{0, 4}.isValid());
    CHECK_FALSE(TimeSignature{-3, 4}.isValid());
    CHECK_FALSE(TimeSignature{4, 0}.isValid());
    CHECK_FALSE(TimeSignature{4, 3}.isValid());
    CHECK_FALSE(TimeSignature{4, 12}.isValid());
    CHECK_FALSE(TimeSignature{4, 64}.isValid());
    CHECK_FALSE(TimeSignature{4, -4}.isValid());
}

TEST_CASE("musicalPosition counts bars and beats from 1", "[musical-time]")
{
    const TimeSignature fourFour;
    CHECK(musicalPosition(0.0, fourFour) == MusicalPosition{1, 1, 0.0});
    CHECK(musicalPosition(23.5, fourFour) == MusicalPosition{1, 1, 23.5});
    CHECK(musicalPosition(24.0, fourFour) == MusicalPosition{1, 2, 0.0});
    CHECK(musicalPosition(30.0, fourFour) == MusicalPosition{1, 2, 6.0});
    CHECK(musicalPosition(95.0, fourFour) == MusicalPosition{1, 4, 23.0});
    CHECK(musicalPosition(96.0, fourFour) == MusicalPosition{2, 1, 0.0});
    CHECK(musicalPosition(16.0 * 96.0, fourFour) == MusicalPosition{17, 1, 0.0});
    // The largest Song Position Pointer: 16383 sixteenths.
    CHECK(musicalPosition(16'383.0 * 6.0, fourFour) == MusicalPosition{1'024, 4, 18.0});

    const TimeSignature sixEight{6, 8};
    CHECK(musicalPosition(12.0, sixEight) == MusicalPosition{1, 2, 0.0});
    CHECK(musicalPosition(71.5, sixEight) == MusicalPosition{1, 6, 11.5});
    CHECK(musicalPosition(72.0, sixEight) == MusicalPosition{2, 1, 0.0});

    const TimeSignature threeFour{3, 4};
    CHECK(musicalPosition(72.0 * 1'000.0 + 50.0, threeFour) == MusicalPosition{1'001, 3, 2.0});
}
