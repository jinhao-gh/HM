#pragma once

#include <juce_core/juce_core.h>

namespace hmdaw
{

/** Headless verification: builds the whole UI offscreen, exercises the document,
    and writes PNG snapshots so the layout can be inspected without a display.
*/
namespace selftest
{

int run (const juce::File& outputDirectory, const juce::File& testPlugin,
         const juce::File& testEffect);

} // namespace selftest

} // namespace hmdaw
