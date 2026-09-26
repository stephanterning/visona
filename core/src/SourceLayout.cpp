#include "visona/SourceLayout.h"

#include <cassert>
#include <stdexcept>

namespace visona
{

SourceLayout::SourceLayout(std::span<const std::size_t> channelCounts)
{
    sources_.reserve(channelCounts.size());
    for (const auto channelCount : channelCounts)
    {
        if (channelCount == 0)
            throw std::invalid_argument("A source must have at least 1 channel");
        sources_.push_back({totalChannelCount_, channelCount});
        totalChannelCount_ += channelCount;
    }
}

SourceLayout::SourceLayout(std::initializer_list<std::size_t> channelCounts)
    : SourceLayout(std::span<const std::size_t>(channelCounts.begin(), channelCounts.size()))
{
}

const SourceLayout::Source& SourceLayout::source(std::size_t index) const noexcept
{
    assert(index < sources_.size());
    return sources_[index];
}

} // namespace visona
