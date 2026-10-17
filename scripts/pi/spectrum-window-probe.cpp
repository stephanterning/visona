// Deterministic analysis-only response and bass-resolution probe. No audio device required.
#include <visona/SpectrumAnalyzer.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <vector>

using namespace visona;
constexpr double rate = 48000;
std::vector<float> signal(std::size_t count, double a, double b = 0)
{
    std::vector<float> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] =
            static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * a * i / rate) +
                               (b > 0 ? 0.4 * std::sin(2 * std::numbers::pi * b * i / rate) : 0));
    return result;
}
float peak(const SpectrumSnapshot& snapshot)
{
    return *std::max_element(snapshot.db[0].begin(), snapshot.db[0].end());
}
int main(int argc, char** argv)
{
    SpectrumSettings settings;
    settings.curves[0].smoothing = 2;
    SpectrumAnalyzer analyzer;
    analyzer.reset(rate);
    analyzer.setEnabled(true);
    analyzer.setSettings(settings);
    auto tone = signal(48000, 1000);
    analyzer.process(tone, tone, 0);
    analyzer.snapshots().fetch();
    const float target = peak(analyzer.snapshots().readBuffer());
    analyzer.reset(rate);
    std::vector<float> silence(spectrumFftSize);
    analyzer.process(silence, silence, 0);
    double halfway = -1, ninety = -1;
    for (std::size_t offset = 0; offset + spectrumHopSize <= tone.size(); offset += spectrumHopSize)
    {
        const auto block = std::span(tone).subspan(offset, spectrumHopSize);
        analyzer.process(block, block, spectrumFftSize + offset);
        if (analyzer.snapshots().fetch())
        {
            const auto db = peak(analyzer.snapshots().readBuffer());
            const auto ms = 1000.0 * (offset + spectrumHopSize) / rate;
            if (halfway < 0 && db >= target + 20 * std::log10(0.5))
                halfway = ms;
            if (ninety < 0 && db >= target + 20 * std::log10(0.9))
                ninety = ms;
        }
    }
    std::cout << "FFT=" << spectrumFftSize << " bin_hz=" << rate / spectrumFftSize
              << " window_ms=" << 1000.0 * spectrumFftSize / rate << " onset_50_ms=" << halfway
              << " onset_90_ms=" << ninety << '\n';
    for (double second : {70.0, 80.0})
    {
        analyzer.reset(rate);
        auto bass = signal(48000, 60, second);
        analyzer.process(bass, bass, 0);
        analyzer.snapshots().fetch();
        const auto snapshot = analyzer.snapshots().readBuffer();
        std::ofstream csv(std::string(argc > 1 ? argv[1] : "/tmp") + "/fft-" +
                          std::to_string(spectrumFftSize) + "-60-" +
                          std::to_string(static_cast<int>(second)) + ".csv");
        csv << "hz,db\n";
        int peaks = 0;
        for (std::size_t i = 1; i + 1 < spectrumPoints; ++i)
        {
            const double hz = 20 * std::pow(1000.0, static_cast<double>(i) / (spectrumPoints - 1));
            if (hz >= 30 && hz <= 110)
            {
                csv << hz << ',' << snapshot.db[0][i] << '\n';
                if (hz >= 50 && hz <= second + 10 && snapshot.db[0][i] > snapshot.db[0][i - 1] &&
                    snapshot.db[0][i] >= snapshot.db[0][i + 1])
                    ++peaks;
            }
        }
        std::cout << "tones=60+" << second << " distinguishable_local_peaks=" << peaks << '\n';
    }
}
