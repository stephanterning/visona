#pragma once

#include <visona/ColumnReduction.h>
#include <visona/SourceLayout.h>
#include <visona/SweepSnapshot.h>
#include <visona/SweepZoom.h>
#include <visona/TripleBuffer.h>
#include <visona/WaveformStyle.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace visona
{

/**
    The sweep scope: one lane per channel, stacked with L on top (D-057). Each lane shows the
    full-band signal in one of three modes (D-091): STD, a thin line through the signal sampled at
    every column edge, in the waveform colour; PRECISE, the signed min/max of every column filled
    in the waveform colour (D-050); and DJ, PRECISE coloured by the column's bands, bass red, mids
    green and highs blue, with each band shifted back by its filter delay (D-092). The shape
    never depends on the colouring (D-056).

    - The write head is a thin accent line followed by a small erase gap. The previous pass ahead
      of it is drawn like the new one (D-068).
    - Faint lines mark the centre and where 0 dBFS and -6 dBFS land after display gain, and
      -12 dBFS and -18 dBFS from +4 and +10 dB of gain (D-088). Display overshoot is cut at the
      lane edge with a neutral marker.
    - While the sweep follows MIDI Clock, a neutral grey grid marks bars, beats and a finer note
      value that depends on how much is in view: sixteenths with 1 bar, down to sixty-fourths with
      ¼ bar or less, and up to quarter notes with more than 2 bars (D-087). Small bar numbers sit
      at the bottom edge, and the bottom right corner names the finest note value and its length,
      such as "1/16 · 125 ms".
    - STOPPED dims the frozen view slightly and shows a pause mark.
    - Display gain and zoom are applied only here (D-024, D-085).

    Zoom (D-085) shows part of the window, down to 1/32 of it, without changing the window. Drag
    across the scope to zoom to the part selected; a drag under 8 pixels is a click and does
    nothing. The scroll wheel, a trackpad pinch and a two-finger touch pinch zoom around the
    pointer. A double-click, Esc (handled by the main component) or a new window resets it. The
    head line shows only while the head is in view.

    Rendering (D-054): the lanes are rasterized on the CPU at physical pixel resolution into
    vertical image tiles. Frames follow the display's vertical blank, capped at 60 per second, and
    only when there is a new snapshot. A frame redraws only the columns the head passed since the
    previous frame, so only the tiles holding them change; a new stream, size, gain or zoom
    redraws everything. The component is opaque and never repaints what did not change.
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

    void setWaveformMode(WaveformMode mode);

    [[nodiscard]] WaveformMode waveformMode() const noexcept
    {
        return mode_;
    }

    /** The colour of the STD and PRECISE modes (D-093). */
    void setWaveformColour(juce::Colour colour);

    /** The snapshot on screen, for the status bar. Message thread only. */
    [[nodiscard]] const SweepSnapshot& snapshot() const noexcept
    {
        return snapshots_.readBuffer();
    }

    void setZoom(SweepZoom zoom);

    void resetZoom()
    {
        setZoom({});
    }

    [[nodiscard]] SweepZoom zoom() const noexcept
    {
        return zoom_;
    }

    /** Where the head is, as a fraction of the window, or a negative number before the first
        pass. */
    [[nodiscard]] double headPosition() const noexcept;

    /** Called when the transport state, the sweep's mode or the window changes. */
    std::function<void()> onTransportChange;

    /** Called when the zoom changes. */
    std::function<void()> onZoomChange;

    /** Called after each frame drawn from a new snapshot. */
    std::function<void()> onFrame;

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
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

private:
    struct Lane
    {
        int top = 0;
        int height = 1;
    };

    enum class GridLine : std::uint8_t
    {
        none,
        fine,     ///< Thirty-seconds and sixty-fourths.
        division, ///< Eighths and sixteenths.
        beat,
        bar
    };

    /** A finger on the screen, for pinching. */
    struct Touch
    {
        int source = -1;
        float x = 0.0f;
    };

    /** What the grid depends on. Musical windows are whole numbers of ticks. */
    struct GridKey
    {
        bool musical = false;
        std::int64_t windowTicks = 0;
        std::int64_t windowStartTick = 0;
        int ticksPerBar = 0;
        int ticksPerBeat = 0;
        int width = 0;

        friend bool operator==(const GridKey&, const GridKey&) = default;
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
    void noticeTransport();
    void updateGrid();

    /** How many bars are in view, or 0 without a musical window. */
    [[nodiscard]] double visibleBars() const noexcept;

    /** The grid's finest note value and its length at the current tempo, such as
        "1/16 · 125 ms", or nothing without a grid. */
    [[nodiscard]] juce::String resolutionText() const;
    [[nodiscard]] juce::Rectangle<int> barNumberArea() const noexcept;
    void drawBarNumbers(juce::Graphics& g) const;
    void drawStopped(juce::Graphics& g) const;

    /** Makes the tiles match the component's size at `scale`. Returns true if they changed. */
    bool ensureTiles(float scale);
    void layoutLanes(std::size_t numLanes);

    /** Brings the tiles up to date with the current snapshot and repaints what changed. */
    void renderChanges();
    void renderAll();
    void renderColumns(int first, int last);
    void drawTileColumns(juce::Image::BitmapData& pixels, int tileStart, int first, int last);

    /** How many bins each band is read later in DJ colouring, to line it up with the shape. */
    [[nodiscard]] std::array<std::size_t, 3> bandShifts() const noexcept;

    /** The first column of the head line and the last column of the erase gap after it, or -1
        for both if nothing has been written or the head is out of view. */
    [[nodiscard]] std::pair<int, int> headColumns(const SweepBuffer& sweep) const noexcept;
    [[nodiscard]] juce::Rectangle<int> logicalColumns(int first, int last) const noexcept;
    void drawLabels(juce::Graphics& g) const;

    /** The part of the scope a drag has selected, or nothing while it is too short. */
    [[nodiscard]] juce::Rectangle<int> selectionArea() const noexcept;
    void drawSelection(juce::Graphics& g) const;
    void zoomAround(float x, double factor);
    [[nodiscard]] bool updatePinch();

    TripleBuffer<SweepSnapshot>& snapshots_;
    const SourceLayout& layout_;

    int gainDb_ = 0;
    WaveformMode mode_ = WaveformMode::precise;
    juce::PixelARGB waveformColour_;

    // Tiles are in physical pixels, scale_ per logical pixel.
    float scale_ = 1.0f;
    int width_ = 0;
    int height_ = 0;
    std::vector<juce::Image> tiles_;
    std::vector<Lane> lanes_;
    SweepZoom zoom_;
    ColumnMapping mapping_{1, 1};
    bool mappingIsStale_ = true;
    std::vector<ColumnSpan> spans_;
    std::vector<BandLevels> bands_;
    std::vector<float> edges_;
    std::vector<GridLine> grid_;
    GridKey gridKey_;
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

    // The transport as last shown.
    TransportState shownState_ = TransportState::waiting;
    bool shownMusical_ = false;
    std::size_t shownWindow_ = 0;
    double shownWindowStart_ = 0.0;
    juce::String shownResolution_;

    // A drag that selects what to zoom to, in logical pixels, and the fingers of a pinch.
    std::optional<int> dragSource_;
    float dragStart_ = 0.0f;
    float dragEnd_ = 0.0f;
    std::array<Touch, 2> touches_;
    bool pinching_ = false;
    float pinchStartDistance_ = 0.0f;
    double pinchAnchor_ = 0.0;
    SweepZoom pinchStartZoom_;

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
