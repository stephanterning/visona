#pragma once

#include <visona/AudioRingBuffer.h>
#include <visona/HostTransport.h>
#include <visona/MidiClockTransport.h>
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
    The analysis thread's work for a plugin, driven by a host playhead instead of MIDI Clock.

    Same threading rules as AnalysisPipeline: setStream() and poll() run under one lock on the
    analysis side; setWindow(), setBandSplitting() and takePeak() may run from any thread.
    Host playhead samples are written from the audio thread and read in poll().
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
        Audio thread. Stores the host playhead at the end of the block just written. `sampleTime` is
        the stream frame index after the block.
    */
    void setPlayhead(double sampleTime, const HostTransport::Playhead& playhead) noexcept;

    /** Analysis side. Returns the number of frames analyzed. */
    std::size_t poll() noexcept;

    [[nodiscard]] TripleBuffer<SweepSnapshot>& snapshots() noexcept
    {
        return snapshots_;
    }

    [[nodiscard]] float takePeak(std::size_t channel) noexcept;

private:
    void analyze(const AudioRingBuffer::ReadRegion& region, std::size_t numFrames,
                 const TransportSpan& span) noexcept;
    void followStart(std::uint64_t startCount, std::uint64_t sampleIndex) noexcept;
    [[nodiscard]] double windowTicks() const noexcept;
    void publish() noexcept;
    [[nodiscard]] HostTransport::Playhead playheadForPoll() const noexcept;

    SweepAnalyzer analyzer_;
    TripleBuffer<SweepSnapshot> snapshots_;
    HostTransport transport_;

    AudioRingBuffer* ring_ = nullptr;
    std::uint64_t streamId_ = 0;
    double sampleRate_ = 0.0;
    std::uint64_t nextSampleIndex_ = 0;
    bool changed_ = true;

    std::atomic<std::size_t> requestedWindow_;
    std::atomic<bool> bandSplitting_{false};
    std::size_t window_;

    std::atomic<bool> playheadValid_{false};
    std::atomic<bool> playheadPlaying_{false};
    std::atomic<double> playheadSample_{0.0};
    std::atomic<double> playheadPpq_{0.0};
    std::atomic<double> playheadBpm_{0.0};
    std::atomic<int> playheadNumerator_{4};
    std::atomic<int> playheadDenominator_{4};

    /** Updated when the host sends a valid playhead; used when it drops out briefly. */
    double lastKnownBpm_ = 0.0;
    double lastKnownPpq_ = 0.0;
    bool lastKnownPlaying_ = false;
    TimeSignature lastKnownTimeSignature_{};

    bool followsStart_ = false;
    std::uint64_t followedStart_ = 0;

    TransportState publishedState_ = TransportState::stopped;

    std::vector<const float*> channels_;
    std::vector<std::atomic<float>> peaks_;
};

} // namespace visona
