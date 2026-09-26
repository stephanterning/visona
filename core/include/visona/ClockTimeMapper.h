#pragma once

#include <visona/BlockTiming.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace visona
{

/**
    Maps host time to stream position, so that MIDI events can be placed on the audio's sample
    timeline (architecture.md 3.3).

    It keeps the BlockTiming of the audio blocks from the last historySeconds and fits a straight
    line, sampleIndex = a + b * hostTime, through them by least squares (D-076). The fit absorbs
    callback jitter in the block timestamps and follows slow drift between the audio clock and the
    host clock. With a single block, or a fit that strays more than 1 % from the nominal sample
    rate, it maps through the newest block at the nominal rate instead.

    Blocks the ring dropped are simply missing points on the line. A block that goes back in stream
    position or host time starts the history over.

    One thread only. Storage is allocated in the constructor; nothing else allocates.
*/
class ClockTimeMapper
{
public:
    static constexpr double defaultHistorySeconds = 2.0;

    /** Throws std::invalid_argument if `capacityBlocks` is less than 2. */
    explicit ClockTimeMapper(double sampleRate, std::size_t capacityBlocks = 4096,
                             double historySeconds = defaultHistorySeconds);

    /** Forgets every block, for a new stream at `sampleRate`. */
    void reset(double sampleRate) noexcept;

    void addBlock(const BlockTiming& block) noexcept;

    /** Whether any block is known, so sampleTimeOf() can answer. */
    [[nodiscard]] bool isReady() const noexcept
    {
        return count_ > 0;
    }

    [[nodiscard]] std::size_t numBlocks() const noexcept
    {
        return count_;
    }

    /** The stream position, in fractional frames, at host time `hostTimeNs`. It may lie beyond
        the newest block. Only meaningful when isReady(). */
    [[nodiscard]] double sampleTimeOf(std::uint64_t hostTimeNs) const noexcept;

    /** Frames per second of host time according to the current fit. */
    [[nodiscard]] double framesPerSecond() const noexcept;

private:
    struct Point
    {
        std::uint64_t hostTimeNs = 0;
        std::uint64_t sampleIndex = 0;
    };

    [[nodiscard]] const Point& point(std::size_t age) const noexcept;
    void fit() const noexcept;

    double sampleRate_;
    double historySeconds_;
    std::vector<Point> points_;
    std::size_t newest_ = 0;
    std::size_t count_ = 0;

    // The fit, relative to the newest block: sample offset = intercept_ + slope_ * seconds.
    mutable bool fitted_ = false;
    mutable double slope_ = 0.0;
    mutable double intercept_ = 0.0;
};

} // namespace visona
