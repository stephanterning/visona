#include <visona/BandAnalyzer.h>

#include <algorithm>
#include <cmath>
#include <numbers>
namespace visona
{
double BandAnalyzer::frequency(std::size_t band) noexcept
{
    return 1000.0 * std::pow(10.0, (static_cast<double>(band) - 17.0) / 10.0);
}
double BandAnalyzer::Filter::tick(double x, std::size_t section) noexcept
{
    auto& s = state[section];
    const auto y = b0 * x + s[0];
    s[0] = -a1 * y + s[1];
    s[1] = -b0 * x - a2 * y;
    // Avoid denormal tails after silence on platforms without flush-to-zero.
    if (std::abs(s[0]) < 1e-25)
        s[0] = 0;
    if (std::abs(s[1]) < 1e-25)
        s[1] = 0;
    return y;
}
void BandAnalyzer::clear() noexcept
{
    for (auto& filter : filters_)
        filter.state = {};
    envelope_.fill(0);
    peak_.fill(0);
    hold_.fill(0);
    sincePublish_ = 0;
}
void BandAnalyzer::reset(double sampleRate) noexcept
{
    rate_ = std::isfinite(sampleRate) && sampleRate >= 80 ? sampleRate : 0;
    clear();
    active_ = false;
    if (rate_ > 0)
    {
        release_ = std::exp(-2.0 / (rate_ * 0.15));
        peakRelease_ = std::pow(10.0, -12.0 / (10.0 * rate_));
        holdSamples_ = static_cast<std::uint32_t>(rate_);
        publishSamples_ = std::max(1U, static_cast<std::uint32_t>(rate_ / 60.0));
        for (std::size_t band = 0; band < spectrumBands; ++band)
        {
            const auto f = frequency(band);
            available_[band] = f * std::pow(10.0, 0.05) < rate_ * 0.5;
            const auto w = 2 * std::numbers::pi * f / rate_;
            // Cascade two unity-centre bandpasses. Correct each section's bandwidth
            // so their combined response is -3 dB at the third-octave edges.
            const auto bandwidth = std::log(10.0) / 10.0;
            const auto alpha = available_[band]
                                   ? std::sin(w) * std::sinh(bandwidth * 0.5 * w / std::sin(w)) /
                                         std::sqrt(std::sqrt(2.0) - 1.0)
                                   : 0.0;
            filters_[band].b0 = alpha / (1 + alpha);
            filters_[band].a1 = -2 * std::cos(w) / (1 + alpha);
            filters_[band].a2 = (1 - alpha) / (1 + alpha);
        }
    }
    auto& snapshot = snapshots_.writeBuffer();
    snapshot = {};
    snapshot.sampleRate = rate_;
    snapshot.db.fill(-160);
    snapshot.peakDb.fill(-160);
    snapshots_.publish();
}
void BandAnalyzer::process(std::span<const float> left, std::span<const float> right,
                           std::uint64_t firstFrame) noexcept
{
    if (!enabled_.load() || rate_ <= 0)
    {
        active_ = false;
        return;
    }
    if (!active_ || nextFrame_ != firstFrame)
        clear();
    active_ = true;
    const auto request = resetRequests_.load();
    if (request != resetRequest_)
    {
        peak_ = envelope_;
        hold_.fill(0);
        resetRequest_ = request;
    }
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const double l = std::isfinite(left[i]) ? static_cast<double>(left[i]) : 0.0;
        const double r = std::isfinite(right[i]) ? static_cast<double>(right[i]) : 0.0;
        for (std::size_t band = 0; band < spectrumBands; ++band)
        {
            if (!available_[band])
                continue;
            auto& f = filters_[band];
            const auto a = f.tick(f.tick(l, 0), 1);
            const auto b = f.tick(f.tick(r, 2), 3);
            const auto power = (a * a + b * b) * 0.5;
            envelope_[band] = std::max(power, envelope_[band] * release_);
            if (power >= peak_[band])
            {
                peak_[band] = power;
                hold_[band] = holdSamples_;
            }
            else if (hold_[band] > 0)
                --hold_[band];
            else
                peak_[band] = std::max(power, peak_[band] * peakRelease_);
        }
        if (++sincePublish_ >= publishSamples_)
        {
            sincePublish_ = 0;
            auto& s = snapshots_.writeBuffer();
            s.valid = true;
            s.sampleRate = rate_;
            s.frameEnd = firstFrame + i + 1;
            for (std::size_t band = 0; band < spectrumBands; ++band)
            {
                s.db[band] = static_cast<float>(10 * std::log10(std::max(envelope_[band], 1e-16)));
                s.peakDb[band] = static_cast<float>(10 * std::log10(std::max(peak_[band], 1e-16)));
            }
            snapshots_.publish();
        }
    }
    nextFrame_ = firstFrame + count;
}
} // namespace visona
