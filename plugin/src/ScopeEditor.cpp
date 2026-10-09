#include "ScopeEditor.h"

#include "ui/ChromeLayout.h"
#include "ui/GainControl.h"
#include "ui/Palette.h"
#include "ui/SyncText.h"

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
constexpr int panelWidth = 360;

juce::String formatSampleRate(double sampleRate)
{
    const auto kilohertz = sampleRate / 1000.0;
    const bool whole = std::abs(kilohertz - std::round(kilohertz)) < 1.0e-9;
    return juce::String(kilohertz, whole ? 0 : 1) + " kHz";
}

} // namespace

ScopeEditor::ScopeEditor(PluginProcessor& processor)
    : processor_(processor)
    , analysis_(processor.analysis())
    , layout_(processor.layout())
    , state_(processor.instanceState())
    , defaults_(processor.globalDefaults())
    , scope_(analysis_.snapshots(), layout_)
    , window_(state_.window)
    , peaks_(layout_.totalChannelCount(), 0.0f)
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
    addChildComponent(appearancePanel_);
    appearancePanel_.addAndMakeVisible(appearanceTitle_);
    appearancePanel_.addAndMakeVisible(appearanceClose_);
    appearancePanel_.addAndMakeVisible(colourLabel_);
    appearancePanel_.addAndMakeVisible(colourSwatches_);

    appearanceTitle_.setText("Appearance", juce::dontSendNotification);
    appearanceTitle_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    appearanceClose_.setButtonText("Close");
    colourLabel_.setText("Waveform colour", juce::dontSendNotification);
    appearancePanel_.setOpaque(true);
    appearancePanel_.setVisible(false);

    controlBar_.window().onWindowChange = [this](std::size_t window) { setWindow(window); };
    controlBar_.gain().onGainChange = [this](int gainDb) { setGainDb(gainDb); };
    controlBar_.gain().onAutoChange = [this](bool isOn) { setAutoGain(isOn); };
    controlBar_.waveform().onModeChange = [this](WaveformMode mode) { setWaveformMode(mode); };
    controlBar_.setPauseVisible(false);
    controlBar_.setTempoVisible(false);
    controlBar_.setFullScreenVisible(false);
    controlBar_.onSettings = [this] { showAppearance(!appearancePanel_.isVisible()); };
    controlBar_.onDiagnostics = [this] { showDiagnostics(!diagnostics_.isVisible()); };

    scope_.onTransportChange = [this]
    {
        zoomOverview_.setTimeline(scope_.snapshot());
        updateStatus();
    };
    scope_.onZoomChange = [this] { updateZoom(); };
    scope_.onFrame = [this]
    {
        if (zoomOverview_.isVisible())
            zoomOverview_.setHead(scope_.headPosition());
        followAutoGain();
    };
    zoomOverview_.onReset = [this] { scope_.resetZoom(); };
    zoomOverview_.onPan = [this](SweepZoom zoom) { scope_.setZoom(zoom); };

    appearanceClose_.onClick = [this] { showAppearance(false); };
    colourSwatches_.onColourChange = [this](std::size_t index) { setWaveformColour(index); };

    auto& autoGain = processor_.autoGain();
    if (state_.autoGain && !processor_.autoGainStarted)
        autoGain.reset(state_.gainDb);
    processor_.autoGainStarted = processor_.autoGainStarted || state_.autoGain;
    controlBar_.gain().setAuto(state_.autoGain);
    showGainDb(shownGainDb());
    controlBar_.window().setWindow(state_.window);
    controlBar_.waveform().setMode(state_.waveformMode);
    scope_.setWaveformMode(state_.waveformMode);
    analysis_.setBandSplitting(state_.waveformMode == WaveformMode::dj);
    analysis_.setWindow(state_.window);
    colourSwatches_.setColour(state_.waveformColour);
    scope_.setWaveformColour(palette::waveformColours[state_.waveformColour].colour);

    controlBar_.setToggles(false, false, false);
    setWantsKeyboardFocus(true);
    startTimerHz(idleRefreshHz);
}

ScopeEditor::~ScopeEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void ScopeEditor::AppearancePanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
}

void ScopeEditor::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
}

