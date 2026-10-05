#include <visona/HeadPacer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

using Catch::Approx;
using visona::HeadPacer;

namespace
{

// One bar of 4/4 at 120 BPM: 131,072 bins in 2 s.
constexpr double binsPerSecond = 131'072.0 / 2.0;
constexpr double sampleRate = 48'000.0;
constexpr double frameSeconds = 1.0 / 60.0;
constexpr double pollSeconds = 0.003;
constexpr double runSeconds = 10.0;

/**
    How far the analysis has written the sweep over time: whole device blocks, each seen at the
    analysis' first poll after it arrives. With jitter, blocks arrive up to 1 ms late and polls
    come up to 1.5 ms late.
*/
class Analysis
{
public:
    Analysis(std::uint32_t blockFrames, bool jitter)
        : blockSeconds_(blockFrames / sampleRate)
    {
        std::mt19937 random(12345);
        std::uniform_real_distribution<double> late(0.0, 1.0);
        for (double block = 1.0; block * blockSeconds_ < runSeconds + 1.0; block += 1.0)
            arrivals_.push_back(block * blockSeconds_ + (jitter ? 0.001 * late(random) : 0.0));
        std::sort(arrivals_.begin(), arrivals_.end());
        for (double poll = 0.0; poll < runSeconds + 1.0;)
        {
            polls_.push_back(poll);
            poll += pollSeconds + (jitter ? 0.0015 * late(random) : 0.0);
        }
    }

    [[nodiscard]] double writtenAt(double seconds) const
    {
        const auto poll = std::upper_bound(polls_.begin(), polls_.end(), seconds);
        const auto polled = poll == polls_.begin() ? 0.0 : *(poll - 1);
        const auto blocks =
            std::upper_bound(arrivals_.begin(), arrivals_.end(), polled) - arrivals_.begin();
        return static_cast<double>(blocks) * blockSeconds_ * binsPerSecond;
    }

private:
    double blockSeconds_;
    std::vector<double> arrivals_;
    std::vector<double> polls_;
};

struct Run
{
    std::vector<double> steps;
    double maxLagSeconds = 0.0;
};

/** Paces frames at 60 Hz with the tempo off by `tempoError`, and records the steps and how far
    behind the newest audio the paced head is, after the first two seconds. */
Run pace(const Analysis& analysis, bool paced = true, double tempoError = 0.0)
{
    HeadPacer pacer;
    Run run;
    double previous = 0.0;
    for (int frame = 0;; ++frame)
    {
        const auto seconds = 0.05 + frame * frameSeconds;
        if (seconds > runSeconds)
            break;
        const auto written = analysis.writtenAt(seconds);
        const auto shown =
            paced ? pacer.update(seconds, written, binsPerSecond * (1.0 + tempoError)) : written;
        REQUIRE(shown <= written);
        REQUIRE(shown >= previous);
        if (seconds > 2.0)
        {
            run.steps.push_back(shown - previous);
            run.maxLagSeconds = std::max(run.maxLagSeconds, seconds - shown / binsPerSecond);
        }
        previous = shown;
    }
    return run;
}

/** The difference between the longest and shortest step, in frames' worth of sweep. */
double spread(const std::vector<double>& steps)
{
    const auto [low, high] = std::minmax_element(steps.begin(), steps.end());
    return (*high - *low) / (frameSeconds * binsPerSecond);
}

} // namespace

TEST_CASE("Unpaced, 512-frame blocks at 48 kHz step unevenly at 60 Hz", "[HeadPacer]")
{
    // One block in some frames and two in others.
    CHECK(spread(pace(Analysis(512, false), false).steps) > 0.5);
}

TEST_CASE("The paced head steps evenly", "[HeadPacer]")
{
    for (const bool jitter : {false, true})
    {
        for (const std::uint32_t block : {64u, 128u, 256u, 512u, 1024u, 2048u})
        {
            INFO("block " << block << (jitter ? " with jitter" : ""));
            const auto run = pace(Analysis(block, jitter));
            // Every step within a few percent of a frame's worth.
            CHECK(spread(run.steps) < 0.06);
            // About a block and a poll behind the newest audio, and a little more.
            const auto blockSeconds = block / sampleRate;
            CHECK(run.maxLagSeconds < 2.0 * blockSeconds + 3.0 * pollSeconds + 0.005);
        }
    }
}

TEST_CASE("A tempo slightly off does not make the paced head drift", "[HeadPacer]")
{
    for (const double error : {-0.01, 0.01})
    {
        INFO("tempo error " << error);
        const auto run = pace(Analysis(512, true), true, error);
        CHECK(spread(run.steps) < 0.06);
        CHECK(run.maxLagSeconds < 0.035);
    }
}

TEST_CASE("The paced head goes on to a head that has stopped", "[HeadPacer]")
{
    const Analysis analysis(512, false);
    HeadPacer pacer;
    double seconds = 0.05;
    for (; seconds < 1.0; seconds += frameSeconds)
        pacer.update(seconds, analysis.writtenAt(seconds), binsPerSecond);
    const auto stopped = analysis.writtenAt(seconds);
    double shown = 0.0;
    const auto stopFrames = static_cast<int>(HeadPacer::stopSeconds / frameSeconds) + 2;
    for (int frame = 0; frame < stopFrames; ++frame, seconds += frameSeconds)
        shown = pacer.update(seconds, stopped, binsPerSecond);
    CHECK(shown == Approx(stopped));
}

TEST_CASE("The paced head shows the head as written", "[HeadPacer]")
{
    HeadPacer pacer;
    CHECK(pacer.update(0.0, 1000.0, binsPerSecond) == Approx(1000.0));

    SECTION("when the tempo is not known")
    {
        CHECK(pacer.update(frameSeconds, 3000.0, 0.0) == Approx(3000.0));
    }

    SECTION("after a jump further ahead than the pacing allows")
    {
        const auto ahead = 1000.0 + HeadPacer::maxLagSeconds * binsPerSecond + 1.0;
        CHECK(pacer.update(frameSeconds, ahead, binsPerSecond) == Approx(ahead));
    }

    SECTION("after the sweep goes back")
    {
        CHECK(pacer.update(frameSeconds, 10.0, binsPerSecond) == Approx(10.0));
    }

    SECTION("after a reset")
    {
        pacer.reset();
        CHECK(pacer.update(frameSeconds, 2000.0, binsPerSecond) == Approx(2000.0));
    }
}

TEST_CASE("The paced head never passes the head as written", "[HeadPacer]")
{
    HeadPacer pacer;
    pacer.update(0.0, 0.0, binsPerSecond);
    CHECK(pacer.update(frameSeconds, 10.0, binsPerSecond) == Approx(10.0));
}
