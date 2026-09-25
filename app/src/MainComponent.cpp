#include "MainComponent.h"

namespace visona
{

MainComponent::MainComponent()
{
    setOpaque(true);
    setSize(1280, 720);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);
}

} // namespace visona
