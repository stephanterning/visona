#pragma once

#include <visona/HostTransport.h>

#include <cmath>
#include <limits>
#include <optional>

namespace visona
{

/** Validates host data shared by both plugins. Audio thread only; no allocation. */
class HostPlayheadReader
{
public:
    /** Missing or invalid tempo/signature retains the last usable value. A finite PPQ
        position is required: a missing position must never masquerade as a frozen one. */
    [[nodiscard]] HostTransport::Playhead read(bool playing, std::optional<double> ppq,
                                               std::optional<double> bpm,
                                               std::optional<TimeSignature> signature) noexcept
    {
        if (bpm && std::isfinite(*bpm) && *bpm > 0.0)
            bpm_ = *bpm;
        if (signature && signature->isValid())
            signature_ = *signature;
        // Leave headroom for the transport's conversion to integral MIDI ticks.
        constexpr auto maxPpq = static_cast<double>(std::numeric_limits<std::int64_t>::max()) /
                                (2.0 * TimeSignature::ticksPerQuarterNote);
        const bool valid = ppq && std::isfinite(*ppq) && std::abs(*ppq) < maxPpq;
        return {valid, playing, valid ? *ppq : 0.0, bpm_, signature_, true};
    }

private:
    double bpm_ = 120.0;
    TimeSignature signature_{};
};

} // namespace visona
