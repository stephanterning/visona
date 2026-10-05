#pragma once

#include "ChromeButton.h"
#include "ChromeLayout.h"
#include "GainControl.h"
#include "TempoControl.h"
#include "WaveformControl.h"
#include "WindowControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace visona
{

/**
    The control bar at the bottom (D-046): the window, the tempo (D-090), display gain and the
    waveform mode (D-091) on the left; diagnostics, full screen and settings on the right. No knobs,
    and touch-sized targets (D-025).

    Everything stays on one row (D-107): each control carries its caption inside, the buttons drop
    their labels for their icons when the labels do not fit, and in the compact step the secondary
    buttons move into the settings panel. Only when even that does not fit do the groups flow onto
    more rows.
*/
class ControlBar final : public juce::Component
{
public:
    ControlBar();

    [[nodiscard]] WindowControl& window() noexcept
    {
        return window_;
    }

    [[nodiscard]] TempoControl& tempo() noexcept
    {
        return tempo_;
    }

    [[nodiscard]] GainControl& gain() noexcept
    {
        return gain_;
    }

    [[nodiscard]] WaveformControl& waveform() noexcept
    {
        return waveform_;
    }

    std::function<void()> onDiagnostics;
    std::function<void()> onFullScreen;
    std::function<void()> onSettings;

    void setToggles(bool diagnostics, bool fullScreen, bool settings);
    void setStep(ChromeStep step);

    /** Hides the tempo group (for example in a plugin where the host sets BPM). */
    void setTempoVisible(bool visible);

    /** Hides the full-screen button (for example in a plugin editor window). */
    void setFullScreenVisible(bool visible);

    /** Whether the diagnostics and full-screen buttons are shown here rather than in settings. */
    [[nodiscard]] bool showsSecondaryControls() const noexcept
    {
        return step_ != ChromeStep::compact;
    }

    /** The height that fits the controls at `width`. */
    [[nodiscard]] int preferredHeight(int width) const;

    /** The narrowest width that fits everything on one row, with icon-only buttons. */
    [[nodiscard]] int minimumWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    /** The groups in order, window, tempo, gain, waveform and buttons, each placed in a row. */
    struct Placement
    {
        std::array<int, 5> rows{};
        int numRows = 1;
        bool buttonLabels = false;
    };

    [[nodiscard]] std::array<int, 5> groupWidths(bool buttonLabels) const;
    /** The width of the groups on one row, with the gaps between them. */
    [[nodiscard]] int rowWidth(const std::array<int, 5>& widths) const;
    [[nodiscard]] Placement place(int width) const;

    WindowControl window_;
    TempoControl tempo_;
    GainControl gain_;
    WaveformControl waveform_;
    ChromeButton diagnostics_{"Diagnostics", ChromeButton::Icon::diagnostics};
    ChromeButton fullScreen_{"Full screen", ChromeButton::Icon::fullScreen};
    ChromeButton settings_{"Settings", ChromeButton::Icon::settings};

    ChromeStep step_ = ChromeStep::wide;
    bool tempoVisible_ = true;
    bool fullScreenVisible_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlBar)
};

} // namespace visona
