#include "ChoiceList.h"

#include "Palette.h"

#include <algorithm>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr int textInset = 8;
/** Like juce::Viewport, which starts to scroll after 8 pixels. */
constexpr float dragThreshold = 8.0f;

} // namespace

class ChoiceList::Item final : public juce::Component
{
public:
    Item(const juce::String& text, bool ticked, const juce::Font& font,
         juce::Justification justification)
        : text_(text)
        , ticked_(ticked)
        , font_(font)
        , justification_(justification)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setRepaintsOnMouseActivity(true);
    }

    std::function<void()> onTap;

    void paint(juce::Graphics& g) override
    {
        const bool isDown = pressed_ && !dragged_;
        const bool isHighlighted = isMouseOver() && !dragged_;
        const auto bounds = getLocalBounds().toFloat().reduced(3.0f, 2.0f);
        if (isDown || isHighlighted || ticked_)
        {
            g.setColour(isDown ? palette::highlight
                               : (ticked_ ? palette::highlight.withAlpha(0.85f)
                                          : palette::surface.brighter(0.08f)));
            g.fillRoundedRectangle(bounds, cornerRadius - 1.0f);
        }

        auto textBounds = getLocalBounds();
        const auto tickArea = textBounds.removeFromLeft(tickWidth).toFloat();
        if (ticked_)
        {
            const auto s = std::min(tickArea.getWidth(), tickArea.getHeight()) * 0.28f;
            const auto c = tickArea.getCentre();
            juce::Path tick;
            tick.startNewSubPath(c.x - s, c.y);
            tick.lineTo(c.x - s * 0.15f, c.y + s * 0.85f);
            tick.lineTo(c.x + s * 1.15f, c.y - s * 0.85f);
            g.setColour(palette::level);
            g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        if (!justification_.testFlags(juce::Justification::horizontallyCentred))
            textBounds.reduce(textInset, 0);
        g.setColour(palette::text);
        g.setFont(font_);
        g.drawText(text_, textBounds, justification_, true);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        pressed_ = true;
        dragged_ = false;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragged_ && event.getScreenPosition().toFloat().getDistanceFrom(
                             event.getMouseDownScreenPosition().toFloat()) > dragThreshold)
        {
            dragged_ = true;
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const bool tap = pressed_ && !dragged_ && getLocalBounds().contains(event.getPosition());
        pressed_ = false;
        dragged_ = false;
        repaint();
        if (tap && onTap)
            onTap();
    }

private:
    juce::String text_;
    bool ticked_ = false;
    juce::Font font_;
    juce::Justification justification_;
    bool pressed_ = false;
    /** A drag scrolls the list, so it must not pick the item it ends on. */
    bool dragged_ = false;
};

ChoiceList::ChoiceList(const juce::StringArray& choices, int selected, const juce::Font& font,
                       juce::Justification justification)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setAlwaysOnTop(true);

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);

    items_.reserve(static_cast<std::size_t>(choices.size()));
    for (int index = 0; index < choices.size(); ++index)
    {
        auto item = std::make_unique<Item>(choices[index], index == selected, font, justification);
        item->onTap = [this, index] { pick(index); };
        content_.addAndMakeVisible(*item);
        items_.push_back(std::move(item));
    }
}

ChoiceList::~ChoiceList()
{
    if (isCurrentlyModal())
        exitModalState(0);
    viewport_.setViewedComponent(nullptr, false);
}

void ChoiceList::show(juce::Component& host, juce::Rectangle<int> bounds, int itemHeight,
                      int firstVisible, int offset)
{
    itemHeight_ = std::max(1, itemHeight);
    setBounds(bounds);
    host.addAndMakeVisible(*this);
    toFront(false);
    viewport_.setViewPosition(0, firstVisible * itemHeight_ - offset);
    enterModalState(false);
}

void ChoiceList::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(palette::surface);
    g.fillRoundedRectangle(bounds, cornerRadius);
    g.setColour(palette::outline);
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
}

void ChoiceList::resized()
{
    viewport_.setBounds(getLocalBounds().reduced(1));
    const auto height = itemHeight_ * static_cast<int>(items_.size());
    // Twice, because whether the scroll bar shows depends on the height.
    content_.setSize(viewport_.getWidth(), height);
    content_.setSize(viewport_.getMaximumVisibleWidth(), height);

    auto area = content_.getLocalBounds();
    for (auto& item : items_)
        item->setBounds(area.removeFromTop(itemHeight_));
}

void ChoiceList::inputAttemptWhenModal()
{
    // Close after this click, so it cannot reopen the list or reach what is under it.
    dismiss();
}

bool ChoiceList::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }
    return false;
}

juce::Component* ChoiceList::hostFor(const juce::Component& component)
{
    auto* host = component.getParentComponent();
    if (host == nullptr)
        return nullptr;
    while (auto* parent = host->getParentComponent())
    {
        if (parent->isOnDesktop())
            break;
        host = parent;
    }
    return host;
}

void ChoiceList::pick(int index)
{
    // The item's mouseUp is still on the stack, so the owner hears about it on the next turn.
    setVisible(false);
    if (isCurrentlyModal())
        exitModalState(0);
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ChoiceList>(this), index]
        {
            if (safe == nullptr || !safe->onPick)
                return;
            const auto callback = safe->onPick;
            callback(index);
        });
}

void ChoiceList::dismiss()
{
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ChoiceList>(this)]
        {
            if (safe == nullptr || !safe->onDismiss)
                return;
            const auto callback = safe->onDismiss;
            callback();
        });
}

} // namespace visona
