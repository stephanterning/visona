#include "ScopeView.h"

#include "ChannelNames.h"
#include "Palette.h"

#include <visona/LaneMapping.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace visona
{

namespace
{

// Narrow tiles keep the pixels handed to the system per frame small: on macOS, drawing a changed
// image copies all of it.
constexpr int tileWidth = 64;

// At most 60 frames per second on average, whatever the display's refresh rate. The tolerance lets
// a 60 Hz display render every vertical blank despite timestamp jitter.
constexpr double frameInterval = 1.0 / 60.0;
constexpr double frameTolerance = 0.001;

// In logical pixels.
constexpr float headLineWidth = 1.5f;
constexpr float eraseGapWidth = 6.0f;
constexpr float clipMarkerHeight = 3.0f;
constexpr float labelMargin = 6.0f;
constexpr float labelFontHeight = 11.0f;
constexpr float gridLineWidth = 1.0f;
constexpr float stoppedDimming = 0.3f;
constexpr float pinnedLabelClearance = 48.0f;

// Zoom input (D-085). A drag shorter than minSelectionWidth logical pixels is a click. A scroll of
// one unit of MouseWheelDetails zooms by 2^wheelZoomRate: a notch of a mouse wheel on macOS is
// about 1.25 times. A pinch starts measuring once the fingers are minPinchDistance apart.
constexpr float minSelectionWidth = 8.0f;
constexpr double wheelZoomRate = 8.0;
constexpr float minPinchDistance = 24.0f;
constexpr float selectionFillAlpha = 0.14f;
constexpr float selectionEdgeAlpha = 0.6f;

/** Sixteenths are one MIDI beat of 6 ticks. The grid counts half ticks, so that sixty-fourths,
    1.5 ticks, are whole numbers of them. */
constexpr int halfTicksPerSixteenth = 12;
constexpr int halfTicksPerThirtySecond = 6;
constexpr int halfTicksPerSixtyFourth = 3;

/** The amplitude reference lines: 0 dBFS and -6 dBFS, with their labels in UTF-8. */
struct Reference
{
    float level;
    const char* label;
};
const std::array<Reference, 2> references{{{1.0f, "0 dB"},
                                           {0.501187f, "\xe2\x88\x92"
                                                       "6 dB"}}};

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

/** Writes opaque pixels into one tile. Columns are in the scope's physical coordinates. */
class TileCanvas
{
public:
    TileCanvas(juce::Image::BitmapData& pixels, int originX) noexcept
        : pixels_(pixels)
        , originX_(originX)
    {
        jassert(pixels.pixelStride == static_cast<int>(sizeof(juce::PixelARGB)));
    }

    /** Fills columns [x0, x1] and rows [y0, y1], all inclusive. */
    void fill(int x0, int x1, int y0, int y1, juce::PixelARGB colour) const noexcept
    {
        const auto count = static_cast<std::size_t>(x1 - x0 + 1);
        for (int y = y0; y <= y1; ++y)
        {
            auto* const row =
                reinterpret_cast<juce::PixelARGB*>(pixels_.getLinePointer(y)) + (x0 - originX_);
            std::fill_n(row, count, colour);
        }
    }

private:
    juce::Image::BitmapData& pixels_;
    int originX_;
};

struct Colours
{
    juce::PixelARGB laneBackground = palette::laneBackground.getPixelARGB();
    juce::PixelARGB laneDivider = palette::laneDivider.getPixelARGB();
    juce::PixelARGB centreLine = palette::centreLine.getPixelARGB();
    juce::PixelARGB referenceLine = palette::referenceLine.getPixelARGB();
    juce::PixelARGB waveform = palette::waveform.getPixelARGB();
    juce::PixelARGB clipMarker = palette::clipMarker.getPixelARGB();
    juce::PixelARGB head = palette::head.getPixelARGB();
    juce::PixelARGB gridBar = palette::gridBar.getPixelARGB();
    juce::PixelARGB gridBeat = palette::gridBeat.getPixelARGB();
    juce::PixelARGB gridSixteenth = palette::gridSixteenth.getPixelARGB();
    juce::PixelARGB gridFine = palette::gridFine.getPixelARGB();
};

const Colours& colours()
{
    static const Colours instance;
    return instance;
}

int toPhysical(float logical, float scale)
{
    return std::max(1, juce::roundToInt(logical * scale));
}

} // namespace

void ScopeView::Timing::add(double ms) noexcept
{
    ++count;
    totalMs += ms;
    maxMs = std::max(maxMs, ms);
}

ScopeView::ScopeView(TripleBuffer<SweepSnapshot>& snapshots, const SourceLayout& layout)
    : snapshots_(snapshots)
    , layout_(layout)
    , vblank_(this, [this](double timestampSeconds) { onVBlank(timestampSeconds); })
{
    setOpaque(true);
    setInterceptsMouseClicks(true, false);
}

void ScopeView::setGainDb(int gainDb)
{
    gainDb = DisplayGain::clampDb(gainDb);
    if (gainDb == gainDb_)
        return;
    gainDb_ = gainDb;
    needsFullRender_ = true;
    repaint();
}

void ScopeView::paint(juce::Graphics& g)
{
    const auto start = nowMs();

    // The tiles follow the scale actually used for painting, so moving the window to a display
    // with another scale redraws them at that display's resolution.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (ensureTiles(scale) || needsFullRender_)
        renderAll();

    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    const auto clip = g.getClipBounds();
    for (std::size_t tile = 0; tile < tiles_.size(); ++tile)
    {
        const auto first = static_cast<int>(tile) * tileWidth;
        if (!logicalColumns(first, first + tiles_[tile].getWidth() - 1).intersects(clip))
            continue;
        g.drawImageTransformed(tiles_[tile],
                               juce::AffineTransform::translation(static_cast<float>(first), 0.0f)
                                   .scaled(1.0f / scale_));
    }
    drawLabels(g);
    drawBarNumbers(g);
    drawStopped(g);
    drawSelection(g);

    paintTiming_.add(nowMs() - start);
}

void ScopeView::resized()
{
    needsFullRender_ = true;
    repaint();
}

void ScopeView::onVBlank(double timestampSeconds)
{
    ++vblanks_;
    updateStats(timestampSeconds);

    // Nothing to draw into before the first paint has sized the tiles.
    if (tiles_.empty() || timestampSeconds + frameTolerance < nextFrameSeconds_)
        return;
    const bool fresh = snapshots_.fetch();
    if (!fresh && !needsFullRender_)
        return;
    // Frames are due at a steady 60 per second; after a stall, the schedule restarts from now
    // instead of catching up with a burst.
    const bool stalled = timestampSeconds - nextFrameSeconds_ > frameInterval;
    nextFrameSeconds_ = (stalled ? timestampSeconds : nextFrameSeconds_) + frameInterval;

    const auto start = nowMs();
    renderChanges();
    renderTiming_.add(nowMs() - start);
    noticeTransport();
    if (renderTiming_.count == 1)
        firstFrameSeconds_ = timestampSeconds;
    lastFrameSeconds_ = timestampSeconds;
    if (onFrame)
        onFrame();
}

void ScopeView::noticeTransport()
{
    const auto& snapshot = snapshots_.readBuffer();
    const bool stateChanged = snapshot.transportState != shownState_;
    const bool modeChanged = snapshot.musical != shownMusical_ || snapshot.window != shownWindow_;
    if (stateChanged && (snapshot.transportState == TransportState::stopped ||
                         shownState_ == TransportState::stopped))
        repaint();
    if (modeChanged || !juce::exactlyEqual(snapshot.windowStartTick, shownWindowStart_))
        repaint(barNumberArea());

    shownState_ = snapshot.transportState;
    shownMusical_ = snapshot.musical;
    shownWindow_ = snapshot.window;
    shownWindowStart_ = snapshot.windowStartTick;
    // A zoom is a part of one window; in another it would show something else.
    if (modeChanged)
        resetZoom();
    if ((stateChanged || modeChanged) && onTransportChange)
        onTransportChange();
}

void ScopeView::updateStats(double timestampSeconds)
{
    const auto elapsed = timestampSeconds - statsWindowStart_;
    if (elapsed < 1.0)
        return;
    if (statsWindowStart_ > 0.0)
    {
        // While frames keep coming, count the intervals between them: counting frames in a window
        // is off by one whenever a frame lands just inside either end of it.
        const bool continuous =
            renderTiming_.count > 1 && timestampSeconds - lastFrameSeconds_ < 2.0 * frameInterval;
        stats_.framesPerSecond =
            continuous ? (renderTiming_.count - 1) / (lastFrameSeconds_ - firstFrameSeconds_)
                       : renderTiming_.count / elapsed;
        stats_.vblanksPerSecond = vblanks_ / elapsed;
        stats_.fullRedrawsPerSecond = fullRedraws_ / elapsed;
        stats_.renderMsAverage =
            renderTiming_.count > 0 ? renderTiming_.totalMs / renderTiming_.count : 0.0;
        stats_.renderMsMax = renderTiming_.maxMs;
        stats_.paintMsAverage =
            paintTiming_.count > 0 ? paintTiming_.totalMs / paintTiming_.count : 0.0;
        stats_.paintMsMax = paintTiming_.maxMs;
    }
    stats_.imageWidth = width_;
    stats_.imageHeight = height_;
    stats_.scale = scale_;
    stats_.tiles = tiles_.size();

    statsWindowStart_ = timestampSeconds;
    vblanks_ = 0;
    fullRedraws_ = 0;
    renderTiming_ = {};
    paintTiming_ = {};
}

bool ScopeView::ensureTiles(float scale)
{
    if (!(scale > 0.0f))
        scale = 1.0f;
    const auto width = std::max(1, juce::roundToInt(static_cast<float>(getWidth()) * scale));
    const auto height = std::max(1, juce::roundToInt(static_cast<float>(getHeight()) * scale));
    if (!tiles_.empty() && width == width_ && height == height_ &&
        juce::approximatelyEqual(scale, scale_))
        return false;

    scale_ = scale;
    width_ = width;
    height_ = height;
    tiles_.clear();
    for (int x = 0; x < width_; x += tileWidth)
        tiles_.emplace_back(juce::Image::ARGB, std::min(tileWidth, width_ - x), height_, false);
    spans_.assign(static_cast<std::size_t>(tileWidth), {});
    lanes_.clear();

    headWidth_ = toPhysical(headLineWidth, scale_);
    gapWidth_ = toPhysical(eraseGapWidth, scale_);
    markerHeight_ = toPhysical(clipMarkerHeight, scale_);
    needsFullRender_ = true;
    return true;
}

void ScopeView::layoutLanes(std::size_t numLanes)
{
    lanes_.assign(std::max<std::size_t>(numLanes, 1), {});
    const auto count = static_cast<int>(lanes_.size());
    const auto divider = std::max(1, juce::roundToInt(scale_));
    const auto available = std::max(count, height_ - (count - 1) * divider);
    for (int lane = 0; lane < count; ++lane)
    {
        const auto top = available * lane / count;
        const auto bottom = available * (lane + 1) / count;
        lanes_[static_cast<std::size_t>(lane)] = {top + lane * divider, std::max(1, bottom - top)};
    }
}

void ScopeView::renderChanges()
{
    const auto& snapshot = snapshots_.readBuffer();
    const auto& sweep = snapshot.sweep;
    const bool sameSweep = rendered_ && !needsFullRender_ && snapshot.streamId == renderedStream_ &&
                           sweep.generation() == renderedGeneration_;
    if (sameSweep && sweep.pass() == 0)
        return;

    // Within a window, the head only changes the bins it passes. Both passes look the same, so
    // the start of a new pass changes only the bins across the end of the window.
    const bool samePass = sweep.pass() == renderedPass_ && sweep.head() >= renderedHead_;
    const bool nextPass = sweep.pass() == renderedPass_ + 1 && sweep.head() < renderedHead_;
    if (!sameSweep || renderedPass_ == 0 || !(samePass || nextPass))
    {
        renderAll();
        repaint();
        return;
    }

    // The columns of the bins from where the head was to where it is now, which a zoomed view
    // may show in two places or not at all, and the head line and erase gap, before and after.
    std::array<ColumnMapping::ColumnRange, 4> dirty;
    std::array<ColumnMapping::ColumnRange, 2> passed;
    std::size_t count = 0;
    const auto passedCount = mapping_.columnsOf(renderedHead_, sweep.head(), passed);
    for (std::size_t range = 0; range < passedCount; ++range)
        dirty[count++] = passed[range];
    const auto [headStart, gapEnd] = headColumns(sweep);
    if (renderedHeadStart_ >= 0)
        dirty[count++] = {static_cast<std::size_t>(renderedHeadStart_),
                          static_cast<std::size_t>(std::min(
                              renderedHeadStart_ + headWidth_ + gapWidth_ - 1, width_ - 1))};
    if (headStart >= 0)
        dirty[count++] = {static_cast<std::size_t>(headStart), static_cast<std::size_t>(gapEnd)};
    renderedPass_ = sweep.pass();
    renderedHead_ = sweep.head();
    renderedHeadStart_ = headStart;

    std::sort(dirty.begin(), dirty.begin() + static_cast<std::ptrdiff_t>(count),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t index = 0; index < count;)
    {
        auto range = dirty[index++];
        while (index < count && dirty[index].first <= range.last + 1)
            range.last = std::max(range.last, dirty[index++].last);
        const auto first = static_cast<int>(range.first);
        const auto last = static_cast<int>(range.last);
        renderColumns(first, last);
        repaint(logicalColumns(first, last));
    }
}

void ScopeView::renderAll()
{
    if (tiles_.empty())
        return;
    const auto& snapshot = snapshots_.readBuffer();
    const auto& sweep = snapshot.sweep;
    renderColumns(0, width_ - 1);

    rendered_ = true;
    renderedStream_ = snapshot.streamId;
    renderedGeneration_ = sweep.generation();
    renderedPass_ = sweep.pass();
    renderedHead_ = sweep.head();
    renderedHeadStart_ = headColumns(sweep).first;
    needsFullRender_ = false;
    ++fullRedraws_;
}

void ScopeView::renderColumns(int first, int last)
{
    const auto& sweep = snapshots_.readBuffer().sweep;
    if (lanes_.size() != std::max<std::size_t>(sweep.numChannels(), 1))
        layoutLanes(sweep.numChannels());
    if (sweep.numBins() > 0 && (mappingIsStale_ || mapping_.numBins() != sweep.numBins() ||
                                mapping_.numColumns() != static_cast<std::size_t>(width_)))
    {
        mapping_ = ColumnMapping(sweep.numBins(), static_cast<std::size_t>(width_), zoom_.offset,
                                 zoom_.span);
        mappingIsStale_ = false;
        grid_.clear();
    }
    updateGrid();

    first = std::max(first, 0);
    last = std::min(last, width_ - 1);
    if (first > last)
        return;
    for (int tile = first / tileWidth; tile <= last / tileWidth; ++tile)
    {
        auto& image = tiles_[static_cast<std::size_t>(tile)];
        const auto tileStart = tile * tileWidth;
        juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
        drawTileColumns(pixels, tileStart, std::max(first, tileStart),
                        std::min(last, tileStart + image.getWidth() - 1));
    }
}

void ScopeView::drawTileColumns(juce::Image::BitmapData& pixels, int tileStart, int first, int last)
{
    const auto& sweep = snapshots_.readBuffer().sweep;
    const auto& colour = colours();
    const TileCanvas canvas(pixels, tileStart);
    const auto gain = DisplayGain::toLinear(gainDb_);
    const auto [headStart, gapEnd] = headColumns(sweep);
    const auto count = static_cast<std::size_t>(last - first + 1);
    const std::span spans(spans_.data(), count);

    for (std::size_t index = 0; index < lanes_.size(); ++index)
    {
        const auto& lane = lanes_[index];
        const auto laneBottom = lane.top + lane.height - 1;
        const LaneMapping mapping(lane.top, lane.height, gain);

        canvas.fill(first, last, lane.top, laneBottom, colour.laneBackground);
        for (int column = first; column <= last; ++column)
        {
            switch (grid_[static_cast<std::size_t>(column)])
            {
            case GridLine::none:
                break;
            case GridLine::fine:
                canvas.fill(column, column, lane.top, laneBottom, colour.gridFine);
                break;
            case GridLine::sixteenth:
                canvas.fill(column, column, lane.top, laneBottom, colour.gridSixteenth);
                break;
            case GridLine::beat:
                canvas.fill(column, column, lane.top, laneBottom, colour.gridBeat);
                break;
            case GridLine::bar:
                canvas.fill(column, column, lane.top, laneBottom, colour.gridBar);
                break;
            }
        }
        for (const auto& reference : references)
        {
            if (!mapping.isInside(reference.level))
                continue;
            for (const auto level : {reference.level, -reference.level})
            {
                const auto row = mapping.rowOf(level);
                canvas.fill(first, last, row, row, colour.referenceLine);
            }
        }
        const auto centre = mapping.rowOf(0.0f);
        canvas.fill(first, last, centre, centre, colour.centreLine);

        if (index >= sweep.numChannels() || sweep.numBins() == 0)
            continue;
        reduceColumns(sweep, index, mapping_, static_cast<std::size_t>(first), spans);
        for (int column = first; column <= last; ++column)
        {
            const auto& span = spans[static_cast<std::size_t>(column - first)];
            if (span.pass == ColumnSpan::Pass::none || (column >= headStart && column <= gapEnd))
                continue;

            const auto rows = mapping.rowsOf(span.min, span.max);
            auto top = rows.top;
            auto bottom = rows.bottom;
            // The waveform stops short of the marker, so the marker reads as a marker.
            const auto marker = colour.clipMarker;
            const auto markerGap = std::max(1, markerHeight_ / 2);
            if (rows.clippedTop)
            {
                canvas.fill(column, column, lane.top, lane.top + markerHeight_ - 1, marker);
                top = std::max(top, lane.top + markerHeight_ + markerGap);
            }
            if (rows.clippedBottom)
            {
                canvas.fill(column, column, laneBottom - markerHeight_ + 1, laneBottom, marker);
                bottom = std::min(bottom, laneBottom - markerHeight_ - markerGap);
            }
            if (top <= bottom)
                canvas.fill(column, column, top, bottom, colour.waveform);
        }
    }

    for (std::size_t index = 0; index + 1 < lanes_.size(); ++index)
    {
        const auto dividerTop = lanes_[index].top + lanes_[index].height;
        const auto dividerBottom = lanes_[index + 1].top - 1;
        if (dividerTop <= dividerBottom)
            canvas.fill(first, last, dividerTop, dividerBottom, colour.laneDivider);
    }

    const auto headEnd = std::min(headStart + headWidth_ - 1, last);
    if (headStart >= 0 && std::max(headStart, first) <= headEnd)
        canvas.fill(std::max(headStart, first), headEnd, 0, height_ - 1, colour.head);
}

void ScopeView::updateGrid()
{
    const auto& snapshot = snapshots_.readBuffer();
    const GridKey key{snapshot.musical,
                      std::llround(snapshot.windowTicks),
                      std::llround(snapshot.windowStartTick),
                      snapshot.timeSignature.ticksPerBar(),
                      snapshot.timeSignature.ticksPerBeat(),
                      width_};
    if (key == gridKey_ && grid_.size() == static_cast<std::size_t>(width_))
        return;
    gridKey_ = key;
    grid_.assign(static_cast<std::size_t>(width_), GridLine::none);
    if (!key.musical || key.windowTicks <= 0 || key.ticksPerBar <= 0 || key.ticksPerBeat <= 0)
        return;

    // The lines of the window the head is in; a column the head has not reached yet keeps the
    // lines it was drawn with. The less of the window is in view, the finer the lines.
    const auto windowHalfTicks = 2 * key.windowTicks;
    const auto barHalfTicks = 2 * key.ticksPerBar;
    const auto beatHalfTicks = 2 * key.ticksPerBeat;
    const auto visibleHalfTicks = static_cast<double>(windowHalfTicks) * mapping_.span();
    const auto finest = 8.0 * visibleHalfTicks <= barHalfTicks   ? halfTicksPerSixtyFourth
                        : 4.0 * visibleHalfTicks <= barHalfTicks ? halfTicksPerThirtySecond
                        : 2.0 * visibleHalfTicks <= barHalfTicks ? halfTicksPerSixteenth
                                                                 : 0;
    const auto lineWidth = toPhysical(gridLineWidth, scale_);
    for (std::int64_t half = 0; half < windowHalfTicks; ++half)
    {
        const auto absolute = 2 * key.windowStartTick + half;
        const auto line = absolute % barHalfTicks == 0            ? GridLine::bar
                          : absolute % beatHalfTicks == 0         ? GridLine::beat
                          : finest == 0                           ? GridLine::none
                          : absolute % halfTicksPerSixteenth == 0 ? GridLine::sixteenth
                          : absolute % finest == 0                ? GridLine::fine
                                                                  : GridLine::none;
        if (line == GridLine::none)
            continue;
        const auto position = static_cast<double>(half) / static_cast<double>(windowHalfTicks);
        const auto column = mapping_.columnOf(position);
        if (column < 0.0)
            continue;
        // A line on a column boundary belongs to the column after it despite rounding.
        const auto first = static_cast<int>(std::floor(column + 1.0e-9));
        for (int x = first; x < std::min(first + lineWidth, width_); ++x)
            grid_[static_cast<std::size_t>(x)] = std::max(grid_[static_cast<std::size_t>(x)], line);
    }
}

juce::Rectangle<int> ScopeView::barNumberArea() const noexcept
{
    const auto height = juce::roundToInt(labelFontHeight + 6.0f);
    return getLocalBounds().removeFromBottom(height);
}

void ScopeView::drawBarNumbers(juce::Graphics& g) const
{
    const auto& snapshot = snapshots_.readBuffer();
    const auto ticksPerBar = snapshot.timeSignature.ticksPerBar();
    const auto ticksPerBeat = snapshot.timeSignature.ticksPerBeat();
    const auto area = barNumberArea();
    if (!snapshot.musical || snapshot.windowTicks <= 0.0 || ticksPerBar <= 0 || ticksPerBeat <= 0 ||
        !g.clipRegionIntersects(area))
        return;

    g.setFont(juce::FontOptions(labelFontHeight).withFeatureEnabled("tnum"));
    g.setColour(palette::laneLabel);
    const auto startTick = static_cast<std::int64_t>(std::llround(snapshot.windowStartTick));
    const auto barBeat = [&](std::int64_t absolute)
    {
        return juce::String(absolute / ticksPerBar + 1) + "." +
               juce::String(absolute % ticksPerBar / ticksPerBeat + 1);
    };
    const auto label = [&](float x, const juce::String& text)
    {
        g.drawText(text,
                   juce::Rectangle<float>(x + 4.0f, static_cast<float>(area.getY()), 60.0f,
                                          static_cast<float>(area.getHeight())),
                   juce::Justification::centredLeft, false);
    };

    // Bars show their number. A window that starts between bars shows where it starts as
    // bar.beat, and so does every beat while a bar or less is in view.
    const bool beats = mapping_.isZoomed() && snapshot.windowTicks * mapping_.span() <= ticksPerBar;
    const auto firstBeat = (ticksPerBeat - startTick % ticksPerBeat) % ticksPerBeat;
    auto firstX = static_cast<float>(getWidth());
    for (auto tick = firstBeat; static_cast<double>(tick) < snapshot.windowTicks;
         tick += ticksPerBeat)
    {
        const auto absolute = startTick + tick;
        const bool bar = absolute % ticksPerBar == 0;
        if (!bar && tick != 0 && !beats)
            continue;
        const auto column = mapping_.columnOf(static_cast<double>(tick) / snapshot.windowTicks);
        if (column < 0.0)
            continue;
        const auto x = static_cast<float>(column) / scale_;
        label(x, bar ? juce::String(absolute / ticksPerBar + 1) : barBeat(absolute));
        firstX = std::min(firstX, x);
    }

    // Zoomed in, the left edge always says which beat the view starts in.
    if (mapping_.isZoomed() && firstX > pinnedLabelClearance)
    {
        const auto viewStart = mapping_.offset() * snapshot.windowTicks;
        const auto beat = static_cast<std::int64_t>(std::floor(viewStart / ticksPerBeat));
        label(0.0f, barBeat(startTick + beat * ticksPerBeat));
    }
}

void ScopeView::drawStopped(juce::Graphics& g) const
{
    if (snapshots_.readBuffer().transportState != TransportState::stopped)
        return;
    g.setColour(palette::background.withAlpha(stoppedDimming));
    g.fillRect(getLocalBounds());

    // A pause mark next to the top lane's name.
    const auto mark = juce::Rectangle<float>(labelMargin + 18.0f, labelMargin, 11.0f, 13.0f);
    g.setColour(palette::text);
    g.fillRoundedRectangle(mark.withWidth(3.5f), 1.0f);
    g.fillRoundedRectangle(mark.withTrimmedLeft(7.5f), 1.0f);
}

std::pair<int, int> ScopeView::headColumns(const SweepBuffer& sweep) const noexcept
{
    if (sweep.pass() == 0 || sweep.numBins() == 0 || width_ <= 0 ||
        mapping_.numBins() != sweep.numBins())
        return {-1, -1};

    // Just after the newest column, but always on screen. A zoomed view shows the line only while
    // the head is in view, or just before it.
    std::array<ColumnMapping::ColumnRange, 2> columns;
    int afterHead = 0;
    if (mapping_.columnsOf(sweep.head(), sweep.head(), columns) > 0)
        afterHead = static_cast<int>(columns[0].last) + 1;
    else if ((sweep.head() + 1) % sweep.numBins() != mapping_.firstBin(0) % sweep.numBins())
        return {-1, -1};
    const auto start = std::max(0, std::min(afterHead, width_ - headWidth_));
    return {start, std::min(start + headWidth_ + gapWidth_ - 1, width_ - 1)};
}

double ScopeView::headPosition() const noexcept
{
    const auto& sweep = snapshots_.readBuffer().sweep;
    if (sweep.pass() == 0 || sweep.numBins() == 0)
        return -1.0;
    return static_cast<double>((sweep.head() + 1) % sweep.numBins()) /
           static_cast<double>(sweep.numBins());
}

void ScopeView::setZoom(SweepZoom zoom)
{
    zoom = SweepZoom::normalized(zoom);
    if (juce::exactlyEqual(zoom.offset, zoom_.offset) && juce::exactlyEqual(zoom.span, zoom_.span))
        return;
    zoom_ = zoom;
    mappingIsStale_ = true;
    needsFullRender_ = true;
    repaint();
    if (onZoomChange)
        onZoomChange();
}

void ScopeView::mouseDown(const juce::MouseEvent& event)
{
    if (event.source.isTouch())
    {
        const auto free = std::find_if(touches_.begin(), touches_.end(),
                                       [](const Touch& touch) { return touch.source < 0; });
        if (free == touches_.end())
            return;
        *free = {event.source.getIndex(), event.position.x};
        if (touches_[0].source >= 0 && touches_[1].source >= 0)
        {
            // A second finger turns the drag into a pinch around the point between the fingers.
            repaint(selectionArea());
            dragSource_.reset();
            pinching_ = false;
            pinchStartZoom_ = zoom_;
            pinchAnchor_ = static_cast<double>((touches_[0].x + touches_[1].x) / 2.0f) /
                           std::max(1, getWidth());
            return;
        }
    }
    if (event.mods.isPopupMenu() || pinching_)
        return;
    dragSource_ = event.source.getIndex();
    dragStart_ = event.position.x;
    dragEnd_ = event.position.x;
}

void ScopeView::mouseDrag(const juce::MouseEvent& event)
{
    if (event.source.isTouch())
    {
        for (auto& touch : touches_)
            if (touch.source == event.source.getIndex())
                touch.x = event.position.x;
        if (updatePinch())
            return;
    }
    if (dragSource_ != event.source.getIndex())
        return;
    const auto before = selectionArea();
    dragEnd_ = event.position.x;
    const auto after = selectionArea();
    if (before != after)
        repaint(before.getUnion(after));
}

void ScopeView::mouseUp(const juce::MouseEvent& event)
{
    if (event.source.isTouch())
    {
        const bool wasPinch = touches_[0].source >= 0 && touches_[1].source >= 0;
        for (auto& touch : touches_)
            if (touch.source == event.source.getIndex())
                touch = {};
        if (wasPinch)
        {
            pinching_ = false;
            return;
        }
    }
    if (dragSource_ != event.source.getIndex())
        return;
    const auto shown = selectionArea();
    dragEnd_ = event.position.x;
    const auto area = selectionArea();
    dragSource_.reset();
    repaint(shown.getUnion(area));
    if (area.isEmpty())
        return;
    const auto width = static_cast<double>(std::max(1, getWidth()));
    setZoom(zoom_.selected(static_cast<double>(std::min(dragStart_, dragEnd_)) / width,
                           static_cast<double>(std::max(dragStart_, dragEnd_)) / width));
}

void ScopeView::mouseDoubleClick(const juce::MouseEvent&)
{
    resetZoom();
}

void ScopeView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Up zooms in, whichever way the system scrolls content.
    const auto delta = static_cast<double>(wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
    if (juce::exactlyEqual(delta, 0.0))
        return;
    zoomAround(event.position.x, std::exp2(delta * wheelZoomRate));
}

void ScopeView::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    zoomAround(event.position.x, static_cast<double>(scaleFactor));
}

