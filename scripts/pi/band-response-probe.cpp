// Analysis-only onset probe; no sound is sent to an audio device.
#include <visona/BandAnalyzer.h>

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>
int main()
{
    constexpr double rate = 48000;
    for (std::size_t band : {0U, 5U, 17U, 26U})
    {
        visona::BandAnalyzer analyzer;
        analyzer.reset(rate);
        analyzer.setEnabled(true);
        std::vector<float> silence(48000), tone(48000);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = static_cast<float>(
                0.5 * std::sin(2 * std::numbers::pi * visona::BandAnalyzer::frequency(band) *
                               static_cast<double>(i) / rate));
        analyzer.process(silence, silence, 0);
        double response = -1;
        for (std::size_t offset = 0; offset < tone.size(); offset += 800)
        {
            const auto block = std::span(tone).subspan(offset, 800);
            analyzer.process(block, block, silence.size() + offset);
            if (analyzer.snapshots().fetch() && response < 0 &&
                analyzer.snapshots().readBuffer().db[band] >= 20 * std::log10(0.5 * 0.9))
                response = 1000.0 * (offset + 800) / rate;
        }
        std::cout << "centre_hz=" << visona::BandAnalyzer::frequency(band)
                  << " onset_90_ms=" << response << '\n';
    }
}
