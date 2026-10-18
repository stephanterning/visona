#pragma once

#include <visona/HostPlayheadReader.h>

#include <juce_audio_processors/juce_audio_processors.h>

namespace visona
{

/** JUCE adapter; validation and retained defaults live in the testable core reader. */
inline HostTransport::Playhead readHostPlayhead(juce::AudioPlayHead* head,
                                                HostPlayheadReader& reader) noexcept
{
    if (head != nullptr)
        if (const auto position = head->getPosition())
        {
            std::optional<TimeSignature> signature;
            if (const auto value = position->getTimeSignature())
                signature = TimeSignature{value->numerator, value->denominator};
            const auto ppq = position->getPpqPosition();
            const auto bpm = position->getBpm();
            return reader.read(position->getIsPlaying(),
                               ppq ? std::optional<double>(*ppq) : std::nullopt,
                               bpm ? std::optional<double>(*bpm) : std::nullopt, signature);
        }
    return {};
}

} // namespace visona