void ScopeView::zoomAround(float x, double factor)
{
    const auto anchor = static_cast<double>(x) / static_cast<double>(std::max(1, getWidth()));
    setZoom(zoom_.zoomedAround(anchor, factor));
}

bool ScopeView::updatePinch()
{
    if (touches_[0].source < 0 || touches_[1].source < 0)
        return false;
    const auto distance = std::abs(touches_[1].x - touches_[0].x);
    if (!pinching_)
    {
        // Fingers that land close together measure nothing useful until they spread.
        if (distance < minPinchDistance)
            return true;
        pinching_ = true;
        pinchStartDistance_ = distance;
        pinchStartZoom_ = zoom_;
        return true;
    }
    setZoom(pinchStartZoom_.zoomedAround(
        pinchAnchor_, static_cast<double>(std::max(distance, 1.0f) / pinchStartDistance_)));
    return true;
}

juce::Rectangle<int> ScopeView::selectionArea() const noexcept
{
    if (!dragSource_.has_value() || std::abs(dragEnd_ - dragStart_) < minSelectionWidth)
        return {};
    const auto width = static_cast<float>(getWidth());
    const auto left = std::clamp(std::min(dragStart_, dragEnd_), 0.0f, width);
    const auto right = std::clamp(std::max(dragStart_, dragEnd_), 0.0f, width);
    return juce::Rectangle<float>(left, 0.0f, right - left, static_cast<float>(getHeight()))
        .getSmallestIntegerContainer();
}

