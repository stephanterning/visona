#pragma once

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

    friend bool operator==(const SweepCell&, const SweepCell&) = default;
};

/**
    The peak level of each split band in one bin, what DJ colouring is mixed from (D-092). All 0
    where the bands were not analyzed, since band splitting only runs while DJ colouring is shown.
*/
struct BandLevels
{
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !(low > 0.0f || mid > 0.0f || high > 0.0f);
    }

    /** Raises each level to `other`'s where that is higher. */
    void merge(const BandLevels& other) noexcept
    {
        low = std::max(low, other.low);
        mid = std::max(mid, other.mid);
        high = std::max(high, other.high);
    }

    friend bool operator==(const BandLevels&, const BandLevels&) = default;
};

/**
    The data behind the sweep display: for every channel, a fixed number of bins across the window,
    each holding the full-band signed min/max of its samples (D-050, D-054). The renderer reduces
    bins to pixel columns, so the number of bins never depends on the screen.

    Each bin also holds the signal's value where the bin starts, for drawing the waveform as a line
    (D-091), and the peak level of each split band, for DJ colouring (D-092).

    Every bin is stamped with the pass that last wrote it. Passes are numbered from 1; pass 0 marks
   a bin that has not been written since the buffer was cleared. The write head is the bin written
    most recently. Bins behind the head belong to the head's pass and bins ahead of it to the
    previous pass, which the renderer dims.

    Channels are numbered as in SourceLayout. Storage is allocated in the constructor; no other
    function allocates.
*/
class SweepBuffer
{
public:
    /** B, the number of bins per window (D-054, D-083): at the deepest zoom, 1/32 of the window,
        about one bin per physical pixel on a 16-inch MacBook Pro's Retina display. */
    static constexpr std::size_t defaultBinCount = 131'072;

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

    /** The cells of one channel, numBins() long. */
    [[nodiscard]] std::span<const SweepCell> channel(std::size_t channel) const noexcept;

    /** A start that is not known: the bin is empty. */
    static constexpr float unknownStart = std::numeric_limits<float>::infinity();

    /** Where the signal of one channel enters each bin, numBins() long: the first sample in it,
        or where the line from the sample before crosses into it (D-084). unknownStart where no
        sample has reached the bin. */
    [[nodiscard]] std::span<const float> starts(std::size_t channel) const noexcept;

    /** The band levels of one channel, numBins() long. */
    [[nodiscard]] std::span<const BandLevels> bands(std::size_t channel) const noexcept;

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

    /**
        Moves the head to bin `bin` of pass `pass`, which must be a later pass, without emptying the
        bins in between: after a relocation, the old content becomes the previous pass. Only `bin`
        itself is emptied and stamped.
    */
    void jumpHead(std::uint64_t pass, std::size_t bin) noexcept;

    /** Widens the head bin of `channel` to include `span`. */
    void addToHead(std::size_t channel, const SweepCell& span) noexcept;

    /** Widens bin `bin` of `channel` to include `span`. The bin must be one the head has just
        entered, so that copies stay correct. */
    void addToBin(std::size_t channel, std::size_t bin, const SweepCell& span) noexcept;

    /** Records where the signal of `channel` enters the head bin, unless that is known already. */
    void markHeadStart(std::size_t channel, float value) noexcept;

    /** Records where the signal of `channel` enters bin `bin`, which must be one the head has just
        entered, unless that is known already. */
    void markBinStart(std::size_t channel, std::size_t bin, float value) noexcept;

    /** Raises the band levels of `channel` in the head bin to `levels`. */
    void addBandsToHead(std::size_t channel, const BandLevels& levels) noexcept;

    /** Raises the band levels of `channel` in bin `bin`, which must be one the head has just
        entered, to `levels`. */
    void addBandsToBin(std::size_t channel, std::size_t bin, const BandLevels& levels) noexcept;

    /**
        Makes this buffer a copy of `source`, which must have the same number of channels and bins.

        If this buffer holds an earlier state of the same generation from the same source, only the
        bins from this buffer's head to the source's head are copied, since the head only changes
        bins it passes. That keeps copying each snapshot cheap. Never allocates.
    */
    void copyFrom(const SweepBuffer& source) noexcept;

    friend bool operator==(const SweepBuffer&, const SweepBuffer&) = default;

private:
    void resetBin(std::size_t bin, std::uint64_t pass) noexcept;
    void copyBins(const SweepBuffer& source, std::size_t first, std::size_t end) noexcept;

    std::size_t numChannels_ = 0;
    std::size_t numBins_ = 0;

    // Channel-major: channel c occupies [c * numBins_, (c + 1) * numBins_).
    std::vector<SweepCell> cells_;
    std::vector<float> starts_;
    std::vector<BandLevels> bands_;
    std::vector<std::uint64_t> passes_;

    std::uint64_t pass_ = 0;
    std::size_t head_ = 0;
    std::uint64_t generation_ = 0;
};

} // namespace visona
