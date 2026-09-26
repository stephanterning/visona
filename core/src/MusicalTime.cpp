#include "visona/MusicalTime.h"

#include <cassert>
#include <cmath>

namespace visona
{

MusicalPosition musicalPosition(double ticks, TimeSignature timeSignature) noexcept
{
    assert(timeSignature.isValid());
    assert(ticks >= 0.0);

    const auto ticksPerBeat = static_cast<double>(timeSignature.ticksPerBeat());
    const auto beats = std::floor(ticks / ticksPerBeat);
    const auto bars = std::floor(beats / timeSignature.numerator);

    MusicalPosition position;
    position.bar = static_cast<std::int64_t>(bars) + 1;
    position.beat = static_cast<int>(beats - bars * timeSignature.numerator) + 1;
    position.tickInBeat = ticks - beats * ticksPerBeat;
    return position;
}

} // namespace visona
