#include "ChannelNames.h"

namespace visona
{

namespace
{

struct Position
{
    std::size_t source = 0;
    std::size_t channelInSource = 0;
    std::size_t sourceChannelCount = 0;
};

Position positionOf(const SourceLayout& layout, std::size_t channel)
{
    for (std::size_t source = 0; source < layout.sourceCount(); ++source)
    {
        const auto& info = layout.source(source);
        if (channel < info.firstChannel + info.channelCount)
            return {source, channel - info.firstChannel, info.channelCount};
    }
    return {0, channel, 0};
}

juce::String withSourceNumber(const SourceLayout& layout, std::size_t source,
                              const juce::String& name)
{
    if (layout.sourceCount() <= 1)
        return name;
    return juce::String(source + 1) + " " + name;
}

} // namespace

juce::String channelShortName(const SourceLayout& layout, std::size_t channel)
{
    const auto position = positionOf(layout, channel);
    const auto name = [&]() -> juce::String
    {
        if (position.sourceChannelCount == 1)
            return "M";
        if (position.sourceChannelCount == 2)
            return position.channelInSource == 0 ? "L" : "R";
        return juce::String(position.channelInSource + 1);
    }();
    return withSourceNumber(layout, position.source, name);
}

juce::String channelName(const SourceLayout& layout, std::size_t channel)
{
    const auto position = positionOf(layout, channel);
    const auto name = [&]() -> juce::String
    {
        if (position.sourceChannelCount == 1)
            return "Mono";
        if (position.sourceChannelCount == 2)
            return position.channelInSource == 0 ? "Left" : "Right";
        return "Channel " + juce::String(position.channelInSource + 1);
    }();
    return withSourceNumber(layout, position.source, name);
}

juce::String inputChannelName(const juce::StringArray& inputNames, int input)
{
    if (input < 0)
        return "-";
    const auto number = juce::String(input + 1);
    const auto name = inputNames[input];
    return name.isEmpty() ? number : number + ": " + name;
}

} // namespace visona
