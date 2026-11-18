#include "WindowPlacement.h"

namespace hmdaw
{
namespace placement
{

juce::Rectangle<int> usableArea (juce::Rectangle<int> displayUserArea, int margin)
{
    const auto gap = juce::jmax (0, margin);

    // jmax(1, ...) rather than trusting the subtraction: a work area smaller than
    // twice the margin would otherwise come out inside out, and every window would
    // then be placed at a negative size.
    return { displayUserArea.getX() + gap,
             displayUserArea.getY() + gap,
             juce::jmax (1, displayUserArea.getWidth() - gap * 2),
             juce::jmax (1, displayUserArea.getHeight() - gap * 2) };
}

juce::Point<int> fitMinimum (juce::Point<int> minimum, juce::Rectangle<int> displayUserArea,
                             int margin)
{
    const auto area = usableArea (displayUserArea, margin);

    return { juce::jlimit (1, area.getWidth(), juce::jmax (1, minimum.x)),
             juce::jlimit (1, area.getHeight(), juce::jmax (1, minimum.y)) };
}

juce::Rectangle<int> placeWindow (juce::Point<int> wanted, juce::Rectangle<int> displayUserArea,
                                  int margin)
{
    const auto area = usableArea (displayUserArea, margin);

    // A window bigger than the screen loses its own bottom-right corner, so the
    // work area wins over what the window asked for. Never zero either: a 0 x 0
    // window is not a window.
    const auto width  = juce::jlimit (1, area.getWidth(), juce::jmax (1, wanted.x));
    const auto height = juce::jlimit (1, area.getHeight(), juce::jmax (1, wanted.y));

    // Centred on the display, the way an ordinary window opens; the clamp then
    // keeps that centre from pushing an edge off the screen.
    const auto x = juce::jlimit (area.getX(), area.getRight() - width,
                                 displayUserArea.getCentreX() - width / 2);
    const auto y = juce::jlimit (area.getY(), area.getBottom() - height,
                                 displayUserArea.getCentreY() - height / 2);

    return { x, y, width, height };
}

juce::Rectangle<int> displayAreaFor (juce::Component* context)
{
    auto& displays = juce::Desktop::getInstance().getDisplays();

    const auto reference = context != nullptr ? context->getScreenBounds()
                                              : juce::Rectangle<int>();

    if (auto* display = displays.getDisplayForRect (reference))
        return display->userBounds.toNearestInt();

    if (auto* mainDisplay = displays.getPrimaryDisplay())
        return mainDisplay->userBounds.toNearestInt();

    // No screens at all (an offscreen run, or a session without a desktop): the
    // geometry still has to hand back something sane.
    return { 0, 0, 1920, 1080 };
}

} // namespace placement
} // namespace hmdaw
