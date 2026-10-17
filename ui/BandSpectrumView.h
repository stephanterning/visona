#pragma once
#include "SpectrumView.h"

#include <visona/BandAnalyzer.h>
namespace visona
{
class BandSpectrumView final : public juce::Component, private juce::Timer
{
public:
    explicit BandSpectrumView(BandAnalyzer& analyzer);
    ~BandSpectrumView() override;
    void setActive(bool active);
    void setDisplayGainDb(int db);
    void setAfterglow(int index);
    void setPaused(bool paused)
    {
        paused_ = paused;
    }
    void setWaveformColour(juce::Colour colour)
    {
        colour_ = colour;
        render();
        repaint();
    }
    SpectrumView::Stats stats() const
    {
        return stats_;
    }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    friend struct SpectrumRenderBenchmark;
    void timerCallback() override;
    void render();
    void drawBands(juce::Graphics&);
    juce::RectangleList<int> changedRegion(const BandSnapshot& previous) const;
    juce::Rectangle<float> plot() const;
    BandAnalyzer& analyzer_;
    BandSnapshot snapshot_;
    juce::Image background_;
    juce::Colour colour_;
    bool paused_ = false;
    float scale_ = 1;
    int gainDb_ = 0, afterglow_ = 2;
    SpectrumView::Stats stats_;
    double start_ = 0, renderTotal_ = 0, renderMax_ = 0, paintTotal_ = 0, paintMax_ = 0;
    int frames_ = 0, paints_ = 0;
};
} // namespace visona
