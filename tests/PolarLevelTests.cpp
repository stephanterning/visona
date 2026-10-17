#include "support/AllocationCounter.h"

#include <visona/PolarLevel.h>
#include <visona/StereoProjection.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <vector>

using namespace visona;

TEST_CASE("Polar Level preserves energy and matches an independent angular oracle",
          "[stereo][polar-level]")
{
    std::vector<StereoSample> samples;
    std::uint32_t state = 72183;
    auto random = [&state]()
    {
        state = 1664525 * state + 1013904223;
        return static_cast<float>(static_cast<double>(state) / 4294967296.0 - 0.5);
    };
    for (int i = 0; i < 4800; ++i)
        samples.push_back({random(), random()});
    std::array<double, polarLevelBins> energy{}, peak{};
    double total = 0;
    // Independent oracle: choose the nearest precomputed unit direction by
    // dot product, without using atan, production bins or its rounding rule.
    for (const auto sample : samples)
    {
        double x = static_cast<double>(sample.right) - static_cast<double>(sample.left);
        double y = static_cast<double>(sample.right) + static_cast<double>(sample.left);
        if (y < 0)
        {
            x = -x;
            y = -y;
        }
        const double radius =
            static_cast<double>(std::max(std::abs(sample.left), std::abs(sample.right)));
        double best = -std::numeric_limits<double>::infinity();
        std::size_t bin = 0;
        for (std::size_t b = 0; b < polarLevelBins; ++b)
        {
            const double angle =
                -std::numbers::pi / 2 + static_cast<double>(b) * std::numbers::pi / 72;
            const double dot = x * std::sin(angle) + y * std::cos(angle);
            if (dot > best)
            {
                best = dot;
                bin = b;
            }
        }
        energy[bin] += radius * radius;
        peak[bin] = std::max(peak[bin], radius);
        total += radius * radius;
    }
    const auto level = polarLevel(samples);
    REQUIRE(level.valid);
    double measured = 0;
    for (std::size_t b = 0; b < polarLevelBins; ++b)
    {
        REQUIRE(level.rms[b] ==
                Catch::Approx(std::sqrt(energy[b] / static_cast<double>(samples.size())))
                    .margin(1e-12));
        REQUIRE(level.peak[b] == Catch::Approx(peak[b]).margin(1e-12));
        REQUIRE(level.rms[b] <= level.peak[b]);
        measured += level.rms[b] * level.rms[b];
    }
    REQUIRE(measured == Catch::Approx(total / static_cast<double>(samples.size())).margin(1e-12));
    auto swapped = samples, louder = samples, inverted = samples;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        std::swap(swapped[i].left, swapped[i].right);
        louder[i].left *= 2;
        louder[i].right *= 2;
        inverted[i].left *= -1;
        inverted[i].right *= -1;
    }
    const auto mirror = polarLevel(swapped), gain = polarLevel(louder),
               inverse = polarLevel(inverted);
    for (std::size_t b = 0; b < polarLevelBins; ++b)
    {
        REQUIRE(mirror.rms[72 - b] == Catch::Approx(level.rms[b]).margin(1e-12));
        REQUIRE(gain.rms[b] == Catch::Approx(2 * level.rms[b]).margin(1e-12));
        REQUIRE(inverse.rms[b] == Catch::Approx(level.rms[b]).margin(1e-12));
    }
}

TEST_CASE("Polar Level directions, silence and rare peaks have defined levels",
          "[stereo][polar-level]")
{
    for (const auto& [sample, bin] :
         std::array<std::pair<StereoSample, std::size_t>, 5>{{{{0.5f, 0.5f}, 36},
                                                              {{0.5f, 0}, 18},
                                                              {{0, 0.5f}, 54},
                                                              {{0.5f, -0.5f}, 0},
                                                              {{-0.5f, 0.5f}, 72}}})
    {
        std::vector<StereoSample> samples(100, sample);
        const auto result = polarLevel(samples);
        REQUIRE(result.rms[bin] == Catch::Approx(0.5));
        REQUIRE(result.peak[bin] == Catch::Approx(0.5));
        REQUIRE(std::accumulate(result.rms.begin(), result.rms.end(), 0.0) == Catch::Approx(0.5));
        const auto point = signalizerPolar(sample.left, sample.right);
        const auto angle = std::atan2(point.x, point.y);
        REQUIRE(
            static_cast<double>(angle) ==
            Catch::Approx(-std::numbers::pi / 2 + static_cast<double>(bin) * std::numbers::pi / 72)
                .margin(1e-6));
    }
    std::vector<StereoSample> transient(100);
    transient[0] = {1, 1};
    const auto pulse = polarLevel(transient);
    REQUIRE(pulse.rms[36] == Catch::Approx(0.1));
    REQUIRE(pulse.peak[36] == Catch::Approx(1));
    transient[0] = {};
    const auto silent = polarLevel(transient);
    REQUIRE(silent.valid);
    REQUIRE(std::accumulate(silent.rms.begin(), silent.rms.end(), 0.0) == 0);
    REQUIRE(std::accumulate(silent.peak.begin(), silent.peak.end(), 0.0) == 0);
    REQUIRE_FALSE(polarLevel({}).valid);
    for (const auto invalid :
         {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1e7f})
    {
        transient[99] = {invalid, 0};
        const auto result = polarLevel(transient);
        REQUIRE_FALSE(result.valid);
        REQUIRE(std::accumulate(result.rms.begin(), result.rms.end(), 0.0) == 0);
    }
}

