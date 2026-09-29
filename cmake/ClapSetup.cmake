include(FetchContent)

# JUCE 9.0.2 has no CLAP client, so the CLAP plugins are built with clap-juce-extensions (D-098).
# Pinned to a commit on main after its JUCE 9 support (#178) and the JUCE 9 editor scale fix (#183).
FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(clap_juce_extensions)
