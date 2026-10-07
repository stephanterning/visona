#include "AudioEngine.h"
#include "MainComponent.h"
#include "Settings.h"
#include "ui/Palette.h"

#include <visona/Version.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

#if JUCE_MAC
#include "MacWindow.h"
#endif
#if JUCE_LINUX
#include "X11Display.h"
#endif

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

/**
    In kiosk mode the window covers its display, and follows it when displays are connected,
    disconnected or change mode, so a display switched on after Visona has started is filled too
    (D-103). On Linux, where JUCE does not notice display changes, the X screen size is polled, and
    the same applies to full screen chosen with F.
*/
class MainWindow final : public juce::DocumentWindow,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    MainWindow(const juce::String& name, AudioEngine& engine, Settings& settings, bool kiosk)
        : DocumentWindow(name, palette::background, kiosk ? 0 : juce::DocumentWindow::allButtons)
        , kiosk_(kiosk)
    {
        if (kiosk)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setTitleBarButtonsRequired(0, false);
        }
        else
        {
#if JUCE_MAC
            setUsingNativeTitleBar(true);
#else
            setUsingNativeTitleBar(false);
#endif
        }

        setContentOwned(new MainComponent(engine, settings, kiosk_), true);

        // On macOS the native title bar is the traffic lights, and a resizable window with a
        // maximise button gets native fullscreen from the green button.
        setResizable(!kiosk, false);
        setResizeLimits(minimumWidth, minimumHeight, maximumSize, maximumSize);

        if (!kiosk)
            centreWithSize(getWidth(), getHeight());

        setVisible(true);
#if JUCE_MAC
        if (!kiosk)
            applyMacWindowChrome(*this);
#endif

        if (kiosk)
            triggerAsyncUpdate();
#if JUCE_LINUX
        screenBounds_ = x11::screenBounds();
        startTimer(displayPollMs);
#endif

        getContentComponent()->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
        stopTimer();
        cancelPendingUpdate();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void parentSizeChanged() override
    {
        DocumentWindow::parentSizeChanged();
        triggerAsyncUpdate();
    }

    void resized() override
    {
        DocumentWindow::resized();
#if JUCE_LINUX
        // F in kiosk mode goes back to the window manager's full screen.
        const bool fullScreen = isFullScreen();
        if (kiosk_ && fullScreen && !wasFullScreen_)
            triggerAsyncUpdate();
        wasFullScreen_ = fullScreen;
#endif
    }

private:
    void handleAsyncUpdate() override
    {
        fillDisplay();
    }

    void timerCallback() override
    {
#if JUCE_LINUX
        const auto bounds = x11::screenBounds();
        if (bounds == screenBounds_)
            return;
        screenBounds_ = bounds;
        x11::refreshDisplays();
        fillDisplay();
#endif
    }

    void fillDisplay()
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getDisplayForRect(getScreenBounds());
        if (display != nullptr &&
            !display->logicalBounds.toNearestInt().intersects(getScreenBounds()))
            display = displays.getPrimaryDisplay();
        // Without a display the window is filled when one appears.
        if (display == nullptr)
            return;

#if JUCE_LINUX
        if (!kiosk_ && !isFullScreen())
            return;
        // Full screen chosen with F keeps the desktop panel visible, as JUCE's setFullScreen()
        // does.
        const auto area = (kiosk_ ? display->logicalBounds : display->userBounds).toNearestInt();
        if (!isFullScreen())
            setFullScreen(true);
        // setBounds() would leave full screen.
        if (auto* const peer = getPeer(); peer != nullptr && getScreenBounds() != area)
            peer->setBounds(area, true);
        // Raspberry Pi OS (labwc) hides wf-panel-pi for a fullscreen window, and keeps it covering
        // its display.
        if (kiosk_)
            x11::requestFullScreen(*this);
#else
        if (!kiosk_)
            return;
        // Cover the whole display with a borderless window (macOS menu bar excluded).
        const auto area = display->logicalBounds.toNearestInt();
        if (getScreenBounds() != area)
            setBounds(area);
        setAlwaysOnTop(true);
        toFront(true);
#endif
    }

    const bool kiosk_;
    static constexpr int minimumWidth = 320;
    static constexpr int minimumHeight = 200;
    static constexpr int maximumSize = 16384;
#if JUCE_LINUX
    static constexpr int displayPollMs = 500;
    std::optional<juce::Rectangle<int>> screenBounds_;
    bool wasFullScreen_ = false;
#endif

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
        kiosk_ =
            commandLineHasFlag(commandLine, "--kiosk") || commandLineHasFlag(commandLine, "-k");

        settings = std::make_unique<Settings>();
        audioEngine = std::make_unique<AudioEngine>(*settings);
        mainWindow =
            std::make_unique<MainWindow>(getApplicationName(), *audioEngine, *settings, kiosk_);

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
