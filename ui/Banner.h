#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace visona
{

/** An error banner over the main view, such as NO AUDIO INPUT. It never takes mouse clicks. */
class Banner final : public juce::Component
{
public:
    Banner();

    void setText(const juce::String& title, const juce::String& detail);

    void paint(juce::Graphics& g) override;

private:
    juce::String title_;
    juce::String detail_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Banner)
};

} // namespace visona
