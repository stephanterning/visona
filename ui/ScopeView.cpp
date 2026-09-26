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
constexpr float gridLineWidthLogical = 1.0f;
constexpr float clipMarkerHeight = 3.0f;
constexpr float labelMargin = 6.0f;
constexpr float labelFontHeight = 11.0f;

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

float srgbToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value)
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

/** A colour in linear light, for mixing. */
struct LinearColour
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;

    explicit LinearColour(juce::Colour colour)
        : red(srgbToLinear(colour.getFloatRed()))
        , green(srgbToLinear(colour.getFloatGreen()))
        , blue(srgbToLinear(colour.getFloatBlue()))
    {
    }
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
    std::array<juce::PixelARGB, splitBands.size()> bands{palette::bandLow.getPixelARGB(),
                                                         palette::bandMid.getPixelARGB(),
                                                         palette::bandHigh.getPixelARGB()};
    std::array<LinearColour, splitBands.size()> linearBands{LinearColour(palette::bandLow),
                                                            LinearColour(palette::bandMid),
                                                            LinearColour(palette::bandHigh)};
};

const Colours& colours()
{
    static const Colours instance;
    return instance;
}

/** The band colours mixed in linear light by `weights`, which sum to 1. */
juce::PixelARGB blendColour(const std::array<float, splitBands.size()>& weights)
{
    const auto& bands = colours().linearBands;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    for (std::size_t band = 0; band < bands.size(); ++band)
    {
        red += weights[band] * bands[band].red;
        green += weights[band] * bands[band].green;
        blue += weights[band] * bands[band].blue;
    }
    const auto toByte = [](float linear)
    {
        return static_cast<juce::uint8>(
            juce::jlimit(0, 255, juce::roundToInt(255.0f * linearToSrgb(linear))));
    };
    return juce::PixelARGB(255, toByte(red), toByte(green), toByte(blue));
}

juce::PixelARGB inkColour(Ink ink, const ColumnPaint& paint)
{
    const auto& colour = colours();
    switch (ink)
    {
    case Ink::low:
        return colour.bands[splitIndex(Band::low)];
    case Ink::mid:
        return colour.bands[splitIndex(Band::mid)];
    case Ink::high:
        return colour.bands[splitIndex(Band::high)];
    case Ink::blend:
        return blendColour(paint.blend);
    case Ink::neutral:
        break;
    }
    return colour.waveform;
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
    setInterceptsMouseClicks(false, false);
}

void ScopeView::setGainDb(int gainDb)
{
    gainDb = DisplayGain::clampDb(gainDb);
    if (gainDb == gainDb_)
        return;
    gainDb_ = gainDb;
    redrawAll();
}

void ScopeView::setColoring(WaveformColoring coloring)
{
    if (coloring == coloring_)
        return;
    coloring_ = coloring;
    redrawAll();
}

void ScopeView::setBandDelayCompensation(bool compensate)
{
    if (compensate == compensateBandDelay_)
        return;
    compensateBandDelay_ = compensate;
    redrawAll();
}

void ScopeView::setDebugGrid(DebugGrid grid)
{
    if (grid == grid_)
        return;
    grid_ = grid;
    layoutGrid();
    redrawAll();
}

void ScopeView::redrawAll()
{
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

    paintTiming_.add(nowMs() - start);
}

void ScopeView::resized()
{
    redrawAll();
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
    const auto dirty = renderChanges();
    renderTiming_.add(nowMs() - start);
    if (renderTiming_.count == 1)
        firstFrameSeconds_ = timestampSeconds;
    lastFrameSeconds_ = timestampSeconds;
    if (!dirty.isEmpty())
        repaint(dirty);
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
    levels_.assign(static_cast<std::size_t>(tileWidth), {});
    lanes_.clear();

    headWidth_ = toPhysical(headLineWidth, scale_);
    gapWidth_ = toPhysical(eraseGapWidth, scale_);
    markerHeight_ = toPhysical(clipMarkerHeight, scale_);
    gridLineWidth_ = toPhysical(gridLineWidthLogical, scale_);
    layoutGrid();
    needsFullRender_ = true;
    return true;
}

