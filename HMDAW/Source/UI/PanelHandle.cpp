#include "PanelHandle.h"
#include "Theme.h"

namespace hmdaw
{

void PanelResizer::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    // A hairline in the middle, so the handle reads as an edge even before the
    // pointer is on it.
    g.setColour (theme::outline);

    if (orientation == Orientation::vertical)
        g.fillRect (0, getHeight() / 2 - 1, getWidth(), 2);
    else
        g.fillRect (getWidth() / 2 - 1, 0, 2, getHeight());
}

void PanelResizer::mouseDown (const juce::MouseEvent&)
{
    if (onDragStart)
        onDragStart();
}

void PanelResizer::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag)
        onDrag (orientation == Orientation::vertical ? e.getDistanceFromDragStartY()
                                                     : e.getDistanceFromDragStartX());
}

void PanelResizer::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onReset)
        onReset();
}

void PanelResizer::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (orientation == Orientation::vertical
                        ? juce::MouseCursor::UpDownResizeCursor
                        : juce::MouseCursor::LeftRightResizeCursor);
}

//==============================================================================
PanelToggleButton::PanelToggleButton (Side panelSideToUse)
    : juce::Button (NI_TXT ("收起 / 展开这一栏")),
      side (panelSideToUse)
{
    setTooltip (NI_TXT ("收起这一栏（再点一下展开）"));
}

void PanelToggleButton::setCollapsed (bool shouldBeCollapsed)
{
    if (collapsed == shouldBeCollapsed)
        return;

    collapsed = shouldBeCollapsed;
    setTooltip (collapsed ? NI_TXT ("展开这一栏") : NI_TXT ("收起这一栏"));
    repaint();
}

void PanelToggleButton::paintButton (juce::Graphics& g, bool highlighted,
                                     bool shouldDrawButtonAsDown)
{
    auto area = getLocalBounds().reduced (1);

    // A button, not a decoration: a raised plate with an outline, so the arrow can
    // be found without hunting for it on a dark panel.
    g.setColour (highlighted || shouldDrawButtonAsDown ? theme::accentDim : theme::panelRaised);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 1.0f);

    // A small chevron pointing the way the panel would move: an open left-hand
    // column folds leftwards, a folded one opens rightwards (and mirrored for the
    // right-hand column).
    const auto size = juce::jmin (12, juce::jmax (7, juce::jmin (area.getWidth(), area.getHeight()) - 6));
    const auto centre = area.getCentre().toFloat();
    const auto open = ! collapsed;

    juce::Path arrow;

    if (side == Side::bottom || side == Side::menu)
    {
        // A bottom lane folds downwards while it is open, and points back up once it
        // is folded away - the arrow always points the way the panel would go. A
        // menu arrow has nothing to fold, so it just stays pointing down.
        const auto dy = (side == Side::menu || open ? 1.0f : -1.0f) * size * 0.7f;

        arrow.startNewSubPath (centre.x - size * 0.7f, centre.y - dy);
        arrow.lineTo (centre.x, centre.y + dy);
        arrow.lineTo (centre.x + size * 0.7f, centre.y - dy);
    }
    else
    {
        // An open left-hand column folds to the left; a folded one opens to the right.
        const auto pointsLeft = (side == Side::left) == open;
        const auto dx = pointsLeft ? -size * 0.45f : size * 0.45f;

        arrow.startNewSubPath (centre.x - dx, centre.y - size * 0.7f);
        arrow.lineTo (centre.x + dx, centre.y);
        arrow.lineTo (centre.x - dx, centre.y + size * 0.7f);
    }

    g.setColour (highlighted || shouldDrawButtonAsDown ? juce::Colours::white : theme::text);
    g.strokePath (arrow, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

} // namespace hmdaw
