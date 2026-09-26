#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace visona
{

/**
    A row of touch-sized segments, one of which is selected, with an optional label in front, such
    as `COLOR [Precise][Blend][Bands]`. No knobs and no hover needed (D-025).

    Like GainControl, it holds no state beyond what it shows: a tap asks for a change through
    onSelect, and setSelected() says what to show. It never takes keyboard focus, so shortcuts keep
    working.
*/
class SegmentedControl final : public juce::Component
{
public:
    struct Segment
    {
        juce::String text;
        juce::String tooltip;
    };

    SegmentedControl(const juce::String& label, std::vector<Segment> segments);
    ~SegmentedControl() override;

    /** Called with the segment the user taps, if it is not the selected one. */
    std::function<void(int index)> onSelect;

    void setSelected(int index);

    [[nodiscard]] int selected() const noexcept
    {
        return selected_;
    }

    void setShowsLabel(bool showsLabel);
    void setFontHeight(float fontHeight);

    /** The width that fits the label, if shown, and every segment's text at `height`. */
    [[nodiscard]] int preferredWidth(int height) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class SegmentButton;

    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] int segmentWidth(int index) const;

    juce::String label_;
    std::vector<Segment> segments_;
    std::vector<std::unique_ptr<SegmentButton>> buttons_;
    juce::Rectangle<int> labelArea_;

    int selected_ = 0;
    bool showsLabel_ = true;
    float fontHeight_ = 14.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedControl)
};

} // namespace visona
