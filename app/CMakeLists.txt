set(visona_app_options
    PRODUCT_NAME "Visona"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "Visona"
    BUNDLE_ID "io.github.stephanterning.visona"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

# Without the microphone permission, macOS delivers silence from every audio input.
if(APPLE)
    list(APPEND visona_app_options
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT
            "Visona displays the audio arriving at your audio interface's inputs. It does not record or store it.")
endif()

juce_add_gui_app(Visona ${visona_app_options})

target_sources(Visona PRIVATE
    src/AnalysisThread.cpp
    src/AnalysisThread.h
    src/AudioEngine.cpp
    src/AudioEngine.h
    src/HostTime.h
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h
    src/MidiClockInput.cpp
    src/MidiClockInput.h
    src/ProcessCpu.h
    src/RealtimeAllocationCheck.cpp
    src/RealtimeAllocationCheck.h
    src/Settings.cpp
    src/Settings.h)

if(APPLE)
    target_sources(Visona PRIVATE
        src/MacWindow.h
        src/MacWindow.mm)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(Visona PRIVATE
        src/X11Display.cpp
        src/X11Display.h)
endif()

target_compile_definitions(Visona PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Visona,JUCE_PRODUCT_NAME>"
    $<$<CONFIG:Debug>:VISONA_REALTIME_ALLOCATION_CHECK=1>)

set(visona_juce_link_libs
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# LTO makes the final link very slow on Raspberry Pi (often 20+ minutes with swap).
if(APPLE)
    list(APPEND visona_juce_link_libs juce::juce_recommended_lto_flags)
endif()

target_link_libraries(Visona PRIVATE visona::core ${visona_juce_link_libs})

# Local builds are ad-hoc signed. The Release workflow signs the app again with a Developer ID,
# the hardened runtime and Visona.entitlements, and notarizes it (D-111).
if(APPLE)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(Visona PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
            XCODE_GENERATE_SCHEME ON)
    else()
        add_custom_command(TARGET Visona POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:Visona>"
            VERBATIM)
    endif()
endif()