void ScopeEditor::resized()
{
    const auto step = chromeStepFor(getWidth(), getHeight());
    statusBar_.setStep(step);
    controlBar_.setStep(step);
    controlBar_.setToggles(diagnostics_.isVisible(), false, appearancePanel_.isVisible());

    const auto statusHeight = statusBar_.preferredHeight();
    const auto controlHeight = controlBar_.preferredHeight(getWidth());
    auto area = getLocalBounds();
    statusBar_.setBounds(area.removeFromTop(statusHeight));
    if (zoomOverview_.isVisible())
        zoomOverview_.setBounds(area.removeFromTop(zoomOverview_.preferredHeight()));
    controlBar_.setBounds(area.removeFromBottom(controlHeight));
    scope_.setBounds(area);

    const auto bannerBounds =
        scope_.getBounds().withSizeKeepingCentre(560, 76).translated(0, -scope_.getHeight() / 6);
    banner_.setBounds(bannerBounds);
    diagnostics_.setBounds(scope_.getBounds().reduced(margin));

    const auto panelHeight = 160;
    appearancePanel_.setBounds(getWidth() - panelWidth - margin, margin + statusHeight, panelWidth,
                               panelHeight);
    auto panelArea = appearancePanel_.getLocalBounds().reduced(margin);
    appearanceTitle_.setBounds(panelArea.removeFromTop(28));
    appearanceClose_.setBounds(panelArea.removeFromTop(28).removeFromRight(72));
    colourLabel_.setBounds(panelArea.removeFromTop(24));
    colourSwatches_.setBounds(panelArea.removeFromTop(48));
}

bool ScopeEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (appearancePanel_.isVisible())
        {
            showAppearance(false);
            return true;
        }
        if (scope_.zoom().isZoomed())
        {
            scope_.resetZoom();
            return true;
        }
    }
    if (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D')
    {
        showDiagnostics(!diagnostics_.isVisible());
        return true;
    }
    return false;
}

void ScopeEditor::timerCallback()
{
    for (std::size_t channel = 0; channel < peaks_.size(); ++channel)
        peaks_[channel] = analysis_.takePeak(channel);

    updateStatus();
    updateZoom();
    if (diagnostics_.isVisible())
        updateDiagnostics();
}

void ScopeEditor::setGainDb(int gainDb)
{
    if (state_.autoGain)
        setAutoGain(false);
    gainDb = DisplayGain::clampDb(gainDb);
    if (gainDb == state_.gainDb)
        return;
    state_.gainDb = gainDb;
    showGainDb(gainDb);
    notifyStateChange();
    updateStatus();
}

int ScopeEditor::shownGainDb()
{
    return state_.autoGain ? processor_.autoGain().gainDb() : state_.gainDb;
}

void ScopeEditor::showGainDb(int gainDb)
{
    scope_.setGainDb(gainDb);
    controlBar_.gain().setGainDb(gainDb);
}

void ScopeEditor::setAutoGain(bool isOn)
{
    if (isOn == state_.autoGain)
        return;
    auto& autoGain = processor_.autoGain();
    if (isOn)
    {
        autoGain.reset(state_.gainDb);
        processor_.autoGainStarted = true;
    }
    else
    {
        // The gain stays where auto gain left it.
        state_.gainDb = autoGain.gainDb();
    }
    state_.autoGain = isOn;
    controlBar_.gain().setAuto(isOn);
    showGainDb(shownGainDb());
    notifyStateChange();
    updateStatus();
}

void ScopeEditor::followAutoGain()
{
    if (state_.autoGain && processor_.autoGain().follow(scope_.snapshot().barPeaks))
    {
        showGainDb(processor_.autoGain().gainDb());
        updateStatus();
    }
}

void ScopeEditor::setWindow(std::size_t window)
{
    window = std::min(window, sweepWindowBars.size() - 1);
    window_ = window;
    state_.window = window;
    scope_.resetZoom();
    analysis_.setWindow(window);
    controlBar_.window().setWindow(window);
    notifyStateChange();
    updateStatus();
}

void ScopeEditor::setWaveformMode(WaveformMode mode)
{
    scope_.setWaveformMode(mode);
    controlBar_.waveform().setMode(mode);
    analysis_.setBandSplitting(mode == WaveformMode::dj);
    state_.waveformMode = mode;
    state_.hasInstanceAppearance = true;
    notifyStateChange();
}

void ScopeEditor::setWaveformColour(std::size_t index)
{
    index = std::min(index, palette::waveformColours.size() - 1);
    scope_.setWaveformColour(palette::waveformColours[index].colour);
    colourSwatches_.setColour(index);
    state_.waveformColour = index;
    state_.hasInstanceAppearance = true;
    notifyStateChange();
}

void ScopeEditor::showAppearance(bool shouldShow)
{
    appearancePanel_.setVisible(shouldShow);
    controlBar_.setToggles(diagnostics_.isVisible(), false, shouldShow);
    if (!shouldShow)
        grabKeyboardFocus();
    resized();
}

void ScopeEditor::showDiagnostics(bool shouldShow)
{
    diagnostics_.setVisible(shouldShow);
    startTimerHz(shouldShow ? diagnosticsRefreshHz : idleRefreshHz);
    updateDiagnostics();
    resized();
}

