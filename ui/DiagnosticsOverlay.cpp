#include "DiagnosticsOverlay.h"

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

constexpr int width = 440;
constexpr int padding = 12;
constexpr int rowHeight = 20;
constexpr int labelWidth = 140;
constexpr float fontHeight = 13.0f;

// The title and the section headings.
constexpr int headingRows = 6;
constexpr int audioRows = 7;
constexpr int midiRows = 5;
constexpr int analysisRows = 1;
constexpr int renderingRows = 5;
constexpr int processRows = 1;

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

juce::String formatState(TransportState state)
{
    switch (state)
    {
    case TransportState::waiting:
        return "waiting";
    case TransportState::running:
        return "running";
    case TransportState::stopped:
        return "stopped";
    case TransportState::clockLost:
        return "clock lost";
    }
    return {};
}

/** A position in ticks as bar.beat.tick, counting bars and beats from 1. */
juce::String formatPosition(std::int64_t tick, const TimeSignature& timeSignature)
{
    const auto perBar = timeSignature.ticksPerBar();
    const auto perBeat = timeSignature.ticksPerBeat();
    return juce::String(tick / perBar + 1) + "." + juce::String(tick % perBar / perBeat + 1) + "." +
           juce::String(tick % perBeat);
}

juce::String formatLoad(double fraction)
{
    return juce::String(fraction * 100.0, 1) + " % of one core";
}

juce::String formatMs(double average, double maximum)
{
    return juce::String(average, 2) + " ms average, " + juce::String(maximum, 2) + " ms max";
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

DiagnosticsOverlay::DiagnosticsOverlay()
{
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}

void DiagnosticsOverlay::update(const Values& values, double elapsedSeconds)
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

int DiagnosticsOverlay::numRows() const
{
    return headingRows + audioRows + midiRows + static_cast<int>(values_.channels.size()) +
           (values_.callbackAllocations.has_value() ? 1 : 0) + analysisRows + renderingRows +
           processRows;
}

juce::Rectangle<int> DiagnosticsOverlay::preferredSize() const
{
    return {width, numRows() * rowHeight + 2 * padding};
}

void DiagnosticsOverlay::paint(juce::Graphics& g)
{
    g.fillAll(palette::surface);
    g.setColour(palette::outline);
    g.drawRect(getLocalBounds());

    auto area = getLocalBounds().reduced(padding);
    const juce::FontOptions labelFont(fontHeight);
    const juce::FontOptions headingFont(fontHeight - 1.0f, juce::Font::bold);
    const juce::FontOptions valueFont(juce::Font::getDefaultMonospacedFontName(), fontHeight,
                                      juce::Font::plain);

    // A window too small for every row shows the rows that fit whole.
    const auto drawHeading = [&](const juce::String& text)
    {
        if (area.getHeight() < rowHeight)
            return;
        g.setFont(headingFont);
        g.setColour(palette::textDim);
        g.drawText(text, area.removeFromTop(rowHeight), juce::Justification::bottomLeft, true);
    };
    const auto drawRow = [&](const juce::String& label, const juce::String& value,
                             juce::Colour valueColour = palette::text)
    {
        if (area.getHeight() < rowHeight)
            return;
        auto row = area.removeFromTop(rowHeight);
        g.setFont(labelFont);
        g.setColour(palette::textDim);
        g.drawText(label, row.removeFromLeft(labelWidth), juce::Justification::centredLeft, true);
        g.setFont(valueFont);
        g.setColour(valueColour);
        g.drawText(value, row, juce::Justification::centredLeft, true);
    };

    {
        auto title = area.removeFromTop(rowHeight);
        g.setFont(juce::FontOptions(fontHeight, juce::Font::bold));
        g.setColour(palette::text);
        g.drawText("DIAGNOSTICS", title, juce::Justification::centredLeft, true);
        g.setFont(labelFont);
        g.setColour(palette::textDim);
        g.drawText("D to hide", title, juce::Justification::centredRight, true);
    }

    drawHeading("AUDIO INPUT");
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
        if (area.getHeight() < rowHeight)
            break;
        const auto& info = values_.channels[channel];
        const auto& meter = meters_[channel];
        auto row = area.removeFromTop(rowHeight);

        g.setFont(labelFont);
        g.setColour(palette::textDim);
        g.drawText("Input " + info.name, row.removeFromLeft(labelWidth),
                   juce::Justification::centredLeft, true);

        g.setFont(valueFont);
        g.setColour(palette::text);
        g.drawText(formatLevel(meter.heldDb), row.removeFromRight(96),
                   juce::Justification::centredRight, true);

        auto bar = row.removeFromRight(std::max(row.getWidth() / 2, 40)).reduced(6, 6).toFloat();
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
            drawRow("Callback allocs", "check is not working", palette::error);
        else
            drawRow("Callback allocs", juce::String(*values_.callbackAllocations),
                    *values_.callbackAllocations > 0 ? palette::error : palette::text);
    }

    drawHeading("MIDI CLOCK");
    drawRow("MIDI input", values_.midiInput.isNotEmpty() ? values_.midiInput : "none");
    drawRow("Transport", formatState(values_.transportState) + ", next tick at " +
                             formatPosition(values_.nextTick, values_.timeSignature));
    drawRow("Tempo", values_.bpm > 0.0 ? juce::String(values_.bpm, 2) + " BPM" : "-");
    drawRow("Messages",
            juce::String(values_.midiEvents) + " (" + juce::String(values_.midiDrops) +
                " dropped, " + juce::String(values_.ignoredSpp) + " SPP ignored)",
            values_.midiDrops > 0 ? palette::error : palette::text);
    drawRow("MIDI offset",
            values_.sampleRate > 0.0
                ? "+" + juce::String(values_.midiOffsetFrames * 1000.0 / values_.sampleRate, 2) +
                      " ms (" + juce::String(juce::roundToInt(values_.midiOffsetFrames)) +
                      " frames)"
                : "-");

    drawHeading("ANALYSIS");
    drawRow("Analysis thread", formatLoad(values_.analysisLoad));

    drawHeading("RENDERING");
    const auto& rendering = values_.rendering;
    drawRow("Frame rate", juce::String(juce::roundToInt(rendering.framesPerSecond)) +
                              " fps (display " +
                              juce::String(juce::roundToInt(rendering.vblanksPerSecond)) + " Hz)");
    drawRow("Render", formatMs(rendering.renderMsAverage, rendering.renderMsMax));
    drawRow("Paint", formatMs(rendering.paintMsAverage, rendering.paintMsMax));
    drawRow("Full redraws", juce::String(rendering.fullRedrawsPerSecond, 1) + " per second");
    drawRow("Image", juce::String(rendering.imageWidth) + " x " +
                         juce::String(rendering.imageHeight) + " px at " +
                         juce::String(rendering.scale, 1) + "x, " +
                         juce::String(static_cast<int>(rendering.tiles)) + " tiles");

    drawHeading("PROCESS");
    drawRow("Visona CPU",
            values_.processCpu.has_value() ? formatLoad(*values_.processCpu) : "unknown");
}

} // namespace visona
