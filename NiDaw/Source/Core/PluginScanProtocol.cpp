#include "PluginScanProtocol.h"

namespace nidaw
{
namespace scanproto
{

namespace
{
    /** The value that follows a switch, or false when there is none. */
    bool valueAfter (const juce::StringArray& tokens, const juce::String& switchName,
                     juce::String& value)
    {
        for (int i = 0; i < tokens.size() - 1; ++i)
        {
            if (tokens[i] != switchName)
                continue;

            const auto candidate = tokens[i + 1];

            // Two switches in a row means this one has no value, not that the next
            // switch is its value.
            if (candidate.startsWith ("--"))
                return false;

            value = candidate;
            return true;
        }

        return false;
    }
}

bool isChildInvocation (const juce::StringArray& tokens)
{
    return tokens.contains (kPluginSwitch);
}

bool parseRequest (const juce::StringArray& tokens, Request& request)
{
    juce::String plugin, out;

    if (! valueAfter (tokens, kPluginSwitch, plugin) || ! valueAfter (tokens, kOutSwitch, out))
        return false;

    request.pluginFile = juce::File (plugin);
    request.resultFile = juce::File (out);
    request.failForTest = tokens.contains (kFailSwitch);
    request.hangForTest = tokens.contains (kHangSwitch);
    return true;
}

juce::StringArray makeChildArguments (const Request& request)
{
    juce::StringArray arguments;
    arguments.add (kPluginSwitch);
    arguments.add (request.pluginFile.getFullPathName());
    arguments.add (kOutSwitch);
    arguments.add (request.resultFile.getFullPathName());

    if (request.failForTest)
        arguments.add (kFailSwitch);

    if (request.hangForTest)
        arguments.add (kHangSwitch);

    return arguments;
}

juce::String serialize (const Result& result)
{
    juce::XmlElement root (kResultTag);
    root.setAttribute ("version", 1);
    root.setAttribute ("error", result.error);

    for (const auto& description : result.plugins)
        if (auto xml = description.createXml())
            root.addChildElement (xml.release());

    return root.toString();
}

bool parse (const juce::String& text, Result& result)
{
    result = Result{};

    const auto xml = juce::XmlDocument::parse (text);

    if (xml == nullptr || ! xml->hasTagName (kResultTag))
        return false;

    result.error = xml->getStringAttribute ("error");

    for (auto* child : xml->getChildIterator())
    {
        juce::PluginDescription description;

        // A child element that is not a plugin (a future format change, a file that
        // arrived truncated) is skipped rather than treated as a find.
        if (description.loadFromXml (*child))
            result.plugins.add (description);
    }

    return true;
}

bool hasFailed (const Result& result)
{
    return ! result.ok();
}

} // namespace scanproto
} // namespace nidaw
