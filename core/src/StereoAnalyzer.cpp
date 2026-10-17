#include <visona/StereoAnalyzer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <lsp-plug.in/dsp-units/meters/Correlometer.h>
#include <lsp-plug.in/dsp-units/meters/Panometer.h>
#include <stdexcept>
#include <vector>

namespace visona
{
namespace
{
constexpr std::size_t capacity = stereoHistoryCapacity * 2;
constexpr double openPower = 1e-9;                   // -90 dBFS RMS.
constexpr double closePower = 2.511886431509582e-10; // -96 dBFS RMS.
float rmsDb(double power) noexcept
{
    return static_cast<float>(10 * std::log10(std::max(1e-16, power)));
}
} // namespace

struct StereoAnalyzer::Impl
{
    lsp::dspu::Correlometer correlation;
    lsp::dspu::Panometer pan;
    std::vector<float> left = std::vector<float>(capacity);
    std::vector<float> right = std::vector<float>(capacity);
    std::vector<unsigned char> bad = std::vector<unsigned char>(capacity);
    std::vector<float> correlationOutput = std::vector<float>(capacity);
    std::vector<float> panOutput = std::vector<float>(capacity);
    TripleBuffer<StereoSnapshot> snapshots;
    std::size_t period = 0, hop = 0, history = 0, head = 0, filled = 0, sincePublish = 0;
    double rate = 0;
    std::uint64_t nextFrame = 0, request = 0;
    bool timeline = false, leftActive = false, rightActive = false;

    Impl()
    {
        if (correlation.init(capacity) != lsp::STATUS_OK || pan.init(capacity) != lsp::STATUS_OK)
            throw std::runtime_error("Could not allocate LSP stereo meters");
    }

    void clear() noexcept
    {
        head = filled = sincePublish = 0;
        timeline = leftActive = rightActive = false;
        // No need to clear sample storage: only filled entries are ever read.
    }

    void publishEmpty(StereoValidity validity = StereoValidity::waiting) noexcept
    {
        auto& out = snapshots.writeBuffer();
        out.count = 0;
        out.sampleRate = rate;
        out.frameEnd = nextFrame;
        out.validity = validity;
        out.correlation = out.balance = 0;
        out.leftRmsDb = out.rightRmsDb = -160;
        snapshots.publish();
    }

    void publish() noexcept
    {
        // Recompute non-negative energies from the actual window, in double precision.
        // Subtracting old energies can leave a false signal after loud audio stops.
        double ll = 0, rr = 0;
        bool invalid = false;
        for (std::size_t i = 0; i < period; ++i)
        {
            ll += static_cast<double>(left[i]) * static_cast<double>(left[i]);
            rr += static_cast<double>(right[i]) * static_cast<double>(right[i]);
            invalid = invalid || bad[i] != 0;
        }
        ll /= static_cast<double>(period);
        rr /= static_cast<double>(period);
        leftActive = !invalid && ll >= (leftActive ? closePower : openPower);
        rightActive = !invalid && rr >= (rightActive ? closePower : openPower);
        auto& out = snapshots.writeBuffer();
        out.sampleRate = rate;
        out.frameEnd = nextFrame;
        out.leftRmsDb = rmsDb(ll);
        out.rightRmsDb = rmsDb(rr);
        out.validity = invalid                     ? StereoValidity::invalidInput
                       : leftActive && rightActive ? StereoValidity::valid
                       : leftActive                ? StereoValidity::leftOnly
                       : rightActive               ? StereoValidity::rightOnly
                                                   : StereoValidity::silent;
        out.correlation = out.balance = 0;
        out.count = 0;
        if (out.signalValid())
        {
            // Only the final window value is used. Starting clean avoids accumulated
            // round-off after level changes, including a loud-to-quiet transition.
            correlation.clear();
            correlation.set_period(period - 1);
            correlation.set_period(period);
            pan.clear();
            pan.set_period(period - 1); // Work around upstream clear() retaining sums.
            pan.set_period(period);
            // Ordering does not affect the final energy/cross-product measurement.
            correlation.process(correlationOutput.data(), left.data(), right.data(), period);
            pan.process(panOutput.data(), left.data(), right.data(), period);
            if (out.correlationValid())
                out.correlation = std::clamp(correlationOutput[period - 1], -1.0f, 1.0f);
            out.balance = std::clamp(2 * panOutput[period - 1] - 1, -1.0f, 1.0f);
            out.count = history;
            for (std::size_t i = 0; i < history; ++i)
            {
                const auto index = (head + period - history + i) % period;
                out.samples[i] = {left[index], right[index]};
            }
        }
        snapshots.publish();
    }
};

StereoAnalyzer::StereoAnalyzer()
    : impl_(std::make_unique<Impl>())
{
}
StereoAnalyzer::~StereoAnalyzer() = default;
void StereoAnalyzer::setEnabled(bool enabled) noexcept
{
    auto previous = request_.load(std::memory_order_relaxed);
    for (;;)
    {
        if ((previous & 1) == static_cast<std::uint64_t>(enabled))
            return;
        const auto next =
            ((previous + 2) & ~std::uint64_t{1}) | static_cast<std::uint64_t>(enabled);
        if (request_.compare_exchange_weak(previous, next, std::memory_order_relaxed))
            return;
    }
}
void StereoAnalyzer::reset(double sampleRate) noexcept
{
    auto& s = *impl_;
    s.rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= stereoMaxSampleRate
                 ? sampleRate
                 : 0;
    s.period = static_cast<std::size_t>(std::llround(s.rate * 0.2));
    s.history = static_cast<std::size_t>(std::llround(s.rate * 0.1));
    s.hop = static_cast<std::size_t>(std::max(1.0, std::round(s.rate / 120)));
    s.clear();
    s.nextFrame = 0;
    s.publishEmpty();
}
void StereoAnalyzer::process(std::span<const float> left, std::span<const float> right,
                             std::uint64_t firstFrame) noexcept
{
    auto& s = *impl_;
    const auto request = request_.load(std::memory_order_relaxed);
    if (request != s.request)
    {
        s.request = request;
        s.clear();
        s.nextFrame = firstFrame;
        s.publishEmpty();
    }
    if ((request & 1) == 0 || s.period == 0)
        return;
    if (left.size() != right.size() ||
        left.size() > std::numeric_limits<std::uint64_t>::max() - firstFrame)
    {
        s.clear();
        s.publishEmpty(StereoValidity::invalidInput);
        return;
    }
    if (left.empty())
        return;
    if (s.timeline && firstFrame != s.nextFrame)
    {
        s.clear();
        s.nextFrame = firstFrame;
        s.publishEmpty();
    }
    s.timeline = true;
    s.nextFrame = firstFrame;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        // Reject non-finite/unrepresentable meter input; retain ordinary float headroom.
        const bool bad = !std::isfinite(left[i]) || !std::isfinite(right[i]) ||
                         std::abs(left[i]) > 1e6f || std::abs(right[i]) > 1e6f;
        s.left[s.head] = bad ? 0 : left[i];
        s.right[s.head] = bad ? 0 : right[i];
        s.bad[s.head] = static_cast<unsigned char>(bad);
        s.head = (s.head + 1) % s.period;
        ++s.nextFrame;
        s.filled = std::min(s.filled + 1, s.period);
        ++s.sincePublish;
        if (s.filled == s.period && s.sincePublish >= s.hop)
        {
            s.publish();
            s.sincePublish = 0;
        }
    }
}
TripleBuffer<StereoSnapshot>& StereoAnalyzer::snapshots() noexcept
{
    return impl_->snapshots;
}
} // namespace visona
