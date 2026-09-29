#pragma once

#include <visona/MidiClockTransport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace visona
{

/** A bar that has ended: its loudest sample over every channel, and how long it lasted. */
struct BarPeak
{
    float peak = 0.0f;
    double seconds = 0.0;
};

/**
    The latest bars that have ended, for auto gain (D-098). Bars are numbered from 1 in the order
    they end; the numbers never go back, even across streams.
*/
struct RecentBarPeaks
{
    static constexpr std::size_t capacity = 16;

    /** How many bars have ended in all. */
    std::uint64_t count = 0;

    /** Bar `number`, which must be among the latest min(count, capacity). */
    [[nodiscard]] const BarPeak& bar(std::uint64_t number) const noexcept
    {
        return bars[(number - 1) % capacity];
    }

    /** The oldest bar still held. */
    [[nodiscard]] std::uint64_t oldest() const noexcept
    {
        return count > capacity ? count - capacity + 1 : 1;
    }

    std::array<BarPeak, capacity> bars{};
};

/**
    Measures the peak of each bar on the analysis thread. A frame belongs to the bar its position
    in ticks lies in, with bar 1 starting at tick 0. A bar ends when a frame lands in another bar,
    so a bar that is still being played, or the one the transport stopped in, has not ended.

    One thread only. Nothing allocates.
*/
class BarPeakMeter
{
public:
    /** Forgets the bar in progress without ending it, such as when the sweep starts over. */
    void restart() noexcept;

    /**
        Adds `numFrames` frames from stream position `sampleIndex` on, at the positions a musical
        `span` gives them. `channels` holds one pointer per channel; a null pointer is silence.
    */
    void process(std::span<const float* const> channels, std::size_t numFrames,
                 std::uint64_t sampleIndex, const TransportSpan& span, double ticksPerBar,
                 double sampleRate) noexcept;

    [[nodiscard]] const RecentBarPeaks& recent() const noexcept
    {
        return recent_;
    }

private:
    void endBar(double sampleRate) noexcept;

    RecentBarPeaks recent_;
    bool inBar_ = false;
    std::int64_t bar_ = 0;
    float peak_ = 0.0f;
    std::uint64_t frames_ = 0;
};

} // namespace visona
