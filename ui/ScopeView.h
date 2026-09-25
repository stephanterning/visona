#pragma once

#include <visona/ColumnReduction.h>
#include <visona/SourceLayout.h>
#include <visona/SweepSnapshot.h>
#include <visona/TripleBuffer.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace visona
{

/**
    The sweep scope: one lane per channel, stacked with L on top (D-057). Each lane shows the
    full-band signed min/max per pixel column in the neutral mono/precise colour (D-050, D-056).

    - The write head is a thin accent line followed by a small erase gap. The previous pass ahead
      of it is drawn like the new one (D-068).
    - Faint lines mark the centre and where 0 dBFS and -6 dBFS land after display gain. Display
      overshoot is cut at the lane edge with a neutral marker.
    - Display gain is applied only here (D-024).

    Rendering (D-054): the lanes are rasterized on the CPU at physical pixel resolution into
    vertical image tiles. Frames follow the display's vertical blank, capped at 60 per second, and
    only when there is a new snapshot. A frame redraws only the columns the head passed since the
    previous frame, so only the tiles holding them change; a new stream, size or gain redraws
    everything. The component is opaque and never repaints what did not change.
*/
class ScopeView final : public juce::Component
{
public:
    /** `snapshots` and `layout` must outlive the view. The view is the snapshots' only reader. */
    ScopeView(TripleBuffer<SweepSnapshot>& snapshots, const SourceLayout& layout);

    /** Display gain in whole dB, clamped to DisplayGain's range. */
    void setGainDb(int gainDb);

    [[nodiscard]] int gainDb() const noexcept
    {
        return gainDb_;
    }

    /** Rendering statistics over the most recent whole second. */
    struct Stats
    {
        double framesPerSecond = 0.0;
        double vblanksPerSecond = 0.0;
        double fullRedrawsPerSecond = 0.0;
        double renderMsAverage = 0.0;
        double renderMsMax = 0.0;
        double paintMsAverage = 0.0;
        double paintMsMax = 0.0;
        int imageWidth = 0;
        int imageHeight = 0;
        float scale = 1.0f;
        std::size_t tiles = 0;
    };

    [[nodiscard]] Stats stats() const noexcept
    {
        return stats_;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Lane
    {
        int top = 0;
        int height = 1;
    };

    struct Timing
    {
        int count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;

        void add(double ms) noexcept;
    };

    void onVBlank(double timestampSeconds);
    void updateStats(double timestampSeconds);

    /** Makes the tiles match the component's size at `scale`. Returns true if they changed. */
    bool ensureTiles(float scale);
    void layoutLanes(std::size_t numLanes);

    /** Brings the tiles up to date with the current snapshot. Returns the area to repaint. */
    juce::Rectangle<int> renderChanges();
    void renderAll();
    void renderColumns(int first, int last);
    void drawTileColumns(juce::Image::BitmapData& pixels, int tileStart, int first, int last);

    /** The first column of the head line and the last column of the erase gap after it, or -1
        for both if nothing has been written. */
    [[nodiscard]] std::pair<int, int> headColumns(const SweepBuffer& sweep) const noexcept;
    [[nodiscard]] juce::Rectangle<int> logicalColumns(int first, int last) const noexcept;
    void drawLabels(juce::Graphics& g) const;

    TripleBuffer<SweepSnapshot>& snapshots_;
    const SourceLayout& layout_;

    int gainDb_ = 0;

    // Tiles are in physical pixels, scale_ per logical pixel.
    float scale_ = 1.0f;
    int width_ = 0;
    int height_ = 0;
    std::vector<juce::Image> tiles_;
    std::vector<Lane> lanes_;
    ColumnMapping mapping_{1, 1};
    std::vector<ColumnSpan> spans_;
    int headWidth_ = 1;
    int gapWidth_ = 0;
    int markerHeight_ = 1;
    bool needsFullRender_ = true;

    // What the tiles show.
    bool rendered_ = false;
    std::uint64_t renderedStream_ = 0;
    std::uint64_t renderedGeneration_ = 0;
    std::uint64_t renderedPass_ = 0;
    std::size_t renderedHead_ = 0;
    int renderedHeadStart_ = 0;

    double nextFrameSeconds_ = 0.0;
    double firstFrameSeconds_ = 0.0;
    double lastFrameSeconds_ = 0.0;
    double statsWindowStart_ = 0.0;
    int vblanks_ = 0;
    int fullRedraws_ = 0;
    Timing renderTiming_;
    Timing paintTiming_;
    Stats stats_;

    // Last, so that it stops calling back before anything above is destroyed.
    juce::VBlankAttachment vblank_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

} // namespace visona
