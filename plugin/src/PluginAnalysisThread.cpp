#include "PluginAnalysisThread.h"

#include <chrono>

namespace visona
{

namespace
{

constexpr int pollIntervalMs = 3;

} // namespace

PluginAnalysisThread::PluginAnalysisThread(std::size_t numChannels)
    : juce::Thread("VisonaPluginAnalysis")
    , pipeline_(numChannels)
{
    startThread();
}

PluginAnalysisThread::~PluginAnalysisThread()
{
    {
        const std::scoped_lock lock(lock_);
        pipeline_.setStream(nullptr, 0.0);
    }
    stopThread(5000);
}

void PluginAnalysisThread::setStream(AudioRingBuffer* ring, double sampleRate)
{
    const std::scoped_lock lock(lock_);
    pipeline_.setStream(ring, sampleRate);
}

void PluginAnalysisThread::run()
{
    while (!threadShouldExit())
    {
        const auto started = std::chrono::steady_clock::now();
        {
            const std::scoped_lock lock(lock_);
            pipeline_.poll();
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;
        busyNanoseconds_.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
            std::memory_order_relaxed);
        wait(pollIntervalMs);
    }
}

} // namespace visona
