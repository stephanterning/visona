#include "SettingsPanel.h"

#include "ChannelNames.h"
#include "Palette.h"

#include <cmath>

namespace visona
{

namespace
{

constexpr int padding = 20;
constexpr int titleHeight = 40;
constexpr int rowHeight = 44;
constexpr int rowGap = 8;
constexpr int labelWidth = 130;
constexpr int errorHeight = 48;

juce::String sampleRateText(double sampleRate)
{
    const auto kilohertz = sampleRate / 1000.0;
    const bool whole = std::abs(kilohertz - std::round(kilohertz)) < 1.0e-9;
    return juce::String(kilohertz, whole ? 0 : 1) + " kHz";
}

juce::String bufferSizeText(int bufferSize, double sampleRate)
{
    auto text = juce::String(bufferSize) + " samples";
    if (sampleRate > 0.0)
        text << " (" << juce::String(bufferSize * 1000.0 / sampleRate, 1) << " ms)";
    return text;
}

} // namespace

SettingsPanel::SettingsPanel(AudioSettings& settings)
    : settings_(settings)
{
    setOpaque(true);

    title_.setText("Settings", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, palette::text);
    addAndMakeVisible(title_);

    closeButton_.setButtonText("Done");
    closeButton_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(closeButton_);

    addRow(deviceTypeRow_, "Audio system");
    addRow(deviceRow_, "Audio device");
    addRow(sampleRateRow_, "Sample rate");
    addRow(bufferSizeRow_, "Buffer size");

    const auto& layout = settings_.layout();
    for (std::size_t channel = 0; channel < layout.totalChannelCount(); ++channel)
    {
        auto& row = *inputRows_.emplace_back(std::make_unique<Row>());
        addRow(row, channelName(layout, channel) + " input");
        row.choices.onChange = [this, channel, &row]
        {
            if (const auto id = row.choices.getSelectedId(); id > 0)
                settings_.setInputChannel(channel, id - 1);
        };
    }

    deviceTypeRow_.choices.onChange = [this]
    {
        if (const auto index = deviceTypeRow_.choices.getSelectedItemIndex(); index >= 0)
            showResult(settings_.selectDeviceType(deviceTypes_[index]));
    };
    deviceRow_.choices.onChange = [this]
    {
        if (const auto index = deviceRow_.choices.getSelectedItemIndex(); index >= 0)
            showResult(settings_.selectDevice(devices_[index]));
    };
    sampleRateRow_.choices.onChange = [this]
    {
        if (const auto index = sampleRateRow_.choices.getSelectedItemIndex(); index >= 0)
            showResult(settings_.selectSampleRate(sampleRates_[index]));
    };
    bufferSizeRow_.choices.onChange = [this]
    {
        if (const auto index = bufferSizeRow_.choices.getSelectedItemIndex(); index >= 0)
            showResult(settings_.selectBufferSize(bufferSizes_[index]));
    };

    errorLabel_.setColour(juce::Label::textColourId, palette::error);
    errorLabel_.setJustificationType(juce::Justification::topLeft);
    errorLabel_.setMinimumHorizontalScale(1.0f);
    addChildComponent(errorLabel_);

    settings_.deviceManager().addChangeListener(this);
    refresh();
}

SettingsPanel::~SettingsPanel()
{
    settings_.deviceManager().removeChangeListener(this);
}

int SettingsPanel::preferredHeight() const
{
    auto rows = static_cast<int>(inputRows_.size());
    for (const auto* row : {&deviceTypeRow_, &deviceRow_, &sampleRateRow_, &bufferSizeRow_})
        if (row->choices.isVisible())
            ++rows;
    return padding + titleHeight + rowGap + rows * (rowHeight + rowGap) +
           (errorLabel_.isVisible() ? errorHeight : 0) + padding;
}

void SettingsPanel::refresh()
{
    auto* const device = settings_.deviceManager().getCurrentAudioDevice();
    refreshDeviceTypes();
    refreshDevices();
    refreshSampleRates(device);
    refreshBufferSizes(device);
    refreshInputChannels(device);
    notifyPreferredHeight();
}

void SettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
    const auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(palette::surface);
    g.fillRoundedRectangle(area, 10.0f);
    g.setColour(palette::outline);
    g.drawRoundedRectangle(area, 10.0f, 1.0f);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(padding);

    auto titleRow = area.removeFromTop(titleHeight);
    closeButton_.setBounds(titleRow.removeFromRight(96));
    title_.setBounds(titleRow);
    area.removeFromTop(rowGap);

