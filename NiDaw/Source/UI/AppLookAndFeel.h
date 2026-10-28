#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** Flat dark theme for the whole application. */
class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel();
    ~AppLookAndFeel() override = default;
};

} // namespace nidaw