void ScopeView::drawSelection(juce::Graphics& g) const
{
    const auto area = selectionArea();
    if (area.isEmpty() || !g.clipRegionIntersects(area))
        return;
    g.setColour(palette::level.withAlpha(selectionFillAlpha));
    g.fillRect(area);
    g.setColour(palette::level.withAlpha(selectionEdgeAlpha));
    g.fillRect(area.withWidth(1));
    g.fillRect(area.withTrimmedLeft(area.getWidth() - 1));
}

juce::Rectangle<int> ScopeView::logicalColumns(int first, int last) const noexcept
{
    const auto left = static_cast<int>(std::floor(static_cast<float>(first) / scale_));
    const auto right = static_cast<int>(std::ceil(static_cast<float>(last + 1) / scale_));
    return {left, 0, right - left, getHeight()};
}

void ScopeView::drawLabels(juce::Graphics& g) const
{
    const auto font = juce::FontOptions(labelFontHeight).withFeatureEnabled("tnum");
    g.setFont(font);
    const auto gain = DisplayGain::toLinear(gainDb_);
    const auto labelHeight = juce::roundToInt(labelFontHeight + 4.0f);

    for (std::size_t index = 0; index < lanes_.size(); ++index)
    {
        const auto top = static_cast<float>(lanes_[index].top) / scale_;
        const auto height = static_cast<float>(lanes_[index].height) / scale_;

        if (index < layout_.totalChannelCount())
        {
            const juce::Rectangle<float> nameArea(labelMargin, top + labelMargin * 0.5f, 40.0f,
                                                  static_cast<float>(labelHeight));
            if (g.clipRegionIntersects(nameArea.getSmallestIntegerContainer()))
            {
                g.setColour(palette::laneLabel);
                g.drawText(channelShortName(layout_, index), nameArea, juce::Justification::topLeft,
                           false);
            }
        }

        const LaneMapping mapping(0, lanes_[index].height, gain);
        for (const auto& reference : references)
        {
            if (!mapping.isInside(reference.level))
                continue;
            // Just below the upper line, but inside the lane.
            const auto y = top + static_cast<float>(mapping.rowOf(reference.level)) / scale_;
            const auto labelTop =
                std::max(top, std::min(y + 1.0f, top + height - static_cast<float>(labelHeight)));
            const juce::Rectangle<float> area(static_cast<float>(getWidth()) - 60.0f - labelMargin,
                                              labelTop, 60.0f, static_cast<float>(labelHeight));
            if (!g.clipRegionIntersects(area.getSmallestIntegerContainer()))
                continue;
            g.setColour(palette::laneLabel);
            g.drawText(juce::String::fromUTF8(reference.label), area, juce::Justification::topRight,
                       false);
        }
    }
}

} // namespace visona
