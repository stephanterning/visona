include("${PROJECT_SOURCE_DIR}/cmake/ClapSetup.cmake")

# Ad-hoc signs every macOS bundle of a plugin, so a DAW loads the local build.
function(visona_sign_plugin_formats target)
    if(NOT APPLE)
        return()
    endif()
    foreach(format IN ITEMS VST3 AU CLAP)
        set(format_target "${target}_${format}")
        if(NOT TARGET "${format_target}")
            continue()
        endif()
        if(CMAKE_GENERATOR STREQUAL "Xcode")
            set_target_properties("${format_target}" PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
        else()
            add_custom_command(TARGET "${format_target}" POST_BUILD
                COMMAND codesign --force --sign - --timestamp=none "$<TARGET_FILE:${format_target}>"
                VERBATIM)
        endif()
    endforeach()
endfunction()

juce_add_plugin(VisonaPlugin
    COMPANY_NAME "Stephan Terning"
    PLUGIN_MANUFACTURER_CODE Ster
    PLUGIN_CODE Visn
    FORMATS VST3 AU
    PRODUCT_NAME "Visona"
    BUNDLE_ID "io.github.stephanterning.visona.plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(VisonaPlugin PRIVATE
    src/PluginAnalysisThread.cpp
    src/PluginAnalysisThread.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginState.cpp
    src/PluginState.h
    src/ScopeEditor.cpp
    src/ScopeEditor.h)

target_compile_definitions(VisonaPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VisonaPlugin
    PRIVATE
        visona::core
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        $<$<BOOL:${APPLE}>:juce::juce_recommended_lto_flags>)

clap_juce_extensions_plugin(TARGET VisonaPlugin
    CLAP_ID "io.github.stephanterning.visona.plugin"
    CLAP_FEATURES audio-effect analyzer stereo
    CLAP_MANUAL_URL "https://github.com/stephanterning/visona/blob/main/docs/plugins.md"
    CLAP_SUPPORT_URL "https://github.com/stephanterning/visona/issues")

visona_sign_plugin_formats(VisonaPlugin)

juce_add_plugin(VisonaSyncPlugin
    COMPANY_NAME "Stephan Terning"
    PLUGIN_MANUFACTURER_CODE Ster
    PLUGIN_CODE Sync
    FORMATS VST3 AU
    PRODUCT_NAME "Visona Sync"
    BUNDLE_ID "io.github.stephanterning.visona.sync"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(VisonaSyncPlugin PRIVATE
    src/SyncEditor.cpp
    src/SyncEditor.h
    src/SyncProcessor.cpp
    src/SyncProcessor.h)

target_compile_definitions(VisonaSyncPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_include_directories(VisonaSyncPlugin PRIVATE "${PROJECT_SOURCE_DIR}")

target_link_libraries(VisonaSyncPlugin
    PRIVATE
        visona::core
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        $<$<BOOL:${APPLE}>:juce::juce_recommended_lto_flags>)

clap_juce_extensions_plugin(TARGET VisonaSyncPlugin
    CLAP_ID "io.github.stephanterning.visona.sync"
    CLAP_FEATURES instrument utility stereo
    CLAP_MANUAL_URL "https://github.com/stephanterning/visona/blob/main/docs/plugins.md"
    CLAP_SUPPORT_URL "https://github.com/stephanterning/visona/issues")

visona_sign_plugin_formats(VisonaSyncPlugin)
