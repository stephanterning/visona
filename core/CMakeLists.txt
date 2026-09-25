# Plain C++20 with no JUCE dependency, so it builds and tests anywhere.

add_library(visona_core STATIC
    include/visona/AudioInputWriter.h
    include/visona/AudioRingBuffer.h
    include/visona/BlockTiming.h
    include/visona/CacheLine.h
    include/visona/MidiClockEvent.h
    include/visona/SourceLayout.h
    include/visona/SpscQueue.h
    include/visona/SweepAnalyzer.h
    include/visona/SweepBuffer.h
    include/visona/TripleBuffer.h
    include/visona/Version.h
    src/AudioInputWriter.cpp
    src/AudioRingBuffer.cpp
    src/SourceLayout.cpp
    src/SweepAnalyzer.cpp
    src/SweepBuffer.cpp
    src/Version.cpp)
add_library(visona::core ALIAS visona_core)

target_include_directories(visona_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(visona_core PUBLIC cxx_std_20)
target_compile_definitions(visona_core PRIVATE VISONA_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(visona_core PRIVATE visona::warnings)
