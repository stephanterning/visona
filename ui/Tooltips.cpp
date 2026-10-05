#include "Tooltips.h"

namespace visona
{

namespace
{

constexpr int holdMs = 500;
constexpr float holdTolerance = 8.0f;

// For this long after a finger, and this close to where it was, the pointer may still move
// because of it rather than because of a mouse.
constexpr double touchEchoMs = 500.0;
constexpr float touchEchoDistance = 8.0f;

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

juce::String tooltipOf(juce::Component* component)
{
    if (component == nullptr || component->isCurrentlyBlockedByAnotherModalComponent())
        return {};
    if (auto* client = dynamic_cast<juce::TooltipClient*>(component))
        return client->getTooltip();
    return {};
}

} // namespace

/** JUCE's hover tooltips, kept quiet while hoverOff_ is set. */
class Tooltips::HoverTips final : public juce::TooltipWindow
{
public:
    HoverTips(juce::Component& parent, const bool& off)
        : juce::TooltipWindow(&parent)
        , off_(off)
    {
    }

    juce::String getTipFor(juce::Component& component) override
    {
        return off_ ? juce::String() : juce::TooltipWindow::getTipFor(component);
    }

private:
    const bool& off_;
};

/** The tooltip of a control a finger holds. */
class Tooltips::Bubble final : public juce::Component
{
public:
    Bubble()
    {
        setAlwaysOnTop(true);
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }

    void show(const juce::String& text, juce::Point<int> at)
    {
        auto* const parent = getParentComponent();
        if (parent == nullptr)
            return;
        text_ = text;
        setBounds(getLookAndFeel().getTooltipBounds(text, at, parent->getLocalBounds()));
        setVisible(true);
        toFront(false);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        getLookAndFeel().drawTooltip(g, text_, getWidth(), getHeight());
    }

private:
    juce::String text_;
};

Tooltips::Tooltips(juce::Component& parent)
    : parent_(parent)
    , hover_(std::make_unique<HoverTips>(parent, hoverOff_))
    , bubble_(std::make_unique<Bubble>())
    , holdTimer_([this] { showHeldTip(); })
{
    parent.addChildComponent(*bubble_);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

Tooltips::~Tooltips()
{
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void Tooltips::mouseMove(const juce::MouseEvent& event)
{
    if (!event.source.isTouch())
        noteMouse(event);
}

void Tooltips::mouseDown(const juce::MouseEvent& event)
{
    if (!event.source.isTouch())
    {
        noteMouse(event);
        return;
    }
    noteTouch(event);
    // A second finger is not a hold.
    if (heldSource_.has_value())
    {
        cancelHeldTip();
        return;
    }
    if (!parent_.isParentOf(event.eventComponent))
        return;
    const auto tip = tooltipOf(event.eventComponent);
    if (tip.isEmpty())
        return;
    heldSource_ = event.source.getIndex();
    heldComponent_ = event.eventComponent;
    heldPosition_ = event.getScreenPosition().toFloat();
    heldTip_ = tip;
    holdTimer_.startTimer(holdMs);
}

void Tooltips::mouseDrag(const juce::MouseEvent& event)
{
    if (!event.source.isTouch())
    {
        noteMouse(event);
        return;
    }
    noteTouch(event);
    if (heldSource_ == event.source.getIndex() &&
        event.getScreenPosition().toFloat().getDistanceFrom(heldPosition_) > holdTolerance)
        cancelHeldTip();
}

void Tooltips::mouseUp(const juce::MouseEvent& event)
{
    if (!event.source.isTouch())
        return;
    noteTouch(event);
    if (heldSource_ == event.source.getIndex())
        cancelHeldTip();
}

void Tooltips::noteTouch(const juce::MouseEvent& event)
{
    hoverOff_ = true;
    lastTouchMs_ = nowMs();
    lastTouchPosition_ = event.getScreenPosition().toFloat();
    hover_->hideTip();
}

void Tooltips::noteMouse(const juce::MouseEvent& event)
{
    if (hoverOff_ && nowMs() - lastTouchMs_ > touchEchoMs &&
        event.getScreenPosition().toFloat().getDistanceFrom(lastTouchPosition_) > touchEchoDistance)
        hoverOff_ = false;
}

void Tooltips::showHeldTip()
{
    holdTimer_.stopTimer();
    // Pressing a select menu opens it, and the tooltip would sit under it.
    if (!heldSource_.has_value() || heldComponent_ == nullptr ||
        heldComponent_->isCurrentlyBlockedByAnotherModalComponent())
        return;
    bubble_->show(heldTip_, parent_.getLocalPoint(nullptr, heldPosition_).roundToInt());
}

void Tooltips::cancelHeldTip()
{
    holdTimer_.stopTimer();
    heldSource_.reset();
    heldComponent_ = nullptr;
    bubble_->setVisible(false);
}

} // namespace visona
