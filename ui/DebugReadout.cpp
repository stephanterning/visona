#include "DebugReadout.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

constexpr float silenceDb = -120.0f;
constexpr float meterFloorDb = -60.0f;
constexpr float barFallDbPerSecond = 24.0f;
constexpr double peakHoldSeconds = 1.5;

constexpr int rowHeight = 26;
constexpr int labelWidth = 170;
constexpr int maxContentWidth = 640;
constexpr float fontHeight = 15.0f;

juce::String formatSampleRate(double sampleRate)
{
    if (sampleRate <= 0.0)
        return "-";
    const auto kilohertz = sampleRate / 1000.0;
    const bool whole = std::abs(kilohertz - std::round(kilohertz)) < 1.0e-9;
    return juce::String(kilohertz, whole ? 0 : 1) + " kHz (" +
           juce::String(juce::roundToInt(sampleRate)) + " Hz)";
}

juce::String formatDuration(double seconds)
{
    const auto total = static_cast<std::int64_t>(std::max(seconds, 0.0));
    return juce::String(total / 3600) + ":" + juce::String((total / 60) % 60).paddedLeft('0', 2) +
           ":" + juce::String(total % 60).paddedLeft('0', 2);
}

float toDecibels(float gain)
{
    return gain > 0.0f ? std::max(20.0f * std::log10(gain), silenceDb) : silenceDb;
}

juce::String formatLevel(float db)
{
    if (db <= silenceDb)
        return "-inf dBFS";
    return juce::String(db, 1) + " dBFS";
}

} // namespace

DebugReadout::DebugReadout()
{
    setOpaque(true);
}

void DebugReadout::update(const Values& values, double elapsedSeconds)
{
    values_ = values;
    meters_.resize(values_.channels.size(), {silenceDb, silenceDb, 0.0});

    for (std::size_t channel = 0; channel < meters_.size(); ++channel)
    {
        auto& meter = meters_[channel];
        const auto peakDb = toDecibels(values_.channels[channel].peak);
        const auto fall = barFallDbPerSecond * static_cast<float>(elapsedSeconds);
        meter.barDb = std::max(peakDb, std::max(meter.barDb - fall, silenceDb));

        meter.heldSeconds += elapsedSeconds;
        if (peakDb >= meter.heldDb || meter.heldSeconds > peakHoldSeconds)
        {
            meter.heldDb = peakDb;
            meter.heldSeconds = 0.0;
        }
    }
    repaint();
}

void DebugReadout::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);

    const auto numRows = 2 + 3 + static_cast<int>(values_.channels.size()) + 4 +
                         (values_.callbackAllocations.has_value() ? 1 : 0);
    const auto width = std::min(maxContentWidth, getWidth() - 32);
    auto area =
        juce::Rectangle<int>(width, numRows * rowHeight).withCentre(getLocalBounds().getCentre());

    const juce::FontOptions labelFont(fontHeight);
    const juce::FontOptions valueFont(juce::Font::getDefaultMonospacedFontName(), fontHeight,
                                      juce::Font::plain);

    const auto drawRow = [&](const juce::String& label, const juce::String& value,
                             juce::Colour valueColour = palette::text)
    {
        auto row = area.removeFromTop(rowHeight);
        g.setFont(labelFont);
        g.setColour(palette::textDim);
        g.drawText(label, row.removeFromLeft(labelWidth), juce::Justification::centredLeft, true);
        g.setFont(valueFont);
        g.setColour(valueColour);
        g.drawText(value, row, juce::Justification::centredLeft, true);
    };

    g.setFont(juce::FontOptions(fontHeight, juce::Font::bold));
    g.setColour(palette::textDim);
    g.drawText("AUDIO INPUT (DEBUG)", area.removeFromTop(rowHeight * 2),
               juce::Justification::centredLeft, true);

    const bool running = values_.running;
    drawRow("Device", running ? values_.device : "-");
    drawRow("Sample rate", running ? formatSampleRate(values_.sampleRate) : "-");

    auto blockSize = juce::String(values_.blockSize) + " frames";
    if (values_.bufferSize > 0 &&
        static_cast<std::uint32_t>(values_.bufferSize) != values_.blockSize)
        blockSize << " (device buffer " << values_.bufferSize << ")";
    drawRow("Block size", running ? blockSize : "-");

    for (std::size_t channel = 0; channel < values_.channels.size(); ++channel)
    {
        const auto& info = values_.channels[channel];
        const auto& meter = meters_[channel];
        auto row = area.removeFromTop(rowHeight);

        g.setFont(labelFont);
        g.setColour(palette::textDim);
        g.drawText("Input " + info.name, row.removeFromLeft(labelWidth),
                   juce::Justification::centredLeft, true);

        g.setFont(valueFont);
        g.setColour(palette::text);
        g.drawText(formatLevel(meter.heldDb), row.removeFromRight(110),
                   juce::Justification::centredRight, true);

        auto bar = row.removeFromRight(std::max(row.getWidth() / 2, 60)).reduced(8, 8).toFloat();
        g.setColour(palette::outline);
        g.fillRect(bar);
        const auto fraction =
            juce::jlimit(0.0f, 1.0f, (meter.barDb - meterFloorDb) / -meterFloorDb);
        g.setColour(palette::level);
        g.fillRect(bar.withWidth(bar.getWidth() * fraction));

        g.setColour(palette::text);
        g.drawText(running ? info.input : "-", row, juce::Justification::centredLeft, true);
    }

    drawRow("Overruns",
            running ? juce::String(values_.overruns) + " (" + juce::String(values_.droppedFrames) +
                          " frames dropped)"
                    : "-",
            values_.overruns > 0 ? palette::error : palette::text);
    drawRow("Stream time", running ? formatDuration(values_.streamSeconds) : "-");
    drawRow("Device xruns", juce::String(values_.deviceXruns),
            values_.deviceXruns > 0 ? palette::error : palette::text);
    drawRow("Host time",
            running ? (values_.deviceHostTime ? "device timestamp" : "monotonic clock") : "-");

    if (values_.callbackAllocations.has_value())
    {
        if (!values_.allocationCheckWorks)
            drawRow("Callback allocations", "check is not working", palette::error);
        else
            drawRow("Callback allocations", juce::String(*values_.callbackAllocations),
                    *values_.callbackAllocations > 0 ? palette::error : palette::text);
    }
}

} // namespace visona
