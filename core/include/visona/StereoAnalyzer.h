#pragma once

#include <visona/TripleBuffer.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace visona
{
inline constexpr double stereoMaxSampleRate = 384000.0;
inline constexpr std::size_t stereoHistoryCapacity = 38400; // 100 ms at 384 kHz.
inline constexpr double stereoGateOpenDb = -90.0;
inline constexpr double stereoGateCloseDb = -96.0;

struct StereoSample
{
    float left{}, right{};
};

enum class StereoValidity
{
    waiting,
    silent,
    leftOnly,
    rightOnly,
    valid,
    invalidInput
};

struct StereoSnapshot
{
    std::array<StereoSample, stereoHistoryCapacity> samples{};
    std::size_t count = 0;
    double sampleRate = 0;
    std::uint64_t frameEnd = 0;
    StereoValidity validity = StereoValidity::waiting;
    float correlation = 0;
    float balance = 0; // Equal-power balance: -1 left, +1 right.
    float leftRmsDb = -160, rightRmsDb = -160;
    bool signalValid() const noexcept
    {
        return validity == StereoValidity::valid || validity == StereoValidity::leftOnly ||
               validity == StereoValidity::rightOnly;
    }
    bool correlationValid() const noexcept
    {
        return validity == StereoValidity::valid;
    }
};

/** Analysis-thread stereo measurement, independent of transport and display mode.
    A 200 ms rectangular window is measured with LSP at 120 Hz, from a fresh meter
    state each time. Per-channel RMS gates use -90/-96 dBFS hysteresis. Snapshots
    contain every sample in the last 100 ms, without display decimation.
    Storage is allocated in the constructor. reset/process do not allocate or lock.
    setEnabled may be called from the UI; reset/process have one analysis owner. */
class StereoAnalyzer
{
public:
    StereoAnalyzer();
    ~StereoAnalyzer();
    StereoAnalyzer(const StereoAnalyzer&) = delete;
    StereoAnalyzer& operator=(const StereoAnalyzer&) = delete;
    void setEnabled(bool enabled) noexcept;
    void reset(double sampleRate) noexcept;
    void process(std::span<const float> left, std::span<const float> right,
                 std::uint64_t firstFrame) noexcept;
    TripleBuffer<StereoSnapshot>& snapshots() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    // A generation changes even if the UI disables/re-enables between process calls.
    std::atomic<std::uint64_t> request_{0};
};
} // namespace visona
