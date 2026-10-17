#pragma once

#include <visona/BandAnalyzer.h>
#include <visona/TripleBuffer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace visona
{
constexpr std::size_t spectrumPoints = 768;
// Pi-validated compromise: 170.7 ms window and 5.86 Hz bin spacing at 48 kHz.
constexpr std::size_t spectrumFftSize = 8192;
constexpr std::size_t spectrumHopSize = 1024;

struct SpectrumCurveSettings
{
    bool enabled = true;
    bool rms = false;
    int speed = 0;     // Fast, Medium, Slow, Hold (peak only).
    int smoothing = 2; // Raw, 1/24, 1/12, 1/6, 1/3, 1 octave.
    int style = 3;     // Line = 0, Fill = 1, Solid = 3; legacy 2 migrates to Solid.
    int colour = 0;    // Spectrum, White, Blue, Orange.
};

struct SpectrumSettings
{
    int channel = 0; // Stereo energy average, Left, Right, Mid, Side.
    std::array<SpectrumCurveSettings, 2> curves{{{}, {true, false, 2, 2, 0, 1}}};
    int topDb = 0;
    int rangeDb = 90;
    int slope = 3; // dB/octave, referenced to 1 kHz; presentation only.
    void sanitize() noexcept;
    [[nodiscard]] std::uint64_t analysisKey() const noexcept;
    static SpectrumSettings fromAnalysisKey(std::uint64_t key) noexcept;
};

struct SpectrumSnapshot
{
    std::array<std::array<float, spectrumPoints>, 2> db{};
    double sampleRate = 0.0;
    double maxFrequency = 20000.0;
    std::uint64_t frameEnd = 0;
    std::uint64_t analysisKey = 0;
    bool valid = false;
};

/** One analysis-thread producer, one UI consumer. Plans and storage are created up front.
    Hann-windowed KISS FFT, amplitude-calibrated to a bin-centred sine. Peak is an instant-attack
    amplitude envelope; RMS is an exponential power average (a sine reads 3.01 dB below Peak).
    Fractional-octave smoothing averages power; it does not change the underlying FFT resolution.
    Input gaps discard the old window. No allocation, locks or I/O during process(). */
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();
    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;
    BandAnalyzer& bands() noexcept
    {
        return bands_;
    }
    void setEnabled(bool enabled) noexcept;
    void setSettings(const SpectrumSettings& settings) noexcept;
    void clearHold() noexcept;
    // Analysis side, including stream changes.
    void reset(double sampleRate) noexcept;
    void process(std::span<const float> left, std::span<const float> right,
                 std::uint64_t firstFrame) noexcept;
    TripleBuffer<SpectrumSnapshot>& snapshots() noexcept
    {
        return snapshots_;
    }

private:
    BandAnalyzer bands_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TripleBuffer<SpectrumSnapshot> snapshots_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> requested_{0};
    std::atomic<unsigned> resetRequests_{0};
};
} // namespace visona
