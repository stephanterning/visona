#include "ui/BandSpectrumView.h"
#include "ui/ControlBar.h"
#include "ui/Palette.h"
#include "ui/SpectrumView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <random>

void runStereoUiChecks(const juce::File& folder);

namespace visona
{
struct SpectrumRenderBenchmark
{
    static bool verifyControls()
    {
        SpectrumAnalyzer analyzer;
        analyzer.reset(48000);
        SpectrumView view(analyzer);
        view.setSize(1280, 720);
        view.setActive(true);
        std::vector<float> tone(spectrumFftSize, 0.25f);
        analyzer.process(tone, tone, 0);
        std::uint64_t frame = tone.size();
        tone.resize(spectrumHopSize);
        for (int detector = 0; detector < 2; ++detector)
        {
            for (std::size_t base : {4U, 10U})
                view.choices_[base + 1].setSelectedItemIndex(detector, juce::sendNotificationSync);
            for (std::size_t field = 0; field < view.choices_.size(); ++field)
            {
                auto& choice = view.choices_[field];
                if (!choice.isEnabled())
                    continue;
                const auto count = choice.getNumItems();
                for (int option = 0; option < count; ++option)
                {
                    choice.setSelectedItemIndex(option, juce::sendNotificationSync);
                    if (choice.getSelectedItemIndex() != option)
                        return false;
                    const auto values = juce::StringArray::fromTokens(view.saveSettings(), ",", "");
                    int expected = option;
                    if (field == 1)
                        expected = std::array<int, 8>{
                            12, 6, 0, -12, -24, -36, -48, -60}[static_cast<std::size_t>(option)];
                    else if (field == 2)
                        expected = (option + 1) * 30;
                    else if (field == 4 || field == 10)
                        expected = option == 0 ? 1 : 0;
                    else if ((field == 8 || field == 14) && option == 2)
                        expected = 3;
                    if (values[static_cast<int>(field)].getIntValue() != expected)
                        return false;
                    analyzer.process(tone, tone, frame);
                    frame += tone.size();
                    view.timerCallback();
                    if (!view.snapshot_.valid ||
                        view.snapshot_.analysisKey != view.settings_.analysisKey())
                        return false;
                    const auto saved = view.saveSettings();
                    view.restoreSettings(saved);
                    if (saved != view.saveSettings())
                        return false;
                }
            }
        }
        return true;
    }
    static bool bandDirtyRegionsMatchFull()
    {
        BandAnalyzer analyzer;
        BandSpectrumView view(analyzer);
        view.setSize(801, 481);
        view.snapshot_.valid = true;
        view.snapshot_.sampleRate = 48000;
        view.snapshot_.db.fill(-30);
        view.snapshot_.peakDb.fill(-20);
        for (int step = 0; step < 8; ++step)
        {
            auto before = view.createComponentSnapshot(view.getLocalBounds());
            const auto previous = view.snapshot_;
            for (std::size_t i = 0; i < spectrumBands; ++i)
            {
                view.snapshot_.db[i] = static_cast<float>((static_cast<int>(i) * 13 + step * 19) % 100 - 80);
                view.snapshot_.peakDb[i] = static_cast<float>((static_cast<int>(i) * 7 + step * 11) % 90 - 70);
            }
            auto dirty = view.changedRegion(previous);
            {
                juce::Graphics partial(before);
                partial.reduceClipRegion(dirty);
                view.paint(partial);
            }
            const auto full = view.createComponentSnapshot(view.getLocalBounds());
            juce::Image::BitmapData a(before, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData b(full, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < full.getHeight(); ++y)
                if (std::memcmp(a.getLinePointer(y), b.getLinePointer(y),
                        static_cast<std::size_t>(full.getWidth() * a.pixelStride)) != 0)
                    return false;
        }
        return true;
    }
    static void update(BandSpectrumView& view)
    {
        view.timerCallback();
    }
    static void render(SpectrumView& view)
    {
        view.render();
    }
    static bool incrementalMatchesFull(SpectrumView& view)
    {
        for (float shift : {25.0f, -50.0f, 25.0f})
        {
            for (auto& curve : view.snapshot_.db)
                for (auto& value : curve)
                    value += shift;
            view.render();
            const auto incremental = view.cached_.createCopy();
            view.backgroundDirty_ = true;
            view.render();
            juce::Image::BitmapData before(incremental, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData after(view.cached_, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < incremental.getHeight(); ++y)
                if (std::memcmp(
                        before.getLinePointer(y), after.getLinePointer(y),
                        static_cast<std::size_t>(incremental.getWidth() * before.pixelStride)) != 0)
                    return false;
        }
        return true;
    }
};
} // namespace visona

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
    visona::BandSpectrumView bands{analyzer.bands()};
    visona::ControlBar controls;
    juce::LookAndFeel_V4 theme;
    Preview()
    {
        theme.setColourScheme(visona::palette::widgetColours());
        setLookAndFeel(&theme);
        addAndMakeVisible(spectrum);
        addChildComponent(bands);
        addAndMakeVisible(controls);
        controls.setSpectrum(true);
        controls.onSpectrumSettings = [this]
        { spectrum.showSettings(!spectrum.settingsVisible()); };
        controls.viewSelector().onSelect = [this](int view)
        {
            controls.setAnalysisView(view);
            bands.setActive(view == 2);
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
        bands.setBounds(bounds);
    }
};
void save(juce::Component& c, const juce::File& folder, const juce::String& name)
{
    auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    auto out = folder.getChildFile(name).createOutputStream();
    check(out != nullptr, "Cannot write screenshot");
    check(out->setPosition(0) && out->truncate().wasOk(), "Cannot replace previous screenshot");
    check(juce::PNGImageFormat().writeImageToStream(image, *out), "Cannot encode screenshot");
}
} // namespace
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        if (argc > 1 && juce::String(argv[1]) == "--benchmark")
        {
            Preview preview;
            preview.setSize(1920, 1080);
            for (int style : {0, 1, 3})
            {
                auto settings =
                    juce::StringArray::fromTokens(preview.spectrum.saveSettings(), ",", "");
                settings.set(8, juce::String(style));
                preview.spectrum.restoreSettings(settings.joinIntoString(","));
                for (int i = 0; i < 3; ++i)
                    visona::SpectrumRenderBenchmark::render(preview.spectrum);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 30; ++i)
                    visona::SpectrumRenderBenchmark::render(preview.spectrum);
                std::cout << "1920x1080 spectrum style " << style << ": "
                          << (juce::Time::getMillisecondCounterHiRes() - start) / 30.0
                          << " ms/frame\n";
            }
            return 0;
        }
        const juce::File folder(argc > 1 ? juce::String(argv[1]) : "/private/tmp/visona-ui-checks");
        check(folder.createDirectory().wasOk(), "Cannot create output directory");
        runStereoUiChecks(folder);
        check(visona::SpectrumRenderBenchmark::bandDirtyRegionsMatchFull(),
              "Band partial repaint differs from a complete redraw");
        check(visona::SpectrumRenderBenchmark::verifyControls(),
              "A spectrum control failed to reach the analyzer or survive restoration");
        Preview preview;
        preview.setVisible(true);
        const auto saved = preview.spectrum.saveSettings();
        preview.spectrum.restoreSettings(saved);
        check(preview.spectrum.saveSettings() == saved, "Settings do not round trip");
        preview.spectrum.restoreSettings("broken");
        check(preview.spectrum.saveSettings() == saved, "Malformed settings changed defaults");
        save(preview, folder, "spectrum-desktop.png");
        preview.spectrum.showSettings(true);
        save(preview, folder, "settings-desktop.png");
        check(visona::SpectrumRenderBenchmark::incrementalMatchesFull(preview.spectrum),
              "Incremental spectrum rendering leaves stale pixels");
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
        for (int view : {1, 2, 0, 2, 1})
        {
            preview.controls.viewSelector().showChoices();
            auto* tile = dynamic_cast<juce::Button*>(tiles->getChildComponent(view));
            check(tile != nullptr, "Missing analysis tile");
            tile->onClick();
            check(!tiles->isVisible(), "Analysis tile still intercepts input after selection");
            auto& selector = preview.controls.viewSelector();
            auto point = preview.getLocalPoint(&selector, selector.getLocalBounds().getCentre());
            auto* hit = preview.getComponentAt(point);
            check(hit == &selector || selector.isParentOf(hit),
                  "View selector cannot receive input after switching views");
        }
        preview.controls.viewSelector().setTouchMode(false);
        preview.controls.viewSelector().showChoices();
        save(preview, folder, "selector-desktop.png");
        preview.setSize(640, 480);
        save(preview, folder, "spectrum-small.png");
        preview.setSize(641, 481);
        for (int style : {0, 1, 3})
        {
            auto settings = juce::StringArray::fromTokens(saved, ",", "");
            settings.set(8, juce::String(style));
            settings.set(14, juce::String(style));
            settings.set(9, juce::String((style + 1) % 4));
            preview.spectrum.restoreSettings(settings.joinIntoString(","));
            check(visona::SpectrumRenderBenchmark::incrementalMatchesFull(preview.spectrum),
                  "Two-curve rendering or fill cache invalidation failed");
        }
        auto solidSettings = juce::StringArray::fromTokens(saved, ",", "");
        solidSettings.set(8, "3");
        preview.spectrum.restoreSettings(solidSettings.joinIntoString(","));
        check(preview.spectrum.saveSettings() == solidSettings.joinIntoString(","),
              "Solid style did not survive settings restoration");
        auto legacy = solidSettings;
        legacy.set(8, "2");
        preview.spectrum.restoreSettings(legacy.joinIntoString(","));
        check(preview.spectrum.saveSettings() == solidSettings.joinIntoString(","),
              "Legacy style did not migrate to Solid");
        preview.setSize(1280, 720);
        const auto beforeColour =
            preview.spectrum.createComponentSnapshot(preview.spectrum.getLocalBounds());
        preview.spectrum.setWaveformColour(juce::Colours::orange);
        const auto afterColour =
            preview.spectrum.createComponentSnapshot(preview.spectrum.getLocalBounds());
        const int sampleX = preview.spectrum.getWidth() / 2;
        const int sampleY = preview.spectrum.getHeight() - 36;
        check(beforeColour.getPixelAt(sampleX, sampleY) != afterColour.getPixelAt(sampleX, sampleY),
              "Solid fill did not follow waveform colour");
        save(preview, folder, "spectrum-solid.png");
        for (int i = 0; i < 100; ++i)
        {
            preview.spectrum.setActive(i % 2 == 0);
            preview.controls.setSpectrum(i % 2 == 0);
            preview.resized();
        }
        preview.controls.viewSelector().onSelect(2);
        auto tone = std::vector<float>(48000);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = static_cast<float>(
                0.3 * std::sin(2 * std::numbers::pi * 1000 * static_cast<double>(i) / 48000));
        preview.analyzer.process(tone, tone, 48000);
        visona::SpectrumRenderBenchmark::update(preview.bands);
        preview.setSize(1280, 720);
        save(preview, folder, "spectrum-band-desktop.png");
        preview.setSize(800, 480);
        save(preview, folder, "spectrum-band-touch.png");
        std::cout << "Spectrum UI checks passed; images in " << folder.getFullPathName() << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
