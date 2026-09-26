#include "visona/ClockTimeMapper.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace visona
{

namespace
{

constexpr double nanosecondsPerSecond = 1.0e9;
constexpr double maxRateDeviation = 0.01;

std::size_t checkedCapacity(std::size_t capacityBlocks)
{
    if (capacityBlocks < 2)
        throw std::invalid_argument("ClockTimeMapper needs room for at least 2 blocks");
    return capacityBlocks;
}

/** `later - earlier` in seconds, for host times that may be in either order. */
double secondsBetween(std::uint64_t earlier, std::uint64_t later) noexcept
{
    return later >= earlier ? static_cast<double>(later - earlier) / nanosecondsPerSecond
                            : -static_cast<double>(earlier - later) / nanosecondsPerSecond;
}

double framesBetween(std::uint64_t earlier, std::uint64_t later) noexcept
{
    return later >= earlier ? static_cast<double>(later - earlier)
                            : -static_cast<double>(earlier - later);
}

} // namespace

ClockTimeMapper::ClockTimeMapper(double sampleRate, std::size_t capacityBlocks,
                                 double historySeconds)
    : sampleRate_(sampleRate)
    , historySeconds_(historySeconds)
    , points_(checkedCapacity(capacityBlocks))
{
}

void ClockTimeMapper::reset(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    count_ = 0;
    newest_ = 0;
    fitted_ = false;
}

void ClockTimeMapper::addBlock(const BlockTiming& block) noexcept
{
    if (count_ > 0)
    {
        const auto& newest = point(0);
        if (block.sampleIndex <= newest.sampleIndex || block.hostTimeNs < newest.hostTimeNs)
            count_ = 0;
    }

    newest_ = count_ == 0 ? 0 : (newest_ + 1) % points_.size();
    points_[newest_] = {block.hostTimeNs, block.sampleIndex};
    count_ = std::min(count_ + 1, points_.size());

    // Keep at least two blocks, however old, so there is always a line to fit.
    while (count_ > 2 &&
           secondsBetween(point(count_ - 1).hostTimeNs, block.hostTimeNs) > historySeconds_)
        --count_;
    fitted_ = false;
}

double ClockTimeMapper::sampleTimeOf(std::uint64_t hostTimeNs) const noexcept
{
    assert(isReady());
    if (count_ == 0)
        return 0.0;
    fit();
    const auto& newest = point(0);
    return static_cast<double>(newest.sampleIndex) + intercept_ +
           slope_ * secondsBetween(newest.hostTimeNs, hostTimeNs);
}

double ClockTimeMapper::framesPerSecond() const noexcept
{
    if (count_ == 0)
        return sampleRate_;
    fit();
    return slope_;
}

const ClockTimeMapper::Point& ClockTimeMapper::point(std::size_t age) const noexcept
{
    assert(age < count_);
    return points_[(newest_ + points_.size() - age) % points_.size()];
}

void ClockTimeMapper::fit() const noexcept
{
    if (fitted_)
        return;
    fitted_ = true;
    slope_ = sampleRate_;
    intercept_ = 0.0;
    if (count_ < 2)
        return;

    // Least squares relative to the newest block, which keeps the numbers small.
    const auto& newest = point(0);
    double meanSeconds = 0.0;
    double meanFrames = 0.0;
    for (std::size_t age = 0; age < count_; ++age)
    {
        meanSeconds += secondsBetween(newest.hostTimeNs, point(age).hostTimeNs);
        meanFrames += framesBetween(newest.sampleIndex, point(age).sampleIndex);
    }
    const auto n = static_cast<double>(count_);
    meanSeconds /= n;
    meanFrames /= n;

    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t age = 0; age < count_; ++age)
    {
        const auto dx = secondsBetween(newest.hostTimeNs, point(age).hostTimeNs) - meanSeconds;
        const auto dy = framesBetween(newest.sampleIndex, point(age).sampleIndex) - meanFrames;
        covariance += dx * dy;
        variance += dx * dx;
    }
    if (variance <= 0.0)
        return;

    const auto slope = covariance / variance;
    if (std::abs(slope / sampleRate_ - 1.0) > maxRateDeviation)
        return;
    slope_ = slope;
    intercept_ = meanFrames - slope * meanSeconds;
}

} // namespace visona
