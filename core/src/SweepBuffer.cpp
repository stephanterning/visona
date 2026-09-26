#include "visona/SweepBuffer.h"

#include <cassert>
#include <stdexcept>

namespace visona
{

namespace
{

std::size_t checkedCellCount(std::size_t numChannels, std::size_t numBins)
{
    if (numBins == 0)
        throw std::invalid_argument("SweepBuffer needs at least 1 bin");
    if (numChannels > std::numeric_limits<std::size_t>::max() / numBins)
        throw std::length_error("SweepBuffer is too large");
    return numChannels * numBins;
}

} // namespace

SweepBuffer::SweepBuffer(std::size_t numChannels, std::size_t numBins)
    : numChannels_(numChannels)
    , numBins_(numBins)
    , cells_(checkedCellCount(numChannels, numBins))
    , starts_(cells_.size(), unknownStart)
    , bands_(cells_.size())
    , passes_(numBins, 0)
{
}

std::span<const SweepCell> SweepBuffer::channel(std::size_t channel) const noexcept
{
    assert(channel < numChannels_);
    return {cells_.data() + channel * numBins_, numBins_};
}

std::span<const float> SweepBuffer::starts(std::size_t channel) const noexcept
{
    assert(channel < numChannels_);
    return {starts_.data() + channel * numBins_, numBins_};
}

std::span<const BandLevels> SweepBuffer::bands(std::size_t channel) const noexcept
{
    assert(channel < numChannels_);
    return {bands_.data() + channel * numBins_, numBins_};
}

void SweepBuffer::clear() noexcept
{
    std::fill(cells_.begin(), cells_.end(), SweepCell{});
    std::fill(starts_.begin(), starts_.end(), unknownStart);
    std::fill(bands_.begin(), bands_.end(), BandLevels{});
    std::fill(passes_.begin(), passes_.end(), std::uint64_t{0});
    pass_ = 0;
    head_ = 0;
    ++generation_;
}

void SweepBuffer::advanceHead(std::uint64_t pass, std::size_t bin) noexcept
{
    assert(bin < numBins_);
    assert(pass > 0);

    if (pass_ == 0)
    {
        resetBin(bin, pass);
    }
    else
    {
        // Head positions counted in bins since the start of pass 0.
        const auto from = pass_ * numBins_ + head_;
        const auto to = pass * numBins_ + bin;
        assert(to >= from && "the head only moves forward");
        if (to <= from)
            return;

        if (to - from >= numBins_)
        {
            for (std::size_t b = 0; b < numBins_; ++b)
                resetBin(b, b <= bin ? pass : pass - 1);
        }
        else
        {
            for (auto position = from + 1; position <= to; ++position)
                resetBin(static_cast<std::size_t>(position % numBins_), position / numBins_);
        }
    }
    pass_ = pass;
    head_ = bin;
}

void SweepBuffer::jumpHead(std::uint64_t pass, std::size_t bin) noexcept
{
    assert(bin < numBins_);
    assert(pass > pass_ && "a jump starts a later pass");
    resetBin(bin, pass);
    pass_ = pass;
    head_ = bin;
}

void SweepBuffer::addToHead(std::size_t channel, const SweepCell& span) noexcept
{
    assert(channel < numChannels_);
    assert(pass_ > 0);
    cells_[channel * numBins_ + head_].merge(span);
}

void SweepBuffer::addToBin(std::size_t channel, std::size_t bin, const SweepCell& span) noexcept
{
    assert(channel < numChannels_ && bin < numBins_);
    cells_[channel * numBins_ + bin].merge(span);
}

void SweepBuffer::markHeadStart(std::size_t channel, float value) noexcept
{
    assert(pass_ > 0);
    markBinStart(channel, head_, value);
}

void SweepBuffer::markBinStart(std::size_t channel, std::size_t bin, float value) noexcept
{
    assert(channel < numChannels_ && bin < numBins_);
    auto& start = starts_[channel * numBins_ + bin];
    if (start == unknownStart)
        start = value;
}

void SweepBuffer::addBandsToHead(std::size_t channel, const BandLevels& levels) noexcept
{
    assert(pass_ > 0);
    addBandsToBin(channel, head_, levels);
}

void SweepBuffer::addBandsToBin(std::size_t channel, std::size_t bin,
                                const BandLevels& levels) noexcept
{
    assert(channel < numChannels_ && bin < numBins_);
    bands_[channel * numBins_ + bin].merge(levels);
}

void SweepBuffer::copyFrom(const SweepBuffer& source) noexcept
{
    assert(source.numChannels_ == numChannels_ && source.numBins_ == numBins_);
    if (source.numChannels_ != numChannels_ || source.numBins_ != numBins_)
        return;

    const bool sameSweep = source.generation_ == generation_ && pass_ > 0 && source.pass_ > 0;
    if (sameSweep && source.pass_ == pass_ && source.head_ >= head_)
    {
        copyBins(source, head_, source.head_ + 1);
    }
    else if (sameSweep && source.pass_ == pass_ + 1 && source.head_ < head_)
    {
        copyBins(source, head_, numBins_);
        copyBins(source, 0, source.head_ + 1);
    }
    else
    {
        copyBins(source, 0, numBins_);
    }

    pass_ = source.pass_;
    head_ = source.head_;
    generation_ = source.generation_;
}

void SweepBuffer::resetBin(std::size_t bin, std::uint64_t pass) noexcept
{
    passes_[bin] = pass;
    for (std::size_t channel = 0; channel < numChannels_; ++channel)
    {
        const auto index = channel * numBins_ + bin;
        cells_[index] = SweepCell{};
        starts_[index] = unknownStart;
        bands_[index] = BandLevels{};
    }
}

void SweepBuffer::copyBins(const SweepBuffer& source, std::size_t first, std::size_t end) noexcept
{
    const auto count = static_cast<std::ptrdiff_t>(end - first);
    const auto offset = static_cast<std::ptrdiff_t>(first);
    for (std::size_t channel = 0; channel < numChannels_; ++channel)
    {
        const auto channelOffset = static_cast<std::ptrdiff_t>(channel * numBins_) + offset;
        std::copy_n(source.cells_.begin() + channelOffset, count, cells_.begin() + channelOffset);
        std::copy_n(source.starts_.begin() + channelOffset, count, starts_.begin() + channelOffset);
        std::copy_n(source.bands_.begin() + channelOffset, count, bands_.begin() + channelOffset);
    }
    std::copy_n(source.passes_.begin() + offset, count, passes_.begin() + offset);
}

} // namespace visona
