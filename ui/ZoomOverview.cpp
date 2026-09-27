#include "ZoomOverview.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace visona
{

namespace
{

constexpr int stripHeight = 26;
constexpr int compactStripHeight = 22;
constexpr float trackHeightRatio = 0.4f;
constexpr float trackRadius = 3.0f;
constexpr float regionOverhang = 2.0f;
constexpr float regionMinWidth = 3.0f;
constexpr float regionFillAlpha = 0.28f;
constexpr float regionEdgeAlpha = 0.75f;
constexpr float headLineWidth = 1.5f;
constexpr float headOverhang = 3.0f;
constexpr float buttonRadius = 4.0f;
constexpr float crossInset = 0.32f;
constexpr float crossThickness = 1.5f;

/** A press that moves less than this many logical pixels is a click. */
constexpr float dragThreshold = 3.0f;

constexpr int ticksPerSixteenth = 6;

/** A position this close above a boundary counts as on it, despite rounding. */
constexpr double boundaryTolerance = 1.0e-6;

juce::String formatFactor(double factor)
{
    const auto number =
        factor < 9.95 ? juce::String(factor, 1) : juce::String(juce::roundToInt(factor));
    return number + juce::String::fromUTF8("\xc3\x97");
}

} // namespace

ZoomOverview::ZoomOverview()
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void ZoomOverview::setZoom(SweepZoom zoom)
{
    if (juce::exactlyEqual(zoom.offset, zoom_.offset) && juce::exactlyEqual(zoom.span, zoom_.span))
        return;
    zoom_ = zoom;
    repaint();
}

void ZoomOverview::setHead(double position)
{
    const bool shown = head_ >= 0.0;
    const bool willShow = position >= 0.0;
    if (shown == willShow && (!shown || std::abs(xOf(position) - xOf(head_)) < 0.25f))
    {
        head_ = position;
        return;
    }
    if (shown)
        repaint(headArea(xOf(head_)));
    head_ = position;
    if (willShow)
        repaint(headArea(xOf(head_)));
}

void ZoomOverview::setTimeline(const SweepSnapshot& snapshot)
{
    std::vector<Mark> marks;
    const auto ticksPerBar = snapshot.timeSignature.ticksPerBar();
    const auto ticksPerBeat = snapshot.timeSignature.ticksPerBeat();
    const auto windowTicks = std::llround(snapshot.windowTicks);
    if (snapshot.musical && windowTicks > 0 && ticksPerBar > 0 && ticksPerBeat > 0)
    {
        // A window of whole bars starts on a bar, so its bars are always in the same place.
        const bool wholeBars = windowTicks % ticksPerBar == 0;
        for (std::int64_t tick = ticksPerBeat; tick < windowTicks; tick += ticksPerBeat)
            marks.push_back({static_cast<double>(tick) / static_cast<double>(windowTicks),
                             wholeBars && tick % ticksPerBar == 0});
    }
    if (marks == marks_)
        return;
    marks_ = std::move(marks);
    repaint();
}

void ZoomOverview::setStep(ChromeStep step)
{
    if (step == step_)
        return;
    step_ = step;
    resized();
    repaint();
}

int ZoomOverview::preferredHeight() const noexcept
{
    return step_ == ChromeStep::compact ? compactStripHeight : stripHeight;
}

juce::String ZoomOverview::describe(SweepZoom zoom, const SweepSnapshot& snapshot)
{
    // A view that runs past the end of the window reads on into the next one, such as 1.4–2.1 in a
    // 1-bar window.
    const auto start = zoom.offset;
    const auto end = zoom.offset + zoom.span;

    const auto ticksPerBar = snapshot.timeSignature.ticksPerBar();
    const auto ticksPerBeat = snapshot.timeSignature.ticksPerBeat();
    auto text = "ZOOM " + formatFactor(zoom.factor());
    if (!snapshot.musical || !(snapshot.windowTicks > 0.0) || ticksPerBar <= 0 || ticksPerBeat <= 0)
        return text;

    const bool sixteenths = zoom.span * snapshot.windowTicks < ticksPerBeat;
    const auto format = [&](double position)
    {
        const auto tick = static_cast<std::int64_t>(
            std::floor(position * snapshot.windowTicks + boundaryTolerance));
        auto result = juce::String(tick / ticksPerBar + 1) + "." +
                      juce::String(tick % ticksPerBar / ticksPerBeat + 1);
        if (sixteenths)
            result << "." << juce::String(tick % ticksPerBeat / ticksPerSixteenth + 1);
        return result;
    };
    return text + juce::String::fromUTF8(" \xc2\xb7 ") + format(start) +
           juce::String::fromUTF8("\xe2\x80\x93") + format(end);
}

void ZoomOverview::paint(juce::Graphics& g)
{
    g.fillAll(palette::chrome);
    g.setColour(palette::outline);
    g.fillRect(getLocalBounds().removeFromBottom(1));

    g.setColour(palette::surface);
    g.fillRoundedRectangle(track_, trackRadius);
    for (const auto& mark : marks_)
    {
        g.setColour(mark.strong ? palette::gridBar : palette::gridBeat);
        g.fillRect(
            juce::Rectangle<float>(xOf(mark.position), track_.getY(), 1.0f, track_.getHeight()));
    }

    const auto region = [&](double from, double to)
    {
        const auto left = xOf(from);
        const auto area = juce::Rectangle<float>(left, track_.getY() - regionOverhang,
                                                 std::max(xOf(to) - left, regionMinWidth),
                                                 track_.getHeight() + 2.0f * regionOverhang);
        g.setColour(palette::level.withAlpha(regionFillAlpha));
        g.fillRoundedRectangle(area, 2.0f);
        g.setColour(palette::level.withAlpha(regionEdgeAlpha));
        g.drawRoundedRectangle(area.reduced(0.5f), 2.0f, 1.0f);
    };
    const auto end = zoom_.offset + zoom_.span;
    if (end <= 1.0)
    {
        region(zoom_.offset, end);
    }
    else
    {
        region(zoom_.offset, 1.0);
        region(0.0, end - 1.0);
    }

    if (head_ >= 0.0)
    {
        g.setColour(palette::head);
        g.fillRect(juce::Rectangle<float>(xOf(head_) - headLineWidth / 2.0f,
                                          track_.getY() - headOverhang, headLineWidth,
                                          track_.getHeight() + 2.0f * headOverhang));
    }

    const auto button = resetButton_.toFloat();
    g.setColour(resetHighlighted_ ? palette::highlight : palette::surface);
    g.fillRoundedRectangle(button, buttonRadius);
    const auto cross = button.reduced(button.getWidth() * crossInset);
    juce::Path path;
    path.startNewSubPath(cross.getTopLeft());
    path.lineTo(cross.getBottomRight());
    path.startNewSubPath(cross.getTopRight());
    path.lineTo(cross.getBottomLeft());
    g.setColour(resetHighlighted_ ? palette::text : palette::level);
    g.strokePath(path, juce::PathStrokeType(crossThickness, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

void ZoomOverview::resized()
{
    const auto metrics = ChromeMetrics::forStep(step_);
    auto area = getLocalBounds().withTrimmedBottom(1).reduced(metrics.padding + 4, 0);
    const auto buttonSize = std::max(1, area.getHeight() - 6);
    resetButton_ = area.removeFromRight(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize);
    area.removeFromRight(metrics.gap);
    const auto trackHeight = std::round(static_cast<float>(area.getHeight()) * trackHeightRatio);
    track_ = area.toFloat().withSizeKeepingCentre(static_cast<float>(area.getWidth()), trackHeight);
}

void ZoomOverview::mouseMove(const juce::MouseEvent& event)
{
    const bool onButton = resetButton_.contains(event.getPosition());
    setResetHighlighted(onButton);
    setMouseCursor(onButton ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::LeftRightResizeCursor);
}

void ZoomOverview::mouseExit(const juce::MouseEvent&)
{
    setResetHighlighted(false);
}

void ZoomOverview::mouseDown(const juce::MouseEvent& event)
{
    if (resetButton_.contains(event.getPosition()))
    {
        if (onReset)
            onReset();
        return;
    }
    if (pressSource_ >= 0)
        return;
    pressSource_ = event.source.getIndex();
    pressX_ = event.position.x;
    pressZoom_ = zoom_;
    dragging_ = false;
}

void ZoomOverview::mouseDrag(const juce::MouseEvent& event)
{
    if (event.source.getIndex() != pressSource_ || track_.getWidth() <= 0.0f)
        return;
    const auto distance = event.position.x - pressX_;
    if (!dragging_ && std::abs(distance) < dragThreshold)
        return;
    dragging_ = true;
    if (onPan)
        onPan(pressZoom_.panned(static_cast<double>(distance / track_.getWidth())));
}

void ZoomOverview::mouseUp(const juce::MouseEvent& event)
{
    if (event.source.getIndex() != pressSource_)
        return;
    pressSource_ = -1;
    if (dragging_)
        return;
    const auto position = positionOf(event.position.x);
    if (!isInView(position) && onPan)
        onPan(zoom_.centredOn(position));
}

void ZoomOverview::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!resetButton_.contains(event.getPosition()) && onReset)
        onReset();
}

juce::String ZoomOverview::getTooltip()
{
    return resetButton_.contains(getMouseXYRelative())
               ? "Reset zoom (Esc, or double-click the scope)"
               : "Drag to move the view, or click to centre it";
}

float ZoomOverview::xOf(double position) const noexcept
{
    return track_.getX() + static_cast<float>(position) * track_.getWidth();
}

double ZoomOverview::positionOf(float x) const noexcept
{
    if (track_.getWidth() <= 0.0f)
        return 0.0;
    return std::clamp(static_cast<double>((x - track_.getX()) / track_.getWidth()), 0.0, 1.0);
}

bool ZoomOverview::isInView(double position) const noexcept
{
    const auto fromStart = position - zoom_.offset;
    return fromStart - std::floor(fromStart) <= zoom_.span;
}

juce::Rectangle<int> ZoomOverview::headArea(float x) const noexcept
{
    return juce::Rectangle<float>(x - headLineWidth, track_.getY() - headOverhang,
                                  2.0f * headLineWidth, track_.getHeight() + 2.0f * headOverhang)
        .getSmallestIntegerContainer()
        .expanded(1);
}

void ZoomOverview::setResetHighlighted(bool highlighted)
{
    if (highlighted == resetHighlighted_)
        return;
    resetHighlighted_ = highlighted;
    repaint(resetButton_);
}

} // namespace visona
