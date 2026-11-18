#include "PluginGrouping.h"

#include <algorithm>

namespace hmdaw
{

namespace
{
    /** The VST3 subcategories that say nothing about what a plugin *is*: they are
        dropped so "Fx|Reverb" files under 混响 rather than under 效果器. */
    bool isGenericCategoryToken (const juce::String& token)
    {
        const auto lower = token.trim().toLowerCase();

        return lower == "fx" || lower == "instrument" || lower == "tools";
    }

    /** The Chinese name of a VST3 subcategory, or the token itself when the table
        does not know it (a plugin is allowed to invent its own subcategory). */
    juce::String localizedType (const juce::String& token)
    {
        const auto key = token.trim().toLowerCase().removeCharacters (" -_/");

        if (key.isEmpty())                 return {};
        if (key == "reverb")               return NI_TXT ("混响");
        if (key == "delay")                return NI_TXT ("延迟");
        if (key == "echo")                 return NI_TXT ("回声");
        if (key == "chorus")               return NI_TXT ("合唱");
        if (key == "flanger")              return NI_TXT ("镶边");
        if (key == "phaser")               return NI_TXT ("相位");
        if (key == "modulation")           return NI_TXT ("调制");
        if (key == "tremolo")              return NI_TXT ("颤音");
        if (key == "vibrato")              return NI_TXT ("揉音");
        if (key == "distortion")           return NI_TXT ("失真");
        if (key == "saturation")           return NI_TXT ("饱和");
        if (key == "ampsimulator")         return NI_TXT ("音箱模拟");
        if (key == "dynamics")             return NI_TXT ("动态");
        if (key == "compressor")           return NI_TXT ("压缩");
        if (key == "limiter")              return NI_TXT ("限制");
        if (key == "gate")                 return NI_TXT ("噪声门");
        if (key == "expander")             return NI_TXT ("扩展");
        if (key == "equalizer" || key == "eq") return NI_TXT ("均衡");
        if (key == "filter")               return NI_TXT ("滤波");
        if (key == "pitchshift")           return NI_TXT ("移调");
        if (key == "instrument")           return NI_TXT ("乐器");
        if (key == "synth")                return NI_TXT ("合成器");
        if (key == "sampler")              return NI_TXT ("采样器");
        if (key == "drum")                 return NI_TXT ("鼓机");
        if (key == "piano")                return NI_TXT ("钢琴");
        if (key == "organ")                return NI_TXT ("风琴");
        if (key == "guitar")               return NI_TXT ("吉他");
        if (key == "bass")                 return NI_TXT ("贝斯");
        if (key == "strings")              return NI_TXT ("弦乐");
        if (key == "brass")                return NI_TXT ("铜管");
        if (key == "woodwind")             return NI_TXT ("木管");
        if (key == "vocal" || key == "voice") return NI_TXT ("人声");
        if (key == "stereo" || key == "mono") return NI_TXT ("声场");
        if (key == "analyzer")             return NI_TXT ("分析器");
        if (key == "mastering")            return NI_TXT ("母带");
        if (key == "ambisonics")           return NI_TXT ("环绕声");
        if (key == "spatial")              return NI_TXT ("空间");
        if (key == "updownmix")            return NI_TXT ("上下混音");

        return token.trim();
    }

    void sortByPluginName (std::vector<juce::PluginDescription>& plugins)
    {
        std::sort (plugins.begin(), plugins.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
        {
            const auto byName = a.name.compareIgnoreCase (b.name);

            return byName != 0 ? byName < 0 : a.manufacturerName.compareIgnoreCase (b.manufacturerName) < 0;
        });
    }

    /** 未知厂家 and 其他 are catch-alls, so they are listed last. */
    bool isCatchAllHeading (const juce::String& name)
    {
        return name == pluginVendorHeading() || name == pluginOtherTypeHeading();
    }
}

juce::String pluginVendorHeading()
{
    return NI_TXT ("未知厂家");
}

juce::String pluginOtherTypeHeading()
{
    return NI_TXT ("其他");
}

juce::String pluginVendorOf (const juce::PluginDescription& plugin)
{
    const auto vendor = plugin.manufacturerName.trim();
    return vendor.isNotEmpty() ? vendor : pluginVendorHeading();
}

juce::String pluginTypeOf (const juce::PluginDescription& plugin)
{
    const auto raw = plugin.category.trim();

    if (raw.isEmpty())
        return pluginOtherTypeHeading();

    auto tokens = juce::StringArray::fromTokens (raw, "|", {});
    tokens.trim();

    // "Fx|Reverb" -> 混响; a bare "Instrument" says nothing more than "it is an
    // instrument", so it is a catch-all too.
    while (! tokens.isEmpty() && isGenericCategoryToken (tokens[0]))
        tokens.remove (0);

    if (tokens.isEmpty())
        return pluginOtherTypeHeading();

    const auto localized = localizedType (tokens[0]);
    return localized.isNotEmpty() ? localized : pluginOtherTypeHeading();
}

std::vector<PluginGroup> groupPlugins (const std::vector<juce::PluginDescription>& plugins,
                                       PluginGroupAxis axis)
{
    std::vector<PluginGroup> groups;

    if (axis == PluginGroupAxis::none)
    {
        PluginGroup everything;
        everything.plugins = plugins;
        sortByPluginName (everything.plugins);

        groups.push_back (std::move (everything));
        return groups;
    }

    for (const auto& plugin : plugins)
    {
        const auto heading = axis == PluginGroupAxis::vendor ? pluginVendorOf (plugin)
                                                             : pluginTypeOf (plugin);

        auto found = std::find_if (groups.begin(), groups.end(),
                                   [&heading] (const PluginGroup& group)
                                   { return group.name == heading; });

        if (found == groups.end())
        {
            groups.push_back ({ heading, {} });
            found = groups.end() - 1;
        }

        found->plugins.push_back (plugin);
    }

    for (auto& group : groups)
        sortByPluginName (group.plugins);

    std::stable_sort (groups.begin(), groups.end(),
                      [] (const PluginGroup& a, const PluginGroup& b)
    {
        const auto aLast = isCatchAllHeading (a.name);
        const auto bLast = isCatchAllHeading (b.name);

        if (aLast != bLast)
            return ! aLast;

        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return groups;
}

juce::String pluginGroupAxisToText (PluginGroupAxis axis)
{
    switch (axis)
    {
        case PluginGroupAxis::vendor: return "vendor";
        case PluginGroupAxis::type:   return "type";
        case PluginGroupAxis::none:   return "none";
    }

    return "vendor";
}

PluginGroupAxis pluginGroupAxisFromText (const juce::String& text, PluginGroupAxis fallback)
{
    const auto value = text.trim().toLowerCase();

    if (value == "vendor") return PluginGroupAxis::vendor;
    if (value == "type")   return PluginGroupAxis::type;
    if (value == "none")   return PluginGroupAxis::none;

    return fallback;
}

} // namespace hmdaw
