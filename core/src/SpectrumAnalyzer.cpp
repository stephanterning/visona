#include <visona/SpectrumAnalyzer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <kiss_fft.h>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace visona
{
void SpectrumSettings::sanitize() noexcept
{
    channel = std::clamp(channel, 0, 4);
    topDb = std::clamp(topDb, -60, 12);
    rangeDb = std::clamp(rangeDb, 30, 120);
    slope = std::clamp(slope, 0, 6);
    for (auto& c : curves)
    {
        c.speed = std::clamp(c.speed, 0, 3);
        c.smoothing = std::clamp(c.smoothing, 0, 5);
        c.style = std::clamp(c.style, 0, 3);
        if (c.style == 2)
            c.style = 3;
        c.colour = std::clamp(c.colour, 0, 3);
        if (c.rms && c.speed == 3)
            c.speed = 2;
    }
}
std::uint64_t SpectrumSettings::analysisKey() const noexcept
{
    auto s = *this;
    s.sanitize();
    auto key = static_cast<std::uint64_t>(s.channel);
    for (std::size_t i = 0; i < s.curves.size(); ++i)
    {
        const auto& c = s.curves[i];
        const auto bits = static_cast<std::uint64_t>(c.rms) |
                          (static_cast<std::uint64_t>(c.speed) << 1U) |
                          (static_cast<std::uint64_t>(c.smoothing) << 3U);
        key |= bits << (8U + i * 8U);
    }
    return key;
}
SpectrumSettings SpectrumSettings::fromAnalysisKey(std::uint64_t key) noexcept
{
    SpectrumSettings s;
    s.channel = static_cast<int>(key & 7U);
    for (std::size_t i = 0; i < s.curves.size(); ++i)
    {
        const auto bits = key >> (8U + i * 8U);
        s.curves[i].rms = (bits & 1U) != 0;
        s.curves[i].speed = static_cast<int>((bits >> 1U) & 3U);
        s.curves[i].smoothing = static_cast<int>((bits >> 3U) & 7U);
    }
    s.sanitize();
    return s;
}
struct SpectrumAnalyzer::Impl
{
    kiss_fft_cfg plan = kiss_fft_alloc(static_cast<int>(spectrumFftSize), 0, nullptr, nullptr);
    std::vector<kiss_fft_cpx> input{spectrumFftSize}, output{spectrumFftSize};
    std::array<std::array<float, spectrumFftSize>, 2> ring{};
    std::array<float, spectrumFftSize> window{};
    std::array<float, spectrumFftSize / 2 + 1> power{};
    std::array<std::array<float, spectrumPoints>, 2> envelope{};
    double rate = 0.0;
    std::size_t position = 0, filled = 0, sinceFft = 0;
    std::uint64_t nextFrame = 0, key = 0;
    unsigned resetRequest = 0;
    bool active = false;
    std::array<bool, 2> first{true, true};
    Impl()
    {
        if (!plan)
            throw std::bad_alloc();
        for (std::size_t i = 0; i < spectrumFftSize; ++i)
            window[i] = static_cast<float>(
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                     static_cast<double>(spectrumFftSize)));
    }
    ~Impl()
    {
        kiss_fft_free(plan);
    }
    void clear() noexcept
    {
        position = filled = sinceFft = 0;
        first.fill(true);
        for (auto& e : envelope)
            e.fill(0.0f);
    }
    void transform(int channel) noexcept
    {
        power.fill(0.0f);
        const auto passes = channel == 0 ? 2 : 1;
        for (int pass = 0; pass < passes; ++pass)
        {
            for (std::size_t i = 0; i < spectrumFftSize; ++i)
            {
                const auto index = (position + i) % spectrumFftSize;
                const auto l = ring[0][index], r = ring[1][index];
                const auto value = channel == 0   ? (pass == 0 ? l : r)
                                   : channel == 1 ? l
                                   : channel == 2 ? r
                                   : channel == 3 ? (l + r) * 0.5f
                                                  : (l - r) * 0.5f;
                input[i] = {value * window[i], 0.0f};
            }
            kiss_fft(plan, input.data(), output.data());
            constexpr float scale = 4.0f / static_cast<float>(spectrumFftSize);
            for (std::size_t i = 0; i < power.size(); ++i)
            {
                const auto re = output[i].r * scale, im = output[i].i * scale;
                const auto edge = i == 0 || i == spectrumFftSize / 2 ? 0.25f : 1.0f;
                power[i] += (re * re + im * im) * edge / static_cast<float>(passes);
            }
        }
    }
    float valueAt(double bin, int smoothing) const noexcept
    {
        constexpr std::array<double, 6> octaves{0.0,       1.0 / 24.0, 1.0 / 12.0,
                                                1.0 / 6.0, 1.0 / 3.0,  1.0};
        if (smoothing == 0)
        {
            // Preserve narrow peaks when many FFT bins map to one log-frequency display cell.
            const auto halfCell = std::pow(std::min(20000.0, rate * 0.5) / 20.0,
                                           0.5 / static_cast<double>(spectrumPoints - 1));
            const auto lo = static_cast<std::size_t>(std::ceil(bin / halfCell));
            const auto hi =
                std::min(static_cast<std::size_t>(std::floor(bin * halfCell)), power.size() - 1);
            if (hi >= lo && lo < power.size())
                return *std::max_element(power.begin() + static_cast<std::ptrdiff_t>(lo),
                                         power.begin() + static_cast<std::ptrdiff_t>(hi + 1));
        }
        const auto half = octaves[static_cast<std::size_t>(smoothing)] * 0.5;
        const auto low = std::max(0.0, bin * std::exp2(-half));
        const auto high = std::min(static_cast<double>(power.size() - 1), bin * std::exp2(half));
        if (high - low < 1.0)
        {
            const auto a = static_cast<std::size_t>(
                std::clamp(bin, 0.0, static_cast<double>(power.size() - 1)));
            const auto b = std::min(a + 1, power.size() - 1);
            const auto t = static_cast<float>(bin - static_cast<double>(a));
            return power[a] + (power[b] - power[a]) * t;
        }
        double sum = 0.0, weight = 0.0;
        const auto begin = static_cast<std::size_t>(std::floor(low));
        const auto end = std::min(static_cast<std::size_t>(std::ceil(high)), power.size() - 1);
        for (auto i = begin; i <= end; ++i)
        {
            const auto w = std::max(0.0, std::min(high, static_cast<double>(i) + 0.5) -
                                             std::max(low, static_cast<double>(i) - 0.5));
            sum += static_cast<double>(power[i]) * w;
            weight += w;
        }
        return weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
    }
    void analyze(SpectrumSnapshot& snapshot) noexcept
    {
        const auto settings = SpectrumSettings::fromAnalysisKey(key);
        transform(settings.channel);
        snapshot.sampleRate = rate;
        snapshot.maxFrequency = std::min(20000.0, rate * 0.5);
        snapshot.valid = true;
        snapshot.frameEnd = nextFrame;
        snapshot.analysisKey = key;
        // Both traces commonly use the same frequency smoothing. Sample the FFT
        // once, then apply their independent peak/RMS envelopes to those powers.
        std::array<std::array<float, spectrumPoints>, 2> sampled{};
        for (std::size_t i = 0; i < spectrumPoints; ++i)
        {
            const auto frequency =
                20.0 * std::pow(snapshot.maxFrequency / 20.0,
                                static_cast<double>(i) / static_cast<double>(spectrumPoints - 1));
            const auto bin = frequency * static_cast<double>(spectrumFftSize) / rate;
            sampled[0][i] = valueAt(bin, settings.curves[0].smoothing);
            sampled[1][i] = settings.curves[1].smoothing == settings.curves[0].smoothing
                                ? sampled[0][i]
                                : valueAt(bin, settings.curves[1].smoothing);
        }
        constexpr std::array<double, 4> seconds{0.15, 0.5, 1.5, 1.5};
        for (std::size_t c = 0; c < 2; ++c)
        {
            const auto& config = settings.curves[c];
            const auto decay = static_cast<float>(
                std::exp(-static_cast<double>(spectrumHopSize) /
                         (rate * seconds[static_cast<std::size_t>(config.speed)])));
            for (std::size_t i = 0; i < spectrumPoints; ++i)
            {
                const auto p = sampled[c][i];
                auto& e = envelope[c][i];
                if (config.rms)
                    e = first[c] ? p * 0.5f : decay * e + (1.0f - decay) * p * 0.5f;
                else
                {
                    const auto amplitude = std::sqrt(std::max(p, 0.0f));
                    e = std::max(amplitude, config.speed == 3 ? e : e * decay);
                }
                const auto amplitude = config.rms ? std::sqrt(std::max(e, 0.0f)) : e;
                snapshot.db[c][i] = 20.0f * std::log10(std::max(amplitude, 1.0e-8f));
            }
        }
        first.fill(false);
    }
};
SpectrumAnalyzer::SpectrumAnalyzer()
    : impl_(std::make_unique<Impl>())
{
    setSettings({});
    reset(0.0);
}
SpectrumAnalyzer::~SpectrumAnalyzer() = default;
void SpectrumAnalyzer::setEnabled(bool enabled) noexcept
{
    enabled_.store(enabled, std::memory_order_relaxed);
}
void SpectrumAnalyzer::setSettings(const SpectrumSettings& settings) noexcept
{
    requested_.store(settings.analysisKey(), std::memory_order_relaxed);
}
void SpectrumAnalyzer::clearHold() noexcept
{
    resetRequests_.fetch_add(1, std::memory_order_relaxed);
}
void SpectrumAnalyzer::reset(double sampleRate) noexcept
{
    bands_.reset(sampleRate);
    impl_->rate = std::isfinite(sampleRate) && sampleRate >= 80.0 ? sampleRate : 0.0;
    impl_->clear();
    impl_->active = false;
    auto& s = snapshots_.writeBuffer();
    s = {};
    s.sampleRate = impl_->rate;
    for (auto& curve : s.db)
        curve.fill(-160.0f);
    snapshots_.publish();
}
void SpectrumAnalyzer::process(std::span<const float> left, std::span<const float> right,
                               std::uint64_t firstFrame) noexcept
{
    bands_.process(left, right, firstFrame);
    auto& s = *impl_;
    const auto enabled = enabled_.load(std::memory_order_relaxed);
    if (!enabled || s.rate <= 0.0)
    {
        s.active = false;
        return;
    }
    const auto key = requested_.load(std::memory_order_relaxed);
    const auto resetRequest = resetRequests_.load(std::memory_order_relaxed);
    if (!s.active || s.nextFrame != firstFrame)
        s.clear();
    else if (key != s.key || resetRequest != s.resetRequest)
    {
        const auto previous = SpectrumSettings::fromAnalysisKey(s.key);
        const auto next = SpectrumSettings::fromAnalysisKey(key);
        for (std::size_t c = 0; c < 2; ++c)
        {
            // Response changes keep the current envelope (including entering Hold).
            // Detector/bandwidth changes reset only that trace, never the other one.
            if (resetRequest != s.resetRequest || previous.channel != next.channel ||
                previous.curves[c].rms != next.curves[c].rms ||
                previous.curves[c].smoothing != next.curves[c].smoothing)
            {
                s.envelope[c].fill(0.0f);
                s.first[c] = true;
            }
        }
    }
    s.active = true;
    s.key = key;
    s.resetRequest = resetRequest;
    s.nextFrame = firstFrame;
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        s.ring[0][s.position] = std::isfinite(left[i]) ? left[i] : 0.0f;
        s.ring[1][s.position] = std::isfinite(right[i]) ? right[i] : 0.0f;
        s.position = (s.position + 1) % spectrumFftSize;
        s.filled = std::min(s.filled + 1, spectrumFftSize);
        ++s.sinceFft;
        ++s.nextFrame;
        if (s.filled == spectrumFftSize && s.sinceFft >= spectrumHopSize)
        {
            s.sinceFft = 0;
            s.analyze(snapshots_.writeBuffer());
            snapshots_.publish();
        }
    }
}
} // namespace visona
