#include "visona/ClockTimeMapper.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace visona
{

ClockTimeMapper::ClockTimeMapper(double sampleRate, std::size_t blockCapacity)
    : sampleRate_(sampleRate)
    , points_(blockCapacity)
{
    if (blockCapacity < 2)
        throw std::invalid_argument("ClockTimeMapper needs room for at least 2 blocks");
}

void ClockTimeMapper::reset(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    startOver();
    lastSampleIndex_.reset();
    restartCount_ = 0;
}

void ClockTimeMapper::addBlock(const BlockTiming& timing) noexcept
{
    const Point point{timing.sampleIndex, timing.hostTimeNs};

    if (lastSampleIndex_ && point.sampleIndex == *lastSampleIndex_)
        return;
    const bool wentBack = lastSampleIndex_ && point.sampleIndex < *lastSampleIndex_;
    lastSampleIndex_ = point.sampleIndex;

    if (wentBack)
    {
        startOver();
        ++restartCount_;
    }
    else if (const auto fit = line())
    {
        const auto expected =
            fit->meanTime + fit->nsPerFrame * (frameOffset(point.sampleIndex) - fit->meanFrame);
        if (std::abs(timeOffset(point.hostTimeNs) - expected) >
            static_cast<double>(outlierThresholdNs))
        {
            if (++consecutiveOutliers_ < outliersBeforeRestart)
                return;
            startOver();
            ++restartCount_;
        }
    }

    consecutiveOutliers_ = 0;
    append(point);
}

std::optional<double> ClockTimeMapper::sampleAt(std::uint64_t hostTimeNs) const noexcept
{
    const auto fit = line();
    if (!fit)
        return std::nullopt;
    const auto frames = fit->meanFrame + (timeOffset(hostTimeNs) - fit->meanTime) / fit->nsPerFrame;
    return static_cast<double>(baseSampleIndex_) + frames;
}

std::optional<std::uint64_t> ClockTimeMapper::hostTimeAt(double sampleIndex) const noexcept
{
    const auto fit = line();
    if (!fit)
        return std::nullopt;
    const auto frames = sampleIndex - static_cast<double>(baseSampleIndex_);
    const auto offset = std::llround(fit->meanTime + fit->nsPerFrame * (frames - fit->meanFrame));
    if (offset < 0 && static_cast<std::uint64_t>(-offset) > baseHostTimeNs_)
        return 0;
    return baseHostTimeNs_ + static_cast<std::uint64_t>(offset);
}

std::optional<double> ClockTimeMapper::midiSampleTime(std::uint64_t hostTimeNs) const noexcept
{
    if (const auto sample = sampleAt(hostTimeNs))
        return *sample - latencyOffset_;
    return std::nullopt;
}

std::optional<double> ClockTimeMapper::measuredSampleRate() const noexcept
{
    if (const auto fit = line())
        return 1.0e9 / fit->nsPerFrame;
    return std::nullopt;
}

std::optional<ClockTimeMapper::Line> ClockTimeMapper::line() const noexcept
{
    if (!hasModel())
        return std::nullopt;

    const auto n = static_cast<double>(count_);
    Line fit;
    fit.meanFrame = sumFrames_ / n;
    fit.meanTime = sumTimes_ / n;
    fit.nsPerFrame = 1.0e9 / sampleRate_;

    if (count_ >= std::min(minBlocksForFittedRate, points_.size()))
    {
        const auto frameVariance = sumFrameSquares_ - n * fit.meanFrame * fit.meanFrame;
        const auto covariance = sumFrameTimes_ - n * fit.meanFrame * fit.meanTime;
        if (frameVariance > 0.0 && covariance > 0.0)
            fit.nsPerFrame = covariance / frameVariance;
    }
    return fit;
}

double ClockTimeMapper::frameOffset(std::uint64_t sampleIndex) const noexcept
{
    return static_cast<double>(static_cast<std::int64_t>(sampleIndex - baseSampleIndex_));
}

double ClockTimeMapper::timeOffset(std::uint64_t hostTimeNs) const noexcept
{
    return static_cast<double>(static_cast<std::int64_t>(hostTimeNs - baseHostTimeNs_));
}

void ClockTimeMapper::startOver() noexcept
{
    first_ = 0;
    count_ = 0;
    sumFrames_ = 0.0;
    sumTimes_ = 0.0;
    sumFrameSquares_ = 0.0;
    sumFrameTimes_ = 0.0;
    appendsSinceRebase_ = 0;
    consecutiveOutliers_ = 0;
}

void ClockTimeMapper::append(const Point& point) noexcept
{
    if (count_ == 0)
    {
        baseSampleIndex_ = point.sampleIndex;
        baseHostTimeNs_ = point.hostTimeNs;
    }
    if (count_ == points_.size())
    {
        addToSums(points_[first_], -1.0);
        first_ = (first_ + 1) % points_.size();
        --count_;
    }
    points_[(first_ + count_) % points_.size()] = point;
    ++count_;
    addToSums(point, 1.0);

    if (++appendsSinceRebase_ >= points_.size())
        rebase();
}

void ClockTimeMapper::addToSums(const Point& point, double sign) noexcept
{
    const auto frame = frameOffset(point.sampleIndex);
    const auto time = timeOffset(point.hostTimeNs);
    sumFrames_ += sign * frame;
    sumTimes_ += sign * time;
    sumFrameSquares_ += sign * frame * frame;
    sumFrameTimes_ += sign * frame * time;
}

void ClockTimeMapper::rebase() noexcept
{
    const auto& newest = points_[(first_ + count_ - 1) % points_.size()];
    baseSampleIndex_ = newest.sampleIndex;
    baseHostTimeNs_ = newest.hostTimeNs;

    sumFrames_ = 0.0;
    sumTimes_ = 0.0;
    sumFrameSquares_ = 0.0;
    sumFrameTimes_ = 0.0;
    for (std::size_t i = 0; i < count_; ++i)
        addToSums(points_[(first_ + i) % points_.size()], 1.0);
    appendsSinceRebase_ = 0;
}

} // namespace visona
