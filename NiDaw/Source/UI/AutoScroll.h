#pragma once

namespace nidaw
{

/** The drag-against-the-edge auto scroll, shared by the two panes.

    Both the arrangement and the editor slide their timeline while a drag is held
    against an edge. The gesture is the same in both, so the numbers live in one
    place: how close to the edge counts, and how far one timer tick moves the view.
    Each pane keeps its own "pixels -> ticks", pointer pinning and drag replay -
    those differ (see the known-divergence list in the batch 25 devlog).
*/
namespace autoscroll
{

/** Distance from an edge, in pixels, that starts the scrolling. */
inline constexpr int kEdgePixels = 24;

/** How far the view slides per timer tick (the panes run it at 30 Hz). */
inline constexpr int kStepPixels = 12;

/** Which way a drag at `pointerX` should scroll the strip that spans
    `areaLeft`..`areaRight`: -kStepPixels, +kStepPixels, or 0 when the pointer is
    not in an edge zone. The comparisons are the ones both panes always used. */
inline int stepFor (double pointerX, double areaLeft, double areaRight) noexcept
{
    if (pointerX > areaRight - (double) kEdgePixels)
        return kStepPixels;

    if (pointerX < areaLeft + (double) kEdgePixels)
        return -kStepPixels;

    return 0;
}

} // namespace autoscroll

} // namespace nidaw
