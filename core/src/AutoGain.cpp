#include "visona/AutoGain.h"

#include <visona/LaneMapping.h>

#include <algorithm>
#include <cmath>

namespace visona
{

namespace
{

/** Sums of bar lengths may miss a whole hold time by rounding. */
constexpr double holdTolerance = 1.0e-6;

bool fits(float peak, int gainDb) noexcept
{
    return peak * DisplayGain::toLinear(gainDb) <= 1.0f;
}

} // namespace

AutoGain::AutoGain() noexcept
    : holdSeconds_(holdChoices[defaultHoldChoice])
{
}

void AutoGain::reset(int gainDb) noexcept
{
    gainDb_ = DisplayGain::clampDb(gainDb);
    historySize_ = 0;
    historyNext_ = 0;
    heldSeconds_ = 0.0;
    synced_ = false;
}

void AutoGain::setHoldSeconds(double seconds) noexcept
{
    holdSeconds_ = std::max(seconds, 0.0);
}

bool AutoGain::follow(const RecentBarPeaks& recent) noexcept
{
    if (!synced_ || recent.count < seenBars_)
    {
        synced_ = true;
        seenBars_ = recent.count;
        return false;
    }
    bool changed = false;
    for (auto number = std::max(seenBars_ + 1, recent.oldest()); number <= recent.count; ++number)
        changed = addBar(recent.bar(number)) || changed;
    seenBars_ = recent.count;
    return changed;
}

bool AutoGain::addBar(const BarPeak& bar) noexcept
{
    history_[historyNext_] = bar;
    historyNext_ = (historyNext_ + 1) % historyCapacity;
    historySize_ = std::min(historySize_ + 1, historyCapacity);
    heldSeconds_ += bar.seconds;

    auto target = gainDb_;
    if (!fits(bar.peak, gainDb_))
        target = gainFor(bar.peak);
    else if (heldSeconds_ + holdTolerance >= holdSeconds_)
        if (const auto loudest = loudestCountedPeak(); loudest > 0.0f)
            target = std::max(gainDb_, gainFor(loudest));

    if (target == gainDb_)
        return false;
    gainDb_ = target;
    heldSeconds_ = 0.0;
    return true;
}

int AutoGain::gainFor(float peak) noexcept
{
    for (auto gainDb = DisplayGain::maxDb / stepDb * stepDb; gainDb > DisplayGain::minDb;
         gainDb -= stepDb)
        if (fits(peak, gainDb))
            return gainDb;
    return DisplayGain::minDb;
}

float AutoGain::loudestCountedPeak() const noexcept
{
    const auto threshold = std::pow(10.0f, thresholdDb / 20.0f);
    float loudest = 0.0f;
    double seconds = 0.0;
    for (std::size_t back = 1; back <= historySize_ && seconds + holdTolerance < holdSeconds_;
         ++back)
    {
        const auto& bar = history_[(historyNext_ + historyCapacity - back) % historyCapacity];
        seconds += bar.seconds;
        if (bar.peak > threshold)
            loudest = std::max(loudest, bar.peak);
    }
    return loudest;
}

} // namespace visona
