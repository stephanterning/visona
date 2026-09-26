#include "MainComponent.h"

#include "AudioEngine.h"
#include "RealtimeAllocationCheck.h"
#include "ui/ChannelNames.h"
#include "ui/Palette.h"

#include <algorithm>

namespace visona
{

namespace
{

constexpr int readoutRefreshHz = 30;
constexpr int buttonWidth = 110;
constexpr int buttonHeight = 44;
constexpr int margin = 16;
constexpr int bannerWidth = 560;
constexpr int bannerHeight = 76;
constexpr int settingsWidth = 480;

double nowSeconds()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

} // namespace

MainComponent::MainComponent(AudioEngine& engine)
    : engine_(engine)
    , settingsPanel_(engine)
    , peaks_(engine.layout().totalChannelCount(), 0.0f)
    , lastUpdateSeconds_(nowSeconds())
    , allocationCheckWorks_(RealtimeAllocationCheck::selfTest())
{
    setOpaque(true);
    lookAndFeel_.setColourScheme(palette::widgetColours());
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(readout_);
    addChildComponent(banner_);

    settingsButton_.setButtonText("Settings");
    settingsButton_.onClick = [this] { showSettings(!settingsPanel_.isVisible()); };
    addAndMakeVisible(settingsButton_);

    settingsPanel_.onClose = [this] { showSettings(false); };
    settingsPanel_.onPreferredHeightChanged = [this] { resized(); };
    addChildComponent(settingsPanel_);

    setWantsKeyboardFocus(true);
    setSize(1280, 720);

    engine_.deviceManager().addChangeListener(this);
    updateDeviceInfo();
    startTimerHz(readoutRefreshHz);
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
    readout_.setBounds(area);

    banner_.setBounds(
        juce::Rectangle<int>(std::min(bannerWidth, getWidth() - 2 * margin), bannerHeight)
            .withCentre({area.getCentreX(), margin + bannerHeight / 2}));

    settingsButton_.setBounds(
        area.reduced(margin).removeFromBottom(buttonHeight).removeFromRight(buttonWidth));

    const auto panelHeight = std::min(settingsPanel_.preferredHeight(), getHeight() - 2 * margin);
    settingsPanel_.setBounds(
        juce::Rectangle<int>(std::min(settingsWidth, getWidth() - 2 * margin), panelHeight)
            .withCentre(area.getCentre()));
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
    return false;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    updateDeviceInfo();
}

void MainComponent::timerCallback()
{
    const auto now = nowSeconds();
    const auto elapsed = now - lastUpdateSeconds_;
    lastUpdateSeconds_ = now;

    const auto stream = engine_.streamStatus();
    const auto& layout = engine_.layout();
    engine_.takePeaks(peaks_);

    DebugReadout::Values values;
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

    if constexpr (RealtimeAllocationCheck::enabled)
    {
        values.callbackAllocations = RealtimeAllocationCheck::allocationCount();
        values.allocationCheckWorks = allocationCheckWorks_;

        // An allocation in the audio callback is a bug; stop here under a debugger.
        if (*values.callbackAllocations > 0 && !reportedCallbackAllocation_)
        {
            reportedCallbackAllocation_ = true;
            jassertfalse;
        }
    }

    readout_.update(values, elapsed);
}

void MainComponent::showSettings(bool shouldShow)
{
    settingsPanel_.setVisible(shouldShow);
    settingsButton_.setToggleState(shouldShow, juce::dontSendNotification);
    if (!shouldShow)
        grabKeyboardFocus();
}

void MainComponent::updateDeviceInfo()
{
    auto* const device = engine_.deviceManager().getCurrentAudioDevice();
    deviceName_ = device != nullptr ? device->getName() : juce::String();
    sampleRate_ = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    bufferSize_ = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
    inputNames_ = device != nullptr ? device->getInputChannelNames() : juce::StringArray();

    const bool noInput = !engine_.isInputRunning();
    if (noInput)
        banner_.setText("NO AUDIO INPUT",
                        engine_.noInputReason() + " Choose a device in Settings.");
    banner_.setVisible(noInput);
}

} // namespace visona
