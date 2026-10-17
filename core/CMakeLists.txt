include("${PROJECT_SOURCE_DIR}/cmake/LspStereo.cmake")

add_library(visona_fft STATIC ../third_party/kissfft/kiss_fft.c)
set_target_properties(visona_fft PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(visona_fft SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/kissfft")

# Plain C++20 with no JUCE dependency, so it builds and tests anywhere.

add_library(visona_core STATIC
    src/PolarLevel.cpp
    include/visona/PolarLevel.h
    src/StereoAnalyzer.cpp
    include/visona/StereoAnalyzer.h
    include/visona/StereoProjection.h
    src/BandAnalyzer.cpp
    include/visona/BandAnalyzer.h
    src/SpectrumAnalyzer.cpp
    include/visona/SpectrumAnalyzer.h
    include/visona/AnalysisPipeline.h
    include/visona/AudioInputWriter.h
    include/visona/AudioRingBuffer.h
    include/visona/AutoGain.h
    include/visona/Band.h
    include/visona/BarPeaks.h
    include/visona/BandSplitter.h
    include/visona/BlockTiming.h
    include/visona/CacheLine.h
    include/visona/ClockTimeMapper.h
    include/visona/BarImpulseScheduler.h
    include/visona/HostAnalysisPipeline.h
    include/visona/HostTransport.h
    include/visona/SidechainSyncDetector.h
    include/visona/ColumnReduction.h
    include/visona/LaneMapping.h
    include/visona/MidiClockEvent.h
    include/visona/MidiClockTransport.h
    include/visona/Ruler.h
    include/visona/ScopeGrid.h
    include/visona/SourceLayout.h
    include/visona/SpscQueue.h
    include/visona/SweepAnalyzer.h
    include/visona/SweepBuffer.h
    include/visona/SweepSnapshot.h
    include/visona/SweepWindow.h
    include/visona/SweepZoom.h
    include/visona/TimeSignature.h
    include/visona/TripleBuffer.h
    include/visona/Version.h
    include/visona/WaveformStyle.h
    src/AnalysisPipeline.cpp
    src/AudioInputWriter.cpp
    src/AudioRingBuffer.cpp
    src/AutoGain.cpp
    src/BandSplitter.cpp
    src/BarPeaks.cpp
    src/ClockTimeMapper.cpp
    src/BarImpulseScheduler.cpp
    src/HostAnalysisPipeline.cpp
    src/HostTransport.cpp
    src/SidechainSyncDetector.cpp
    src/ColumnReduction.cpp
    src/LaneMapping.cpp
    src/MidiClockTransport.cpp
    src/Ruler.cpp
    src/ScopeGrid.cpp
    src/SourceLayout.cpp
    src/SweepAnalyzer.cpp
    src/SweepBuffer.cpp
    src/SweepWindow.cpp
    src/SweepZoom.cpp
    src/Version.cpp
    src/WaveformStyle.cpp)
add_library(visona::core ALIAS visona_core)

target_include_directories(visona_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(visona_core PUBLIC cxx_std_20)
target_compile_definitions(visona_core PRIVATE VISONA_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(visona_core PRIVATE visona::warnings visona_fft visona_lsp_stereo)

if(VISONA_BUILD_PLUGIN)
    set_target_properties(visona_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
