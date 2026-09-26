#pragma once

#include <visona/AudioRingBuffer.h>
#include <visona/SweepAnalyzer.h>
#include <visona/SweepSnapshot.h>
#include <visona/TripleBuffer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace visona
{

/**
    The analysis thread's work, without the thread itself: poll() drains the audio ring into a
    SweepAnalyzer and publishes a SweepSnapshot for the UI thread.

    setStream() and poll() are the analysis side. They must not run concurrently; the app calls
    both under one lock, which only the analysis thread and stream changes take. The UI thread is
    the only consumer of snapshots(). takePeak() and setWindowSeconds() may be called from any
    thread.

    All storage, including the three snapshots, is allocated in the constructor. setStream() and
    poll() never allocate.
*/
class AnalysisPipeline
{
public:
    /** Throws std::invalid_argument if `numBins` is 0. */
    explicit AnalysisPipeline(std::size_t numChannels,
                              std::size_t numBins = SweepBuffer::defaultBinCount);

    AnalysisPipeline(const AnalysisPipeline&) = delete;
    AnalysisPipeline& operator=(const AnalysisPipeline&) = delete;

    [[nodiscard]] std::size_t numChannels() const noexcept
    {
        return channels_.size();
    }

    /**
        Analysis side. Starts following `ring`, a new stream at `sampleRate`, with a cleared
        free-running sweep, or follows nothing if `ring` is null. The next poll() publishes the
        change. `ring` must have numChannels() channels and stay alive until the next call.
    */
    void setStream(AudioRingBuffer* ring, double sampleRate) noexcept;

    /**
        Any thread. Sets the length of the free-running window, 2 s unless changed (D-060). The
        next poll() restarts the sweep with it. Until PR 7 adds the WINDOW control, this serves the
        temporary debug control (D-076).
    */
    void setWindowSeconds(double seconds) noexcept;

    /**
        Analysis side. Analyzes every frame in the ring and publishes a snapshot if anything
        changed. Returns the number of frames analyzed.
    */
    std::size_t poll() noexcept;

    /** Consumer side: the UI thread fetches and reads snapshots here. */
    [[nodiscard]] TripleBuffer<SweepSnapshot>& snapshots() noexcept
    {
        return snapshots_;
    }

    /** Any thread. The highest absolute sample value of `channel` since the previous call. */
    [[nodiscard]] float takePeak(std::size_t channel) noexcept;

private:
    void startSweep() noexcept;
    void publish() noexcept;

    SweepAnalyzer analyzer_;
    TripleBuffer<SweepSnapshot> snapshots_;

    AudioRingBuffer* ring_ = nullptr;
    std::uint64_t streamId_ = 0;
    double sampleRate_ = 0.0;
    std::uint64_t nextSampleIndex_ = 0;
    bool changed_ = true;

    std::atomic<double> requestedWindowSeconds_{freeRunningWindowSeconds};
    double windowSeconds_ = freeRunningWindowSeconds;

    std::vector<const float*> channels_;
    std::vector<std::atomic<float>> peaks_;
};

} // namespace visona
