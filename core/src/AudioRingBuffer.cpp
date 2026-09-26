#include "visona/AudioRingBuffer.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace visona
{

namespace
{

std::size_t checkedSampleCount(std::size_t numChannels, std::size_t capacityFrames,
                               std::size_t capacityBlocks)
{
    if (numChannels == 0 || capacityFrames == 0 || capacityBlocks == 0)
        throw std::invalid_argument(
            "AudioRingBuffer needs at least 1 channel, 1 frame and 1 block of capacity");
    if (capacityFrames > std::numeric_limits<std::size_t>::max() / numChannels)
        throw std::length_error("AudioRingBuffer capacity is too large");
    return numChannels * capacityFrames;
}

} // namespace

AudioRingBuffer::ReadRegion::ReadRegion(const BlockTiming& block, std::uint64_t sampleIndex,
                                        std::size_t numFrames, std::size_t numChannels,
                                        const float* firstSample,
                                        std::size_t channelStride) noexcept
    : block_(block)
    , sampleIndex_(sampleIndex)
    , numFrames_(numFrames)
    , numChannels_(numChannels)
    , firstSample_(firstSample)
    , channelStride_(channelStride)
{
}

std::span<const float> AudioRingBuffer::ReadRegion::channel(std::size_t index) const noexcept
{
    assert(index < numChannels_);
    return {firstSample_ + index * channelStride_, numFrames_};
}

AudioRingBuffer::AudioRingBuffer(std::size_t numChannels, std::size_t capacityFrames,
                                 std::size_t capacityBlocks)
    : numChannels_(numChannels)
    , capacityFrames_(capacityFrames)
    , samples_(checkedSampleCount(numChannels, capacityFrames, capacityBlocks))
    , blocks_(capacityBlocks)
{
}

bool AudioRingBuffer::push(std::span<const float* const> channels,
                           const BlockTiming& timing) noexcept
{
    assert(channels.size() == numChannels_);
    if (channels.size() != numChannels_)
        return false;

    const std::size_t numFrames = timing.numFrames;
    if (numFrames == 0)
        return true;
    newestFrameEnd_.store(timing.sampleIndex + numFrames, std::memory_order_relaxed);

    const auto unreadFrames = writeFrame_ - readFrame_.load(std::memory_order_acquire);
    if (numFrames > capacityFrames_ - unreadFrames)
    {
        countOverrun(timing.numFrames);
        return false;
    }

    const auto start = static_cast<std::size_t>(writeFrame_ % capacityFrames_);
    const auto firstPart = std::min(numFrames, capacityFrames_ - start);
    const auto secondPart = numFrames - firstPart;
    for (std::size_t channel = 0; channel < numChannels_; ++channel)
    {
        float* const destination = samples_.data() + channel * capacityFrames_;
        if (const float* const source = channels[channel])
        {
            std::copy_n(source, firstPart, destination + start);
            std::copy_n(source + firstPart, secondPart, destination);
        }
        else
        {
            std::fill_n(destination + start, firstPart, 0.0f);
            std::fill_n(destination, secondPart, 0.0f);
        }
    }

    // The frames written above are not visible to the consumer until the timing is pushed, so a
    // full timing queue simply leaves them in free space.
    if (!blocks_.tryPush(timing))
    {
        countOverrun(timing.numFrames);
        return false;
    }
    writeFrame_ += numFrames;
    return true;
}

std::optional<AudioRingBuffer::ReadRegion> AudioRingBuffer::peek() const noexcept
{
    const BlockTiming* const oldest = blocks_.peek();
    if (oldest == nullptr)
        return std::nullopt;

    const std::size_t unreadInBlock = oldest->numFrames - framesConsumedFromOldestBlock_;
    const auto start =
        static_cast<std::size_t>(readFrame_.load(std::memory_order_relaxed) % capacityFrames_);
    return ReadRegion(*oldest, oldest->sampleIndex + framesConsumedFromOldestBlock_,
                      std::min(unreadInBlock, capacityFrames_ - start), numChannels_,
                      samples_.data() + start, capacityFrames_);
}

void AudioRingBuffer::consume(std::size_t numFrames) noexcept
{
    auto readFrame = readFrame_.load(std::memory_order_relaxed);
    while (numFrames > 0)
    {
        const BlockTiming* const oldest = blocks_.peek();
        assert(oldest != nullptr && "consumed more frames than are unread");
        if (oldest == nullptr)
            break;

        const std::uint32_t unreadInBlock = oldest->numFrames - framesConsumedFromOldestBlock_;
        const auto taken =
            static_cast<std::uint32_t>(std::min<std::size_t>(numFrames, unreadInBlock));
        framesConsumedFromOldestBlock_ += taken;
        readFrame += taken;
        numFrames -= taken;
        if (framesConsumedFromOldestBlock_ == oldest->numFrames)
        {
            static_cast<void>(blocks_.tryPop());
            framesConsumedFromOldestBlock_ = 0;
        }
    }
    readFrame_.store(readFrame, std::memory_order_release);
}

void AudioRingBuffer::countOverrun(std::uint32_t numFrames) noexcept
{
    overruns_.fetch_add(1, std::memory_order_relaxed);
    droppedFrames_.fetch_add(numFrames, std::memory_order_relaxed);
}

} // namespace visona
