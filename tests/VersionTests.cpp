#include <visona/Version.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("versionString matches the CMake project version", "[version]")
{
    CHECK(visona::versionString() == VISONA_EXPECTED_VERSION);
}
