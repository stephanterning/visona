#pragma once

#include <cstdint>
#include <optional>

namespace visona
{

/** Message-thread watchdog, polled every 500 ms. A stopped or stalled input gets two
    seconds to recover before reopening. Device callbacks only supply an error flag. */
class AudioDeviceWatchdog
{
public:
    [[nodiscard]] bool shouldRestart(bool inputSelected, bool playing,
                                     std::optional<std::uint64_t> frames, bool error) noexcept
    {
        if (!inputSelected)
        {
            reset();
            return false;
        }
        if (error)
        {
            reset();
            return true;
        }
        if (playing && frames && (!lastFrames_ || *frames != *lastFrames_))
        {
            lastFrames_ = frames;
            stalledPolls_ = 0;
            return false;
        }
        if (++stalledPolls_ < 4)
            return false;
        reset();
        return true;
    }

    void reset() noexcept
    {
        lastFrames_.reset();
        stalledPolls_ = 0;
    }

private:
    std::optional<std::uint64_t> lastFrames_;
    int stalledPolls_ = 0;
};

} // namespace visona
