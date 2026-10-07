#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace visona
{

/**
    A list of choices drawn inside the window, for the select menus of the control bar and the
    settings panel (D-108). JUCE's PopupMenu opens a window of its own, which never sees a button
    press from XInput touch events, so on the kiosk touchscreen a tap on an item did nothing.

    The current choice is ticked. A list taller than its bounds scrolls, with a drag of the finger
    or the mouse wheel; a drag never picks an item. While it is showing the list is modal: a tap or
    click outside it, or Escape, closes it without reaching what is under it.
*/
class ChoiceList final : public juce::Component
{
public:
    /** The column on the left of each item that holds the tick. */
    static constexpr int tickWidth = 18;

    ChoiceList(const juce::StringArray& choices, int selected, const juce::Font& font,
               juce::Justification justification);
    ~ChoiceList() override;

    /** Called on the next message-loop turn with the index of the item the user picks. The list is
        hidden by then and may be deleted. */
    std::function<void(int index)> onPick;

    /** Called on the next message-loop turn after a tap or click outside the list, or Escape. */
    std::function<void()> onDismiss;

    /**
        Adds the list to `host` at `bounds`, each item `itemHeight` tall, and makes it modal. It
        scrolls so that the top of `firstVisible` is `offset` pixels below the top of the list.
    */
    void show(juce::Component& host, juce::Rectangle<int> bounds, int itemHeight, int firstVisible,
              int offset);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void inputAttemptWhenModal() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /** The top-level component under the desktop window that holds `component`, so a list added to
        it covers the whole window; nullptr if `component` is not in a window. */
    [[nodiscard]] static juce::Component* hostFor(const juce::Component& component);

private:
    class Item;

    void pick(int index);
    void dismiss();

    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<Item>> items_;
    int itemHeight_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceList)
};

} // namespace visona