    const auto placeRow = [&](Row& row)
    {
        if (!row.choices.isVisible())
            return;
        auto bounds = area.removeFromTop(rowHeight);
        row.label.setBounds(bounds.removeFromLeft(labelWidth));
        row.choices.setBounds(bounds);
        area.removeFromTop(rowGap);
    };
    for (auto* row : {&deviceTypeRow_, &deviceRow_, &sampleRateRow_, &bufferSizeRow_})
        placeRow(*row);
    for (auto& row : inputRows_)
        placeRow(*row);

    errorLabel_.setBounds(area.removeFromTop(errorHeight));
}

void SettingsPanel::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void SettingsPanel::addRow(Row& row, const juce::String& labelText)
{
    row.label.setText(labelText, juce::dontSendNotification);
    row.label.setColour(juce::Label::textColourId, palette::textDim);
    row.label.setFont(juce::FontOptions(15.0f));
    addAndMakeVisible(row.label);
    addAndMakeVisible(row.choices);
}

void SettingsPanel::showResult(const juce::String& error)
{
    errorLabel_.setText(error, juce::dontSendNotification);
    errorLabel_.setVisible(error.isNotEmpty());
    notifyPreferredHeight();
}

void SettingsPanel::notifyPreferredHeight()
{
    resized();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

void SettingsPanel::refreshDeviceTypes()
{
    auto& deviceManager = settings_.deviceManager();
    deviceTypes_.clear();
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        deviceTypes_.add(type->getTypeName());

    auto& choices = deviceTypeRow_.choices;
    choices.clear(juce::dontSendNotification);
    choices.addItemList(deviceTypes_, 1);
    choices.setSelectedItemIndex(deviceTypes_.indexOf(deviceManager.getCurrentAudioDeviceType()),
                                 juce::dontSendNotification);

    // On macOS there is only CoreAudio, so the row only appears where there is a choice.
    const bool hasChoice = deviceTypes_.size() > 1;
    deviceTypeRow_.label.setVisible(hasChoice);
    choices.setVisible(hasChoice);
}

void SettingsPanel::refreshDevices()
{
    auto& deviceManager = settings_.deviceManager();
    auto* const type = deviceManager.getCurrentDeviceTypeObject();
    devices_ = type != nullptr ? type->getDeviceNames(true) : juce::StringArray{};

    auto& choices = deviceRow_.choices;
    choices.clear(juce::dontSendNotification);
    choices.addItemList(devices_, 1);
    choices.setTextWhenNothingSelected(devices_.isEmpty() ? "No input devices" : "Choose a device");
    choices.setTextWhenNoChoicesAvailable("No input devices");

    const bool open = deviceManager.getCurrentAudioDevice() != nullptr;
    const auto current = deviceManager.getAudioDeviceSetup().inputDeviceName;
    choices.setSelectedItemIndex(open ? devices_.indexOf(current) : -1, juce::dontSendNotification);
}

void SettingsPanel::refreshSampleRates(juce::AudioIODevice* device)
{
    auto& choices = sampleRateRow_.choices;
    choices.clear(juce::dontSendNotification);
    sampleRates_ = device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>{};
    for (int index = 0; index < sampleRates_.size(); ++index)
        choices.addItem(sampleRateText(sampleRates_[index]), index + 1);

    if (device != nullptr)
        choices.setSelectedItemIndex(sampleRates_.indexOf(device->getCurrentSampleRate()),
                                     juce::dontSendNotification);
    choices.setEnabled(device != nullptr);
}

void SettingsPanel::refreshBufferSizes(juce::AudioIODevice* device)
{
    auto& choices = bufferSizeRow_.choices;
    choices.clear(juce::dontSendNotification);
    bufferSizes_ = device != nullptr ? device->getAvailableBufferSizes() : juce::Array<int>{};
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    for (int index = 0; index < bufferSizes_.size(); ++index)
        choices.addItem(bufferSizeText(bufferSizes_[index], sampleRate), index + 1);

    if (device != nullptr)
        choices.setSelectedItemIndex(bufferSizes_.indexOf(device->getCurrentBufferSizeSamples()),
                                     juce::dontSendNotification);
    choices.setEnabled(device != nullptr);
}

void SettingsPanel::refreshInputChannels(juce::AudioIODevice* device)
{
    const auto inputNames =
        device != nullptr ? device->getInputChannelNames() : juce::StringArray{};
    for (std::size_t channel = 0; channel < inputRows_.size(); ++channel)
    {
        auto& choices = inputRows_[channel]->choices;
        choices.clear(juce::dontSendNotification);
        for (int input = 0; input < inputNames.size(); ++input)
            choices.addItem(inputChannelName(inputNames, input), input + 1);

        choices.setSelectedId(settings_.inputChannel(channel) + 1, juce::dontSendNotification);
        choices.setEnabled(!inputNames.isEmpty());
    }
}

} // namespace visona
