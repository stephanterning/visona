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
#include <visona/SweepWindow.h>

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

} // namespace

MainComponent::MainComponent(AudioEngine& engine, Settings& settings, bool kioskMode)
    : engine_(engine)
    , settings_(settings)
    , kioskMode_(kioskMode)
    , scope_(engine.snapshots(), engine.layout())
    , settingsPanel_(engine)
    , window_(defaultSweepWindow)
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
    addChildComponent(zoomOverview_);
    addAndMakeVisible(scope_);
    addAndMakeVisible(controlBar_);
    addChildComponent(banner_);
    addChildComponent(diagnostics_);
    addChildComponent(settingsPanel_);

    controlBar_.window().onWindowChange = [this](std::size_t window) { setWindow(window); };
    controlBar_.tempo().onTempoChange = [this](double bpm) { setFreeTempo(bpm); };
    controlBar_.gain().onGainChange = [this](int gainDb) { setGainDb(gainDb); };
    statusBar_.onStateClick = [this] { runFree(); };
    controlBar_.waveform().onModeChange = [this](WaveformMode mode) { setWaveformMode(mode); };
    settingsPanel_.onWaveformColourChange = [this](std::size_t index) { setWaveformColour(index); };
    scope_.onTransportChange = [this]
    {
        zoomOverview_.setTimeline(scope_.snapshot());
        updateBanner();
        updateStatus();
    };
    scope_.onZoomChange = [this] { updateZoom(); };
    scope_.onFrame = [this]
    {
        if (zoomOverview_.isVisible())
            zoomOverview_.setHead(scope_.headPosition());
    };
    zoomOverview_.onReset = [this] { scope_.resetZoom(); };
    zoomOverview_.onPan = [this](SweepZoom zoom) { scope_.setZoom(zoom); };
    controlBar_.onDiagnostics = [this] { showDiagnostics(!diagnostics_.isVisible()); };
    controlBar_.onFullScreen = [this] { toggleFullScreen(); };
    controlBar_.onSettings = [this] { showSettings(!settingsPanel_.isVisible()); };

    settingsPanel_.onClose = [this] { showSettings(false); };
    settingsPanel_.onPreferredHeightChanged = [this] { resized(); };
    settingsPanel_.onDiagnostics = [this] { showDiagnostics(!diagnostics_.isVisible()); };
    settingsPanel_.onFullScreen = [this] { toggleFullScreen(); };

    const auto mode = settings_.waveformMode();
    controlBar_.waveform().setMode(mode);
    scope_.setWaveformMode(mode);
    engine_.setBandSplitting(mode == WaveformMode::dj);
    const auto colour =
        settings_.waveformColour(palette::waveformColours.size(), palette::defaultWaveformColour);
    settingsPanel_.setWaveformColour(colour);
    scope_.setWaveformColour(palette::waveformColours[colour].colour);

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
    zoomOverview_.setStep(step);
    controlBar_.setStep(step);
    settingsPanel_.setViewControlsVisible(!controlBar_.showsSecondaryControls());

    statusBar_.setBounds(area.removeFromTop(statusBar_.preferredHeight()));
    if (zoomOverview_.isVisible())
        zoomOverview_.setBounds(area.removeFromTop(zoomOverview_.preferredHeight()));
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
    if (key == juce::KeyPress::escapeKey && scope_.zoom().isZoomed())
    {
        scope_.resetZoom();
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
    if (character >= '1' && character < static_cast<juce::juce_wchar>('1' + sweepWindowBars.size()))
    {
        setWindow(static_cast<std::size_t>(character - '1'));
        return true;
    }
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
    if (character == 'w')
    {
        const auto next = (static_cast<int>(scope_.waveformMode()) + 1) % 3;
        setWaveformMode(static_cast<WaveformMode>(next));
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    updateDeviceInfo();
}

void MainComponent::timerCallback()
{
    updateToggles();
    updateStatus();
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

void MainComponent::setWindow(std::size_t window)
{
    window = std::min(window, sweepWindowBars.size() - 1);
    window_ = window;
    scope_.resetZoom();
    engine_.setWindow(window);
    controlBar_.window().setWindow(window);
    updateStatus();
}

void MainComponent::setFreeTempo(double bpm)
{
    engine_.setFreeTempo(bpm);
    updateStatus();
}

void MainComponent::runFree()
{
    // At the tempo MIDI Clock last had, if it is known.
    const auto& snapshot = scope_.snapshot();
    if (snapshot.transportState != TransportState::freeRunning && snapshot.bpm > 0.0)
        engine_.setFreeTempo(snapshot.bpm);
    engine_.runFree();
    updateStatus();
}

void MainComponent::setWaveformMode(WaveformMode mode)
{
    scope_.setWaveformMode(mode);
    controlBar_.waveform().setMode(mode);
    engine_.setBandSplitting(mode == WaveformMode::dj);
    settings_.setWaveformMode(mode);
}

void MainComponent::setWaveformColour(std::size_t index)
{
    index = std::min(index, palette::waveformColours.size() - 1);
    scope_.setWaveformColour(palette::waveformColours[index].colour);
    settingsPanel_.setWaveformColour(index);
    settings_.setWaveformColour(index);
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
    idleCursorHider_.setActive(fullScreen || kioskMode_);
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
    updateBanner();
    updateStatus();
}

void MainComponent::updateBanner()
{
    if (!inputRunning_)
        banner_.setText("NO AUDIO INPUT",
                        engine_.noInputReason() + " Choose a device in Settings.");
    else if (scope_.snapshot().transportState == TransportState::clockLost)
        banner_.setText("MIDI CLOCK LOST",
                        "No MIDI Clock for over half a second. The view is frozen until it "
                        "returns, or click MIDI CLOCK LOST above to run free.");
    banner_.setVisible(!inputRunning_ ||
                       scope_.snapshot().transportState == TransportState::clockLost);
}

void MainComponent::updateStatus()
{
    const auto& snapshot = scope_.snapshot();
    const bool free = snapshot.transportState == TransportState::freeRunning;
    // While running free, the tempo just set, before the analysis has taken it.
    const auto bpm = free ? engine_.freeTempo() : snapshot.bpm;
    controlBar_.tempo().setEditable(free);
    controlBar_.tempo().setBpm(bpm);

    StatusBar::Values values;
    if (bpm > 0.0)
        values.bpm = juce::String(bpm, 1) + " BPM";
    if (!inputRunning_)
    {
        values.state = "NO INPUT";
        values.stateIsError = true;
    }
    else
    {
        switch (snapshot.transportState)
        {
        case TransportState::freeRunning:
            values.state = "FREE";
            break;
        case TransportState::running:
            values.state = "MIDI RUN";
            break;
        case TransportState::stopped:
            values.state = "STOPPED";
            break;
        case TransportState::clockLost:
            values.state = "MIDI CLOCK LOST";
            values.stateIsError = true;
            break;
        }
        // Stopped or without a clock, a click runs the sweep free (D-090).
        values.stateIsAction = snapshot.transportState == TransportState::stopped ||
                               snapshot.transportState == TransportState::clockLost;
        if (values.stateIsAction)
            values.stateTooltip =
                "Run free at " +
                juce::String(snapshot.bpm > 0.0 ? clampFreeBpm(snapshot.bpm) : engine_.freeTempo(),
                             1) +
                " BPM until MIDI Clock starts again";
    }
    if (inputRunning_ && sampleRate_ > 0.0)
        values.sampleRate = formatSampleRate(sampleRate_);
    values.window = WindowControl::describe(window_);
    values.gain = GainControl::format(gainDb_);
    if (scope_.zoom().isZoomed())
        values.zoom = ZoomOverview::describe(scope_.zoom(), snapshot);
    statusBar_.setValues(values);
}

void MainComponent::updateZoom()
{
    const auto zoom = scope_.zoom();
    zoomOverview_.setZoom(zoom);
    if (zoom.isZoomed() != zoomOverview_.isVisible())
    {
        zoomOverview_.setTimeline(scope_.snapshot());
        zoomOverview_.setHead(scope_.headPosition());
        zoomOverview_.setVisible(zoom.isZoomed());
        resized();
    }
    updateStatus();
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

    const auto& snapshot = scope_.snapshot();
    const auto& midi = engine_.midi();
    values.midiInput =
        midi.isOpen() || midi.name().isEmpty() ? midi.name() : midi.name() + " (not found)";
    values.transportState = snapshot.transportState;
    values.nextTick = snapshot.nextTick;
    values.timeSignature = snapshot.timeSignature;
    values.bpm = snapshot.bpm;
    values.midiEvents = snapshot.midiEvents;
    values.midiDrops = midi.droppedEvents();
    values.ignoredSpp = snapshot.ignoredSpp;
    values.midiOffsetFrames = engine_.midiOffsetFrames();

    diagnostics_.update(values, elapsed);
}

} // namespace visona
