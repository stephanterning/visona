#pragma once

#include "ScopeView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

/**
    A plain-text overlay for checking Visona on real hardware, hidden by default (D-070):

    - audio: the device, sample rate, block size, the level of each channel, and the overrun
      counters;
    - analysis: the analysis thread's load;
    - rendering: frame rate and time per frame;
    - process: Visona's total CPU use.

    It is opaque, so updating it never makes the scope behind it repaint.
*/
class DiagnosticsOverlay final : public juce::Component
{
public:
    struct Channel
    {
        juce::String name;
        juce::String input;
        /** The highest absolute sample value since the previous update. */
        float peak = 0.0f;
    };

    struct Values
    {
        bool running = false;
        juce::String device;
        double sampleRate = 0.0;
        int bufferSize = 0;
        std::uint32_t blockSize = 0;
        std::vector<Channel> channels;
        std::uint64_t overruns = 0;
        std::uint64_t droppedFrames = 0;
        double streamSeconds = 0.0;
        int deviceXruns = 0;
        bool deviceHostTime = false;
        /** Allocations in the audio callback, if the build checks for them. */
        std::optional<std::uint64_t> callbackAllocations;
        bool allocationCheckWorks = true;

        /** Fraction of one CPU core. */
        double analysisLoad = 0.0;
        ScopeView::Stats rendering;
        /** Fraction of one CPU core, if known. */
        std::optional<double> processCpu;
    };

    DiagnosticsOverlay();

    /** Shows `values`. `elapsedSeconds` is the time since the previous update, for the meters. */
    void update(const Values& values, double elapsedSeconds);

    /** The size that fits every row. */
    [[nodiscard]] juce::Rectangle<int> preferredSize() const;

    void paint(juce::Graphics& g) override;

private:
    struct Meter
    {
        float barDb;
        float heldDb;
        double heldSeconds;
    };

    [[nodiscard]] int numRows() const;

    Values values_;
    std::vector<Meter> meters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiagnosticsOverlay)
};

} // namespace visona
