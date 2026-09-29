#pragma once

#include <visona/AudioRingBuffer.h>
#include <visona/BarPeaks.h>
#include <visona/HostTransport.h>
#include <visona/MidiClockTransport.h>
#include <visona/SpscQueue.h>
#include <visona/SweepAnalyzer.h>
#include <visona/SweepSnapshot.h>
#include <visona/TripleBuffer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace visona
{

/** A host playhead and the stream index of the frame it belongs to. */
struct TimedPlayhead
{
    std::uint64_t sampleIndex = 0;
    HostTransport::Playhead playhead;
};

/**
    The analysis thread's work for a plugin, driven by a host playhead instead of MIDI Clock.

    Same threading rules as AnalysisPipeline: setStream() and poll() run under one lock on the
    analysis side; setWindow(), setBandSplitting(), setAnalysisOffset() and takePeak() may run from
    any thread. The audio thread hands over the playhead of every block with pushPlayhead().
*/
class HostAnalysisPipeline
{
public:
    explicit HostAnalysisPipeline(std::size_t numChannels,
                                  std::size_t numBins = SweepBuffer::defaultBinCount);

    HostAnalysisPipeline(const HostAnalysisPipeline&) = delete;
    HostAnalysisPipeline& operator=(const HostAnalysisPipeline&) = delete;

    [[nodiscard]] std::size_t numChannels() const noexcept
    {
        return channels_.size();
    }

    void setStream(AudioRingBuffer* ring, double sampleRate) noexcept;

    /** Any thread. Selects the musical window, an index into sweepWindowBars. */
    void setWindow(std::size_t windowIndex) noexcept;

    /** Any thread. Turns the band splitting for DJ colouring on or off (D-092). */
    void setBandSplitting(bool enabled) noexcept;

    /**
        Audio thread. Queues the host playhead of a block. Hosts report the playhead at a block's
        first frame, so `blockStartSample` is the stream index of that frame.
    */
    void pushPlayhead(std::uint64_t blockStartSample,
                      const HostTransport::Playhead& playhead) noexcept;

    /**
        Any thread. How many frames the audio reaching the plugin lags its playhead, as measured
        with Visona Sync. Ableton Live, for one, reports the same playhead to every plugin on a
        track, so the audio after a plugin with latency arrives late. A positive offset draws the
        audio that much earlier. A new offset restarts the sweep.
    */
    void setAnalysisOffset(double frames) noexcept;

    /** Analysis side. Returns the number of frames analyzed. */
    std::size_t poll() noexcept;

    [[nodiscard]] TripleBuffer<SweepSnapshot>& snapshots() noexcept
    {
        return snapshots_;
    }

    [[nodiscard]] float takePeak(std::size_t channel) noexcept;

private:
    void applyOffset() noexcept;
    void syncPlayheads() noexcept;
    void analyze(const AudioRingBuffer::ReadRegion& region, std::size_t numFrames,
                 const TransportSpan& span) noexcept;
    void followStart(std::uint64_t startCount) noexcept;
    [[nodiscard]] double windowTicks() const noexcept;
    void publish() noexcept;

    SweepAnalyzer analyzer_;
    BarPeakMeter barPeaks_;
    TripleBuffer<SweepSnapshot> snapshots_;
    HostTransport transport_;
    SpscQueue<TimedPlayhead> playheads_;

    AudioRingBuffer* ring_ = nullptr;
    std::uint64_t streamId_ = 0;
    double sampleRate_ = 0.0;
    std::uint64_t nextSampleIndex_ = 0;
    bool changed_ = true;

    std::atomic<std::size_t> requestedWindow_;
    std::atomic<bool> bandSplitting_{false};
    std::size_t window_;

    std::atomic<double> requestedOffset_{0.0};
    /** The offset the timeline was built with, in whole frames. */
    double offset_ = 0.0;

    /** The newest valid playhead, to go on from across blocks where the host reports none. */
    bool hasLastPlayhead_ = false;
    TimedPlayhead lastPlayhead_;

    bool followsStart_ = false;
    std::uint64_t followedStart_ = 0;

    TransportState publishedState_ = TransportState::stopped;

    std::vector<const float*> channels_;
    std::vector<std::atomic<float>> peaks_;
};

} // namespace visona
