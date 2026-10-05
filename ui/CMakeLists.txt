# JUCE components compiled into each binary target (app and/or plugin), because each binary
# must compile the JUCE modules exactly once.

set(visona_ui_sources
    AudioSettings.h
    Banner.cpp
    Banner.h
    ChannelNames.cpp
    ChannelNames.h
    ChromeButton.cpp
    ChromeButton.h
    ChromeLayout.h
    ChoiceControl.cpp
    ChoiceControl.h
    ColourSwatches.cpp
    ColourSwatches.h
    ControlBar.cpp
    ControlBar.h
    ControlText.h
    DiagnosticsOverlay.cpp
    DiagnosticsOverlay.h
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
    TempoControl.cpp
    TempoControl.h
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
