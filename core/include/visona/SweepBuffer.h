#pragma once

#include <visona/Band.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace visona
{

/**
    The signed minimum and maximum of the samples that fell into one bin (D-050). A cell that no
    sample has reached is empty: its min is above its max.
*/
struct SweepCell
{
    float min = std::numeric_limits<float>::infinity();
    float max = -std::numeric_limits<float>::infinity();

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return min > max;
    }

    /** Widens the cell to include `other`. NaN values are ignored. */
    void merge(const SweepCell& other) noexcept
    {
        min = std::min(min, other.min);
        max = std::max(max, other.max);
    }

    /** Widens the cell to include `value`. NaN is ignored. */
    void add(float value) noexcept
    {
        min = std::min(min, value);
        max = std::max(max, value);
    }

    friend bool operator==(const SweepCell&, const SweepCell&) = default;
};

/**
    The data behind the sweep display: for every channel, a fixed number of bins across the window,
    each holding the signed min/max of its samples in every Band (D-050, D-054, D-091). The full
    band defines the waveform's shape; low, mid and high drive the colouring only (D-056). The
    renderer reduces bins to pixel columns, so the number of bins never depends on the screen.

    Every bin is stamped with the pass that last wrote it. Passes are numbered from 1; pass 0 marks
   a bin that has not been written since the buffer was cleared. The write head is the bin written
    most recently. Bins behind the head belong to the head's pass and bins ahead of it to the
    previous pass.

    Channels are numbered as in SourceLayout. Storage is allocated in the constructor; no other
    function allocates.
*/
class SweepBuffer
{
public:
    /** B, the number of bins per window (D-054). */
    static constexpr std::size_t defaultBinCount = 4096;

    /** No channels and no bins. */
    SweepBuffer() = default;

    /** An empty buffer. Throws std::invalid_argument if `numBins` is 0. */
    explicit SweepBuffer(std::size_t numChannels, std::size_t numBins = defaultBinCount);

    [[nodiscard]] std::size_t numChannels() const noexcept
    {
        return numChannels_;
    }

    [[nodiscard]] std::size_t numBins() const noexcept
    {
        return numBins_;
    }

    /** The full-band cells of one channel, numBins() long. */
    [[nodiscard]] std::span<const SweepCell> channel(std::size_t channel) const noexcept
    {
        return band(channel, Band::full);
    }

    /** The cells of one band of one channel, numBins() long. */
    [[nodiscard]] std::span<const SweepCell> band(std::size_t channel, Band band) const noexcept;

    /** The pass that last wrote each bin, numBins() long. */
    [[nodiscard]] std::span<const std::uint64_t> passes() const noexcept
    {
        return passes_;
    }

    /** The pass the head is in, or 0 if nothing has been written since the buffer was cleared. */
    [[nodiscard]] std::uint64_t pass() const noexcept
    {
        return pass_;
    }

    /** The bin written most recently. Only meaningful while pass() is above 0. */
    [[nodiscard]] std::size_t head() const noexcept
    {
        return head_;
    }

    /** Counts the calls to clear(). Within one generation the head only moves forward. */
    [[nodiscard]] std::uint64_t generation() const noexcept
    {
        return generation_;
    }

    /** Empties every bin, stamps it with pass 0 and starts a new generation. */
    void clear() noexcept;

    /**
        Moves the head forward to bin `bin` of pass `pass`, which must not be behind the head, and
        empties every bin it enters on the way, stamped with the pass it is entered in. After
        clear(), only `bin` itself is stamped; the bins before it stay empty with pass 0.
    */
    void advanceHead(std::uint64_t pass, std::size_t bin) noexcept;

    /** Widens the full-band head bin of `channel` to include `span`. */
    void addToHead(std::size_t channel, const SweepCell& span) noexcept
    {
        addToHead(channel, Band::full, span);
    }

    /** Widens the head bin of `band` of `channel` to include `span`. */
    void addToHead(std::size_t channel, Band band, const SweepCell& span) noexcept;

    /**
        Makes this buffer a copy of `source`, which must have the same number of channels and bins.

        If this buffer holds an earlier state of the same generation from the same source, only the
        bins from this buffer's head to the source's head are copied, since the head only changes
        bins it passes. That keeps copying each snapshot cheap. Never allocates.
    */
    void copyFrom(const SweepBuffer& source) noexcept;

    friend bool operator==(const SweepBuffer&, const SweepBuffer&) = default;

private:
    [[nodiscard]] std::size_t rowOffset(std::size_t channel, Band band) const noexcept
    {
        return (channel * bandCount + static_cast<std::size_t>(band)) * numBins_;
    }

    void resetBin(std::size_t bin, std::uint64_t pass) noexcept;
    void copyBins(const SweepBuffer& source, std::size_t first, std::size_t end) noexcept;

    std::size_t numChannels_ = 0;
    std::size_t numBins_ = 0;

    // One row of numBins_ cells per channel and band, channel-major: rowOffset() is where a row
    // starts.
    std::vector<SweepCell> cells_;
    std::vector<std::uint64_t> passes_;

    std::uint64_t pass_ = 0;
    std::size_t head_ = 0;
    std::uint64_t generation_ = 0;
};

} // namespace visona
