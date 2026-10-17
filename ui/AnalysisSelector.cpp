#include "AnalysisSelector.h"

#include "Palette.h"

namespace visona
{
namespace
{
const juce::StringArray viewNames{"Scope", "Spectrum (FFT)", "Spectrum (Band)", "Stereo"};

class SelectorButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void resetPress() noexcept
    {
        pressedSource_ = -1;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        pressedSource_ = event.source.getIndex();
        juce::TextButton::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (pressedSource_ != event.source.getIndex())
            return;
        // Hover/exit events from another pointer can clear Button's visual down state
        // during a touch. Match the press and release instead of relying on that state.
        const bool picked = isShowing() && isEnabled() &&
                            !isCurrentlyBlockedByAnotherModalComponent() &&
                            getLocalBounds().toFloat().contains(event.position);
        resetPress();
        setState(picked ? buttonDown : buttonNormal);
        juce::TextButton::mouseUp(event);
    }

private:
    int pressedSource_ = -1;
};
} // namespace
struct AnalysisSelector::Tiles final : juce::Component
{
    struct Tile final : SelectorButton
    {
        int view = 0;
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            g.setColour(getToggleState() ? palette::highlight : palette::surface);
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3), 10.0f);
            g.setColour(palette::text);
            auto area = getLocalBounds().reduced(16);
            g.setFont(18.0f);
            g.drawText(getButtonText(), area.removeFromBottom(28), juce::Justification::centred);
            auto icon = area.toFloat().reduced(12.0f, 8.0f);
            juce::Path p;
            for (int i = 0; i <= 64; ++i)
            {
                const auto x = static_cast<float>(i) / 64.0f;
                const auto y = view == 0
                                   ? 0.5f + 0.32f * std::sin(x * 19.0f)
                                   : 0.9f - 0.7f * std::exp(-50.0f * (x - 0.3f) * (x - 0.3f)) -
                                         0.35f * std::exp(-35.0f * (x - 0.7f) * (x - 0.7f));
                if (i == 0)
                    p.startNewSubPath(icon.getX(), icon.getY() + y * icon.getHeight());
                else
                    p.lineTo(icon.getX() + x * icon.getWidth(), icon.getY() + y * icon.getHeight());
            }
            g.setColour(juce::Colour(0xff79ddc6).withAlpha(down ? 0.6f : over ? 1.0f : 0.85f));
            if (view == 3)
            {
                juce::Path stereo;
                stereo.addEllipse(
                    icon.withSizeKeepingCentre(icon.getWidth() * 0.28f, icon.getHeight()));
                g.strokePath(stereo, juce::PathStrokeType(2.0f));
                g.drawVerticalLine(juce::roundToInt(icon.getCentreX()), icon.getY(),
                                   icon.getBottom());
            }
            else if (view == 2)
            {
                for (int i = 0; i < 12; ++i)
                {
                    const auto height =
                        icon.getHeight() *
                        (0.25f + 0.65f * std::abs(std::sin(static_cast<float>(i) * 0.37f)));
                    g.fillRect(icon.getX() + static_cast<float>(i) * icon.getWidth() / 12,
                               icon.getBottom() - height, icon.getWidth() / 12 - 3, height);
                }
            }
            else
                g.strokePath(p, juce::PathStrokeType(2.0f));
        }
    };
    std::vector<std::unique_ptr<Tile>> buttons;
    SelectorButton close{"Close"};
    std::function<void(int)> onPick;
    Tiles()
    {
        for (int i = 0; i < viewNames.size(); ++i)
        {
            auto& b = *buttons.emplace_back(std::make_unique<Tile>());
            b.view = static_cast<int>(i);
            b.setButtonText(viewNames[static_cast<int>(i)]);
            addAndMakeVisible(b);
            b.onClick = [this, i]
            {
                if (onPick)
                    onPick(static_cast<int>(i));
                setVisible(false);
            };
        }
        addAndMakeVisible(close);
        close.onClick = [this] { setVisible(false); };
    }
    juce::Rectangle<int> panel() const
    {
        const auto rows = (viewNames.size() + 1) / 2;
        return getLocalBounds().withSizeKeepingCentre(std::min(520, getWidth() - 24),
                                                      std::min(getHeight() - 24, 84 + rows * 134));
    }
    void resized() override
    {
        auto area = panel().reduced(16);
        close.setBounds(area.removeFromTop(40).removeFromRight(80));
        area.removeFromTop(12);
        const auto rows = std::max(1, (viewNames.size() + 1) / 2);
        const auto cellWidth = area.getWidth() / 2;
        const auto cellHeight = area.getHeight() / rows;
        for (std::size_t i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds(area.getX() + static_cast<int>(i % 2) * cellWidth,
                                  area.getY() + static_cast<int>(i / 2) * cellHeight, cellWidth,
                                  cellHeight);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.65f));
        g.setColour(palette::chrome);
        g.fillRoundedRectangle(panel().toFloat(), 12.0f);
        g.setColour(palette::text);
        g.setFont(20.0f);
        g.drawText("Analysis view", panel().reduced(20).removeFromTop(36),
                   juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!panel().contains(e.getPosition()))
            setVisible(false);
    }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k.isKeyCode(juce::KeyPress::escapeKey))
        {
            setVisible(false);
            return true;
        }
        return false;
    }
};
AnalysisSelector::AnalysisSelector()
{
    setView(0);
    setTooltip("Analysis view (V). Alt+1: Scope; Alt+2: Spectrum (FFT); Alt+3: Spectrum (Band); "
               "Alt+4: Stereo.");
    onClick = [this] { showChoices(); };
}
AnalysisSelector::~AnalysisSelector() = default;
void AnalysisSelector::setView(int view)
{
    selected_ = std::clamp(view, 0, viewNames.size() - 1);
    setButtonText(viewNames[selected_] + "  ▾");
}
void AnalysisSelector::choose(int view)
{
    setView(view);
    if (onSelect)
        onSelect(view);
}
void AnalysisSelector::mouseDown(const juce::MouseEvent& e)
{
    lastTouch_ = e.source.isTouch();
    juce::TextButton::mouseDown(e);
}
void AnalysisSelector::parentSizeChanged()
{
    list_.reset();
    if (tiles_)
        tiles_->setVisible(false);
}
void AnalysisSelector::showChoices()
{
    auto* host = ChoiceList::hostFor(*this);
    if (!host)
        return;
    if (touchMode_ || lastTouch_)
    {
        if (!tiles_)
        {
            tiles_ = std::make_unique<Tiles>();
            tiles_->onPick = [this](int view) { choose(view); };
        }
        host->addAndMakeVisible(*tiles_);
        tiles_->setBounds(host->getLocalBounds());
        for (std::size_t i = 0; i < tiles_->buttons.size(); ++i)
        {
            tiles_->buttons[i]->resetPress();
            tiles_->buttons[i]->setToggleState(static_cast<int>(i) == selected_,
                                               juce::dontSendNotification);
        }
        tiles_->close.resetPress();
        tiles_->toFront(true);
        tiles_->setWantsKeyboardFocus(true);
        tiles_->grabKeyboardFocus();
        return;
    }
    list_ = std::make_unique<ChoiceList>(viewNames, selected_, juce::Font(juce::FontOptions(16.0f)),
                                         juce::Justification::centredLeft);
    list_->onPick = [this](int view)
    {
        list_.reset();
        choose(view);
    };
    list_->onDismiss = [this] { list_.reset(); };
    auto target = host->getLocalArea(this, getLocalBounds());
    const auto height = viewNames.size() * 44 + 2;
    auto bounds = juce::Rectangle<int>(target.getX(), target.getY() - height, 192, height)
                      .constrainedWithin(host->getLocalBounds());
    list_->show(*host, bounds, 44, 0, 0);
}
} // namespace visona
