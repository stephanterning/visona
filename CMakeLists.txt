cmake_minimum_required(VERSION 3.22)

# These must be set before project() to take effect. They are ignored on other platforms.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures for macOS builds")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version to target")

project(Visona
    VERSION 0.1.0
    DESCRIPTION "Beat-synced stereo oscilloscope"
    HOMEPAGE_URL "https://github.com/stephanterning/visona"
    LANGUAGES C CXX)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Visona supports Apple Silicon only. Set CMAKE_OSX_ARCHITECTURES=arm64.")
endif()

if(APPLE)
    set(visona_build_app_default ON)
else()
    # Linux app builds (for example the `pi` preset on Raspberry Pi OS) opt in with
    # -DVISONA_BUILD_APP=ON or a preset that sets it.
    set(visona_build_app_default OFF)
endif()

option(VISONA_BUILD_APP "Build the JUCE app (downloads JUCE)" ${visona_build_app_default})
option(VISONA_BUILD_PLUGIN "Build the VST3/AU/CLAP plugin (downloads JUCE)" OFF)
option(VISONA_BUILD_TESTS "Build the core unit tests (downloads Catch2)" ON)
option(VISONA_WARNINGS_AS_ERRORS "Treat warnings in Visona's own non-JUCE code as errors" ON)
set(VISONA_SANITIZERS "" CACHE STRING
    "Sanitizers to enable for all targets, as a -fsanitize= list, e.g. address,undefined")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(VISONA_SANITIZERS)
    add_compile_options(-fsanitize=${VISONA_SANITIZERS} -fno-sanitize-recover=all
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${VISONA_SANITIZERS})
endif()

include(cmake/VisonaWarnings.cmake)

add_subdirectory(core)

if(VISONA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VISONA_BUILD_APP OR VISONA_BUILD_PLUGIN)
    include(cmake/JuceSetup.cmake)
endif()

if(VISONA_BUILD_APP)
    add_subdirectory(app)
endif()

if(VISONA_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(VISONA_BUILD_APP OR VISONA_BUILD_PLUGIN)
    add_subdirectory(ui)
endif()
