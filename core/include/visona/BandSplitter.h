#pragma once

#include <visona/Band.h>

#include <array>

namespace visona
{

/** One frame of a BandSplitter's output. */
struct BandSample
{
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
};

/**
    Splits one channel into low, mid and high bands with fourth-order Linkwitz-Riley (LR4)
    crossovers (D-051, D-072).

    Each LR4 filter is two cascaded second-order Butterworth sections. The low band also passes the
    allpass that the upper crossover's low- and high-pass sum to, so low + mid + high is an allpass:
    the bands sum flat, and both bands are at -6 dB at their crossover. The coefficients come from
    the sample rate by the bilinear transform, prewarped at each crossover, so the crossovers are
    exact at every rate.

    The bands feed the frequency colouring only; the waveform's shape always comes from the
    unfiltered input (D-056). Nothing allocates.
*/
class BandSplitter
{
public:
    static constexpr double defaultLowMidHz = 200.0;
    static constexpr double defaultMidHighHz = 2'500.0;

    /** A second-order section in transposed direct form II, with double-precision state. */
    struct Biquad
    {
        double b0 = 0.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double s1 = 0.0;
        double s2 = 0.0;

        double process(double x) noexcept
        {
            const auto y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }

        /** The group delay at `omega` radians per sample, in samples. */
        [[nodiscard]] double groupDelay(double omega) const noexcept;
    };

    /** A splitter without a sample rate: every band is silent. */
    BandSplitter() noexcept = default;

    /**
        Crossovers at `lowMidHz` and `midHighHz` for audio at `sampleRate`. The upper crossover is
        kept at or below 0.45 × sampleRate and the lower one at or below half the upper, so even a
        low sample rate gives three bands. A sample rate or crossover of 0 or less gives a splitter
        without a sample rate.
    */
    explicit BandSplitter(double sampleRate, double lowMidHz = defaultLowMidHz,
                          double midHighHz = defaultMidHighHz) noexcept;

    [[nodiscard]] bool hasSampleRate() const noexcept
    {
        return sampleRate_ > 0.0;
    }

    [[nodiscard]] double sampleRate() const noexcept
    {
        return sampleRate_;
    }

    /** The crossovers in use, after keeping them below the Nyquist frequency. */
    [[nodiscard]] double lowMidHz() const noexcept
    {
        return lowMidHz_;
    }

    [[nodiscard]] double midHighHz() const noexcept
    {
        return midHighHz_;
    }

    /** Forgets the past input, as if the splitter had only ever received silence. */
    void reset() noexcept;

    /** Splits the next sample. */
    BandSample process(float sample) noexcept
    {
        const auto x = static_cast<double>(sample);
        const auto low = lowAllpass_.process(lowPass1_[1].process(lowPass1_[0].process(x)));
        const auto rest = highPass1_[1].process(highPass1_[0].process(x));
        const auto mid = lowPass2_[1].process(lowPass2_[0].process(rest));
        const auto high = highPass2_[1].process(highPass2_[0].process(rest));
        return {static_cast<float>(low), static_cast<float>(mid), static_cast<float>(high)};
    }

    /**
        The group delay of split band `band` at `frequencyHz`, in seconds, computed from the
        coefficients. 0 for Band::full, which is the unfiltered input, and without a sample rate.
    */
    [[nodiscard]] double groupDelaySeconds(Band band, double frequencyHz) const noexcept;

    /**
        The frequency at which the delay of `band` is compensated: the geometric centre of the band,
        with 20 Hz and 20 kHz (or the Nyquist frequency, if lower) as its outer edges. With the
        default crossovers that is 63 Hz, 707 Hz and 7.1 kHz.
    */
    [[nodiscard]] double referenceHz(Band band) const noexcept;

    /** How many frames `band` lags the unfiltered input: its group delay at referenceHz(). */
    [[nodiscard]] double delayFrames(Band band) const noexcept;

private:
    double sampleRate_ = 0.0;
    double lowMidHz_ = 0.0;
    double midHighHz_ = 0.0;

    std::array<Biquad, 2> lowPass1_{};
    std::array<Biquad, 2> highPass1_{};
    std::array<Biquad, 2> lowPass2_{};
    std::array<Biquad, 2> highPass2_{};
    Biquad lowAllpass_{};
};

} // namespace visona
