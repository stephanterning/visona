#pragma once
#include <visona/TripleBuffer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace visona
{
constexpr std::size_t spectrumBands = 31;
struct BandSnapshot
{
    std::array<float, spectrumBands> db{}, peakDb{};
    double sampleRate = 0;
    std::uint64_t frameEnd = 0;
    bool valid = false;
};
/** Streaming fourth-order bandpass bank. Stereo energy average, peak-calibrated bars,
    150 ms release; markers hold for one second then fall 12 dB/s. No FFT window.
    Construct/reset/process on the analysis thread; enable/reset requests from the UI.
    No allocation, locking or I/O in process. */
class BandAnalyzer
{
public:
    static double frequency(std::size_t band) noexcept;
    void setEnabled(bool enabled) noexcept
    {
        enabled_.store(enabled);
    }
    void setHoldSeconds(double seconds) noexcept
    {
        requestedHoldMs_.store(static_cast<unsigned>(seconds * 1000.0));
    }
    void clearPeaks() noexcept
    {
        resetRequests_.fetch_add(1);
    }
    void reset(double sampleRate) noexcept;
    void process(std::span<const float> left, std::span<const float> right,
                 std::uint64_t firstFrame) noexcept;
    TripleBuffer<BandSnapshot>& snapshots() noexcept
    {
        return snapshots_;
    }

private:
    struct Filter
    {
        double b0 = 0, a1 = 0, a2 = 0;
        std::array<std::array<double, 2>, 4> state{};
        double tick(double x, std::size_t section) noexcept;
    };
    void clear() noexcept;
    std::array<Filter, spectrumBands> filters_{};
    std::array<double, spectrumBands> envelope_{}, peak_{};
    std::array<std::uint32_t, spectrumBands> hold_{};
    std::array<bool, spectrumBands> available_{};
    double rate_ = 0, release_ = 0, peakRelease_ = 0;
    std::uint32_t holdSamples_ = 0, publishSamples_ = 1, sincePublish_ = 0;
    std::uint64_t nextFrame_ = 0;
    unsigned resetRequest_ = 0;
    bool active_ = false;
    std::atomic<bool> enabled_{false};
    std::atomic<unsigned> resetRequests_{0};
    std::atomic<unsigned> requestedHoldMs_{1000};
    TripleBuffer<BandSnapshot> snapshots_;
};
} // namespace visona
