#pragma once

#include "ChoiceControl.h"

#include <visona/WaveformStyle.h>

#include <functional>

namespace visona
{

/**
    The waveform's drawing mode as a select menu, WAVE above `PRECISE`, with the choices STD,
    PRECISE and DJ (D-091, D-108). Like WindowControl, it shows what setMode() says and asks for
    changes through onModeChange.
*/
class WaveformControl final : public ChoiceControl
{
public:
    WaveformControl();

    std::function<void(WaveformMode mode)> onModeChange;

    void setMode(WaveformMode mode);

private:
    void choose(int index) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformControl)
};

} // namespace visona
