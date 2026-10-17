#include "LspStereo.h"
#include "RealtimeAllocationCheck.h"
#include "SignalizerPolar.h"
#include "Signals.h"
#include "StereoProjection.h"

#include <visona/TripleBuffer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

using namespace stereo_spike;

namespace
{
std::size_t checks = 0;
double worstCorrelation = 0, worstPan = 0, worstPartition = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

// Independent double-precision sliding sums. No LSP math is used here.
void verifyOracle(const Signal& signal, std::size_t period, std::span<const float> correlation,
                  std::span<const float> pan, bool linear)
{
    double ll = 0, rr = 0, lr = 0;
    for (std::size_t i = 0; i < signal.left.size(); ++i)
    {
        const double left = signal.left[i], right = signal.right[i];
        ll += left * left;
        rr += right * right;
        lr += left * right;
        if (i >= period)
        {
            const double oldLeft = signal.left[i - period], oldRight = signal.right[i - period];
            ll -= oldLeft * oldLeft;
            rr -= oldRight * oldRight;
            lr -= oldLeft * oldRight;
        }
        const double product = ll * rr;
        const double expectedCorrelation = product >= 1e-18 ? lr / std::sqrt(product) : 0;
        double a = std::abs(ll) / period, b = std::abs(rr) / period;
        if (linear)
        {
            a = std::sqrt(a);
            b = std::sqrt(b);
        }
        const double expectedPan = a + b > (linear ? 1e-18 : 1e-36) ? b / (a + b) : 0.5;
        worstCorrelation =
            std::max(worstCorrelation, std::abs(correlation[i] - expectedCorrelation));
        worstPan = std::max(worstPan, std::abs(pan[i] - expectedPan));
        require(std::isfinite(correlation[i]) && std::isfinite(pan[i]), "Non-finite meter output");
        require(std::abs(correlation[i] - expectedCorrelation) < 2e-4,
                "Correlation oracle mismatch");
        require(std::abs(pan[i] - expectedPan) < 2e-4, "Pan oracle mismatch");
    }
}

void verifyMeters()
{
    constexpr std::array<std::size_t, 8> sizes{1, 7, 64, 127, 256, 511, 1024, 4096};
    for (const int rate : {44100, 48000, 96000, 192000})
    {
        const auto period = static_cast<std::size_t>(rate / 5);
        const auto count = period * 3 + 103;
        for (std::size_t kind = 0; kind < signalNames.size(); ++kind)
        {
            const auto signal = makeSignal(kind, rate, count);
            for (bool linear : {false, true})
            {
                LspStereo whole(period, linear), split(period, linear);
                std::vector<float> correlation(count), pan(count), splitCorrelation(count),
                    splitPan(count);
                {
                    visona::RealtimeAllocationCheck::Scope scope;
                    whole.process(signal.left.data(), signal.right.data(), correlation.data(),
                                  pan.data(), count);
                    std::size_t block = 0;
                    for (std::size_t offset = 0; offset < count; ++block)
                    {
                        const auto n = std::min(sizes[block % sizes.size()], count - offset);
                        split.process(signal.left.data() + offset, signal.right.data() + offset,
                                      splitCorrelation.data() + offset, splitPan.data() + offset,
                                      n);
                        offset += n;
                    }
                }
                verifyOracle(signal, period, correlation, pan, linear);
                verifyOracle(signal, period, splitCorrelation, splitPan, linear);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const double difference =
                        std::max(std::abs(correlation[i] - splitCorrelation[i]),
                                 std::abs(pan[i] - splitPan[i]));
                    worstPartition = std::max(worstPartition, difference);
                    require(difference < 2e-4, "Host block partition changed meter result");
                }
                if (rate == 48000 && !linear)
                    std::cout << signalNames[kind] << ": correlation=" << correlation.back()
                              << ", right-energy fraction=" << pan.back() << '\n';
                if (kind == 0 || kind == 1 || kind == 6)
                    require(std::abs(correlation.back() - (kind == 1 ? -1.0f : 1.0f)) < 2e-4,
                            "Analytical mono correlation mismatch");
                if (kind == 4)
                    require(std::abs(correlation.back()) < 2e-4, "Quadrature correlation mismatch");
                if (kind == 5)
                    require(std::abs(correlation.back()) < 0.05f,
                            "Independent noise unexpectedly correlated");
                if (kind == 6)
                    require(std::abs(pan.back() - (linear ? 1.0f / 3 : 0.2f)) < 2e-4,
                            "Pan law definition mismatch");
            }
        }
    }
}

