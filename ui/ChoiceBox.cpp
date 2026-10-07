#include "ChoiceBox.h"

#include "ChoiceList.h"

#include <algorithm>

namespace visona
{

namespace
{

/** The space the list keeps from the edges of the window. */
constexpr int windowMargin = 8;

} // namespace

ChoiceBox::ChoiceBox() = default;

ChoiceBox::~ChoiceBox()
{
    list_.reset();
}

void ChoiceBox::showPopup()
{
    if (list_ != nullptr)
        return;
    auto* const host = ChoiceList::hostFor(*this);
    if (host == nullptr || !isEnabled() || getNumItems() == 0)
    {
        // ComboBox marks the popup active before it calls this.
        hidePopup();
        return;
    }

    juce::StringArray choices;
    for (int index = 0; index < getNumItems(); ++index)
        choices.add(getItemText(index));
    const auto selected = getSelectedItemIndex();

    list_ = std::make_unique<ChoiceList>(choices, selected, getLookAndFeel().getComboBoxFont(*this),
                                         juce::Justification::centredLeft);
    list_->onPick = [this](int index)
    {
        dismissList();
        setSelectedItemIndex(index);
    };
    list_->onDismiss = [this] { dismissList(); };

    // The current choice lies over the box, or as near as the window allows. The tick sits in a
    // column left of the box, so the text lines up with the box's.
    const auto target = host->getLocalArea(this, getLocalBounds());
    const auto itemHeight = target.getHeight();
    const auto area = host->getLocalBounds().reduced(windowMargin);
    const auto first = std::max(selected, 0);
    const auto bounds =
        juce::Rectangle<int>(target.getX() - ChoiceList::tickWidth,
                             target.getY() - first * itemHeight,
                             target.getWidth() + ChoiceList::tickWidth,
                             std::min(itemHeight * choices.size(), area.getHeight()))
            .constrainedWithin(area);
    list_->show(*host, bounds, itemHeight, first, target.getY() - bounds.getY());
}

void ChoiceBox::enablementChanged()
{
    juce::ComboBox::enablementChanged();
    if (!isEnabled() && list_ != nullptr)
        dismissList();
}

void ChoiceBox::visibilityChanged()
{
    juce::ComboBox::visibilityChanged();
    if (!isShowing() && list_ != nullptr)
        dismissList();
}

void ChoiceBox::parentHierarchyChanged()
{
    juce::ComboBox::parentHierarchyChanged();
    if (list_ != nullptr)
        dismissList();
}

void ChoiceBox::dismissList()
{
    list_.reset();
    hidePopup();
}

} // namespace visona
