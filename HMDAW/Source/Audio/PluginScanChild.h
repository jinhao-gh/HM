#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace hmdaw
{

/** The child half of an out-of-process plugin scan.

    HM starts a copy of itself with `--scan-plugin <file> --scan-out <file>` to find
    out what one .vst3 bundle contains. That copy loads exactly one plugin and then
    leaves the process immediately, so a plugin that crashes while being read can
    only take the child with it - the editor never sees anything worse than one
    "读取失败" file.
*/
namespace pluginscan
{

/** Runs the child when the command line asks for it and returns the exit code the
    parent should see. nullopt means "not a scan child": carry on starting the app.
*/
std::optional<int> runChildIfRequested (const juce::StringArray& commandLineTokens);

} // namespace pluginscan
} // namespace hmdaw
