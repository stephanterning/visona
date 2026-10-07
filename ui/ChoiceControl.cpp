#include "ChoiceControl.h"

#include "ChoiceList.h"
#include "ControlText.h"
#include "Palette.h"

#include <algorithm>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr int sidePadding = 10;
constexpr float arrowSize = 7.0f;
constexpr int arrowGap = 6;

} // namespace

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
    g.setColour(menu_ != nullptr ? palette::highlight : palette::surface);
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
    if (menu_ != nullptr || event.mouseWasDraggedSinceMouseDown() ||
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
    if (menu_ != nullptr || choices_.isEmpty())
        return;
    auto* const host = ChoiceList::hostFor(*this);
    if (host == nullptr)
        return;

    // Host coordinates include the control bar's scale, so the items match a finger (D-108).
    const auto target = host->getLocalArea(this, getLocalBounds());
    const auto scale =
        static_cast<float>(target.getHeight()) / static_cast<float>(std::max(1, getHeight()));
    menu_ = std::make_unique<ChoiceList>(choices_, selected_,
                                         controlText::valueFont(fontHeight_ * scale),
                                         juce::Justification::centred);
    menu_->onPick = [this](int index)
    {
        dismissMenu();
        if (index != selected_)
            choose(index);
    };
    menu_->onDismiss = [this] { dismissMenu(); };

    const auto height = target.getHeight() * choices_.size();
    // The tick sits in a column of its own, so the menu is wider than the control and opens a
    // little to the left. The labels then stay in the control's width and do not sit under it.
    auto bounds =
        juce::Rectangle<int>(target.getX() - ChoiceList::tickWidth, target.getY() - height,
                             target.getWidth() + ChoiceList::tickWidth, height);
    if (bounds.getY() < 0)
        bounds.setY(target.getBottom());
    bounds = bounds.constrainedWithin(host->getLocalBounds());
    menu_->show(*host, bounds, target.getHeight(), 0, 0);
    repaint();
}

void ChoiceControl::dismissMenu()
{
    if (menu_ == nullptr)
        return;
    menu_.reset();
    repaint();
}

} // namespace visona
