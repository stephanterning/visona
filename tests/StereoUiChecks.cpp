#include "ui/ControlBar.h"
#include "ui/Palette.h"
#include "ui/StereoView.h"

#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
#include <numeric>
#include <stdexcept>

void runStereoUiChecks(const juce::File& folder);

namespace visona
{
struct StereoUiChecks
{
    static void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    static void save(juce::Component& component, const juce::File& folder, const char* name)
    {
        const auto image = component.createComponentSnapshot(component.getLocalBounds());
        auto stream = folder.getChildFile(name).createOutputStream();
        check(stream != nullptr, "Cannot write stereo screenshot");
        check(stream->setPosition(0) && stream->truncate().wasOk(),
              "Cannot replace previous stereo screenshot");
        check(juce::PNGImageFormat().writeImageToStream(image, *stream),
              "Cannot encode stereo screenshot");
    }
    static double firstBrightness(const StereoView& view)
    {
        for (const auto& tile : view.historyTiles_)
            for (const auto& point : tile.pixels)
                if (point.index != std::numeric_limits<unsigned>::max())
                    return point.brightness;
        return 0;
    }
    static void run(const juce::File& folder)
    {
        struct Preview final : juce::Component
        {
            juce::LookAndFeel_V4 look;
            StereoAnalyzer analyzer;
            StereoView view{analyzer};
            ControlBar controls;
            Preview()
            {
                look.setColourScheme(palette::widgetColours());
                setLookAndFeel(&look);
                addAndMakeVisible(view);
                addAndMakeVisible(controls);
                controls.setAnalysisView(3);
                controls.onSpectrumSettings = [this] { view.resetView(); };
                analyzer.reset(48000);
                setSize(1280, 720);
                view.setActive(true);
            }
            ~Preview() override
            {
                setLookAndFeel(nullptr);
            }
            void resized() override
            {
                auto area = getLocalBounds();
                controls.setBounds(area.removeFromBottom(controls.preferredHeight(getWidth())));
                view.setBounds(area);
            }
        } preview;
        auto& view = preview.view;
        for (std::uint64_t frame = 0; frame < stereoHistoryCapacity * 2; ++frame)
        {
            const StereoSample sample{
                static_cast<float>(std::sin(static_cast<double>(frame))),
                static_cast<float>(std::cos(static_cast<double>(frame) * 1.3))};
            const auto expected = signalizerPolar(sample.left, sample.right);
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                const auto actual = view.cachedPolar(sample, frame);
                check(std::bit_cast<std::uint32_t>(actual.x) ==
                              std::bit_cast<std::uint32_t>(expected.x) &&
                          std::bit_cast<std::uint32_t>(actual.y) ==
                              std::bit_cast<std::uint32_t>(expected.y),
                      "Cached projection changed sample coordinates");
            }
        }
        std::vector<float> left(28800), right(28800);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto t = static_cast<double>(i) / 48000;
            const auto common = 0.35 * std::sin(2 * std::numbers::pi * 203 * t);
            left[i] = static_cast<float>(common + 0.18 * std::sin(2 * std::numbers::pi * 733 * t));
            right[i] =
                static_cast<float>(common + 0.18 * std::sin(2 * std::numbers::pi * 1103 * t));
        }
        preview.analyzer.process(left, right, 0);
        view.timerCallback();
        check(view.snapshot_->correlationValid(), "Stereo UI did not receive measurement");
        check(view.status().isEmpty(), "Valid stereo shows a no-signal message");
        // Publishing a new interval must replace maxima, not retain a lifetime maximum.
        view.stats_.renderMsMax = view.stats_.paintMsMax = 99.0;
        view.renderMax_ = 2.0;
        view.paintMax_ = 3.0;
        view.start_ = juce::Time::getMillisecondCounterHiRes() - 1100.0;
        view.timerCallback();
        check(view.stats().renderMsMax == 2.0 && view.stats().paintMsMax == 3.0,
              "Stereo diagnostics retained an old interval's maximum");
        view.start_ -= 1100.0;
        view.timerCallback();
        check(view.stats().renderMsMax == 0.0 && view.stats().paintMsMax == 0.0,
              "Stereo diagnostics did not reset interval counters");
        const auto correlation = view.snapshot_->correlation;
        for (int mode = 0; mode < 3; ++mode)
            for (int gain = 0; gain < view.gain_.getNumItems(); ++gain)
            {
                view.selectMode(mode);
                view.gain_.setSelectedItemIndex(gain, juce::sendNotificationSync);
                const auto saved = view.saveSettings();
                view.resetView();
                view.restoreSettings(saved);
                check(view.saveSettings() == saved, "Stereo view settings failed to round trip");
                check(std::abs(view.snapshot_->correlation - correlation) < 1e-6f,
                      "Display control changed the measurement");
            }
        for (int persistence = 0; persistence < 4; ++persistence)
        {
            view.persistence_.setSelectedItemIndex(persistence, juce::sendNotificationSync);
            const auto setting = view.saveSettings();
            view.resetView();
            view.restoreSettings(setting);
            check(view.saveSettings() == setting, "Persistence setting did not round trip");
        }
        view.restoreSettings("0,2");
        check(view.persistence_.getSelectedItemIndex() == 2,
              "Legacy settings lost default persistence");
        const auto saved = view.saveSettings();
        for (const auto* malformed :
             {"broken", "-1,2", "3,2", "1,99", "0,", "0,2junk", "0,2,4", "0,2,", "0,2,1,2"})
        {
            view.restoreSettings(malformed);
            check(view.saveSettings() == saved, "Malformed stereo settings changed the view");
        }
        view.resetView();
        view.persistence_.setSelectedItemIndex(0, juce::sendNotificationSync);
        for (int mode = 0; mode < 2; ++mode)
        {
            view.selectMode(mode);
            check(std::accumulate(view.counts_.begin(), view.counts_.end(), std::uint64_t{0}) ==
                      4 * view.snapshot_->count,
                  "Sparse rendering discarded sample contributions");
            const auto reference = view.density_.createCopy();
            view.render();
            for (int y = 0; y < reference.getHeight(); ++y)
                for (int x = 0; x < reference.getWidth(); ++x)
                    check(reference.getPixelAt(x, y) == view.density_.getPixelAt(x, y),
                          "Repeated sparse rendering accumulated a trail");
        }
        view.resetView();
        save(preview, folder, "stereo-polar-desktop.png");
        view.level_.onClick();
        check(view.mode_ == 2, "Polar Level button failed");
        view.start_ = juce::Time::getMillisecondCounterHiRes();
        view.lastAudio_ = view.start_;
        const auto framesBeforePolling = view.frames_;
        for (int poll = 0; poll < 10; ++poll)
            view.timerCallback();
        check(view.frames_ == framesBeforePolling,
              "Polling without fresh audio inflated the frame count");
        save(preview, folder, "stereo-level-desktop.png");
        view.levelProfile_.setSelectedItemIndex(1, juce::sendNotificationSync);
        save(preview, folder, "stereo-level-energy-desktop.png");
        const auto energySettings = view.saveSettings();
        view.resetView();
        view.restoreSettings(energySettings);
        check(view.levelProfile_.getSelectedItemIndex() == 1, "Level profile did not round trip");
        view.levelProfile_.setSelectedItemIndex(0, juce::sendNotificationSync);
        preview.setSize(800, 480);
        save(preview, folder, "stereo-level-touch.png");
        preview.setSize(1280, 720);
        view.lissajous_.onClick();
        check(view.mode_ == 1, "Lissajous button failed");
        save(preview, folder, "stereo-lissajous-desktop.png");
        preview.setSize(800, 480);
        save(preview, folder, "stereo-lissajous-touch.png");
        view.polar_.onClick();
        save(preview, folder, "stereo-polar-touch.png");
        view.gain_.setSelectedItemIndex(10, juce::sendNotificationSync);
        check(view.clipped_, "Display clipping was not reported");
        view.resetView();
        view.persistence_.setSelectedItemIndex(0, juce::sendNotificationSync);
        view.setPaused(true);
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        preview.analyzer.process(left, right, left.size());
        view.timerCallback();
        check(view.snapshot_->correlationValid(), "Pause did not hold the stereo snapshot");
        view.setPaused(false);
        view.timerCallback();
        check(!view.snapshot_->signalValid() && view.snapshot_->count == 0,
              "Silence retained the stereo cloud or measurement");
        check(view.status() == "No measurable signal", "Silence status is incorrect");
        check(view.touched_.empty(), "Silence retained occupied pixels");
        for (int y = 0; y < view.density_.getHeight(); ++y)
            for (int x = 0; x < view.density_.getWidth(); ++x)
                check(view.density_.getPixelAt(x, y) == view.background_.getPixelAt(x, y),
                      "Silence failed to restore the cached grid");
        save(preview, folder, "stereo-silence.png");
        std::fill(left.begin(), left.end(), 0.5f);
        preview.analyzer.process(left, right, 2 * left.size());
        view.timerCallback();
        check(view.snapshot_->signalValid() && !view.snapshot_->correlationValid(),
              "One-channel UI measurement validity is incorrect");
        save(preview, folder, "stereo-left-only.png");
        view.selectMode(2);
        const auto levelCorrelation = view.snapshot_->correlation;
        check(!view.cloudBounds_.isEmpty(), "Polar Level omitted the one-channel profile");
        const auto levelReference = view.density_.createCopy();
        view.render();
        for (int y = 0; y < levelReference.getHeight(); ++y)
            for (int x = 0; x < levelReference.getWidth(); ++x)
                check(levelReference.getPixelAt(x, y) == view.density_.getPixelAt(x, y),
                      "Polar Level rendering accumulated a peak trail");
        view.gain_.setSelectedItemIndex(10, juce::sendNotificationSync);
        check(view.clipped_, "Polar Level clipping was not reported");
        check(std::abs(view.snapshot_->correlation - levelCorrelation) < 1e-6f,
              "Polar Level gain changed correlation");
        view.gain_.setSelectedItemIndex(2, juce::sendNotificationSync);
        view.setPaused(true);
        view.lastAudio_ -= 1000;
        view.timerCallback();
        check(!view.stale_, "Paused Polar Level was not held");
        view.setPaused(false);
        view.lastAudio_ -= 1000;
        view.timerCallback();
        check(view.stale_ && view.status() == "Waiting for audio",
              "Missing stream retained stale readouts");
        check(view.cloudBounds_.isEmpty(), "Stale Polar Level retained its profile");
        for (int y = 0; y < view.density_.getHeight(); ++y)
            for (int x = 0; x < view.density_.getWidth(); ++x)
                check(view.density_.getPixelAt(x, y) == view.background_.getPixelAt(x, y),
                      "Stale Polar Level did not restore the background");
        view.stale_ = false;
        for (const auto invalid :
             {StereoValidity::silent, StereoValidity::invalidInput, StereoValidity::waiting})
        {
            view.snapshot_->validity = invalid;
            view.render();
            check(view.cloudBounds_.isEmpty(), "Invalid Polar Level retained a profile");
        }
        // Display envelopes are time-based and never change the measurement.
        view.snapshot_->validity = StereoValidity::valid;
        view.snapshot_->count = 1;
        view.snapshot_->samples[0] = {0.5f, 0.5f};
        view.persistence_.setSelectedItemIndex(2, juce::sendNotificationSync);
        for (const int mode : {0, 1, 2})
        {
            view.selectMode(mode);
            view.render();
            const auto rawCorrelation = view.snapshot_->correlation;
            const auto initial = mode != 2 ? firstBrightness(view) : view.released_.peak[36];
            view.snapshot_->validity = StereoValidity::silent;
            view.lastRender_ = juce::Time::getMillisecondCounterHiRes() - 500;
            view.render();
            const auto remaining = mode != 2 ? firstBrightness(view) : view.released_.peak[36];
            check(std::abs(remaining / initial - 0.1) < 0.01,
                  "Display decay does not follow elapsed time");
            check(std::abs(view.snapshot_->correlation - rawCorrelation) < 1e-7f &&
                      !view.snapshot_->signalValid(),
                  "Display history changed signal validity or correlation");
            view.snapshot_->validity = StereoValidity::valid;
            view.render();
            const auto attacked = mode != 2 ? firstBrightness(view) : view.released_.peak[36];
            check(std::abs(attacked - initial) < 1e-6, "Display attack was delayed");
            view.snapshot_->validity = StereoValidity::silent;
            for (int step = 0; step < 2; ++step)
            {
                view.lastRender_ = juce::Time::getMillisecondCounterHiRes() - 250;
                view.render();
            }
            const auto split = mode != 2 ? firstBrightness(view) : view.released_.peak[36];
            check(std::abs(split / initial - remaining / initial) < 0.01,
                  "Display decay depends on the number of frames");
            view.snapshot_->validity = StereoValidity::valid;
        }
        // Lissajous exposes the same control and drops history immediately with Off.
        view.selectMode(1);
        check(view.persistence_.isEnabled(), "Lissajous disabled the Afterglow control");
        view.persistence_.setSelectedItemIndex(0, juce::sendNotificationSync);
        view.snapshot_->validity = StereoValidity::silent;
        view.render();
        check(view.cloudBounds_.isEmpty(), "Lissajous Off retained display history");
        view.persistence_.setSelectedItemIndex(2, juce::sendNotificationSync);
        // Independent dense reference checks sparse tile expiry and slot reuse.
        for (const int mode : {0, 1})
        {
            view.selectMode(mode);
            view.snapshot_->sampleRate = 48000;
            std::vector<float> reference(view.counts_.size(), 0);
            view.clearHistory();
            for (int frame = 0; frame < 30; ++frame)
            {
                view.snapshot_->validity =
                    frame % 3 == 0 ? StereoValidity::silent : StereoValidity::valid;
                view.snapshot_->samples[0] =
                    frame % 2 == 0 ? StereoSample{0.5f, 0.25f} : StereoSample{0.1f, 0.6f};
                view.lastRender_ = juce::Time::getMillisecondCounterHiRes() - 200;
                view.render();
                std::vector<float> actual(reference.size(), 0);
                for (const auto& tile : view.historyTiles_)
                    for (const auto& point : tile.pixels)
                        if (point.index != std::numeric_limits<unsigned>::max())
                            actual[point.index] = point.brightness;
                juce::Image::BitmapData pixels(view.density_, juce::Image::BitmapData::readOnly);
                juce::Image::BitmapData backdrop(view.background_,
                                                 juce::Image::BitmapData::readOnly);
                for (std::size_t i = 0; i < reference.size(); ++i)
                {
                    reference[i] =
                        std::max(1.0f - std::exp(-0.4f * static_cast<float>(view.counts_[i])),
                                 reference[i] * static_cast<float>(view.decay_));
                    if (reference[i] < 1.0f / 255.0f)
                        reference[i] = 0;
                    check(std::abs(actual[i] - reference[i]) < 1e-6f,
                          "Sparse afterglow differs from dense envelope reference");
                    const int x = static_cast<int>(i % static_cast<std::size_t>(pixels.width));
                    const int y = static_cast<int>(i / static_cast<std::size_t>(pixels.width));
                    auto expected =
                        *reinterpret_cast<const juce::PixelRGB*>(backdrop.getPixelPointer(x, y));
                    if (reference[i] > 0)
                        expected.blend(view.colour_
                                           .withAlpha(static_cast<juce::uint8>(
                                               juce::roundToInt(reference[i] * 255)))
                                           .getPixelARGB());
                    check(expected.getNativeARGB() ==
                              reinterpret_cast<const juce::PixelRGB*>(pixels.getPixelPointer(x, y))
                                  ->getNativeARGB(),
                          "Cached afterglow pixels differ from direct reference rendering");
                }
            }
        }
        view.setActive(false);
        view.setActive(true);
        check(view.snapshot_->count == 0 && view.stale_, "Reactivation reused a stale cloud");
        preview.controls.viewSelector().setTouchMode(true);
        preview.controls.viewSelector().showChoices();
        save(preview, folder, "stereo-selector-touch.png");
        // Exercise every retained sample at the maximum supported rate, not just
        // the lower-rate screenshot fixture. Timing is diagnostic, not a pass threshold.
        preview.setSize(1920, 1080);
        preview.analyzer.reset(384000);
        left.resize(230400);
        right.resize(left.size());
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto t = static_cast<double>(i) / 384000;
            left[i] = static_cast<float>(0.35 * std::sin(2 * std::numbers::pi * 203 * t) +
                                         0.18 * std::sin(2 * std::numbers::pi * 733 * t));
            right[i] = static_cast<float>(0.35 * std::sin(2 * std::numbers::pi * 203 * t) +
                                          0.18 * std::sin(2 * std::numbers::pi * 1103 * t));
        }
        preview.analyzer.process(left, right, 0);
        view.timerCallback();
        check(view.snapshot_->count == 38400, "Maximum-rate UI history lost samples");
        for (int mode = 0; mode < 3; ++mode)
        {
            view.selectMode(mode);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int frame = 0; frame < 30; ++frame)
                view.render();
            std::cout << "Stereo 384 kHz / 1920x1080 "
                      << (mode == 0   ? "Polar Sample"
                          : mode == 1 ? "Lissajous"
                                      : "Polar Level")
                      << ": " << (juce::Time::getMillisecondCounterHiRes() - start) / 30
                      << " ms/render\n";
        }
        // Stress every angle at high level with no live audio device changes.
        // This offline render+paint cost is not a physical refresh-rate measurement.
        view.selectMode(2);
        view.snapshot_->count = 4800;
        view.snapshot_->validity = StereoValidity::valid;
        view.snapshot_->sampleRate = 48000;
        view.stale_ = false;
        for (std::size_t i = 0; i < view.snapshot_->count; ++i)
        {
            const double angle =
                -std::numbers::pi / 2 + static_cast<double>(i % 73) * std::numbers::pi / 72;
            const double l = std::cos(angle) - std::sin(angle);
            const double r = std::cos(angle) + std::sin(angle);
            const double scale = 0.95 / std::max(std::abs(l), std::abs(r));
            view.snapshot_->samples[i] = {static_cast<float>(l * scale),
                                          static_cast<float>(r * scale)};
        }
        juce::Image target(juce::Image::RGB, view.getWidth(), view.getHeight(), true,
                           juce::SoftwareImageType());
        view.render();
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int frame = 0; frame < 60; ++frame)
        {
            view.render();
            juce::Graphics graphics(target);
            juce::RectangleList<int> dirty;
            dirty.add(view.damage_.translated(juce::roundToInt(view.plot().getX()),
                                              juce::roundToInt(view.plot().getY())));
            dirty.add({0, view.getHeight() - 110, view.getWidth(), 110});
            graphics.reduceClipRegion(dirty);
            view.paint(graphics);
        }
        std::cout << "Polar Level 48 kHz / 1920x1080 full-angle stress: "
                  << (juce::Time::getMillisecondCounterHiRes() - start) / 60
                  << " ms/render+paint\n";
        save(preview, folder, "stereo-level-full-angle.png");
        // Match a Retina paint context and switching back to a 1x display.
        preview.setSize(800, 480);
        view.snapshot_->count = 2;
        view.snapshot_->samples[0] = {0.5f, 0.25f};
        view.snapshot_->samples[1] = {-0.3f, -0.6f};
        view.persistence_.setSelectedItemIndex(0, juce::sendNotificationSync);
        const auto correlationBeforeScale = view.snapshot_->correlation;
        for (const int mode : {0, 1, 2})
        {
            view.selectMode(mode);
            for (const float scale : {1.0f, 2.0f, 1.5f, 1.0f})
            {
                const auto screenshot =
                    view.createComponentSnapshot(view.getLocalBounds(), true, scale);
                check(std::abs(view.stats().scale - scale) < 0.001f,
                      "Stereo diagnostics scale is incorrect");
                check(std::abs(view.renderScale_ - scale) < 0.001f,
                      "Stereo cache ignored physical display scale");
                check(view.density_.getWidth() == juce::roundToInt(view.plot().getWidth() * scale),
                      "Stereo cache does not match physical pixel resolution");
                check(std::abs(view.snapshot_->correlation - correlationBeforeScale) < 1e-7f,
                      "Display scale changed measurements");
                check(view.logicalImageBounds(view.damage_).getWidth() <=
                          juce::roundToInt(view.plot().getWidth()) + 2,
                      "Physical damage was not converted to logical bounds");
                if (mode != 2)
                    check(std::accumulate(view.counts_.begin(), view.counts_.end(), 0u) == 8,
                          "Retina rendering lost sample contributions");
                if (scale == 2.0f)
                {
                    auto output =
                        folder.getChildFile("stereo-retina-" + juce::String(mode) + ".png")
                            .createOutputStream();
                    check(output != nullptr, "Cannot write Retina screenshot");
                    output->setPosition(0);
                    output->truncate();
                    check(juce::PNGImageFormat().writeImageToStream(screenshot, *output),
                          "Cannot encode Retina screenshot");
                }
            }
        }
        view.selectMode(1);
        view.setDisplayGainDb(0);
        view.setAfterglow(3);
        view.snapshot_->count = 1;
        view.snapshot_->samples[0] = {0.25f, 0.25f};
        view.snapshot_->validity = StereoValidity::valid;
        view.render();
        view.snapshot_->validity = StereoValidity::silent;
        view.setDisplayGainDb(6);
        check(firstBrightness(view) > 0, "Global gain discarded stereo afterglow");
        const auto centreY = view.density_.getHeight() / 2;
        check(view.cloudBounds_.getBottom() < centreY, "Gain moved mono history across the origin");
        view.resetView();
        check(view.displayGainDb_ == 6 && view.persistence_.getSelectedItemIndex() == 3,
              "Reset view changed global controls");
        view.setAfterglow(0);
        view.render();
        check(view.cloudBounds_.isEmpty(), "Global Afterglow Off retained history");
        for (int mode = 0; mode < 4; ++mode)
        {
            preview.controls.setAnalysisView(mode);
            check(preview.controls.gain().isVisible(), "Global gain hidden in an analysis view");
            check(preview.controls.afterglow().isVisible() == (mode != 0),
                  "Afterglow visibility does not follow analysis view");
        }
        std::cout << "Stereo UI checks passed\n";
    }
};
} // namespace visona

void runStereoUiChecks(const juce::File& folder)
{
    visona::StereoUiChecks::run(folder);
}
