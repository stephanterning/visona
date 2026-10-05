#include <visona/HeadPacer.h>

#include <algorithm>
#include <cmath>

namespace visona
{

double HeadPacer::update(double seconds, double written, double binsPerSecond) noexcept
{
    const bool paced = binsPerSecond > 0.0 && std::isfinite(binsPerSecond);
    if (!started_ || !paced || !(written >= shown_) ||
        written - shown_ > maxLagSeconds * binsPerSecond)
    {
        started_ = true;
        shown_ = written;
        lastSeconds_ = seconds;
        lastWritten_ = written;
        lastMoveSeconds_ = seconds;
        restartLine(written);
        return shown_;
    }

    const auto elapsed = std::clamp(seconds - lastSeconds_, 0.0, maxFrameSeconds);
    lastSeconds_ = seconds;
    const auto advance = elapsed * binsPerSecond;
    if (written > lastWritten_)
    {
        lastWritten_ = written;
        lastMoveSeconds_ = seconds;
    }
    else if (seconds - lastMoveSeconds_ >= stopSeconds)
    {
        shown_ = std::min(shown_ + advance, written);
        restartLine(written);
        return shown_;
    }

    line_ += advance;
    line_ += lineEasing * (written - line_);
    const auto step = written - line_;
    const auto release = stepRelease * advance;
    lowest_ = std::min(lowest_ + release, step);
    highest_ = std::max(highest_ - release, step);
    const auto target = line_ + lowest_ - margin * (highest_ - lowest_);

    auto next = shown_ + advance;
    next += easing * (target - next);
    shown_ = std::clamp(next, shown_, written);
    return shown_;
}

void HeadPacer::restartLine(double written) noexcept
{
    line_ = written;
    lowest_ = 0.0;
    highest_ = 0.0;
}

} // namespace visona
