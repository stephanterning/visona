#include "WaveformControl.h"

#include <algorithm>
#include <array>

namespace visona
{

namespace
{

constexpr std::array<WaveformMode, 3> modes{WaveformMode::standard, WaveformMode::precise,
                                            WaveformMode::dj};

} // namespace

WaveformControl::WaveformControl()
    : ChoiceControl("WAVE", {"STD", "PRECISE", "DJ"})
{
    setTitle("Waveform");
    setTooltip("How the waveform is drawn: STD is a thin line and the lightest to draw, PRECISE "
               "fills every peak, and DJ colours it by frequency (W)");
    setMode(WaveformMode::precise);
}

void WaveformControl::setMode(WaveformMode mode)
{
    const auto found = std::find(modes.begin(), modes.end(), mode);
    if (found != modes.end())
        setSelected(static_cast<int>(found - modes.begin()));
}

void WaveformControl::choose(int index)
{
    if (onModeChange)
        onModeChange(modes[static_cast<std::size_t>(index)]);
}

} // namespace visona