void ScopeView::layoutGrid()
{
    gridLines_.clear();
    if (grid_ == DebugGrid::off || width_ <= 0)
        return;
    const auto& colour = colours();
    for (int sixteenth = 0; sixteenth < 16; ++sixteenth)
    {
        const auto column = std::min(width_ * sixteenth / 16, width_ - gridLineWidth_);
        const auto lineColour = sixteenth == 0       ? colour.gridBar
                                : sixteenth % 4 == 0 ? colour.gridBeat
                                                     : colour.gridSixteenth;
        gridLines_.push_back({std::max(column, 0), lineColour});
    }
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

juce::Rectangle<int> ScopeView::renderChanges()
{
    const auto& snapshot = snapshots_.readBuffer();
    const auto& sweep = snapshot.sweep;
    const bool sameSweep = rendered_ && !needsFullRender_ && snapshot.streamId == renderedStream_ &&
                           sweep.generation() == renderedGeneration_;
    if (sameSweep && sweep.pass() == 0)
        return {};

    // Within a window, the head only changes the columns it passes, and the band levels of the
    // columns up to settlingBins() behind where it was. Both passes look the same, so the start of
    // a new pass changes only the columns across the end of the window.
    const bool samePass = sweep.pass() == renderedPass_ && sweep.head() >= renderedHead_;
    const bool nextPass = sweep.pass() == renderedPass_ + 1 && sweep.head() < renderedHead_;
    const auto settling = settlingBins(snapshot);
    // Settling columns before the start of the window lie at its end, in the previous pass.
    const bool settlingWraps = settling > renderedHead_;
    if (!sameSweep || renderedPass_ == 0 || !(samePass || nextPass) ||
        settling >= sweep.numBins() || (settlingWraps && nextPass))
    {
        renderAll();
        return getLocalBounds();
    }

    // The columns from where the head was, less the settling ones, to the end of the erase gap
    // after where it is now.
    const auto settledBin = settlingWraps ? 0 : renderedHead_ - settling;
    const auto first =
        std::min(static_cast<int>(mapping_.firstColumn(settledBin)), renderedHeadStart_);
    const auto [headStart, gapEnd] = headColumns(sweep);
    const auto last = std::max(gapEnd, static_cast<int>(mapping_.lastColumn(sweep.head())));
    const auto previousHead = renderedHead_;
    renderedPass_ = sweep.pass();
    renderedHead_ = sweep.head();
    renderedHeadStart_ = headStart;

    if (settlingWraps)
    {
        const auto tail =
            static_cast<int>(mapping_.firstColumn(sweep.numBins() - (settling - previousHead)));
        renderColumns(tail, width_ - 1);
        repaint(logicalColumns(tail, width_ - 1));
    }
    if (samePass)
    {
        renderColumns(first, last);
        return logicalColumns(first, last);
    }
    renderColumns(first, width_ - 1);
    renderColumns(0, last);
    repaint(logicalColumns(first, width_ - 1));
    return logicalColumns(0, last);
}

bool ScopeView::readsBands(const SweepSnapshot& snapshot) const noexcept
{
    return coloring_ != WaveformColoring::precise && snapshot.hasStream &&
           snapshot.sampleRate > 0.0 && snapshot.windowFrames > 0;
}

std::size_t ScopeView::settlingBins(const SweepSnapshot& snapshot) const noexcept
{
    if (!readsBands(snapshot))
        return 0;
    const auto reading =
        bandReadingFor(snapshot.sampleRate, snapshot.bandDelayFrames, compensateBandDelay_);
    return bandReachBins(reading, snapshot.sweep.numBins(), snapshot.windowFrames);
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
    if (sweep.numBins() > 0 && (mapping_.numBins() != sweep.numBins() ||
                                mapping_.numColumns() != static_cast<std::size_t>(width_)))
        mapping_ = ColumnMapping(sweep.numBins(), static_cast<std::size_t>(width_));

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
    const auto& snapshot = snapshots_.readBuffer();
    const auto& sweep = snapshot.sweep;
    const auto& colour = colours();
    const TileCanvas canvas(pixels, tileStart);
    const auto gain = DisplayGain::toLinear(gainDb_);
    const auto [headStart, gapEnd] = headColumns(sweep);
    const auto count = static_cast<std::size_t>(last - first + 1);
    const std::span spans(spans_.data(), count);
    const std::span levels(levels_.data(), count);
    const bool bands = readsBands(snapshot);
    const auto reading =
        bandReadingFor(snapshot.sampleRate, snapshot.bandDelayFrames, compensateBandDelay_);

    const auto drawGrid = [&](int top, int bottom)
    {
        for (const auto& line : gridLines_)
        {
            const auto lineFirst = std::max(line.column, first);
            const auto lineLast = std::min(line.column + gridLineWidth_ - 1, last);
            if (lineFirst <= lineLast)
                canvas.fill(lineFirst, lineLast, top, bottom, line.colour);
        }
    };

    for (std::size_t index = 0; index < lanes_.size(); ++index)
    {
        const auto& lane = lanes_[index];
        const auto laneBottom = lane.top + lane.height - 1;
        const LaneMapping mapping(lane.top, lane.height, gain);

        canvas.fill(first, last, lane.top, laneBottom, colour.laneBackground);
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
        if (grid_ == DebugGrid::behind)
            drawGrid(lane.top, laneBottom);

        if (index >= sweep.numChannels() || sweep.numBins() == 0)
            continue;
        reduceColumns(sweep, index, mapping_, static_cast<std::size_t>(first), spans);
        if (bands)
            reduceBandLevels(sweep, index, mapping_, snapshot.windowFrames, reading,
                             static_cast<std::size_t>(first), levels);
        else
            std::fill(levels.begin(), levels.end(), BandLevels{-1.0f, -1.0f, -1.0f});

        for (int column = first; column <= last; ++column)
        {
            const auto offset = static_cast<std::size_t>(column - first);
            const auto& span = spans[offset];
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

            // Every colouring fills exactly [top, bottom]: the shape never depends on it (D-056).
            const auto paint = paintColumn(coloring_, top, bottom, levels[offset], mapping);
            for (std::size_t run = 0; run < paint.count; ++run)
            {
                const auto& fill = paint.runs[run];
                canvas.fill(column, column, fill.top, fill.bottom, inkColour(fill.ink, paint));
            }
        }
        if (grid_ == DebugGrid::over)
            drawGrid(lane.top, laneBottom);
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

std::pair<int, int> ScopeView::headColumns(const SweepBuffer& sweep) const noexcept
{
    if (sweep.pass() == 0 || sweep.numBins() == 0 || width_ <= 0 ||
        mapping_.numBins() != sweep.numBins())
        return {-1, -1};
    // Just after the newest column, but always on screen.
    const auto afterHead = static_cast<int>(mapping_.lastColumn(sweep.head())) + 1;
    const auto start = std::max(0, std::min(afterHead, width_ - headWidth_));
    return {start, std::min(start + headWidth_ + gapWidth_ - 1, width_ - 1)};
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
