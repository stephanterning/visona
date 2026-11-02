#pragma once
#include "ChoiceList.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace visona
{
/** Stable view selector. Desktop list, in-window touch tiles; extensible independently of layout.
 */
class AnalysisSelector final : public juce::TextButton
{
public:
    AnalysisSelector();
    ~AnalysisSelector() override;
    std::function<void(int)> onSelect;
    void setView(int view);
    void setTouchMode(bool touch)
    {
        touchMode_ = touch;
    }
    void showChoices();
    void mouseDown(const juce::MouseEvent& e) override;
    void parentSizeChanged() override;

private:
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    struct Tiles;
    void choose(int view);
    int selected_ = 0;
    bool touchMode_ = false, lastTouch_ = false;
    std::unique_ptr<ChoiceList> list_;
    std::unique_ptr<Tiles> tiles_;
};
} // namespace visona
