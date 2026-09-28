include(FetchContent)

FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.2.tar.gz
    URL_HASH SHA256=16d01c27e8327f3644306cc5c223aa6e21af1cc1ec62faa709fc0dced57d208a)
FetchContent_MakeAvailable(JUCE)
