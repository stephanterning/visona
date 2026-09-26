#pragma once

#include <visona/BlockTiming.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace visona
{

/**
    Maps host time to stream position and back, to give each MIDI event a sample time
    (architecture §3.3, D-085, D-086).

    The model is a least-squares line through the (sampleIndex, hostTimeNs) of the last N audio
    blocks. Averaging over the window absorbs callback jitter, and the slope follows drift between
    the audio clock and the host clock. Until the window holds minBlocksForFittedRate blocks, or is
    full if it is smaller, the slope is the nominal sample rate: a slope fitted to a few jittery
    blocks is worse than the nominal one.

    A MIDI event's sample time is sampleAt(hostTimeNs) − latencyOffset().

    Block timings are taken like this:
    - A block more than outlierThresholdNs off the line is skipped, such as a callback that ran
      late. outliersBeforeRestart in a row mean the line itself has moved, and the mapper starts
      over from the latest block.
    - A block with the same sampleIndex as the previous one is ignored: the analysis thread sees a
      block's timing again when it reads the block in parts.
    - A block with a lower sampleIndex than the previous one starts over.
    - A jump forward in sampleIndex, such as a block the audio ring dropped (D-062), stays on the
      line and needs nothing special.

    Host times are on the clock of BlockTiming::hostTimeNs and MidiClockEvent::hostTimeNs (D-065).
    All storage is allocated in the constructor. addBlock() and the queries never allocate, lock or
    wait. The class is not thread-safe; only the analysis thread uses it.
*/
class ClockTimeMapper
{
public:
    /** About 5.5 s of 512-frame blocks at 96 kHz. */
    static constexpr std::size_t defaultBlockCapacity = 1024;

    static constexpr std::size_t minBlocksForFittedRate = 64;
    static constexpr std::int64_t outlierThresholdNs = 20'000'000;
    static constexpr int outliersBeforeRestart = 4;

    /** Throws std::invalid_argument if `blockCapacity` is less than 2. */
    explicit ClockTimeMapper(double sampleRate, std::size_t blockCapacity = defaultBlockCapacity);

    /** Forgets every block, for a new stream at `sampleRate`. The latency offset is kept. */
    void reset(double sampleRate) noexcept;

    void addBlock(const BlockTiming& timing) noexcept;

    /** Whether there is a model: a sample rate above 0 and at least one block. */
    [[nodiscard]] bool hasModel() const noexcept
    {
        return sampleRate_ > 0.0 && count_ > 0;
    }

    /** The stream position at `hostTimeNs`, in frames, or std::nullopt without a model. */
    [[nodiscard]] std::optional<double> sampleAt(std::uint64_t hostTimeNs) const noexcept;

    /** The host time of stream position `sampleIndex`, or std::nullopt without a model. */
    [[nodiscard]] std::optional<std::uint64_t> hostTimeAt(double sampleIndex) const noexcept;

    /** The sample time of a MIDI event at `hostTimeNs`: sampleAt(hostTimeNs) − latencyOffset(). */
    [[nodiscard]] std::optional<double> midiSampleTime(std::uint64_t hostTimeNs) const noexcept;

    /** Frames by which MIDI events are moved earlier in the stream. It may be negative; it is 0
        until set. */
    void setLatencyOffset(double frames) noexcept
    {
        latencyOffset_ = frames;
    }

    [[nodiscard]] double latencyOffset() const noexcept
    {
        return latencyOffset_;
    }

    /** Frames per second of host time according to the model, or std::nullopt without a model. */
    [[nodiscard]] std::optional<double> measuredSampleRate() const noexcept;

    /** Blocks in the window. */
    [[nodiscard]] std::size_t blockCount() const noexcept
    {
        return count_;
    }

    /** Times the mapper started over since reset(), after outliers or a lower sampleIndex. */
    [[nodiscard]] std::uint64_t restartCount() const noexcept
    {
        return restartCount_;
    }

private:
    struct Point
    {
        std::uint64_t sampleIndex = 0;
        std::uint64_t hostTimeNs = 0;
    };

    /** hostTime = meanTime + nsPerFrame × (frame − meanFrame), relative to the base point. */
    struct Line
    {
        double meanFrame = 0.0;
        double meanTime = 0.0;
        double nsPerFrame = 0.0;
    };

    [[nodiscard]] std::optional<Line> line() const noexcept;
    [[nodiscard]] double frameOffset(std::uint64_t sampleIndex) const noexcept;
    [[nodiscard]] double timeOffset(std::uint64_t hostTimeNs) const noexcept;
    void startOver() noexcept;
    void append(const Point& point) noexcept;
    void addToSums(const Point& point, double sign) noexcept;
    void rebase() noexcept;

    double sampleRate_;
    double latencyOffset_ = 0.0;

    std::vector<Point> points_;
    std::size_t first_ = 0;
    std::size_t count_ = 0;

    // Sums over the window, relative to a base point so that they stay precise. The base moves to
    // the newest point, and the sums are recomputed, once per window length of blocks.
    std::uint64_t baseSampleIndex_ = 0;
    std::uint64_t baseHostTimeNs_ = 0;
    double sumFrames_ = 0.0;
    double sumTimes_ = 0.0;
    double sumFrameSquares_ = 0.0;
    double sumFrameTimes_ = 0.0;
    std::size_t appendsSinceRebase_ = 0;

    std::optional<std::uint64_t> lastSampleIndex_;
    int consecutiveOutliers_ = 0;
    std::uint64_t restartCount_ = 0;
};

} // namespace visona
