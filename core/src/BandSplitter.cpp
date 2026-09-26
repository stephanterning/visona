#include "visona/BandSplitter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace visona
{

namespace
{

// Butterworth: two of these in series make an LR4 filter, and its low- and high-pass sum to the
// allpass with the same Q.
constexpr double butterworthQ = std::numbers::sqrt2 / 2.0;

constexpr double lowestAudibleHz = 20.0;
constexpr double highestAudibleHz = 20'000.0;

enum class Kind
{
    lowPass,
    highPass,
    allPass
};

/** The bilinear transform of a second-order analog prototype, prewarped at `frequencyHz`. */
BandSplitter::Biquad design(Kind kind, double frequencyHz, double sampleRate) noexcept
{
    const auto omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const auto cosOmega = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0 * butterworthQ);
    // 1 - cos(omega), without the cancellation at low crossovers and high sample rates.
    const auto oneMinusCos = 2.0 * std::pow(std::sin(omega / 2.0), 2.0);
    const auto a0 = 1.0 + alpha;

    BandSplitter::Biquad biquad;
    switch (kind)
    {
    case Kind::lowPass:
        biquad.b0 = oneMinusCos / 2.0 / a0;
        biquad.b1 = oneMinusCos / a0;
        biquad.b2 = biquad.b0;
        break;
    case Kind::highPass:
        biquad.b0 = (2.0 - oneMinusCos) / 2.0 / a0;
        biquad.b1 = -(2.0 - oneMinusCos) / a0;
        biquad.b2 = biquad.b0;
        break;
    case Kind::allPass:
        biquad.b0 = (1.0 - alpha) / a0;
        biquad.b1 = -2.0 * cosOmega / a0;
        biquad.b2 = 1.0;
        break;
    }
    biquad.a1 = -2.0 * cosOmega / a0;
    biquad.a2 = (1.0 - alpha) / a0;
    return biquad;
}

/** The group delay of the polynomial p0 + p1 z^-1 + p2 z^-2 at `omega`, in samples. */
double polynomialDelay(double p0, double p1, double p2, double omega) noexcept
{
    const auto z1 = std::polar(1.0, -omega);
    const auto z2 = std::polar(1.0, -2.0 * omega);
    const auto value = p0 + p1 * z1 + p2 * z2;
    const auto weighted = p1 * z1 + 2.0 * p2 * z2;
    return std::abs(value) > 0.0 ? (weighted / value).real() : 0.0;
}

} // namespace

double BandSplitter::Biquad::groupDelay(double omega) const noexcept
{
    return polynomialDelay(b0, b1, b2, omega) - polynomialDelay(1.0, a1, a2, omega);
}

BandSplitter::BandSplitter(double sampleRate, double lowMidHz, double midHighHz) noexcept
{
    const auto upper = std::min(midHighHz, 0.45 * sampleRate);
    const auto lower = std::min(lowMidHz, 0.5 * upper);
    if (!(sampleRate > 0.0) || !(lower > 0.0))
        return;

    sampleRate_ = sampleRate;
    lowMidHz_ = lower;
    midHighHz_ = upper;
    for (auto& biquad : lowPass1_)
        biquad = design(Kind::lowPass, lower, sampleRate);
    for (auto& biquad : highPass1_)
        biquad = design(Kind::highPass, lower, sampleRate);
    for (auto& biquad : lowPass2_)
        biquad = design(Kind::lowPass, upper, sampleRate);
    for (auto& biquad : highPass2_)
        biquad = design(Kind::highPass, upper, sampleRate);
    lowAllpass_ = design(Kind::allPass, upper, sampleRate);
}

void BandSplitter::reset() noexcept
{
    for (auto* filters : {&lowPass1_, &highPass1_, &lowPass2_, &highPass2_})
        for (auto& biquad : *filters)
            biquad.s1 = biquad.s2 = 0.0;
    lowAllpass_.s1 = lowAllpass_.s2 = 0.0;
}

double BandSplitter::groupDelaySeconds(Band band, double frequencyHz) const noexcept
{
    if (!hasSampleRate() || band == Band::full)
        return 0.0;

    const auto omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate_;
    const auto sum = [omega](const auto& biquads)
    {
        double delay = 0.0;
        for (const auto& biquad : biquads)
            delay += biquad.groupDelay(omega);
        return delay;
    };

    double samples = 0.0;
    switch (band)
    {
    case Band::low:
        samples = sum(lowPass1_) + lowAllpass_.groupDelay(omega);
        break;
    case Band::mid:
        samples = sum(highPass1_) + sum(lowPass2_);
        break;
    case Band::high:
        samples = sum(highPass1_) + sum(highPass2_);
        break;
    case Band::full:
        break;
    }
    return samples / sampleRate_;
}

double BandSplitter::referenceHz(Band band) const noexcept
{
    if (!hasSampleRate())
        return 0.0;
    const auto top = std::min(highestAudibleHz, 0.5 * sampleRate_);
    switch (band)
    {
    case Band::low:
        return std::sqrt(lowestAudibleHz * lowMidHz_);
    case Band::mid:
        return std::sqrt(lowMidHz_ * midHighHz_);
    case Band::high:
        return std::sqrt(midHighHz_ * std::max(top, midHighHz_));
    case Band::full:
        break;
    }
    return 0.0;
}

double BandSplitter::delayFrames(Band band) const noexcept
{
    return groupDelaySeconds(band, referenceHz(band)) * sampleRate_;
}

} // namespace visona
