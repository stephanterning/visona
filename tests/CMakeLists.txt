include(FetchContent)

FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
    URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34)
FetchContent_MakeAvailable(Catch2)

add_executable(visona_core_tests
    VersionTests.cpp)

target_compile_definitions(visona_core_tests PRIVATE VISONA_EXPECTED_VERSION="${PROJECT_VERSION}")
target_link_libraries(visona_core_tests PRIVATE visona::core visona::warnings Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(visona_core_tests DISCOVERY_MODE PRE_TEST)
