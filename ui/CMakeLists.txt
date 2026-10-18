# JUCE components compiled into each binary target (app and/or plugin), because each binary
# must compile the JUCE modules exactly once.

set(visona_ui_sources
    StereoView.cpp
    StereoView.h
    BandSpectrumView.cpp
    BandSpectrumView.h
    SpectrumView.cpp
    SpectrumView.h
    AnalysisSelector.cpp
    AnalysisSelector.h
    AudioSettings.h
    Banner.cpp
    Banner.h
    ChannelNames.cpp
    ChannelNames.h
    ChromeButton.cpp
    ChromeButton.h
    ChromeLayout.h
    ChoiceBox.cpp
    ChoiceBox.h
    ChoiceControl.cpp
    ChoiceControl.h
    ChoiceList.cpp
    ChoiceList.h
    ColourSwatches.cpp
    ColourSwatches.h
    ControlBar.cpp
    ControlBar.h
    ControlText.h
    DiagnosticsOverlay.cpp
    DiagnosticsOverlay.h
    RenderStats.h
    GainControl.cpp
    GainControl.h
    Palette.h
    ScopeView.cpp
    ScopeView.h
    SettingsPanel.cpp
    SettingsPanel.h
    StatusBar.cpp
    StatusBar.h
    StepButton.cpp
    StepButton.h
    SyncText.h
    TempoControl.cpp
    TempoControl.h
    Tooltips.cpp
    Tooltips.h
    WaveformControl.cpp
    WaveformControl.h
    WindowControl.cpp
    WindowControl.h
    ZoomOverview.cpp
    ZoomOverview.h)

foreach(visona_ui_target IN ITEMS Visona VisonaPlugin)
    if(TARGET ${visona_ui_target})
        target_sources(${visona_ui_target} PRIVATE ${visona_ui_sources})
        target_include_directories(${visona_ui_target} PRIVATE "${PROJECT_SOURCE_DIR}")
    endif()
endforeach()

option(VISONA_BUILD_UI_CHECKS "Build offline analysis UI checks and screenshot renderers" OFF)
if(VISONA_BUILD_UI_CHECKS)
    juce_add_console_app(visona_ui_checks PRODUCT_NAME "Visona UI checks")
    target_sources(visona_ui_checks PRIVATE ../tests/SpectrumUiChecks.cpp ../tests/StereoUiChecks.cpp ../tests/AnalysisSelectorUiChecks.cpp ${visona_ui_sources})
    target_include_directories(visona_ui_checks PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(visona_ui_checks PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(visona_ui_checks PRIVATE visona::core juce::juce_audio_utils
        juce::juce_gui_basics juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