TEST_CASE("Polar Level has sample-rate-independent RMS and performs no allocations",
          "[stereo][polar-level]")
{
    for (const int rate : {8000, 44100, 48000, 96000, 192000, 384000})
    {
        std::vector<StereoSample> samples(static_cast<std::size_t>(rate / 10));
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto value = static_cast<float>(
                0.5 * std::sin(2 * std::numbers::pi * 100 * static_cast<double>(i) / rate));
            samples[i] = {value, value};
        }
        test::AllocationCounter allocations;
        const auto result = polarLevel(samples);
        const auto directional = polarDirectionalLevel(samples);
        const auto allocated = allocations.count();
        REQUIRE(allocated == 0);
        // Exact zero samples have no direction and contribute no angular support.
        const auto nonzero = std::count_if(samples.begin(), samples.end(),
                                           [](auto sample) { return sample.left != 0; });
        REQUIRE(directional.rms[36] ==
                Catch::Approx(result.rms[36] * std::sqrt(static_cast<double>(samples.size()) /
                                                         static_cast<double>(nonzero)))
                    .margin(1e-7));
        REQUIRE(result.rms[36] == Catch::Approx(std::sqrt(0.125)).margin(1e-7));
        REQUIRE(result.peak[36] == Catch::Approx(0.5).margin(1e-5));
    }
}

TEST_CASE("Polar Level quantizes angular boundaries without dropping energy",
          "[stereo][polar-level]")
{
    for (std::size_t boundary = 0; boundary < polarLevelBins - 1; ++boundary)
        for (const double offset : {-1e-4, 1e-4})
        {
            const double angle = -std::numbers::pi / 2 +
                                 (static_cast<double>(boundary) + 0.5) * std::numbers::pi / 72 +
                                 offset;
            const StereoSample sample{
                static_cast<float>((std::cos(angle) - std::sin(angle)) * 0.5),
                static_cast<float>((std::cos(angle) + std::sin(angle)) * 0.5)};
            const auto result = polarLevel(std::span<const StereoSample>(&sample, 1));
            const auto expected = boundary + (offset > 0 ? 1u : 0u);
            const double radius =
                static_cast<double>(std::max(std::abs(sample.left), std::abs(sample.right)));
            REQUIRE(result.rms[expected] == Catch::Approx(radius));
            REQUIRE(std::accumulate(result.rms.begin(), result.rms.end(), 0.0) ==
                    Catch::Approx(radius));
        }
}

TEST_CASE("Directional level separates level from angular occupancy", "[stereo][polar-level]")
{
    std::vector<StereoSample> samples(1000, {0.5f, 0.5f});
    std::fill_n(samples.begin(), 40, StereoSample{0.5f, 0.0f});
    const auto directional = polarDirectionalLevel(samples);
    const auto energy = polarLevel(samples);
    REQUIRE(directional.valid);
    REQUIRE(directional.rms[18] == Catch::Approx(0.5));
    REQUIRE(energy.rms[18] == Catch::Approx(0.1));
    REQUIRE(directional.rms[36] == Catch::Approx(0.5));
    samples.assign(1000, {});
    samples[0] = {1, 1};
    REQUIRE(polarDirectionalLevel(samples).rms[36] == Catch::Approx(std::sqrt(0.1)));
    REQUIRE(polarDirectionalLevel(samples).peak[36] == 1);
    samples.push_back({std::numeric_limits<float>::quiet_NaN(), 0});
    REQUIRE_FALSE(polarDirectionalLevel(samples).valid);
    REQUIRE_FALSE(polarDirectionalLevel({}).valid);
}

TEST_CASE("Directional level interpolates angular support and preserves scaling",
          "[stereo][polar-level]")
{
    // Independent geometry: direction halfway between centre bins 40 and 41.
    const double angle = 11.25 * std::numbers::pi / 180;
    const double mid = std::cos(angle), side = std::sin(angle);
    const StereoSample sample{static_cast<float>((mid - side) / (mid + side)), 1};
    std::vector<StereoSample> samples(1000);
    samples[0] = sample;
    const auto result = polarDirectionalLevel(samples);
    REQUIRE(result.rms[40] == Catch::Approx(std::sqrt(0.05)).margin(1e-6));
    REQUIRE(result.rms[41] == Catch::Approx(std::sqrt(0.05)).margin(1e-6));
    for (auto& value : samples)
    {
        value.left *= -0.25f;
        value.right *= -0.25f;
    }
    const auto scaled = polarDirectionalLevel(samples);
    for (std::size_t i = 0; i < polarLevelBins; ++i)
    {
        REQUIRE(scaled.rms[i] == Catch::Approx(result.rms[i] * 0.25).margin(1e-8));
        REQUIRE(scaled.rms[i] <= scaled.peak[i]);
    }
    const auto copy = samples;
    samples.insert(samples.end(), copy.begin(), copy.end());
    REQUIRE(polarDirectionalLevel(samples).rms == scaled.rms);
    samples.assign(1000, {});
    const auto silence = polarDirectionalLevel(samples);
    REQUIRE(silence.valid);
    REQUIRE(std::accumulate(silence.rms.begin(), silence.rms.end(), 0.0) == 0);
}
