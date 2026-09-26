#pragma once

#include <visona/AudioRingBuffer.h>
#include <visona/ClockTimeMapper.h>
#include <visona/MidiClockEvent.h>
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
    The analysis thread's work, without the thread itself (architecture.md 3.2).

    Each poll():
    1. places the MIDI Clock events from the MIDI queue on the audio's sample timeline with a
       ClockTimeMapper, and hands them to the MidiClockTransport;
    2. drains the audio ring into the SweepAnalyzer as the transport's spans say: free-running
       before the first Start, at interpolated musical positions while running, and writing
       nothing while frozen. Audio after the latest tick waits in the ring for the next one;
    3. publishes a SweepSnapshot for the UI thread if anything changed.

    setStream(), setMidiQueue() and poll() are the analysis side. They must not run concurrently;
    the app calls them under one lock, which only the analysis thread and stream changes take. The
    UI thread is the only consumer of snapshots(). setWindow(), setBandSplitting(),
    setMidiOffset() and takePeak() may be called from any thread.

    All storage, including the three snapshots, is allocated in the constructor. Nothing else
    allocates.
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
        free-running sweep and the transport back to waiting, or follows nothing if `ring` is null.
        The next poll() publishes the change. `ring` must have numChannels() channels and stay alive
        until the next call.
    */
    void setStream(AudioRingBuffer* ring, double sampleRate) noexcept;

    /** Analysis side. Takes MIDI Clock events from `queue`, of which the pipeline is the only
        consumer, or none if it is null. `queue` must stay alive until the next call. */
    void setMidiQueue(MidiClockQueue* queue) noexcept;

    /** Any thread. Selects the musical window, an index into sweepWindowBars. A new window clears
        the musical sweep. */
    void setWindow(std::size_t windowIndex) noexcept;

    /** Any thread. Turns the band splitting for DJ colouring on or off (D-092). */
    void setBandSplitting(bool enabled) noexcept;

    /** Any thread. Frames added to every MIDI event's mapped position, for the latency between the
        audio and MIDI timestamps (D-078). */
    void setMidiOffset(double frames) noexcept;

    /**
        Analysis side. Handles the waiting MIDI events and analyzes the audio in the ring as far as
        the transport allows. Publishes a snapshot if anything changed. Returns the number of frames
        analyzed.
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
    void mapBlock(const BlockTiming& block) noexcept;

    /** Returns false if the events must wait for the stream's first block. */
    bool handleMidi() noexcept;
    void analyze(const AudioRingBuffer::ReadRegion& region, std::size_t numFrames,
                 const TransportSpan& span) noexcept;
    void followStart(std::uint64_t startCount) noexcept;
    [[nodiscard]] double windowTicks() const noexcept;
    void publish() noexcept;

    SweepAnalyzer analyzer_;
    TripleBuffer<SweepSnapshot> snapshots_;
    ClockTimeMapper mapper_;
    MidiClockTransport transport_;

    AudioRingBuffer* ring_ = nullptr;
    MidiClockQueue* midi_ = nullptr;
    std::uint64_t streamId_ = 0;
    double sampleRate_ = 0.0;
    std::uint64_t nextSampleIndex_ = 0;
    bool changed_ = true;

    std::atomic<std::size_t> requestedWindow_;
    std::atomic<double> midiOffset_{0.0};
    std::atomic<bool> bandSplitting_{false};
    std::size_t window_;

    // The block last added to the mapper, and the Start the musical sweep belongs to.
    bool mappedAnyBlock_ = false;
    std::uint64_t mappedBlock_ = 0;
    bool followsStart_ = false;
    std::uint64_t followedStart_ = 0;

    std::uint64_t midiEvents_ = 0;
    TransportState publishedState_ = TransportState::waiting;

    std::vector<const float*> channels_;
    std::vector<std::atomic<float>> peaks_;
};

} // namespace visona
