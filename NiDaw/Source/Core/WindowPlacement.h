#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** Where a window goes on screen.

    Every window HM opens asks this first, because a window that is bigger than the
    display it opens on is a window whose bottom-right corner - the buttons, the
    transport bar, the plugin's own controls - can never be reached. The rule is
    simple: shrink to fit the work area, then sit completely inside it.

    The geometry is a free function, so the unit tests can check it without a
    screen; the two helpers that ask JUCE where the displays are live here as well,
    so the app and the self test share one implementation.
*/
namespace placement
{

/** The gap kept between a window and the edge of the work area. */
inline constexpr int kMargin = 8;

/** The main window: the size the layout is designed for, and the smallest it still
    makes sense at. Both give way to the screen - a display that cannot give the
    minimum gets a smaller minimum rather than a window that hangs off the edge. */
inline constexpr int kMainWindowWidth  = 1460;
inline constexpr int kMainWindowHeight = 950;
inline constexpr int kMainWindowMinimumWidth  = 1120;
inline constexpr int kMainWindowMinimumHeight = 720;

/** The area a window may occupy, keeping `margin` clear of the screen edges.
    Never empty: even a silly-small work area gives a 1 x 1 area back. */
juce::Rectangle<int> usableArea (juce::Rectangle<int> displayUserArea, int margin = kMargin);

/** Where a window that would like to be `wanted` really goes: shrunk when it does
    not fit, centred when it does, and always completely inside `displayUserArea`. */
juce::Rectangle<int> placeWindow (juce::Point<int> wanted,
                                  juce::Rectangle<int> displayUserArea,
                                  int margin = kMargin);

/** The smallest size a window may ask for on this display. A resize limit that
    insists on a minimum the screen cannot give would put the window back over the
    edge of the screen, so on a small display the minimum gives way first. */
juce::Point<int> fitMinimum (juce::Point<int> minimum,
                             juce::Rectangle<int> displayUserArea,
                             int margin = kMargin);

/** The work area of the display `context` is on, or of the main display when
    `context` is null. Every window of one HM run therefore opens on the screen the
    user is already looking at, and not always on the primary one. */
juce::Rectangle<int> displayAreaFor (juce::Component* context);

} // namespace placement

} // namespace nidaw
