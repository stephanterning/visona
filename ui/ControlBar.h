#pragma once

#include "ChromeButton.h"
#include "ChromeLayout.h"
#include "GainControl.h"
#include "SegmentedControl.h"

#include <visona/WaveformColoring.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace visona
{

/**
    The control bar at the bottom (D-046): display gain and the waveform colouring on the left;
    diagnostics, full screen and settings on the right. No knobs, and touch-sized targets (D-025).

    It reflows in steps (ChromeStep): labels shorten when narrow, the groups wrap onto two rows if
    they do not fit on one, and in the compact step the secondary buttons move into the settings
    panel.
*/
class ControlBar final : public juce::Component
{
public:
    ControlBar();

    [[nodiscard]] GainControl& gain() noexcept
    {
        return gain_;
    }

    std::function<void(WaveformColoring)> onColoring;
    std::function<void()> onDiagnostics;
    std::function<void()> onFullScreen;
    std::function<void()> onSettings;

    void setColoring(WaveformColoring coloring);
    void setToggles(bool diagnostics, bool fullScreen, bool settings);
    void setStep(ChromeStep step);

    /** Whether the diagnostics and full-screen buttons are shown here rather than in settings. */
    [[nodiscard]] bool showsSecondaryControls() const noexcept
    {
        return step_ != ChromeStep::compact;
    }

    /** The height that fits the controls at `width`. */
    [[nodiscard]] int preferredHeight(int width) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    [[nodiscard]] int leftWidth() const;
    [[nodiscard]] int rightWidth() const;
    [[nodiscard]] int rowsFor(int width) const;

    GainControl gain_;
    SegmentedControl coloring_;
    ChromeButton diagnostics_{"Diagnostics", ChromeButton::Icon::diagnostics};
    ChromeButton fullScreen_{"Full screen", ChromeButton::Icon::fullScreen};
    ChromeButton settings_{"Settings", ChromeButton::Icon::settings};

    ChromeStep step_ = ChromeStep::wide;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlBar)
};

} // namespace visona
