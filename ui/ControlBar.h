#pragma once

#include "ChromeButton.h"
#include "ChromeLayout.h"
#include "GainControl.h"
#include "TempoControl.h"
#include "WaveformControl.h"
#include "WindowControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <functional>

namespace visona
{

/**
    The control bar at the bottom (D-046): the pause button (D-110), the window, the tempo (D-090),
    display gain and the waveform mode (D-091) on the left; diagnostics, full screen and settings on
    the right. No knobs, and touch-sized targets (D-025).

    Everything stays on one row (D-108): each control carries its caption inside, the buttons show
    only their icons (D-113), and in the compact step the secondary buttons move into the settings
    panel. Only when even that does not fit do the groups flow onto
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

    /** What the pause button does when pressed: its icon and tooltip say which (D-110). */
    enum class PauseAction
    {
        pause,
        resume,
        /** While MIDI Clock is stopped or lost, the view is already frozen; this runs it free. */
        runFree
    };

    std::function<void()> onPauseButton;
    std::function<void()> onDiagnostics;
    std::function<void()> onFullScreen;
    std::function<void()> onSettings;

    /** Shows `action` on the pause button, which is lit while the view is paused. */
    void setPauseAction(PauseAction action, const juce::String& tooltip);
    void setToggles(bool diagnostics, bool fullScreen, bool settings);
    void setStep(ChromeStep step);

    /** Hides the pause button (for example in a plugin, where it is of no use). */
    void setPauseVisible(bool visible);

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

    /** The narrowest width that fits everything on one row. */
    [[nodiscard]] int minimumWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr std::size_t numGroups = 6;

    /** The groups in order, pause, window, tempo, gain, waveform and buttons, each placed in a
        row. */
    struct Placement
    {
        std::array<int, numGroups> rows{};
        int numRows = 1;
    };

    [[nodiscard]] std::array<int, numGroups> groupWidths() const;
    /** The width of the groups on one row, with the gaps between them. */
    [[nodiscard]] int rowWidth(const std::array<int, numGroups>& widths) const;
    [[nodiscard]] Placement place(int width) const;

    ChromeButton pause_{"Pause", ChromeButton::Icon::pause};
    WindowControl window_;
    TempoControl tempo_;
    GainControl gain_;
    WaveformControl waveform_;
    ChromeButton diagnostics_{"Diagnostics", ChromeButton::Icon::diagnostics};
    ChromeButton fullScreen_{"Full screen", ChromeButton::Icon::fullScreen};
    ChromeButton settings_{"Settings", ChromeButton::Icon::settings};

    ChromeStep step_ = ChromeStep::wide;
    bool pauseVisible_ = true;
    bool tempoVisible_ = true;
    bool fullScreenVisible_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlBar)
};

} // namespace visona
