#include "ui/ControlBar.h"
#include "ui/Palette.h"
#include "ui/SpectrumView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <numbers>
#include <random>

namespace
{
void check(bool pass, const char* message)
{
    if (!pass)
        throw std::runtime_error(message);
}
struct Preview final : juce::Component
{
    visona::SpectrumAnalyzer analyzer;
    visona::SpectrumView spectrum{analyzer};
    visona::ControlBar controls;
    juce::LookAndFeel_V4 theme;
    Preview()
    {
        theme.setColourScheme(visona::palette::widgetColours());
        setLookAndFeel(&theme);
        addAndMakeVisible(spectrum);
        addAndMakeVisible(controls);
        controls.setSpectrum(true);
        controls.onSpectrumSettings = [this]
        { spectrum.showSettings(!spectrum.settingsVisible()); };
        controls.viewSelector().onSelect = [this](int view)
        {
            controls.setSpectrum(view == 1);
            spectrum.setActive(view == 1);
            resized();
        };
        analyzer.reset(48000);
        analyzer.setEnabled(true);
        std::vector<float> l(48000), r(48000);
        std::mt19937 random(42);
        std::uniform_real_distribution<float> noise(-1, 1);
        float low = 0;
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            const auto t = static_cast<double>(i) / 48000.0;
            low = low * 0.98f + noise(random) * 0.02f;
            l[i] = low * 0.4f + noise(random) * 0.002f;
            for (int h = 1; h <= 12; ++h)
                l[i] += static_cast<float>(0.12 / h * std::sin(2 * std::numbers::pi * 55 * h * t));
            r[i] = l[i] * 0.9f + noise(random) * 0.001f;
        }
        analyzer.process(l, r, 0);
        spectrum.setActive(true);
        setSize(1280, 720);
    }
    ~Preview() override
    {
        setLookAndFeel(nullptr);
    }
    void resized() override
    {
        auto bounds = getLocalBounds();
        controls.setBounds(bounds.removeFromBottom(controls.preferredHeight(getWidth())));
        spectrum.setBounds(bounds);
    }
};
void save(juce::Component& c, const juce::File& folder, const juce::String& name)
{
    auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    auto out = folder.getChildFile(name).createOutputStream();
    check(out != nullptr, "Cannot write screenshot");
    check(juce::PNGImageFormat().writeImageToStream(image, *out), "Cannot encode screenshot");
}
} // namespace
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const juce::File folder(argc > 1 ? juce::String(argv[1]) : "/private/tmp/visona-ui-checks");
        check(folder.createDirectory().wasOk(), "Cannot create output directory");
        Preview preview;
        const auto saved = preview.spectrum.saveSettings();
        preview.spectrum.restoreSettings(saved);
        check(preview.spectrum.saveSettings() == saved, "Settings do not round trip");
        preview.spectrum.restoreSettings("broken");
        check(preview.spectrum.saveSettings() == saved, "Malformed settings changed defaults");
        save(preview, folder, "spectrum-desktop.png");
        preview.spectrum.showSettings(true);
        save(preview, folder, "settings-desktop.png");
        preview.setSize(800, 480);
        save(preview, folder, "settings-touch.png");
        preview.spectrum.showSettings(false);
        save(preview, folder, "spectrum-touch.png");
        preview.controls.viewSelector().setTouchMode(true);
        preview.controls.viewSelector().showChoices();
        save(preview, folder, "selector-touch.png");
        auto* tiles = preview.getChildComponent(preview.getNumChildComponents() - 1);
        check(tiles != nullptr && tiles->isVisible(), "Touch selector did not open");
        check(tiles->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
              "Touch selector Escape failed");
        check(!tiles->isVisible(), "Touch selector did not dismiss");
        preview.controls.viewSelector().setTouchMode(false);
        preview.controls.viewSelector().showChoices();
        save(preview, folder, "selector-desktop.png");
        preview.setSize(640, 480);
        save(preview, folder, "spectrum-small.png");
        for (int i = 0; i < 100; ++i)
        {
            preview.spectrum.setActive(i % 2 == 0);
            preview.controls.setSpectrum(i % 2 == 0);
            preview.resized();
        }
        std::cout << "Spectrum UI checks passed; images in " << folder.getFullPathName() << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
