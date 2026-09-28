#include "AudioEngine.h"
#include "MainComponent.h"
#include "Settings.h"
#include "ui/Palette.h"

#include <visona/Version.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace visona
{

namespace
{

bool commandLineHasFlag(const juce::String& commandLine, const juce::String& flag)
{
    const juce::StringArray tokens =
        juce::StringArray::fromTokens(commandLine.trim(), " ", juce::String());
    return tokens.contains(flag);
}

} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, AudioEngine& engine, Settings& settings, bool kiosk)
        : DocumentWindow(name, palette::background,
                         kiosk ? 0 : juce::DocumentWindow::allButtons)
        , kiosk_(kiosk)
    {
        setUsingNativeTitleBar(false);
        if (kiosk)
        {
            setTitleBarHeight(0);
            setTitleBarButtonsRequired(0, false);
        }

        setContentOwned(new MainComponent(engine, settings), true);

        // On macOS, a resizable window with a maximise button gets native fullscreen
        // from the green title bar button.
        setResizable(!kiosk, false);
        setResizeLimits(minimumWidth, minimumHeight, maximumSize, maximumSize);

        if (!kiosk)
            centreWithSize(getWidth(), getHeight());

        setVisible(true);

        if (kiosk)
        {
            // JUCE's setFullScreen() on Linux uses the desktop work area (below the panel).
            // Cover the whole display, including the Pi taskbar, with a borderless window.
            juce::MessageManager::callAsync([this]
            {
                if (auto* const display =
                        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                    setBounds(display->logicalBounds.toNearestInt());
                setAlwaysOnTop(true);
                toFront(true);
            });
        }

        getContentComponent()->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    const bool kiosk_;
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

    void initialise(const juce::String& commandLine) override
    {
        kiosk_ = commandLineHasFlag(commandLine, "--kiosk") ||
                 commandLineHasFlag(commandLine, "-k");

        settings = std::make_unique<Settings>();
        audioEngine = std::make_unique<AudioEngine>(*settings);
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *audioEngine, *settings,
                                                  kiosk_);

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
    bool kiosk_ = false;
    std::unique_ptr<Settings> settings;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace visona

START_JUCE_APPLICATION(visona::VisonaApplication)
