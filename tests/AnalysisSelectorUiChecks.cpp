#include "ui/AnalysisSelector.h"

#include <stdexcept>

void runAnalysisSelectorUiChecks();

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

juce::MouseEvent eventFor(juce::Component& target, juce::Point<float> position)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            position,
            juce::ModifierKeys::leftButtonModifier,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &target,
            &target,
            now,
            position,
            now,
            1,
            false};
}
} // namespace

void runAnalysisSelectorUiChecks()
{
    juce::Component host;
    visona::AnalysisSelector selector;
    host.setSize(800, 480);
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    host.setVisible(true);
    host.addAndMakeVisible(selector);
    selector.setBounds(0, 430, 190, 50);
    selector.setTouchMode(true);
    int selected = 0;
    int picks = 0;
    selector.onSelect = [&](int view)
    {
        selected = view;
        ++picks;
    };

    // Reopen the same panel repeatedly, as on the kiosk touchscreen. A pointer exit
    // between press and release must not discard an otherwise valid tap.
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        for (int index = 0; index < 5; ++index)
        {
            selector.showChoices();
            auto* panel = host.getChildComponent(host.getNumChildComponents() - 1);
            check(panel != nullptr && panel->isVisible(), "Selector did not reopen");
            auto* button = panel->getChildComponent(index);
            check(dynamic_cast<juce::Button*>(button) != nullptr, "Selector button missing");
            const auto event = eventFor(*button, button->getLocalBounds().toFloat().getCentre());
            const auto before = picks;
            const bool changesView = index < 4 && index != selected;

            button->mouseUp(event);
            check(panel->isVisible() && picks == before,
                  "Release without press activated selector");
            button->mouseDown(event);
            button->mouseExit(event);
            button->mouseUp(eventFor(*button, {-10.0f, -10.0f}));
            check(panel->isVisible() && picks == before, "Release outside activated selector");

            button->mouseDown(event);
            button->mouseExit(event);
            button->mouseUp(event);
            check(!panel->isVisible(), "Pointer exit swallowed selector tap");
            check(picks == before + (changesView ? 1 : 0), "Selector callback count incorrect");
            if (index < 4)
                check(selected == index, "Selector picked wrong view");
        }
    }
}
