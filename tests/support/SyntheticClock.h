#pragma once

#include <visona/BlockTiming.h>
#include <visona/MidiClockTransport.h>
#include <visona/MusicalTime.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace visona::test
{

/**
    An audio interface's clock as the host sees it: frame s is captured at host time
    streamStartNs + s × nsPerFrame(). A positive drift makes the audio clock slower than nominal.
*/
struct AudioClock
{
    double sampleRate = 96'000.0;
    std::uint64_t streamStartNs = 0;
    double driftPpm = 0.0;

    [[nodiscard]] double nsPerFrame() const
    {
        return 1.0e9 / sampleRate * (1.0 + driftPpm * 1.0e-6);
    }

    [[nodiscard]] std::uint64_t hostTimeOf(double sampleIndex) const
    {
        return streamStartNs + static_cast<std::uint64_t>(std::llround(sampleIndex * nsPerFrame()));
    }

    [[nodiscard]] double sampleOf(std::uint64_t hostTimeNs) const
    {
        const auto sinceStart = static_cast<std::int64_t>(hostTimeNs - streamStartNs);
        return static_cast<double>(sinceStart) / nsPerFrame();
    }
};

/**
    Timings of `numBlocks` consecutive blocks of `blockSize` frames from frame 0 on. Each block's
    host time is moved by a random amount of up to ±maxJitterNs, like a callback that reads the
    clock when it starts.
*/
inline std::vector<BlockTiming> blockTimings(const AudioClock& clock, std::size_t numBlocks,
                                             std::uint32_t blockSize, double maxJitterNs = 0.0,
                                             std::uint32_t seed = 1)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> jitter(-maxJitterNs, maxJitterNs);
    std::vector<BlockTiming> blocks(numBlocks);
    for (std::size_t block = 0; block < numBlocks; ++block)
    {
        const auto sampleIndex = static_cast<std::uint64_t>(block) * blockSize;
        const auto offset = std::llround(maxJitterNs > 0.0 ? jitter(random) : 0.0);
        const auto hostTime = clock.hostTimeOf(static_cast<double>(sampleIndex));
        blocks[block] = {sampleIndex, hostTime + static_cast<std::uint64_t>(offset), blockSize};
    }
    return blocks;
}

/** Samples between two MIDI Clock ticks at `bpm`. */
inline double samplesPerTick(double bpm, double sampleRate)
{
    return 60.0 * sampleRate / (ticksPerQuarterNote * bpm);
}

/**
    Sample times of `count` MIDI Clock ticks from `firstSample` on. The interval after tick k
    follows the tempo bpmAtTick(k).
*/
template <typename BpmAtTick>
std::vector<double> tickTimes(std::size_t count, double firstSample, double sampleRate,
                              BpmAtTick bpmAtTick)
{
    std::vector<double> times(count);
    auto time = firstSample;
    for (std::size_t tick = 0; tick < count; ++tick)
    {
        times[tick] = time;
        time += samplesPerTick(bpmAtTick(tick), sampleRate);
    }
    return times;
}

/** Sample times of `count` ticks at a constant tempo. */
inline std::vector<double> tickTimes(std::size_t count, double firstSample, double sampleRate,
                                     double bpm)
{
    return tickTimes(count, firstSample, sampleRate, [bpm](std::size_t) { return bpm; });
}

/** `times`, each moved by a random amount of up to ±maxJitter, from a fixed seed. */
inline std::vector<double> withJitter(std::vector<double> times, double maxJitter,
                                      std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> jitter(-maxJitter, maxJitter);
    for (auto& time : times)
        time += jitter(random);
    return times;
}

/**
    How a sweep would write the samples in [from, to): each sample's position selects a pass,
    ⌊position / windowTicks⌋, and a bin within it. The sweep of PR 7 needs every bin written once
    per pass, in order.
*/
struct SweepWalk
{
    /** Bins skipped: a sample's bin is more than one past the previous sample's. */
    std::size_t holes = 0;

    /** Bins written again: a sample's bin or pass is before the previous sample's. */
    std::size_t rewrites = 0;

    std::size_t samplesWithPosition = 0;
    std::size_t completePasses = 0;
};

inline SweepWalk walkSweep(const MidiClockTransport& transport, std::int64_t from, std::int64_t to,
                           double windowTicks, std::size_t numBins)
{
    SweepWalk walk;
    std::optional<std::int64_t> previousPass;
    std::size_t previousBin = 0;
    for (auto sample = from; sample < to; ++sample)
    {
        const auto position = transport.positionAt(static_cast<double>(sample));
        if (!position)
            continue;
        ++walk.samplesWithPosition;

        const auto windows = *position / windowTicks;
        const auto pass = static_cast<std::int64_t>(std::floor(windows));
        const auto phase = windows - static_cast<double>(pass);
        const auto bin = static_cast<std::size_t>(phase * static_cast<double>(numBins));

        if (previousPass)
        {
            if (pass < *previousPass || (pass == *previousPass && bin < previousBin))
            {
                ++walk.rewrites;
            }
            else if (pass == *previousPass)
            {
                walk.holes += bin > previousBin + 1 ? bin - previousBin - 1 : 0;
            }
            else
            {
                walk.holes += numBins - 1 - previousBin;
                walk.holes += bin;
                walk.holes += static_cast<std::size_t>(pass - *previousPass - 1) * numBins;
                walk.completePasses += previousBin == numBins - 1 ? 1 : 0;
            }
        }
        previousPass = pass;
        previousBin = bin;
    }
    return walk;
}

/**
    The first whole sample in [from, to) whose position is at least `ticks`, or std::nullopt.
    Bar lines at whole ticks land on the sample this returns.
*/
inline std::optional<std::int64_t> firstSampleReaching(const MidiClockTransport& transport,
                                                       double ticks, std::int64_t from,
                                                       std::int64_t to)
{
    for (auto sample = from; sample < to; ++sample)
    {
        const auto position = transport.positionAt(static_cast<double>(sample));
        if (position && *position >= ticks)
            return sample;
    }
    return std::nullopt;
}

} // namespace visona::test
