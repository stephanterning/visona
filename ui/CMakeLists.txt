# JUCE components. They are compiled into the app target rather than a library of their own,
# because each binary must compile the JUCE modules exactly once.

target_sources(Visona PRIVATE
    AudioSettings.h
    Banner.cpp
    Banner.h
    ChannelNames.cpp
    ChannelNames.h
    DebugReadout.cpp
    DebugReadout.h
    Palette.h
    SettingsPanel.cpp
    SettingsPanel.h)
