#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

/**
    A plain-text readout of the audio input for checking it on real hardware: the device, sample
    rate, block size, the level of each channel, and the overrun counters.
*/
class DebugReadout final : public juce::Component
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
    };

    DebugReadout();

    /** Shows `values`. `elapsedSeconds` is the time since the previous update, for the meters. */
    void update(const Values& values, double elapsedSeconds);

    void paint(juce::Graphics& g) override;

private:
    struct Meter
    {
        float barDb;
        float heldDb;
        double heldSeconds;
    };

    Values values_;
    std::vector<Meter> meters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugReadout)
};

} // namespace visona
