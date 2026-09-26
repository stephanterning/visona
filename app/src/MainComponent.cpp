#include "MainComponent.h"

#include "AudioEngine.h"
#include "ProcessCpu.h"
#include "RealtimeAllocationCheck.h"
#include "Settings.h"
#include "ui/ChannelNames.h"
#include "ui/ChromeLayout.h"
#include "ui/GainControl.h"
#include "ui/Palette.h"

#include <visona/LaneMapping.h>
#include <visona/SweepAnalyzer.h>

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

constexpr int diagnosticsRefreshHz = 15;
constexpr int idleRefreshHz = 4;
constexpr int margin = 12;
constexpr int bannerWidth = 560;
constexpr int bannerHeight = 76;
constexpr int settingsWidth = 480;

double nowSeconds()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

juce::String formatSampleRate(double sampleRate)
{
    const auto kilohertz = sampleRate / 1000.0;
    const bool whole = std::abs(kilohertz - std::round(kilohertz)) < 1.0e-9;
    return juce::String(kilohertz, whole ? 0 : 1) + " kHz";
}

juce::String formatSeconds(double seconds)
{
    return (seconds >= 1.0 ? juce::String(juce::roundToInt(seconds))
                           : juce::String(seconds, 3).trimCharactersAtEnd("0")) +
           " s";
}

int defaultWindowIndex()
{
    const auto& windows = SettingsPanel::debugWindows();
    const auto found = std::find(windows.begin(), windows.end(), freeRunningWindowSeconds);
    return static_cast<int>(std::distance(windows.begin(), found));
}

} // namespace

MainComponent::MainComponent(AudioEngine& engine, Settings& settings)
    : engine_(engine)
    , settings_(settings)
    , scope_(engine.snapshots(), engine.layout())
    , settingsPanel_(engine)
    , peaks_(engine.layout().totalChannelCount(), 0.0f)
    , lastUpdateSeconds_(nowSeconds())
    , lastAnalysisBusyNs_(engine.analysisBusyNanoseconds())
    , lastCpuSeconds_(processCpuSeconds())
    , allocationCheckWorks_(RealtimeAllocationCheck::selfTest())
{
    setOpaque(true);
    lookAndFeel_.setColourScheme(palette::widgetColours());
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(statusBar_);
    addAndMakeVisible(scope_);
    addAndMakeVisible(controlBar_);
    addChildComponent(banner_);
    addChildComponent(diagnostics_);
    addChildComponent(settingsPanel_);

    controlBar_.gain().onGainChange = [this](int gainDb) { setGainDb(gainDb); };
    controlBar_.onColoring = [this](WaveformColoring coloring) { setColoring(coloring); };
    controlBar_.onDiagnostics = [this] { showDiagnostics(!diagnostics_.isVisible()); };
    controlBar_.onFullScreen = [this] { toggleFullScreen(); };
    controlBar_.onSettings = [this] { showSettings(!settingsPanel_.isVisible()); };

    settingsPanel_.onClose = [this] { showSettings(false); };
    settingsPanel_.onPreferredHeightChanged = [this] { resized(); };
    settingsPanel_.onDiagnostics = [this] { showDiagnostics(!diagnostics_.isVisible()); };
    settingsPanel_.onFullScreen = [this] { toggleFullScreen(); };
    settingsPanel_.onDebugChange = [this](const SettingsPanel::DebugValues& values)
    { setDebugValues(values); };

    coloring_ = settings_.waveformColoring();
    colouredMethod_ =
        coloring_ == WaveformColoring::precise ? WaveformColoring::layered : coloring_;
    scope_.setColoring(coloring_);
    controlBar_.setColoring(coloring_);
    debug_.window = defaultWindowIndex();
    settingsPanel_.setDebugValues(debug_);

    setWantsKeyboardFocus(true);
    setSize(1280, 720);

    engine_.deviceManager().addChangeListener(this);
    updateDeviceInfo();
    startTimerHz(idleRefreshHz);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine_.deviceManager().removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    const auto step = chromeStepFor(getWidth(), getHeight());
    statusBar_.setStep(step);
    controlBar_.setStep(step);
    settingsPanel_.setViewControlsVisible(!controlBar_.showsSecondaryControls());

    statusBar_.setBounds(area.removeFromTop(statusBar_.preferredHeight()));
    controlBar_.setBounds(area.removeFromBottom(controlBar_.preferredHeight(area.getWidth())));
    scope_.setBounds(area);

    banner_.setBounds(
        juce::Rectangle<int>(std::min(bannerWidth, area.getWidth() - 2 * margin), bannerHeight)
            .withCentre({area.getCentreX(), area.getY() + margin + bannerHeight / 2}));

    const auto overlay = diagnostics_.preferredSize();
    const auto overlayWidth = std::min(overlay.getWidth(), area.getWidth() - 2 * margin);
    const auto overlayHeight = std::min(overlay.getHeight(), area.getHeight() - 2 * margin);
    diagnostics_.setBounds(area.getRight() - margin - overlayWidth, area.getY() + margin,
                           overlayWidth, overlayHeight);

    const auto panelHeight = std::min(settingsPanel_.preferredHeight(), getHeight() - 2 * margin);
    settingsPanel_.setBounds(
        juce::Rectangle<int>(std::min(settingsWidth, getWidth() - 2 * margin), panelHeight)
            .withCentre(getLocalBounds().getCentre()));

    // Entering or leaving full screen resizes the window.
    updateToggles();
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && settingsPanel_.isVisible())
    {
        showSettings(false);
        return true;
    }
    if (key == juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0))
    {
        showSettings(!settingsPanel_.isVisible());
        return true;
    }

    const auto modifiers = key.getModifiers();
    if (modifiers.isCommandDown() || modifiers.isCtrlDown() || modifiers.isAltDown())
        return false;

    const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::numberPadAdd) ||
        character == '+' || character == '=')
    {
        setGainDb(gainDb_ + 1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::downKey) ||
        key.isKeyCode(juce::KeyPress::numberPadSubtract) || character == '-')
    {
        setGainDb(gainDb_ - 1);
        return true;
    }
    if (character == 'f')
    {
        toggleFullScreen();
        return true;
    }
    if (character == 'd')
    {
        showDiagnostics(!diagnostics_.isVisible());
        return true;
    }
    if (character == 'm')
    {
        setColoring(coloring_ == WaveformColoring::precise ? colouredMethod_
                                                           : WaveformColoring::precise);
        return true;
    }
    if (character == 'c')
    {
        setColoring(coloring_ == WaveformColoring::blended ? WaveformColoring::layered
                                                           : WaveformColoring::blended);
        return true;
    }

    auto debug = debug_;
    const auto numWindows = static_cast<int>(SettingsPanel::debugWindows().size());
    // , and . also work on layouts where [ and ] need Option, which the check above turns away.
    if (character == '[' || character == ']' || character == ',' || character == '.')
        debug.window = std::clamp(debug.window + (character == ']' || character == '.' ? 1 : -1), 0,
                                  numWindows - 1);
    else if (character == 'b')
        debug.compensateBandDelay = !debug.compensateBandDelay;
    else if (character == 'g')
        debug.grid = (debug.grid + 1) % 3;
    else
        return false;
    setDebugValues(debug);
    settingsPanel_.setDebugValues(debug_);
    return true;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    updateDeviceInfo();
}

