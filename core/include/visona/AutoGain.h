#pragma once

#include <visona/BarPeaks.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace visona
{

/**
    Auto gain (D-100): picks the display gain in 3 dB steps, from 0 dB to DisplayGain::maxDb, from
    the peaks of the bars. It is presentation only, like the gain it sets (D-024).

    - As soon as a peak of the bar in progress lands above the top of the lane, the gain drops to
      the highest step that fits it.
    - At the end of each bar, once the gain has held for the hold time, it rises at once to the
      highest step that fits the loudest peak of the bars in the last hold time, if that is higher.

    The highest step that fits a peak puts it within the top 3 dB of the lane. Bars whose peak is
    at or below thresholdDb do not count when rising, so silence never zooms in; a loud peak keeps
    the gain down for the whole hold time after it.

    Message thread only. Nothing allocates.
*/
class AutoGain
{
public:
    static constexpr int stepDb = 3;
    static constexpr float thresholdDb = -50.0f;

    /** How long the peaks must stay low before the gain rises, in seconds (D-108). */
    static constexpr double defaultHoldSeconds = 10.0;

    AutoGain() noexcept;

    /** Starts over at `gainDb`, clamped to DisplayGain's range, forgetting every bar. The next
        call to follow() only notes which bars have ended so far. */
    void reset(int gainDb) noexcept;

    void setHoldSeconds(double seconds) noexcept;

    [[nodiscard]] double holdSeconds() const noexcept
    {
        return holdSeconds_;
    }

    /** Adds the bars that ended since the previous call, then drops the gain if the bar in progress
        goes past the lane. Returns whether the gain changed. */
    bool follow(const RecentBarPeaks& recent) noexcept;

    /** Drops the gain if `peak`, of the bar in progress, goes past the lane. Returns whether the
        gain changed. */
    bool notePeak(float peak) noexcept;

    /** Adds one bar that has ended. Returns whether the gain changed. */
    bool addBar(const BarPeak& bar) noexcept;

    [[nodiscard]] int gainDb() const noexcept
    {
        return gainDb_;
    }

    /** The highest step, from 0 dB to DisplayGain::maxDb, at which `peak` fits in the lane. */
    [[nodiscard]] static int gainFor(float peak) noexcept;

private:
    /** The bars held for rising: more than the longest hold time at 300 BPM in 1/16. */
    static constexpr std::size_t historyCapacity = 2'048;

    [[nodiscard]] float loudestCountedPeak() const noexcept;

    std::array<BarPeak, historyCapacity> history_{};
    std::size_t historySize_ = 0;
    std::size_t historyNext_ = 0;

    double holdSeconds_;
    double heldSeconds_ = 0.0;
    int gainDb_ = 0;

    bool synced_ = false;
    std::uint64_t seenBars_ = 0;
};

} // namespace visona