void ScopeEditor::updateStatus()
{
    const auto& snapshot = scope_.snapshot();

    StatusBar::Values values;
    if (snapshot.bpm > 0.0)
        values.bpm = juce::String(snapshot.bpm, 1) + " BPM";
    switch (snapshot.transportState)
    {
    case TransportState::running:
        values.state = "HOST RUN";
        break;
    case TransportState::stopped:
        values.state = "STOPPED";
        break;
    case TransportState::freeRunning:
    case TransportState::clockLost:
    default:
        values.state = "HOST";
        break;
    }

    switch (processor_.sidechainSyncState())
    {
    case SidechainSyncState::locked:
    {
        const auto frames = processor_.sidechainSyncOffsetFrames();
        const auto offset = syncText::offset(frames, snapshot.sampleRate);
        values.sidechainSync = "SC " + offset;
        values.sidechainSyncTooltip =
            "Visona Sync on the sidechain: the audio here arrives " + offset + " (" +
            juce::String(juce::roundToInt(frames)) +
            " samples) after the host playhead, so Visona draws it that much earlier.";
        break;
    }
    case SidechainSyncState::waiting:
        values.sidechainSync = "SC ...";
        values.sidechainSyncTooltip = "The sidechain input is on. Waiting for a bar impulse from "
                                      "Visona Sync while the host plays.";
        break;
    case SidechainSyncState::off:
        break;
    }

    if (snapshot.hasStream && snapshot.sampleRate > 0.0)
        values.sampleRate = formatSampleRate(snapshot.sampleRate);
    values.window = WindowControl::describe(window_);
    values.gain = GainControl::format(shownGainDb(), state_.autoGain);
    if (scope_.zoom().isZoomed())
        values.zoom = ZoomOverview::describe(scope_.zoom(), snapshot);
    statusBar_.setValues(values);
    banner_.setVisible(false);
}

void ScopeEditor::updateZoom()
{
    const auto zoomed = scope_.zoom().isZoomed();
    zoomOverview_.setVisible(zoomed);
    if (zoomed)
    {
        zoomOverview_.setTimeline(scope_.snapshot());
        zoomOverview_.setHead(scope_.headPosition());
        zoomOverview_.setZoom(scope_.zoom());
    }
    resized();
}

void ScopeEditor::updateDiagnostics()
{
    static double lastUpdateSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    static std::uint64_t lastAnalysisBusyNs = 0;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = std::max(now - lastUpdateSeconds, 1.0e-3);
    lastUpdateSeconds = now;

    const auto busyNs = analysis_.busyNanoseconds();
    const auto analysisLoad = static_cast<double>(busyNs - lastAnalysisBusyNs) * 1.0e-9 / elapsed;
    lastAnalysisBusyNs = busyNs;

    const auto& snapshot = scope_.snapshot();
    DiagnosticsOverlay::Values values;
    values.running = snapshot.hasStream;
    values.device = "Host track";
    values.sampleRate = snapshot.sampleRate;
    for (std::size_t channel = 0; channel < peaks_.size(); ++channel)
        values.channels.push_back({channel == 0 ? "L" : "R", {}, peaks_[channel]});
    values.overruns = snapshot.overruns;
    values.droppedFrames = snapshot.droppedFrames;
    values.analysisLoad = analysisLoad;
    values.rendering = scope_.stats();
    values.transportState = snapshot.transportState;
    values.nextTick = snapshot.nextTick;
    values.timeSignature = snapshot.timeSignature;
    values.bpm = snapshot.bpm;
    values.blockSize = processor_.lastBlockSize();
    if (snapshot.sampleRate > 0.0)
        values.streamSeconds = static_cast<double>(snapshot.nextSampleIndex) / snapshot.sampleRate;

    const auto sidechainPeak = processor_.takeSidechainPeak();
    switch (processor_.sidechainSyncState())
    {
    case SidechainSyncState::off:
        values.sidechainSync = "off: no sidechain input";
        break;
    case SidechainSyncState::waiting:
        values.sidechainSync = "waiting, sidechain at " + syncText::peak(sidechainPeak);
        break;
    case SidechainSyncState::locked:
        values.sidechainSync =
            syncText::offset(processor_.sidechainSyncOffsetFrames(), snapshot.sampleRate) + " (" +
            juce::String(juce::roundToInt(processor_.sidechainSyncOffsetFrames())) +
            " frames), impulse " + syncText::peak(processor_.sidechainImpulsePeak());
        break;
    }
    diagnostics_.update(values, elapsed);
}

void ScopeEditor::notifyStateChange()
{
    if (onStateChange)
        onStateChange(state_);
}

} // namespace visona