void MainComponent::timerCallback()
{
    updateToggles();
    updateDiagnostics();
}

void MainComponent::setGainDb(int gainDb)
{
    gainDb = DisplayGain::clampDb(gainDb);
    if (gainDb == gainDb_)
        return;
    gainDb_ = gainDb;
    scope_.setGainDb(gainDb_);
    controlBar_.gain().setGainDb(gainDb_);
    updateStatus();
}

void MainComponent::setColoring(WaveformColoring coloring)
{
    coloring_ = coloring;
    if (coloring != WaveformColoring::precise)
        colouredMethod_ = coloring;
    scope_.setColoring(coloring);
    controlBar_.setColoring(coloring);
    settings_.setWaveformColoring(coloring);
}

void MainComponent::setDebugValues(const SettingsPanel::DebugValues& values)
{
    const auto& windows = SettingsPanel::debugWindows();
    debug_ = values;
    debug_.window = std::clamp(values.window, 0, static_cast<int>(windows.size()) - 1);
    engine_.setWindowSeconds(windows[static_cast<std::size_t>(debug_.window)]);
    scope_.setBandDelayCompensation(debug_.compensateBandDelay);
    scope_.setDebugGrid(static_cast<ScopeView::DebugGrid>(std::clamp(debug_.grid, 0, 2)));
    updateStatus();
}

void MainComponent::showSettings(bool shouldShow)
{
    settingsPanel_.setVisible(shouldShow);
    updateToggles();
    if (!shouldShow)
        grabKeyboardFocus();
}

void MainComponent::showDiagnostics(bool shouldShow)
{
    diagnostics_.setVisible(shouldShow);
    startTimerHz(shouldShow ? diagnosticsRefreshHz : idleRefreshHz);
    updateDiagnostics();
    resized();
}

