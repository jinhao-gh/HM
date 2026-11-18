#pragma once

#include "../Core/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The thin bar between a side column and the canvas: drag it to change the
    column's width, double-click it to put the width back to the default.

    It only reports the drag; whoever owns the column decides what the width means
    and where the limit is.
*/
class PanelResizer : public juce::Component
{
public:
    /** Which way the handle reports its drag: a horizontal one sits between two side
        columns and reports how far the pointer moved sideways (the default), a
        vertical one sits above a lane at the bottom and reports how far it moved up
        and down. */
    enum class Orientation { horizontal, vertical };

    explicit PanelResizer (Orientation orientationToUse = Orientation::horizontal)
        : orientation (orientationToUse)
    {
    }

    /** Called once when a drag starts, so the owner can remember the width it began
        at (a drag reports the distance from the press, not a running total). */
    std::function<void()> onDragStart;
    std::function<void (int deltaX)> onDrag;
    /** Double-click: back to the panel's default width. */
    std::function<void()> onReset;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;

private:
    Orientation orientation = Orientation::horizontal;
};

//==============================================================================
/** The little arrow that folds a column down to a thin bar and back.

    It lives in the column's own header while the column is open, and becomes the
    whole thin bar while it is folded - which is why it is a button of its own rather
    than something the panel paints: a folded column keeps exactly one visible child.
*/
class PanelToggleButton : public juce::Button
{
public:
    /** Which way the arrow points when the panel is open: a left-hand column folds
        to the left, the right-hand one to the right. */
    enum class Side
    {
        left,
        right,
        /** A lane that lives at the bottom of its parent: it folds downwards, and
            the arrow points up once it is folded away. */
        bottom,
        /** A plain "open a menu from here" arrow: it always points down. */
        menu
    };

    explicit PanelToggleButton (Side panelSideToUse);

    void setCollapsed (bool shouldBeCollapsed);

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    Side side;
    bool collapsed = false;
};

} // namespace hmdaw
