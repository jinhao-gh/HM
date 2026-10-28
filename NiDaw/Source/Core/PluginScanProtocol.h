#pragma once

#include "Types.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace nidaw
{

/** The contract between HM and the copy of itself that reads one plugin.

    Scanning a plugin means loading somebody else's DLL, and a bad one takes the
    whole process with it (measured on this machine: pointing a scan at
    `C:\Program Files\Common Files\VST3` with Kontakt / Serum / FabFilter in it
    killed HM during the scan). So the loading happens in a child of HM's own
    process, and the two halves talk through this: a command line on the way in, one
    XML file on the way out.

    Nothing here touches a plugin - it is pure text handling, which is what makes it
    testable without one installed.
*/
namespace scanproto
{

/** How the parent asks the child to read one file. */
constexpr const char* kPluginSwitch = "--scan-plugin";
/** Where the child writes what it found. */
constexpr const char* kOutSwitch    = "--scan-out";
/** Self test: makes the child die the way a crashing plugin would. */
constexpr const char* kFailSwitch   = "--scan-plugin-fail-for-test";
/** Self test: makes the child hang, so the parent has to time it out. */
constexpr const char* kHangSwitch   = "--scan-plugin-hang-for-test";

/** The root element of the result file. */
constexpr const char* kResultTag = "HMPluginScan";

/** How long one plugin file gets before the parent kills the child. Kontakt-sized
    libraries take a while to open the first time. */
constexpr int kDefaultTimeoutMs = 30000;

/** One child's job. */
struct Request
{
    juce::File pluginFile;
    juce::File resultFile;
    bool failForTest = false;
    bool hangForTest = false;
};

/** What the child found. `error` is set for a file that could not be read at all. */
struct Result
{
    juce::Array<juce::PluginDescription> plugins;
    juce::String error;

    bool ok() const noexcept { return error.isEmpty() && ! plugins.isEmpty(); }
};

/** True when the command line asks for the child half. */
bool isChildInvocation (const juce::StringArray& tokens);

/** Reads a request out of a command line; false when the switches are unusable. */
bool parseRequest (const juce::StringArray& tokens, Request& request);

/** The arguments the parent starts the child with. */
juce::StringArray makeChildArguments (const Request& request);

/** The result file's text. */
juce::String serialize (const Result& result);

/** Reads a result file back. False for text that is not a result at all. */
bool parse (const juce::String& text, Result& result);

/** A file counts as failed when the child said so, or came back with nothing. */
bool hasFailed (const Result& result);

} // namespace scanproto
} // namespace nidaw
