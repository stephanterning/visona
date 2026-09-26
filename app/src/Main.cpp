#include "AudioEngine.h"
#include "MainComponent.h"
#include "Settings.h"
#include "ui/Palette.h"

#include <visona/Version.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace visona
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, AudioEngine& engine, Settings& settings)
        : DocumentWindow(name, palette::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(engine, settings), true);

        // On macOS, a resizable window with a maximise button gets native fullscreen
        // from the green title bar button.
        setResizable(true, false);
        setResizeLimits(minimumWidth, minimumHeight, maximumSize, maximumSize);

        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        getContentComponent()->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    static constexpr int minimumWidth = 320;
    static constexpr int minimumHeight = 200;
    static constexpr int maximumSize = 16384;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class VisonaApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return JUCE_APPLICATION_NAME_STRING;
    }

    const juce::String getApplicationVersion() override
    {
        const auto version = versionString();
        return juce::String(version.data(), version.size());
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String&) override
    {
        settings = std::make_unique<Settings>();
        audioEngine = std::make_unique<AudioEngine>(*settings);
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *audioEngine, *settings);

        // Opened after the window is up, so the system's microphone prompt appears over it.
        audioEngine->openSavedDevice();
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        audioEngine = nullptr;
        settings = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<Settings> settings;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace visona

START_JUCE_APPLICATION(visona::VisonaApplication)
