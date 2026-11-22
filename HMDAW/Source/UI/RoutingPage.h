#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** Placeholder for the effect-routing page. The per-track insert chains still
    live in the arrangement; this page will own the wiring graph later. */
class RoutingPage : public juce::Component
{
public:
    RoutingPage() = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingPage)
};

} // namespace hmdaw
