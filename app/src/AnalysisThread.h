#pragma once

#include <visona/AnalysisPipeline.h>
#include <visona/AudioRingBuffer.h>
#include <visona/SweepSnapshot.h>
#include <visona/TripleBuffer.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace visona
{

/**
    The analysis thread: every few milliseconds it drains the current stream's audio ring into the
    sweep and publishes a snapshot for the UI (see AnalysisPipeline). It lives as long as the audio
    engine, while streams come and go with the audio device.

    The audio thread never waits for it, and it never waits for the UI: the UI reads snapshots
    through a triple buffer.
*/
class AnalysisThread final : private juce::Thread
{
public:
    /** Starts the thread, following no stream. */
    explicit AnalysisThread(std::size_t numChannels);

    /** Stops the thread. */
    ~AnalysisThread() override;

    /**
        Follows `ring`, a new stream at `sampleRate`, or nothing if `ring` is null. Returns once the
        thread no longer reads the previous ring, which may then be destroyed.
    */
    void setStream(AudioRingBuffer* ring, double sampleRate);

    /** Takes MIDI Clock events from `queue`, which must outlive the thread or the next call. */
    void setMidiQueue(MidiClockQueue* queue);

    /** Any thread. */
    void setWindow(std::size_t windowIndex) noexcept
    {
        pipeline_.setWindow(windowIndex);
    }

    /** Any thread. */
    void setFreeTempo(double bpm) noexcept
    {
        pipeline_.setFreeTempo(bpm);
    }

    /** Any thread. */
    void runFree() noexcept
    {
        pipeline_.runFree();
    }

    /** Any thread. */
    void setPaused(bool paused) noexcept
    {
        pipeline_.setPaused(paused);
    }

    /** Any thread. */
    void setBandSplitting(bool enabled) noexcept
    {
        pipeline_.setBandSplitting(enabled);
    }

    /** Any thread. */
    void setMidiOffset(double frames) noexcept
    {
        pipeline_.setMidiOffset(frames);
    }

    /** Any thread. */
    void setSyncInput(bool enabled) noexcept
    {
        pipeline_.setSyncInput(enabled);
    }

    /** The UI thread's end of the snapshots. Only one thread may fetch from it. */
    [[nodiscard]] TripleBuffer<SweepSnapshot>& snapshots() noexcept
    {
        return pipeline_.snapshots();
    }

    /** Any thread. The highest absolute sample value of `channel` since the previous call. */
    [[nodiscard]] float takePeak(std::size_t channel) noexcept
    {
        return pipeline_.takePeak(channel);
    }

    /** Any thread. The highest absolute sample value of the sync input since the previous call. */
    [[nodiscard]] float takeSyncPeak() noexcept
    {
        return pipeline_.takeSyncPeak();
    }

    /** Any thread. Time spent analyzing since the thread started, in nanoseconds. */
    [[nodiscard]] std::uint64_t busyNanoseconds() const noexcept
    {
        return busyNanoseconds_.load(std::memory_order_relaxed);
    }

private:
    void run() override;

    AnalysisPipeline pipeline_;

    // Held while polling and while changing streams, and by nothing else.
    std::mutex lock_;
    std::atomic<std::uint64_t> busyNanoseconds_{0};
};

} // namespace visona
