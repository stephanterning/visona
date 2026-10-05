#include "ChoiceControl.h"

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

ChoiceControl::~ChoiceControl() = default;

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

void ChoiceControl::mouseDown(const juce::MouseEvent&)
{
    if (menuOpen_)
        return;
    menuOpen_ = true;
    repaint();
    // Like a ComboBox, after this press has been handled everywhere, so that it cannot reach the
    // menu or what is under it.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ChoiceControl>(this)]
        {
            if (safe != nullptr)
                safe->showMenu();
        });
}

void ChoiceControl::showMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    for (int index = 0; index < choices_.size(); ++index)
        menu.addItem(index + 1, choices_[index], true, index == selected_);

    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(this)
            .withMinimumWidth(getWidth())
            .withStandardItemHeight(getHeight())
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards),
        [safe = juce::Component::SafePointer<ChoiceControl>(this)](int result)
        {
            if (safe == nullptr)
                return;
            safe->menuOpen_ = false;
            safe->repaint();
            if (result > 0 && result - 1 != safe->selected_)
                safe->choose(result - 1);
        });
}

} // namespace visona
