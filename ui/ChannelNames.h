#pragma once

#include <visona/SourceLayout.h>

#include <juce_core/juce_core.h>

#include <cstddef>

namespace visona
{

/**
    A short name for a source channel: L and R in a stereo source, M in a mono source, and
    otherwise the channel's number within its source. With more than one source, the source number
    comes first, as in "2 L".
*/
[[nodiscard]] juce::String channelShortName(const SourceLayout& layout, std::size_t channel);

/** A longer name for a source channel, such as "Left", "Right", "Mono" or "Channel 3". */
[[nodiscard]] juce::String channelName(const SourceLayout& layout, std::size_t channel);

/** A device input channel as "number: name", counting from 1. */
[[nodiscard]] juce::String inputChannelName(const juce::StringArray& inputNames, int input);

} // namespace visona