void verifyReset()
{
    // A reset mid-window exposes the pinned upstream Panometer clear() behavior.
    constexpr std::size_t period = 9600;
    const auto signal = makeSignal(6, 48000, 103);
    std::array<float, 103> correlation{}, pan{}, silence{};
    lsp::dspu::Panometer raw;
    require(raw.init(period) == lsp::STATUS_OK, "Raw Panometer allocation failed");
    raw.set_period(period);
    raw.process(pan.data(), signal.left.data(), signal.right.data(), pan.size());
    raw.clear();
    raw.process(pan.data(), silence.data(), silence.data(), pan.size());
    std::cout << "Upstream clear() then silence: pan=" << pan.back() << " (default is 0.5)\n";
    require(std::abs(pan.back() - 0.2f) < 2e-4,
            "Pinned upstream reset behavior changed; review workaround");

    for (auto window : {std::size_t{1}, std::size_t{2}, period})
    {
        LspStereo meter(window);
        meter.process(signal.left.data(), signal.right.data(), correlation.data(), pan.data(),
                      pan.size());
        {
            visona::RealtimeAllocationCheck::Scope scope;
            meter.reset();
            meter.process(silence.data(), silence.data(), correlation.data(), pan.data(),
                          pan.size());
        }
        for (std::size_t i = 0; i < pan.size(); ++i)
            require(correlation[i] == 0 && pan[i] == 0.5f, "Adapter reset did not remove history");
    }
    bool rejected = false;
    try
    {
        LspStereo invalid(0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Zero period must be rejected before LSP process()");
}

void verifyProjections()
{
    for (int l = -100; l <= 100; ++l)
        for (int r = -100; r <= 100; ++r)
        {
            const float left = l * 0.01f, right = r * 0.01f;
            const auto cartesian = lissajous(left, right);
            require(std::abs(cartesian.y - cartesian.x - left) < 1e-6 &&
                        std::abs(cartesian.y + cartesian.x - right) < 1e-6,
                    "Lissajous inverse changed channel order or scale");
            require(std::abs(cartesian.x) + std::abs(cartesian.y) <= 1.000001f,
                    "Normalized input escaped Lissajous diamond");
            const auto p = signalizerPolar(left, right);
            const double radius = std::max(std::abs(left), std::abs(right));
            require(std::isfinite(p.x) && std::isfinite(p.y), "Non-finite polar point");
            require(p.y >= -1e-6 && std::abs(std::hypot(p.x, p.y) - radius) < 1e-6,
                    "Polar peak radius or upper-half-plane mismatch");
            // Independent Cartesian geometry; exact anti-phase is the fold seam.
            double x = double(right) - left, y = double(left) + right;
            const auto norm = std::hypot(x, y);
            if (norm > 0 && y != 0)
            {
                if (y < 0)
                {
                    x = -x;
                    y = -y;
                }
                require(std::abs(p.x - x * radius / norm) < 2e-6 &&
                            std::abs(p.y - y * radius / norm) < 2e-6,
                        "Signalizer polar disagrees with independent geometry");
            }
            const auto swapped = signalizerPolar(right, left);
            require(std::abs(swapped.x + p.x) < 2e-6 && std::abs(swapped.y - p.y) < 2e-6,
                    "Channel swap does not mirror the polar view");
        }
    require(signalizerPolar(1, 1).y > 0.999f, "Mono should be vertical");
    require(signalizerPolar(1, 0).x < 0 && signalizerPolar(0, 1).x > 0,
            "Polar L/R orientation reversed");
    require(std::abs(signalizerPolar(1, -1).x) > 0.999f, "Anti-mono should reach the fold seam");
    require(signalizerPolar(std::numeric_limits<float>::quiet_NaN(), 0).x == 0,
            "Non-finite display input must be rejected");
}

void verifySnapshotHandoff()
{
    struct Snapshot
    {
        std::uint64_t frames{};
        float correlation{}, pan{};
    };
    visona::TripleBuffer<Snapshot> snapshots;
    LspStereo meter(9600);
    const auto signal = makeSignal(6, 48000, 64000);
    std::array<float, 64> correlation{}, pan{};
    // A slow UI may miss snapshots, but the analysis still consumes every sample.
    for (std::size_t offset = 0; offset < signal.left.size(); offset += 64)
    {
        visona::RealtimeAllocationCheck::Scope scope;
        meter.process(signal.left.data() + offset, signal.right.data() + offset, correlation.data(),
                      pan.data(), 64);
        snapshots.writeBuffer() = {offset + 64, correlation.back(), pan.back()};
        snapshots.publish();
    }
    require(snapshots.fetch() && snapshots.readBuffer().frames == 64000,
            "Latest snapshot lost its sample position");
    require(std::abs(snapshots.readBuffer().correlation - 1) < 2e-4 &&
                std::abs(snapshots.readBuffer().pan - 0.2f) < 2e-4,
            "Slow UI changed measurement");
}

void inspectSilenceTransition()
{
    constexpr std::size_t period = 9600, active = 10371;
    auto signal = makeSignal(6, 48000, active + 3 * period);
    std::fill(signal.left.begin() + active, signal.left.end(), 0);
    std::fill(signal.right.begin() + active, signal.right.end(), 0);
    LspStereo meter(period);
    std::vector<float> correlation(signal.left.size()), pan(signal.left.size());
    meter.process(signal.left.data(), signal.right.data(), correlation.data(), pan.data(),
                  pan.size());
    double silentCorrelation = 0, silentPanOffset = 0;
    for (auto i = active + period; i < pan.size(); ++i)
    {
        require(std::isfinite(correlation[i]) && std::isfinite(pan[i]),
                "Signal-to-silence transition produced non-finite results");
        silentCorrelation = std::max(silentCorrelation, double(std::abs(correlation[i])));
        silentPanOffset = std::max(silentPanOffset, double(std::abs(pan[i] - 0.5f)));
    }
    require(correlation.back() == 0 && pan.back() == 0.5f,
            "Meters did not settle to their silence defaults after rebasing");
    std::cout << "Silent window after active signal: max abs(raw correlation)=" << silentCorrelation
              << ", max abs(raw pan - 0.5)=" << silentPanOffset << '\n';
}
} // namespace

int main()
{
    try
    {
        lsp::dsp::init();
        require(visona::RealtimeAllocationCheck::selfTest(),
                "Allocation instrumentation self-test failed");
        const auto start = std::chrono::steady_clock::now();
        verifyMeters();
        verifyReset();
        verifyProjections();
        verifySnapshotHandoff();
        inspectSilenceTransition();
        require(visona::RealtimeAllocationCheck::allocationCount() == 0,
                "Processing allocated via new");
        const auto seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout
            << "PASS " << checks << " checks in " << seconds << " s\n"
            << "Worst absolute errors: correlation=" << worstCorrelation << ", pan=" << worstPan
            << ", block partition=" << worstPartition << "\n"
            << "C++ allocations during process/reset/publication: 0 (malloc is not intercepted)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
