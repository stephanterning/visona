#pragma once

#include <algorithm>
#include <cstddef>
#include <lsp-plug.in/dsp-units/meters/Correlometer.h>
#include <lsp-plug.in/dsp-units/meters/Panometer.h>
#include <stdexcept>

namespace stereo_spike
{
// Experiment adapter. Own on the analysis thread, never the UI or audio callback.
// Call lsp::dsp::init() once on a setup thread before constructing any instance.
class LspStereo
{
public:
    explicit LspStereo(std::size_t period, bool linearPan = false)
        : period_(period)
    {
        if (period == 0 || period > 192000)
            throw std::invalid_argument("Stereo window must contain 1..192000 samples");
        if (correlation_.init(period) != lsp::STATUS_OK || pan_.init(period) != lsp::STATUS_OK)
            throw std::runtime_error("LSP meter allocation failed");
        correlation_.set_period(period);
        pan_.set_period(period);
        pan_.set_pan_law(linearPan ? lsp::dspu::PAN_LAW_LINEAR : lsp::dspu::PAN_LAW_EQUAL_POWER);
    }

    // No allocation. Use for stream discontinuities, including lost audio frames.
    void reset() noexcept
    {
        correlation_.clear();
        pan_.clear();
        // Pinned Panometer::clear() leaves running sums/window age intact. Force
        // set_period() to invalidate them without processing a zero-length window.
        pan_.set_period(period_ - 1);
        pan_.set_period(period_);
    }

    // Buffers contain count finite samples, are non-overlapping and preallocated.
    // Results are raw LSP values: correlation 0 does NOT establish valid stereo
    // when a channel is silent. UI validity/gating is a separate integration task.
    void process(const float* left, const float* right, float* correlation, float* pan,
                 std::size_t count) noexcept
    {
        correlation_.process(correlation, left, right, count);
        pan_.process(pan, left, right, count);
    }

private:
    std::size_t period_;
    lsp::dspu::Correlometer correlation_;
    lsp::dspu::Panometer pan_;
};
} // namespace stereo_spike
