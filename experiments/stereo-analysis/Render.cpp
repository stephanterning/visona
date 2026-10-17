#include "LspStereo.h"
#include "SignalizerPolar.h"
#include "Signals.h"
#include "StereoProjection.h"

#include <juce_graphics/juce_graphics.h>

#include <iostream>

using namespace stereo_spike;

namespace
{
void text(juce::Graphics& g, const juce::String& label, juce::Rectangle<float> area, float size,
          juce::Colour colour = juce::Colour(0xffd8e4eb))
{
    g.setColour(colour);
    g.setFont(juce::FontOptions(size));
    g.drawText(label, area, juce::Justification::centred);
}

void plot(juce::Graphics& g, const Signal& signal, juce::Rectangle<float> area, bool polar)
{
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(area.toNearestInt());
    const float radius =
        std::min(area.getWidth() * 0.44f, area.getHeight() * (polar ? 0.85f : 0.44f));
    const float cx = area.getCentreX();
    const float cy = polar ? area.getBottom() - 8 : area.getCentreY();
    const auto screen = [&](Point p)
    { return juce::Point<float>{cx + p.x * radius, cy - p.y * radius}; };
    g.setColour(juce::Colour(0xff30424e));
    if (polar)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, radius, radius, 0, -juce::MathConstants<float>::halfPi,
                          juce::MathConstants<float>::halfPi, true);
        g.strokePath(arc, juce::PathStrokeType(1));
        g.drawLine(cx - radius, cy, cx + radius, cy);
        constexpr float k = std::numbers::sqrt2_v<float> * 0.5f;
        g.drawLine(cx, cy, cx - k * radius, cy - k * radius);
        g.drawLine(cx, cy, cx + k * radius, cy - k * radius);
    }
    else
    {
        juce::Path diamond;
        diamond.startNewSubPath(cx, cy - radius);
        diamond.lineTo(cx + radius, cy);
        diamond.lineTo(cx, cy + radius);
        diamond.lineTo(cx - radius, cy);
        diamond.closeSubPath();
        g.strokePath(diamond, juce::PathStrokeType(1));
        g.drawLine(cx, cy - radius, cx, cy + radius);
        g.drawLine(cx - radius, cy, cx + radius, cy);
        g.drawLine(cx - radius * 0.5f, cy - radius * 0.5f, cx + radius * 0.5f, cy + radius * 0.5f);
        g.drawLine(cx + radius * 0.5f, cy - radius * 0.5f, cx - radius * 0.5f, cy + radius * 0.5f);
    }
    g.setColour(juce::Colour(0xff30d4da).withAlpha(0.15f));
    // Draw every sample in the last 100 ms. No line joins, auto gain or per-channel scaling.
    const auto begin = signal.left.size() > 4800 ? signal.left.size() - 4800 : 0;
    for (auto i = begin; i < signal.left.size(); ++i)
    {
        const auto left = signal.left[i], right = signal.right[i];
        // Standard rotated L/R projection with peak-normalized diamond coordinates.
        // This trivial linear transform is Visona code; only polar math is ported.
        const Point p = polar ? signalizerPolar(left, right) : lissajous(left, right);
        const auto at = screen(p);
        g.fillRect(at.x - 0.7f, at.y - 0.7f, 1.4f, 1.4f);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: stereo_render /absolute/path/stereo-views.png\n";
        return 1;
    }
    lsp::dsp::init();
    juce::Image image(juce::Image::RGB, 1440, 1080, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff0c141a));
    text(g, "Stereo analysis - library integration experiment", {0, 14, 1440, 40}, 27);
    text(g,
         "Identical input and fixed gain | upper: Lissajous | lower: Signalizer polar adaptation",
         {0, 55, 1440, 28}, 17);
    for (std::size_t kind = 0; kind < 6; ++kind)
    {
        const float x = 20 + static_cast<float>(kind % 3) * 473;
        const float y = 100 + static_cast<float>(kind / 3) * 470;
        const juce::Rectangle<float> card{x, y, 454, 450};
        g.setColour(juce::Colour(0xff14212a));
        g.fillRoundedRectangle(card, 12);
        const auto signal = makeSignal(kind, 48000, 24000);
        LspStereo meter(9600);
        std::vector<float> correlation(24000), pan(24000);
        meter.process(signal.left.data(), signal.right.data(), correlation.data(), pan.data(),
                      pan.size());
        text(g, juce::String(signalNames[kind].data()), {x, y + 8, 454, 28}, 21);
        const auto correlationLabel = (kind == 2 || kind == 3)
                                          ? juce::String("undefined (one channel silent)")
                                          : juce::String(correlation.back(), 3);
        text(g, "Correlation: " + correlationLabel, {x, y + 39, 454, 23}, 14);
        plot(g, signal, {x + 10, y + 68, 434, 202}, false);
        plot(g, signal, {x + 10, y + 283, 434, 130}, true);
        text(g, "L", {x + 39, y + 414, 25, 22}, 14);
        text(g, "R", {x + 390, y + 414, 25, 22}, 14);
    }
    text(g,
         "200 ms rectangular measurement window | 100 ms point history | no Ozone Polar Level "
         "approximation",
         {0, 1045, 1440, 24}, 15);
    juce::File file(juce::String::fromUTF8(argv[1]));
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() || !stream.setPosition(0) || stream.truncate().failed() ||
        !juce::PNGImageFormat().writeImageToStream(image, stream))
    {
        std::cerr << "Could not write PNG\n";
        return 1;
    }
    stream.flush();
    std::cout << file.getFullPathName() << '\n';
}
