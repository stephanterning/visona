juce_add_plugin(VisonaPlugin
    COMPANY_NAME "Stephan Terning"
    PLUGIN_MANUFACTURER_CODE Ster
    PLUGIN_CODE Visn
    FORMATS VST3
    PRODUCT_NAME "Visona"
    BUNDLE_ID "io.github.stephanterning.visona.plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Analyzer)

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

if(APPLE)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(VisonaPlugin_VST3 PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
    else()
        add_custom_command(TARGET VisonaPlugin_VST3 POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_FILE:VisonaPlugin_VST3>"
            VERBATIM)
    endif()
endif()

juce_add_plugin(VisonaSyncPlugin
    COMPANY_NAME "Stephan Terning"
    PLUGIN_MANUFACTURER_CODE Ster
    PLUGIN_CODE Sync
    FORMATS VST3
    PRODUCT_NAME "Visona Sync"
    BUNDLE_ID "io.github.stephanterning.visona.sync"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Generator)

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

if(APPLE)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(VisonaSyncPlugin_VST3 PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
    else()
        add_custom_command(TARGET VisonaSyncPlugin_VST3 POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_FILE:VisonaSyncPlugin_VST3>"
            VERBATIM)
    endif()
endif()
