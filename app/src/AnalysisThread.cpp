#include "AnalysisThread.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <chrono>

namespace visona
{

namespace
{

// The ring holds about a second of audio, so this leaves a wide margin for scheduling delays.
constexpr int pollIntervalMs = 3;
constexpr int stopTimeoutMs = 2'000;

} // namespace

AnalysisThread::AnalysisThread(std::size_t numChannels)
    : juce::Thread("Visona analysis")
    , pipeline_(numChannels)
{
    startThread();
}

AnalysisThread::~AnalysisThread()
{
    stopThread(stopTimeoutMs);
}

void AnalysisThread::setStream(AudioRingBuffer* ring, double sampleRate)
{
    const std::scoped_lock lock(lock_);
    pipeline_.setStream(ring, sampleRate);
}

void AnalysisThread::run()
{
    // The band splitters' filter state decays towards denormals in silence, which are slow on
    // some CPUs (D-077).
    const juce::ScopedNoDenormals noDenormals;

    using Clock = std::chrono::steady_clock;
    while (!threadShouldExit())
    {
        const auto start = Clock::now();
        {
            const std::scoped_lock lock(lock_);
            pipeline_.poll();
        }
        const auto busy =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);
        busyNanoseconds_.fetch_add(static_cast<std::uint64_t>(busy.count()),
                                   std::memory_order_relaxed);
        wait(pollIntervalMs);
    }
}

} // namespace visona
