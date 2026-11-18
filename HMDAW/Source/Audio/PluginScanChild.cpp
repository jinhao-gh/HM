#include "PluginScanChild.h"

#include "../Core/PluginScanProtocol.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdlib>

namespace hmdaw
{
namespace pluginscan
{

namespace
{
    /** What the parent sees when the child could not even read the file. */
    constexpr int kBadArguments = 2;
    constexpr int kFailedForTest = 3;
    constexpr int kCouldNotWriteResult = 4;
}

std::optional<int> runChildIfRequested (const juce::StringArray& commandLineTokens)
{
    if (! scanproto::isChildInvocation (commandLineTokens))
        return std::nullopt;

    // The app itself never gets as far as a window in this mode: no MainWindow, no
    // audio device, no cache files. JUCE is initialised because the plugin loader
    // wants a message loop's thread state (COM on Windows), which is what the
    // ScopedJuceInitialiser sets up.
    juce::ScopedJuceInitialiser_GUI juceInit;

    scanproto::Request request;

    if (! scanproto::parseRequest (commandLineTokens, request))
        return kBadArguments;

    // The two self test switches make this child behave like the plugins the process
    // split exists for: one dies on the spot, the other never comes back.
    if (request.failForTest)
        return kFailedForTest;

    if (request.hangForTest)
    {
        juce::Thread::sleep (600000);
        return kFailedForTest;
    }

    scanproto::Result found;

    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;

        format.findAllTypesForFile (types, request.pluginFile.getFullPathName());

        for (const auto* type : types)
            found.plugins.add (*type);
    }

    if (found.plugins.isEmpty())
        found.error = NI_TXT ("这个 .vst3 里读不到插件信息。");

    if (! request.resultFile.replaceWithText (scanproto::serialize (found)))
        return kCouldNotWriteResult;

    // Leave without running any destructor: unloading a plugin that has just been
    // read is exactly the kind of thing that has taken hosts down before, and there
    // is nothing left to do here - the answer is already on disk.
    std::_Exit (0);
}

} // namespace pluginscan
} // namespace hmdaw
