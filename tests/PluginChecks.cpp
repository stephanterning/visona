#include "support/AllocationCounter.h"

#ifdef VISONA_CHECK_SYNC
#include "plugin/src/SyncProcessor.h"
#else
#include "plugin/src/PluginProcessor.h"
#include "plugin/src/ScopeEditor.h"
#include "ui/Palette.h"

#include <visona/SweepWindow.h>
#endif

#include <array>
#include <atomic>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Playhead : juce::AudioPlayHead
{
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override
    {
        return position;
    }
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        Playhead head;
        head.position.setBpm(120);
        head.position.setPpqPosition(0);
        head.position.setIsPlaying(true);
#ifdef VISONA_CHECK_SYNC
        visona::SyncProcessor processor;
#else
        visona::PluginProcessor processor;
#endif
        processor.setPlayHead(&head);
        juce::AudioBuffer<float> audio(2, 1024);
        juce::MidiBuffer midi;
        audio.clear();
        for (int prepare = 0; prepare < 20; ++prepare)
        {
            // Exercise both repeated preparation and destruction without releaseResources.
            processor.prepareToPlay(48000, 4096);
            for (int block = 0; block < 16; ++block)
            {
                head.position.setPpqPosition(static_cast<double>(block * 1024) / 24000.0);
                visona::test::AllocationCounter counter;
                processor.processBlock(audio, midi);
                const auto allocations = counter.count();
                check(allocations == 0, "Plugin processBlock allocated");
            }
        }
#ifdef VISONA_CHECK_SYNC
        head.position.setPpqPosition(4.0 - 511.75 / 24000.0);
        processor.processBlock(audio, midi);
        check(audio.getSample(0, 512) > 0.5f, "Sync lost the rounded bar impulse");
        check(std::bit_cast<std::uint32_t>(audio.getSample(1, 512)) ==
                  std::bit_cast<std::uint32_t>(audio.getSample(0, 512)),
              "Sync channels differ");
        head.position.setPpqPosition({});
        processor.processBlock(audio, midi);
        check(audio.getMagnitude(0, audio.getNumSamples()) == 0.0f,
              "Missing PPQ produced impulses");

        std::atomic<bool> finished{false};
        std::thread reader(
            [&]
            {
                while (!finished.load(std::memory_order_acquire))
                    static_cast<void>(processor.lastPlayhead());
            });
        for (int i = 0; i < 10000; ++i)
            processor.processBlock(audio, midi);
        finished.store(true, std::memory_order_release);
        reader.join();
#else
        auto& state = processor.instanceState();
        for (const int malformed : {-1, 8, 999999})
        {
            juce::XmlElement xml("VisonaPlugin");
            xml.setAttribute("hasInstanceAppearance", true);
            xml.setAttribute("waveformColour", malformed);
            xml.setAttribute("window", malformed);
            state.readFrom(xml, processor.globalDefaults());
            check(state.waveformColour < visona::palette::waveformColours.size(),
                  "Unbounded colour");
            check(state.window < visona::sweepWindowBars.size(), "Unbounded window");
            visona::ScopeEditor editor(processor);
            check(!editor.keyPressed(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 'd')),
                  "Plugin consumed the host's Cmd+D shortcut");
            check(!editor.keyPressed(juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 'd')),
                  "Plugin consumed the host's Ctrl+D shortcut");
            check(editor.keyPressed(juce::KeyPress('d', 0, 'd')), "D no longer opens diagnostics");
        }
        state.waveformColour = 3;
        state.window = 1;
        juce::XmlElement saved("VisonaPlugin");
        state.writeTo(saved);
        check(saved.getIntAttribute("version") == 1, "Missing state version");
        state.readFrom(saved, processor.globalDefaults());
        check(state.waveformColour == 3 && state.window == 1, "Valid state did not round trip");
#endif
        std::cout << "Plugin checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
