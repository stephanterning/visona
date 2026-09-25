#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace visona
{

/**
    How the input channels are grouped into sources. A source is a group of 1..N channels that
    belong together, such as a stereo pair. MVP 1.0 has one source with 2 channels, but nothing
    here or in code that uses it may assume that.

    Channels are numbered consecutively across sources: source 0 has the first channels, source 1
    the next ones, and so on. AudioRingBuffer stores channels in this order.
*/
class SourceLayout
{
public:
    struct Source
    {
        /** Index of the source's first channel in the numbering across all sources. */
        std::size_t firstChannel = 0;
        std::size_t channelCount = 0;

        friend bool operator==(const Source&, const Source&) = default;
    };

    /** No sources and no channels. */
    SourceLayout() = default;

    /** One source per element, with that many channels. Throws std::invalid_argument if any count
        is 0. */
    explicit SourceLayout(std::span<const std::size_t> channelCounts);
    explicit SourceLayout(std::initializer_list<std::size_t> channelCounts);

    [[nodiscard]] std::size_t sourceCount() const noexcept
    {
        return sources_.size();
    }

    /** Channels across all sources. */
    [[nodiscard]] std::size_t totalChannelCount() const noexcept
    {
        return totalChannelCount_;
    }

    /** `index` must be less than sourceCount(). */
    [[nodiscard]] const Source& source(std::size_t index) const noexcept;

    [[nodiscard]] std::span<const Source> sources() const noexcept
    {
        return sources_;
    }

    friend bool operator==(const SourceLayout&, const SourceLayout&) = default;

private:
    std::vector<Source> sources_;
    std::size_t totalChannelCount_ = 0;
};

} // namespace visona
