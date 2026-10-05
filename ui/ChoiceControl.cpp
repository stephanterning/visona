#include "ChoiceControl.h"

#include "ControlText.h"
#include "Palette.h"

#include <algorithm>
#include <vector>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr int sidePadding = 10;
constexpr float arrowSize = 7.0f;
constexpr int arrowGap = 6;
constexpr int tickWidth = 18;

} // namespace

/** The list of choices, parented to the window so a tap reaches the items (D-108). */
class ChoiceControl::Menu final : public juce::Component
{
public:
    Menu(ChoiceControl& owner, const juce::StringArray& choices, int selected, float fontHeight)
        : owner_(owner)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setAlwaysOnTop(true);
        setBroughtToFrontOnMouseClick(true);

        items_.reserve(static_cast<std::size_t>(choices.size()));
        for (int index = 0; index < choices.size(); ++index)
        {
            auto item = std::make_unique<Item>(choices[index], index == selected, fontHeight);
            item->onClick = [this, index] { owner_.pick(index); };
            addAndMakeVisible(*item);
            items_.push_back(std::move(item));
        }
    }

    void resized() override
    {
        if (items_.empty())
            return;
        const auto height = getHeight() / static_cast<int>(items_.size());
        auto area = getLocalBounds();
        for (auto& item : items_)
            item->setBounds(area.removeFromTop(height));
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(palette::surface);
        g.fillRoundedRectangle(bounds, cornerRadius);
        g.setColour(palette::outline);
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    void inputAttemptWhenModal() override
    {
        // Dismiss after this click, so it cannot reopen the menu or reach what is under it.
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<ChoiceControl>(&owner_)]
            {
                if (safe != nullptr)
                    safe->dismissMenu();
            });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            owner_.dismissMenu();
            return true;
        }
        return false;
    }

private:
    class Item final : public juce::Button
    {
    public:
        Item(const juce::String& text, bool ticked, float fontHeight)
            : juce::Button(text)
            , ticked_(ticked)
            , fontHeight_(fontHeight)
        {
            setWantsKeyboardFocus(false);
            setMouseClickGrabsKeyboardFocus(false);
        }

        void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(3.0f, 2.0f);
            if (isDown || isHighlighted || ticked_)
            {
                g.setColour(isDown ? palette::highlight
                                   : (ticked_ ? palette::highlight.withAlpha(0.85f)
                                              : palette::surface.brighter(0.08f)));
                g.fillRoundedRectangle(bounds, cornerRadius - 1.0f);
            }

            if (ticked_)
            {
                const auto tickArea =
                    getLocalBounds().removeFromLeft(tickWidth + sidePadding).toFloat();
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

            g.setColour(palette::text);
            g.setFont(controlText::valueFont(fontHeight_));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        bool ticked_ = false;
        float fontHeight_ = 14.0f;
    };

    ChoiceControl& owner_;
    std::vector<std::unique_ptr<Item>> items_;
};

ChoiceControl::ChoiceControl(const juce::String& caption, juce::StringArray choices)
    : caption_(caption)
    , choices_(std::move(choices))
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle(caption);
}

ChoiceControl::~ChoiceControl()
{
    dismissMenu();
}

void ChoiceControl::setSelected(int index)
{
    index = std::clamp(index, 0, choices_.size() - 1);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

void ChoiceControl::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    repaint();
}

int ChoiceControl::preferredWidth(int height) const
{
    auto widest =
        juce::GlyphArrangement::getStringWidthInt(controlText::captionFont(fontHeight_), caption_);
    for (const auto& choice : choices_)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(
                                      controlText::valueFont(fontHeight_), choice));
    // The arrow sits on the right, so the text is centred in what is left.
    return std::max(height, widest + 2 * sidePadding + arrowGap + juce::roundToInt(arrowSize));
}

void ChoiceControl::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(menuOpen_ ? palette::highlight : palette::surface);
    g.fillRoundedRectangle(bounds, cornerRadius);

    auto area = getLocalBounds().reduced(sidePadding, 0);
    const auto arrowArea = area.removeFromRight(juce::roundToInt(arrowSize)).toFloat();
    area.removeFromRight(arrowGap);
    controlText::draw(g, area, caption_, choices_[selected_], palette::textDim, palette::text,
                      fontHeight_);

    // An arrow up, because the choices open above.
    const auto centre = arrowArea.getCentre().translated(0.0f, arrowSize * 0.35f);
    juce::Path arrow;
    arrow.startNewSubPath(centre.x - arrowSize * 0.5f, centre.y + arrowSize * 0.25f);
    arrow.lineTo(centre.x, centre.y - arrowSize * 0.25f);
    arrow.lineTo(centre.x + arrowSize * 0.5f, centre.y + arrowSize * 0.25f);
    g.setColour(palette::level);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void ChoiceControl::mouseUp(const juce::MouseEvent& event)
{
    if (menuOpen_ || event.mouseWasDraggedSinceMouseDown() ||
        !getLocalBounds().contains(event.getPosition()))
        return;
    showMenu();
}

void ChoiceControl::visibilityChanged()
{
    if (!isShowing())
        dismissMenu();
}

void ChoiceControl::parentHierarchyChanged()
{
    dismissMenu();
}

void ChoiceControl::parentSizeChanged()
{
    dismissMenu();
}

void ChoiceControl::showMenu()
{
    if (menuOpen_ || choices_.isEmpty())
        return;
    auto* const host = overlayHost();
    if (host == nullptr)
        return;

    // Host coordinates include the control bar's scale, so the items match a finger (D-108).
    const auto target = host->getLocalArea(this, getLocalBounds());
    const auto scale =
        static_cast<float>(target.getHeight()) / static_cast<float>(std::max(1, getHeight()));
    menu_ = std::make_unique<Menu>(*this, choices_, selected_, fontHeight_ * scale);

    const auto height = target.getHeight() * choices_.size();
    auto bounds =
        juce::Rectangle<int>(target.getX(), target.getY() - height, target.getWidth(), height);
    if (bounds.getY() < 0)
        bounds.setY(target.getBottom());
    bounds = bounds.constrainedWithin(host->getLocalBounds());
    menu_->setBounds(bounds);
    host->addAndMakeVisible(*menu_);
    menu_->toFront(false);
    menuOpen_ = true;
    repaint();
    menu_->enterModalState(false);
}

void ChoiceControl::dismissMenu()
{
    if (!menuOpen_ && menu_ == nullptr)
        return;
    menuOpen_ = false;
    if (menu_ != nullptr)
    {
        if (menu_->isCurrentlyModal())
            menu_->exitModalState(0);
        if (auto* parent = menu_->getParentComponent())
            parent->removeChildComponent(menu_.get());
        menu_.reset();
    }
    repaint();
}

void ChoiceControl::pick(int index)
{
    // The item's onClick is still on the stack, so delete the menu on the next turn.
    if (menu_ != nullptr)
        menu_->setVisible(false);
    menuOpen_ = false;
    repaint();
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ChoiceControl>(this), index]
        {
            if (safe == nullptr)
                return;
            safe->dismissMenu();
            if (index != safe->selected_)
                safe->choose(index);
        });
}

juce::Component* ChoiceControl::overlayHost() const
{
    auto* host = getParentComponent();
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

} // namespace visona
