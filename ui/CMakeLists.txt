# JUCE components. They are compiled into the app target rather than a library of their own,
# because each binary must compile the JUCE modules exactly once.

target_sources(Visona PRIVATE
    AudioSettings.h
    Banner.cpp
    Banner.h
    ChannelNames.cpp
    ChannelNames.h
    ChromeButton.cpp
    ChromeButton.h
    ChromeLayout.h
    ControlBar.cpp
    ControlBar.h
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
    StatusBar.h)
