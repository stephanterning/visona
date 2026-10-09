#include "ChromeButton.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace visona
{

namespace
{

constexpr float cornerRadius = 6.0f;
constexpr float iconSize = 18.0f;
constexpr float labelGap = 8.0f;
constexpr float sidePadding = 12.0f;

} // namespace

ChromeButton::ChromeButton(const juce::String& label, Icon icon)
    : juce::Button(label)
    , icon_(icon)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip(label);
}

void ChromeButton::setIcon(Icon icon)
{
    if (icon == icon_)
        return;
    icon_ = icon;
    repaint();
}

void ChromeButton::setWidestLabel(const juce::String& label)
{
    widestLabel_ = label;
}

void ChromeButton::setShowsLabel(bool showsLabel)
{
    if (showsLabel == showsLabel_)
        return;
    showsLabel_ = showsLabel;
    repaint();
}

void ChromeButton::setFontHeight(float fontHeight)
{
    fontHeight_ = fontHeight;
    repaint();
}

int ChromeButton::preferredWidth(int height, bool withLabel) const
{
    if (!withLabel)
        return height;
    // A little slack, so rounding never cuts the last letter.
    const juce::FontOptions font(fontHeight_, juce::Font::bold);
    const auto labelWidth = std::max(juce::GlyphArrangement::getStringWidth(font, getButtonText()),
                                     juce::GlyphArrangement::getStringWidth(font, widestLabel_));
    return static_cast<int>(std::ceil(sidePadding * 2.0f + iconSize + labelGap + labelWidth)) + 4;
}

void ChromeButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();

    g.setColour(on || isDown
                    ? palette::highlight
                    : (isHighlighted ? palette::surface.brighter(0.08f) : palette::surface));
    g.fillRoundedRectangle(bounds, cornerRadius);
    if (on)
    {
        g.setColour(palette::level.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    const auto content = on ? palette::text : palette::level;
    if (!showsLabel_)
    {
        paintIcon(g, bounds.withSizeKeepingCentre(iconSize, iconSize), content);
        return;
    }

    auto area = bounds.reduced(sidePadding, 0.0f);
    paintIcon(g, area.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize), content);
    area.removeFromLeft(labelGap);
    g.setColour(content);
    g.setFont(juce::FontOptions(fontHeight_, juce::Font::bold));
    g.drawText(getButtonText(), area, juce::Justification::centredLeft, false);
}

void ChromeButton::paintIcon(juce::Graphics& g, juce::Rectangle<float> area,
                             juce::Colour colour) const
{
    g.setColour(colour);
    const auto s = area.getWidth();
    const auto x = area.getX();
    const auto y = area.getY();
    const juce::PathStrokeType stroke(s * 0.11f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);
    juce::Path path;

    switch (icon_)
    {
    case Icon::settings:
    {
        const auto centre = area.getCentre();
        path.addEllipse(juce::Rectangle<float>(s * 0.44f, s * 0.44f).withCentre(centre));
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const auto angle = static_cast<float>(tooth) * std::numbers::pi_v<float> / 4.0f;
            const auto dx = std::sin(angle);
            const auto dy = -std::cos(angle);
            path.startNewSubPath(centre.x + dx * s * 0.3f, centre.y + dy * s * 0.3f);
            path.lineTo(centre.x + dx * s * 0.46f, centre.y + dy * s * 0.46f);
        }
        g.strokePath(path, stroke);
        break;
    }
    case Icon::diagnostics:
    {
        // Three bars of different heights.
        for (const auto [column, height] : {std::pair{0.2f, 0.45f}, {0.5f, 0.8f}, {0.8f, 0.6f}})
        {
            path.startNewSubPath(x + s * column, y + s * 0.9f);
            path.lineTo(x + s * column, y + s * (0.9f - height));
        }
        g.strokePath(path, stroke);
        break;
    }
    case Icon::fullScreen:
    {
        // Corner brackets that point outwards, or inwards while in full screen.
        const bool inwards = getToggleState();
        const auto arm = s * 0.3f;
        for (const auto [cx, cy] :
             {std::pair{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}})
        {
            const auto cornerX = x + s * (0.1f + cx * 0.8f);
            const auto cornerY = y + s * (0.1f + cy * 0.8f);
            const auto dirX = (cx == 0.0f ? 1.0f : -1.0f) * arm;
            const auto dirY = (cy == 0.0f ? 1.0f : -1.0f) * arm;
            if (inwards)
            {
                const juce::Point<float> tip(cornerX + dirX, cornerY + dirY);
                path.startNewSubPath(tip.x - dirX, tip.y);
                path.lineTo(tip);
                path.lineTo(tip.x, tip.y - dirY);
            }
            else
            {
                path.startNewSubPath(cornerX + dirX, cornerY);
                path.lineTo(cornerX, cornerY);
                path.lineTo(cornerX, cornerY + dirY);
            }
        }
        g.strokePath(path, stroke);
        break;
    }
    case Icon::pause:
    {
        const auto bar = juce::Rectangle<float>(s * 0.22f, s * 0.7f);
        g.fillRoundedRectangle(bar.withPosition(x + s * 0.2f, y + s * 0.15f), s * 0.05f);
        g.fillRoundedRectangle(bar.withPosition(x + s * 0.58f, y + s * 0.15f), s * 0.05f);
        break;
    }
    case Icon::play:
    {
        path.addTriangle(x + s * 0.25f, y + s * 0.15f, x + s * 0.25f, y + s * 0.85f, x + s * 0.85f,
                         y + s * 0.5f);
        g.fillPath(path);
        break;
    }
    }
}

} // namespace visona
