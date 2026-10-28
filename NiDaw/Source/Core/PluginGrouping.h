#pragma once

#include "Types.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace nidaw
{

/** How the plugin picker arranges what it found. */
enum class PluginGroupAxis
{
    vendor = 0,   // 按厂家
    type,         // 按类型（VST3 的子类别）
    none          // 不分组
};

/** One heading in the picker's tree, with the plugins filed under it.

    `name` is empty for the "no grouping" axis: the picker then lists the plugins
    as top level rows.
*/
struct PluginGroup
{
    juce::String name;
    std::vector<juce::PluginDescription> plugins;
};

/** The heading a plugin with no manufacturer is filed under, and the one for a
    plugin whose category says nothing useful. */
juce::String pluginVendorHeading();
juce::String pluginOtherTypeHeading();

/** The vendor to file a plugin under (empty manufacturer -> 未知厂家). */
juce::String pluginVendorOf (const juce::PluginDescription& plugin);

/** The type to file a plugin under.

    VST3 puts its subcategories in `category`, joined with "|" and usually led by
    a generic token ("Fx", "Instrument", "Tools"), e.g. "Fx|Reverb" or
    "Instrument|Synth". The generic part is dropped and the specific one is shown
    in Chinese where the table knows it, otherwise exactly as the plugin wrote it.
    A plugin that says nothing at all ends up under 其他.
*/
juce::String pluginTypeOf (const juce::PluginDescription& plugin);

/** Arranges plugins into headings: inside a heading sorted by name (ignoring
    case), headings themselves by name with 其他 / 未知厂家 last. The `none` axis
    gives a single heading with an empty name holding everything.
*/
std::vector<PluginGroup> groupPlugins (const std::vector<juce::PluginDescription>& plugins,
                                       PluginGroupAxis axis);

/** How the axis is written in the settings file (and read back). */
juce::String pluginGroupAxisToText (PluginGroupAxis axis);
PluginGroupAxis pluginGroupAxisFromText (const juce::String& text,
                                         PluginGroupAxis fallback = PluginGroupAxis::vendor);

} // namespace nidaw