void MainComponent::toggleFullScreen()
{
    if (auto* const window = findParentComponentOfClass<juce::ResizableWindow>())
        window->setFullScreen(!window->isFullScreen());
}

bool MainComponent::isFullScreen() const
{
    const auto* const window = findParentComponentOfClass<juce::ResizableWindow>();
    return window != nullptr && window->isFullScreen();
}

void MainComponent::updateToggles()
{
    const auto fullScreen = isFullScreen();
    controlBar_.setToggles(diagnostics_.isVisible(), fullScreen, settingsPanel_.isVisible());
    settingsPanel_.setViewToggles(diagnostics_.isVisible(), fullScreen);
}

void MainComponent::updateDeviceInfo()
{
    auto* const device = engine_.deviceManager().getCurrentAudioDevice();
    deviceName_ = device != nullptr ? device->getName() : juce::String();
    sampleRate_ = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    bufferSize_ = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
    inputNames_ = device != nullptr ? device->getInputChannelNames() : juce::StringArray();
    inputRunning_ = engine_.isInputRunning();

    if (!inputRunning_)
        banner_.setText("NO AUDIO INPUT",
                        engine_.noInputReason() + " Choose a device in Settings.");
    banner_.setVisible(!inputRunning_);
    updateStatus();
}

void MainComponent::updateStatus()
{
    StatusBar::Values values;
    values.state = inputRunning_ ? "FREE RUN" : "NO INPUT";
    values.stateIsError = !inputRunning_;
    if (inputRunning_ && sampleRate_ > 0.0)
        values.sampleRate = formatSampleRate(sampleRate_);
    values.window = formatSeconds(
        SettingsPanel::debugWindows()[static_cast<std::size_t>(std::max(debug_.window, 0))]);
    values.gain = GainControl::format(gainDb_);
    statusBar_.setValues(values);
}

void MainComponent::updateDiagnostics()
{
    const auto now = nowSeconds();
    const auto elapsed = std::max(now - lastUpdateSeconds_, 1.0e-3);
    lastUpdateSeconds_ = now;

    const auto busyNs = engine_.analysisBusyNanoseconds();
    const auto analysisLoad = static_cast<double>(busyNs - lastAnalysisBusyNs_) * 1.0e-9 / elapsed;
    lastAnalysisBusyNs_ = busyNs;

    const auto cpuSeconds = processCpuSeconds();
    std::optional<double> processCpu;
    if (cpuSeconds.has_value() && lastCpuSeconds_.has_value())
        processCpu = (*cpuSeconds - *lastCpuSeconds_) / elapsed;
    lastCpuSeconds_ = cpuSeconds;

    std::optional<std::uint64_t> callbackAllocations;
    if constexpr (RealtimeAllocationCheck::enabled)
    {
        callbackAllocations = RealtimeAllocationCheck::allocationCount();

        // An allocation in the audio callback is a bug; stop here under a debugger.
        if (*callbackAllocations > 0 && !reportedCallbackAllocation_)
        {
            reportedCallbackAllocation_ = true;
            jassertfalse;
        }
    }

    if (!diagnostics_.isVisible())
        return;

    const auto stream = engine_.streamStatus();
    const auto& layout = engine_.layout();
    engine_.takePeaks(peaks_);

    DiagnosticsOverlay::Values values;
    values.running = stream.has_value();
    values.device = deviceName_;
    values.sampleRate = sampleRate_;
    values.bufferSize = bufferSize_;
    for (std::size_t channel = 0; channel < layout.totalChannelCount(); ++channel)
        values.channels.push_back({channelShortName(layout, channel),
                                   inputChannelName(inputNames_, engine_.inputChannel(channel)),
                                   peaks_[channel]});
    if (stream.has_value())
    {
        values.blockSize = stream->blockSize;
        values.overruns = stream->overruns;
        values.droppedFrames = stream->droppedFrames;
        values.streamSeconds =
            sampleRate_ > 0.0 ? static_cast<double>(stream->framesDelivered) / sampleRate_ : 0.0;
        values.deviceHostTime = stream->deviceHostTime;
    }
    values.deviceXruns = engine_.deviceManager().getXRunCount();
    values.callbackAllocations = callbackAllocations;
    values.allocationCheckWorks = allocationCheckWorks_;
    values.analysisLoad = analysisLoad;
    values.rendering = scope_.stats();
    values.processCpu = processCpu;

    diagnostics_.update(values, elapsed);
}

} // namespace visona
