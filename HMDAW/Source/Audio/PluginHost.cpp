#include "PluginHost.h"
#include "../Core/ControllerMath.h"

#include <juce_events/juce_events.h>

namespace hmdaw
{

namespace
{
    void trace (const juce::String& message)
    {
        if (PluginHostManager::traceHook != nullptr)
            PluginHostManager::traceHook (message);
    }

    /** Where the scan cache goes. The offscreen self test points this at its own
        output folder so a run never touches the real per-user list. */
    juce::File& defaultListFileOverride()
    {
        static juce::File file;
        return file;
    }

    /** Where the plugin event log goes (same idea, same override). */
    juce::File& eventsLogFileOverride()
    {
        static juce::File file;
        return file;
    }

    /** How often an offline instance was built off the message thread. */
    std::atomic<int>& offlineThreadViolations()
    {
        static std::atomic<int> count { 0 };
        return count;
    }

    /** How often the live-instance guard had to put a state back. */
    std::atomic<int>& liveStateRestores()
    {
        static std::atomic<int> count { 0 };
        return count;
    }

    /** The event log stays small and readable: once it passes this it starts again. */
    constexpr juce::int64 kEventsLogLimitBytes = 512 * 1024;

    /** A short fingerprint of a state block, so two captures of the same state can be
        told apart from two captures of different ones in the log. */
    juce::String stateHash (const juce::MemoryBlock& block)
    {
        juce::uint32 hash = 2166136261u;

        for (size_t i = 0; i < block.getSize(); ++i)
            hash = (hash ^ ((const juce::uint8*) block.getData())[i]) * 16777619u;

        return juce::String::toHexString ((juce::int64) hash).paddedLeft ('0', 8);
    }

    /** How long a plugin is given to finish its release, whatever it claims. */
    constexpr double kMinimumTailSeconds = 0.5;
    constexpr double kMaximumTailSeconds = 8.0;

    /** The centre of a 14 bit pitch wheel. */
    constexpr int kBendCentre = 8192;

    juce::String describeError (const juce::String& error)
    {
        return error.isNotEmpty() ? error : NI_TXT ("插件无法实例化。");
    }

    /** How an effect slot is named in an error message: its plugin name, or the
        path when it has never loaded. */
    juce::String describeSlot (const EffectSlot& slot)
    {
        return slot.name.isNotEmpty() ? slot.name : slot.pluginPath;
    }
}

std::function<void (const juce::String&)> PluginHostManager::traceHook;

//==============================================================================
HostedPlugin::HostedPlugin (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                            juce::String pluginName)
    : plugin (std::move (instanceToUse)),
      name (std::move (pluginName))
{
}

void HostedPlugin::prepare (double sampleRate, int maxBlockSize)
{
    if (plugin == nullptr)
        return;

    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto block = juce::jlimit (32, 16384, maxBlockSize);

    // Only the first prepare may touch the bus layout. Re-configuring the buses
    // means deactivating and reactivating the plugin, and some instruments (Kontakt
    // above all) re-initialise their whole engine there - which is how a device
    // restart used to reset an instrument's own volume. The layout has nothing to
    // do with the sample rate, so asking once is enough.
    if (! layoutApplied)
    {
        applyPreferredBusesLayout();
        layoutApplied = true;
    }

    plugin->setRateAndBufferSizeDetails (currentSampleRate, block);
    plugin->prepareToPlay (currentSampleRate, block);

    const auto pluginChannels = juce::jmax (1, plugin->getTotalNumOutputChannels());
    scratch.setSize (pluginChannels, block, false, true, true);
    scratch.clear();

    channels.reset();
    channelNote.fill (-1);
    midi.clear();
    rangeSent.fill (false);
    lastControllerValue.fill (0);
    controllerSent.fill (false);
    evictedNoteCount = 0;

    const auto tail = juce::jlimit (kMinimumTailSeconds, kMaximumTailSeconds,
                                    plugin->getTailLengthSeconds());
    tailSamplesPerNote = (juce::int64) std::ceil (tail * currentSampleRate);
    tailSamplesLeft = 0;
    prepared = true;
}

void HostedPlugin::applyPreferredBusesLayout()
{
    // An instrument wants to be heard: no audio input, and a stereo (or mono)
    // output. A plugin that refuses both keeps whatever it already had.
    auto layout = plugin->getBusesLayout();

    for (int i = 0; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.setUnchecked (i, juce::AudioChannelSet::disabled());

    auto setOutputs = [&layout] (const juce::AudioChannelSet& main)
    {
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.setUnchecked (i, i == 0 ? main
                                                       : juce::AudioChannelSet::disabled());
    };

    setOutputs (juce::AudioChannelSet::stereo());

    if (plugin->checkBusesLayoutSupported (layout) && plugin->setBusesLayout (layout))
    {
        layoutSummary = describeLayout();
        return;
    }

    setOutputs (juce::AudioChannelSet::mono());

    if (plugin->checkBusesLayoutSupported (layout))
        plugin->setBusesLayout (layout);

    layoutSummary = describeLayout();
}

void HostedPlugin::reset()
{
    channels.reset();
    channelNote.fill (-1);
    midi.clear();
    rangeSent.fill (false);
    lastControllerValue.fill (0);
    controllerSent.fill (false);
    evictedNoteCount = 0;
    tailSamplesLeft = 0;

    if (plugin != nullptr && prepared)
        plugin->reset();
}

void HostedPlugin::sendPitchBendRange (int channel)
{
    // RPN 0: without it the plugin would use its own bend range and every note
    // that is not exactly on a semitone would land somewhere else.
    for (const auto& message : vst::pitchBendRangeMessages())
        midi.addEvent (juce::MidiMessage::controllerEvent (channel + 1, message.controller,
                                                           message.value),
                       0);
}

void HostedPlugin::replayControllerState (int channel)
{
    for (int controller = 0; controller < 128; ++controller)
    {
        if (! controllerSent[(size_t) controller])
            continue;

        midi.addEvent (juce::MidiMessage::controllerEvent (channel + 1, controller,
                                                           lastControllerValue[(size_t) controller]),
                       0);
    }
}

void HostedPlugin::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (plugin == nullptr || ! prepared)
        return;

    const auto pitch = vst::midiPitchForHz ((double) frequencyHz);
    const auto allocation = channels.allocate (noteId);
    const auto channel = allocation.channel;

    if (channel < 0)
        return;

    if (allocation.evictedNoteId != 0)
    {
        // Every channel was busy, so this one was taken from the note that has been
        // held the longest. Let that note go with a real note-off: it ends the way a
        // player would end it (its release and its reverb still decay) instead of
        // being left to sound under this note's bend. Its own note-off, when it
        // arrives, finds no channel and does nothing - it must not cut this note.
        const auto evictedNote = channelNote[(size_t) channel] >= 0
                                     ? channelNote[(size_t) channel] : 60;

        midi.addEvent (juce::MidiMessage::noteOff (channel + 1, evictedNote), 0);
        channelNote[(size_t) channel] = -1;
        ++evictedNoteCount;
    }

    if (! rangeSent[(size_t) channel])
    {
        sendPitchBendRange (channel);
        rangeSent[(size_t) channel] = true;
    }

    // A pedal or an expression curve pressed before this note has to be in place on
    // the channel this note is about to use - the channels are per note, so the
    // curve's last value travels with each of them.
    replayControllerState (channel);

    // The bend has to be in place before the note starts.
    midi.addEvent (juce::MidiMessage::pitchWheel (channel + 1,
                                                  vst::bendValueFor (pitch.cents) + kBendCentre),
                   0);

    // Velocity 0 would be read as a note off, so a note always speaks.
    const auto midiVelocity = juce::jlimit (1, 127, (int) std::lround (velocity));
    midi.addEvent (juce::MidiMessage::noteOn (channel + 1, pitch.note,
                                              (juce::uint8) midiVelocity),
                   0);

    channelNote[(size_t) channel] = pitch.note;
    tailSamplesLeft = 0;
}

void HostedPlugin::noteOff (juce::int64 noteId)
{
    if (plugin == nullptr || ! prepared)
        return;

    const auto channel = channels.channelFor (noteId);

    if (channel < 0)
        return;

    const auto note = channelNote[(size_t) channel] >= 0 ? channelNote[(size_t) channel]
                                                         : 60;
    midi.addEvent (juce::MidiMessage::noteOff (channel + 1, note), 0);
    channelNote[(size_t) channel] = -1;
    channels.release (noteId);

    if (channels.activeCount() == 0)
        tailSamplesLeft = tailSamplesPerNote;
}

void HostedPlugin::sendController (int controllerNumber, int value)
{
    if (plugin == nullptr || ! prepared)
        return;

    const auto controller = juce::jlimit (0, 127, controllerNumber);

    // The controllers this host uses for its own bookkeeping (the pitch bend range
    // RPN, all notes off) are never sent from a drawn curve: doing so would fight
    // the host's own messages. The lane does not offer them either.
    if (controllers::isReservedController (controller))
        return;

    const auto clamped = juce::jlimit (0, 127, value);

    // Remembered so a note that starts later - on a channel of its own - gets the
    // same state before it speaks (see `replayControllerState`).
    controllerSent[(size_t) controller] = true;
    lastControllerValue[(size_t) controller] = clamped;

    auto sentAny = false;

    // A drawn curve belongs to the notes that are playing, so it goes to the
    // channels they are on. With nothing playing, channel 1 is the conventional
    // place for "this is the state of the instrument" - a pedal pressed before the
    // first note, say.
    for (int channel = 0; channel < vst::kMidiChannels; ++channel)
    {
        if (channelNote[(size_t) channel] < 0)
            continue;

        midi.addEvent (juce::MidiMessage::controllerEvent (channel + 1, controller, clamped), 0);
        sentAny = true;
    }

    if (! sentAny)
        midi.addEvent (juce::MidiMessage::controllerEvent (1, controller, clamped), 0);
}

void HostedPlugin::releaseHeldNotes()
{
    if (plugin == nullptr || ! prepared)
        return;

    // A real note-off per held note, exactly as if the player lifted their hands.
    // No CC123 here: that is the hard stop (allNotesOff), and many plugins treat it
    // as "be quiet now", which would cut the release and the plugin's own reverb.
    for (int channel = 0; channel < vst::kMidiChannels; ++channel)
    {
        if (channelNote[(size_t) channel] < 0)
            continue;

        midi.addEvent (juce::MidiMessage::noteOff (channel + 1,
                                                   channelNote[(size_t) channel]),
                       0);
        channelNote[(size_t) channel] = -1;
    }

    channels.reset();

    // The tail is deliberately kept: isActive() stays true while it rings, so the
    // plugin keeps being rendered until it really has finished.
    if (tailSamplesPerNote > 0)
        tailSamplesLeft = tailSamplesPerNote;
}

void HostedPlugin::allNotesOff()
{
    if (plugin == nullptr || ! prepared)
        return;

    // Only the channels that are really holding something: a sweep of all sixteen
    // controllers to a plugin that is not expecting them is exactly the kind of
    // thing that changes a sound behind the user's back.
    for (int channel = 0; channel < vst::kMidiChannels; ++channel)
    {
        if (channelNote[(size_t) channel] < 0)
            continue;

        midi.addEvent (juce::MidiMessage::noteOff (channel + 1,
                                                   channelNote[(size_t) channel]),
                       0);
        channelNote[(size_t) channel] = -1;

        // Belt and braces: a plugin that tracks its own voices honours this even
        // for a note this side has lost track of.
        midi.addEvent (juce::MidiMessage::allNotesOff (channel + 1), 0);
    }

    channels.reset();
    tailSamplesLeft = 0;
}

void HostedPlugin::renderAdding (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                 float gainLeft, float gainRight)
{
    if (plugin == nullptr || ! prepared || numSamples <= 0
        || scratch.getNumSamples() < numSamples)
    {
        midi.clear();
        return;
    }

    // The buffer handed to the plugin is exactly as long as the block: plugins
    // are allowed to read its length. It is a view of the scratch, so no
    // allocation happens on the audio thread.
    juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(),
                                    scratch.getNumChannels(), 0, numSamples);
    block.clear();

    plugin->processBlock (block, midi);
    midi.clear();

    const auto pluginChannels = block.getNumChannels();
    const auto mixChannels = juce::jmin (2, buffer.getNumChannels());

    for (int channel = 0; channel < mixChannels; ++channel)
    {
        const auto source = juce::jmin (channel, pluginChannels - 1);
        const auto gain = channel == 0 ? gainLeft : gainRight;

        if (source >= 0)
            buffer.addFrom (channel, startSample, block, source, 0, numSamples, gain);
    }

    if (channels.activeCount() == 0 && tailSamplesLeft > 0)
        tailSamplesLeft = juce::jmax ((juce::int64) 0, tailSamplesLeft - numSamples);
}

bool HostedPlugin::isActive() const noexcept
{
    return plugin != nullptr && prepared
               && (channels.activeCount() > 0 || tailSamplesLeft > 0);
}

juce::MemoryBlock HostedPlugin::captureState() const
{
    juce::MemoryBlock block;

    if (plugin != nullptr)
        plugin->getStateInformation (block);

    return block;
}

//==============================================================================
HostedEffectPlugin::HostedEffectPlugin (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                                        juce::String pluginName, bool shouldBypass)
    : plugin (std::move (instanceToUse)),
      name (std::move (pluginName)),
      bypass (shouldBypass)
{
}

void HostedEffectPlugin::applyPreferredBusesLayout()
{
    // An effect takes the track's stereo signal and gives it back: both the main
    // input and the main output want the same layout. A plugin that refuses stereo
    // gets mono; one that refuses both keeps whatever it had.
    auto layout = plugin->getBusesLayout();

    auto setMainBuses = [&layout] (const juce::AudioChannelSet& set)
    {
        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.setUnchecked (i, i == 0 ? set : juce::AudioChannelSet::disabled());

        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.setUnchecked (i, i == 0 ? set : juce::AudioChannelSet::disabled());
    };

    setMainBuses (juce::AudioChannelSet::stereo());

    if (plugin->checkBusesLayoutSupported (layout) && plugin->setBusesLayout (layout))
    {
        layoutSummary = describeLayout();
        return;
    }

    setMainBuses (juce::AudioChannelSet::mono());

    if (plugin->checkBusesLayoutSupported (layout))
        plugin->setBusesLayout (layout);

    layoutSummary = describeLayout();
}

void HostedEffectPlugin::prepare (double sampleRate, int maxBlockSize)
{
    if (plugin == nullptr)
        return;

    const auto block = juce::jlimit (32, 16384, maxBlockSize);

    trace ("fx prepare: " + name + " at " + juce::String (sampleRate, 0)
               + " Hz, block " + juce::String (block));

    // Same rule as an instrument's: the layout is a property of the plugin, not of
    // the device, so it is arranged once and never touched again.
    if (! layoutApplied)
    {
        applyPreferredBusesLayout();
        layoutApplied = true;
    }

    trace ("fx layout: in " + juce::String (plugin->getTotalNumInputChannels())
               + ", out " + juce::String (plugin->getTotalNumOutputChannels()));

    plugin->setRateAndBufferSizeDetails (sampleRate > 0.0 ? sampleRate : 48000.0, block);
    plugin->prepareToPlay (sampleRate > 0.0 ? sampleRate : 48000.0, block);

    // The buffer a plugin sees is as wide as the wider of its two sides, not their
    // sum: the first `inputs` channels hold what comes in and the plugin writes its
    // output over the first `outputs` channels (JUCE's in-place convention).
    const auto channels = juce::jmax (1, juce::jmax (plugin->getTotalNumInputChannels(),
                                                     plugin->getTotalNumOutputChannels()));

    scratch.setSize (channels, block, false, true, true);
    scratch.clear();
    midi.clear();

    latencySamples = juce::jmax (0, plugin->getLatencySamples());
    prepared = true;

    trace ("fx ready: " + name + " buffer " + juce::String (scratch.getNumChannels())
               + " ch, latency " + juce::String (latencySamples));
}

void HostedEffectPlugin::reset()
{
    if (plugin != nullptr && prepared)
        plugin->reset();
}

void HostedEffectPlugin::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! firstBlockTraced)
    {
        firstBlockTraced = true;
        trace ("fx first block: " + name + " samples " + juce::String (numSamples)
                   + ", host buffer " + juce::String (scratch.getNumChannels())
                   + " ch, audio " + juce::String (buffer.getNumChannels()) + " ch");
    }

    if (plugin == nullptr || ! prepared || bypass.load() || numSamples <= 0)
        return;

    const auto inputs  = plugin->getTotalNumInputChannels();
    const auto outputs = plugin->getTotalNumOutputChannels();

    if (inputs <= 0 || outputs <= 0 || scratch.getNumSamples() < numSamples)
        return;

    juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(),
                                    scratch.getNumChannels(), 0, numSamples);
    block.clear();

    // The track's audio goes into the plugin's input channels...
    const auto channels = juce::jmin (2, buffer.getNumChannels());
    const auto inputsToCopy = juce::jmin (channels, inputs);

    for (int channel = 0; channel < inputsToCopy; ++channel)
        block.copyFrom (channel, 0, buffer, channel, startSample, numSamples);

    midi.clear();
    plugin->processBlock (block, midi);

    // ...and the result is in the first `outputs` channels, in place.
    const auto outputsToCopy = juce::jmin (channels, outputs);

    for (int channel = 0; channel < outputsToCopy; ++channel)
        buffer.copyFrom (channel, startSample, block, channel, 0, numSamples);
}

juce::MemoryBlock HostedEffectPlugin::captureState() const
{
    juce::MemoryBlock block;

    if (plugin != nullptr)
        plugin->getStateInformation (block);

    return block;
}

//==============================================================================
PluginHostManager::PluginHostManager()
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    vst3Format = dynamic_cast<juce::VST3PluginFormat*> (formatManager.getFormat (0));
    listFile = getDefaultListFile();
    lifetimeGuard = std::make_shared<PluginHostManager*> (this);
    loadCache();
}

PluginHostManager::~PluginHostManager()
{
    // A scan that is still running must not call back into a manager that is
    // being destroyed. The audio device is already stopped by this point, which
    // is what makes it safe for the instances to go now.
    *lifetimeGuard = nullptr;
    statusText.clear();
}

juce::File PluginHostManager::getDefaultListFile()
{
    if (defaultListFileOverride() != juce::File())
        return defaultListFileOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HMDAW")
               .getChildFile ("pluginlist.xml");
}

void PluginHostManager::setDefaultListFileForTesting (juce::File file)
{
    defaultListFileOverride() = std::move (file);
}

juce::File PluginHostManager::getEventsLogFile()
{
    if (eventsLogFileOverride() != juce::File())
        return eventsLogFileOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HMDAW")
               .getChildFile ("plugin-events.log");
}

int PluginHostManager::getOfflineThreadViolationsForTest() noexcept
{
    return offlineThreadViolations().load();
}

int PluginHostManager::getLiveStateRestoresForTest() noexcept
{
    return liveStateRestores().load();
}

void PluginHostManager::setEventsLogFileForTesting (juce::File file)
{
    eventsLogFileOverride() = std::move (file);
}

void PluginHostManager::logEvent (const juce::String& text) const
{
    // Everything that can write into a plugin's own sound happens on the message
    // thread, so that is where this is written from and no lock is needed. It exists
    // so "the plugin changed by itself" can be answered with evidence: a run that
    // never logs a state write or a recreate cannot have been changed by HM.
    const auto file = getEventsLogFile();

    if (file.getSize() > kEventsLogLimitBytes)
        file.deleteFile();

    file.getParentDirectory().createDirectory();
    file.appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + text
                         + juce::newLine);
}

juce::FileSearchPath PluginHostManager::searchPath() const
{
    juce::FileSearchPath path;

    if (vst3Format != nullptr)
        path = vst3Format->getDefaultLocationsToSearch();

    for (const auto& folder : extraFolders)
        path.addIfNotAlreadyThere (folder);

    return path;
}

void PluginHostManager::addExtraFolder (const juce::File& folder)
{
    const auto path = folder.getFullPathName();

    if (path.isEmpty() || extraFolders.contains (path))
        return;

    extraFolders.add (path);
    saveCache();
}

bool PluginHostManager::loadCache()
{
    const auto foldersFile = getFoldersFile();

    if (foldersFile.existsAsFile())
    {
        const auto parsed = juce::JSON::parse (foldersFile.loadFileAsString());

        // The file used to be a plain array of folders; it is an object now so the
        // picker's grouping choice can live next to them. Both shapes load.
        if (auto* array = parsed.getArray())
            for (const auto& value : *array)
                extraFolders.addIfNotAlreadyThere (value.toString());

        if (auto* object = parsed.getDynamicObject())
        {
            if (auto* array = object->getProperty ("folders").getArray())
                for (const auto& value : *array)
                    extraFolders.addIfNotAlreadyThere (value.toString());

            groupingAxis = pluginGroupAxisFromText (object->getProperty ("groupBy").toString(),
                                                    groupingAxis);
        }
    }

    if (! listFile.existsAsFile())
        return false;

    const auto xml = juce::XmlDocument::parse (listFile);

    if (xml == nullptr)
        return false;

    knownList.recreateFromXml (*xml);
    return true;
}

bool PluginHostManager::saveCache() const
{
    juce::Array<juce::var> folders;

    for (const auto& folder : extraFolders)
        folders.add (folder);

    auto* settings = new juce::DynamicObject();
    settings->setProperty ("folders", juce::var (folders));
    settings->setProperty ("groupBy", pluginGroupAxisToText (groupingAxis));

    getFoldersFile().getParentDirectory().createDirectory();
    getFoldersFile().replaceWithText (juce::JSON::toString (juce::var (settings)));

    if (auto xml = knownList.createXml())
    {
        listFile.getParentDirectory().createDirectory();
        return xml->writeTo (listFile);
    }

    return false;
}

void PluginHostManager::setGroupingAxis (PluginGroupAxis axis)
{
    if (groupingAxis == axis)
        return;

    groupingAxis = axis;
    saveCache();
}

void PluginHostManager::mergeScannedPlugins (const juce::Array<juce::PluginDescription>& found,
                                             const juce::StringArray& failedFiles)
{
    for (const auto& type : found)
        knownList.addType (type);

    auto text = NI_TXT ("扫描完成：找到 ") + juce::String (knownList.getNumTypes())
                    + NI_TXT (" 个 VST3 插件");

    if (! failedFiles.isEmpty())
        text += NI_TXT ("（") + juce::String (failedFiles.size())
                    + NI_TXT (" 个文件读取失败）");

    setScanStatus (text);
    saveCache();
}

juce::String PluginHostManager::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

void PluginHostManager::setScanStatus (const juce::String& text)
{
    const juce::ScopedLock sl (statusLock);
    statusText = text;
}

void PluginHostManager::cancelScan()
{
    scanCancelled.store (true);
}

scanproto::Result PluginHostManager::scanFileWithChild (const juce::File& file,
                                                        bool honourCancel) const
{
    scanproto::Result outcome;

    scanproto::Request request;
    request.pluginFile = file;
    request.resultFile = juce::File::createTempFile (".xml");

    for (const auto& argument : scanChildExtraArgs)
    {
        if (argument == scanproto::kFailSwitch)
            request.failForTest = true;
        else if (argument == scanproto::kHangSwitch)
            request.hangForTest = true;
    }

    // A copy of HM does the loading. It never opens a window or an audio device, and
    // if the plugin inside it does anything fatal, this is where HM stops caring.
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto arguments = scanproto::makeChildArguments (request);
    arguments.insert (0, executable.getFullPathName());

    juce::ChildProcess child;

    // No pipes on the child's output: a plugin that prints (they do) must not be able
    // to fill a pipe nobody is reading and hang there instead of dying.
    if (! child.start (arguments, 0))
    {
        outcome.error = NI_TXT ("无法启动扫描子进程。");
        request.resultFile.deleteFile();
        return outcome;
    }

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) scanTimeoutMs;

    while (child.isRunning())
    {
        if ((honourCancel && scanCancelled.load())
            || juce::Time::getMillisecondCounter() > deadline)
        {
            child.kill();
            break;
        }

        juce::Thread::sleep (20);
    }

    child.waitForProcessToFinish (2000);

    const auto text = request.resultFile.existsAsFile() ? request.resultFile.loadFileAsString()
                                                        : juce::String();
    request.resultFile.deleteFile();

    if (! scanproto::parse (text, outcome))
    {
        outcome.error = honourCancel && scanCancelled.load()
                            ? NI_TXT ("扫描已取消。")
                            : NI_TXT ("这个插件在被读取时没有回答（已跳过）。");
    }

    return outcome;
}

juce::Array<juce::PluginDescription> PluginHostManager::scanWithChildProcesses (
    const juce::FileSearchPath& folders, bool recursive, juce::StringArray& failedFiles)
{
    juce::Array<juce::PluginDescription> result;
    failedFiles.clear();

    // A cancel from an earlier scan (a picker closing, a project load) must not carry
    // into this one: the flag is a request to stop what is running, not a state the
    // host stays in.
    scanCancelled.store (false);

    if (vst3Format == nullptr)
    {
        failedFiles.add (NI_TXT ("（这个版本没有编进 VST3 支持）"));
        return result;
    }

    // Only the file *list* comes from this process: enumerating bundles does not load
    // them. Everything that does load one happens in a child.
    const auto files = vst3Format->searchPathsForPlugins (folders, recursive, false);
    const auto total = files.size();

    for (const auto& path : files)
    {
        if (scanCancelled.load())
            break;

        const juce::File file (path);

        setScanStatus (NI_TXT ("正在扫描：") + file.getFileName()
                           + NI_TXT ("（") + juce::String (result.size() + failedFiles.size() + 1)
                           + "/" + juce::String (total) + NI_TXT ("）"));

        const auto outcome = scanFileWithChild (file, true);

        if (scanproto::hasFailed (outcome))
        {
            failedFiles.add (path);
            continue;
        }

        result.addArray (outcome.plugins);
    }

    return result;
}

void PluginHostManager::startScan (std::function<void()> onFinished)
{
    if (scanning)
        return;

    scanning = true;
    scanCancelled.store (false);
    setScanStatus (NI_TXT ("正在扫描 VST3 插件…"));

    const auto folders = searchPath();
    auto guard = lifetimeGuard;

    juce::Thread::launch ([this, guard, folders, onFinished]
    {
        // The list stays local while the scan runs: the message thread reads the
        // manager's list to fill the picker, and a half-scanned list would make
        // the entries appear and disappear under the user's cursor.
        juce::StringArray failed;
        const auto found = scanWithChildProcesses (folders, true, failed);

        if (guard == nullptr || *guard == nullptr)
            return;

        juce::MessageManager::callAsync ([this, guard, found, failed, onFinished]
        {
            if (guard == nullptr || *guard != this)
                return;

            scanning = false;
            mergeScannedPlugins (found, failed);

            if (onFinished)
                onFinished();
        });
    });
}

int PluginHostManager::scanFolderNow (const juce::File& folder, juce::String& error,
                                      int* failedFiles)
{
    error.clear();

    if (vst3Format == nullptr)
    {
        error = NI_TXT ("VST3 支持没有编进这个版本。");
        return 0;
    }

    if (! folder.isDirectory())
    {
        error = NI_TXT ("目录不存在：") + folder.getFullPathName();
        return 0;
    }

    juce::StringArray failed;
    const auto found = scanWithChildProcesses (juce::FileSearchPath (folder.getFullPathName()),
                                               false, failed);

    for (const auto& type : found)
        knownList.addType (type);

    if (failedFiles != nullptr)
        *failedFiles = failed.size();

    return found.size();
}

std::unique_ptr<juce::PluginDescription> PluginHostManager::describeFile (const juce::File& file) const
{
    for (const auto& type : knownList.getTypes())
        if (juce::File (type.fileOrIdentifier) == file)
            return std::make_unique<juce::PluginDescription> (type);

    if (vst3Format == nullptr || ! file.exists())
        return nullptr;

    // Even this one file goes through a child: "read this one plugin's description"
    // is a load, and a load is what kills processes when the plugin is broken.
    // One file, read on demand (a project that names a plugin the cache does not
    // know, an insert slot being loaded): not a scan, so a cancel aimed at a scan
    // must not touch it.
    const auto found = scanFileWithChild (file, false);

    if (found.plugins.isEmpty())
        return nullptr;

    return std::make_unique<juce::PluginDescription> (found.plugins.getFirst());
}

std::unique_ptr<juce::PluginDescription> PluginHostManager::findDescription (const VstParams& params) const
{
    if (! params.hasPlugin())
        return nullptr;

    const juce::File file (params.pluginPath);

    auto matches = [&params] (const juce::PluginDescription& type)
    {
        if (params.pluginUid.isNotEmpty()
            && juce::String ((juce::int64) type.uniqueId) == params.pluginUid)
            return true;

        return params.name.isNotEmpty()
                   && (type.name == params.name || type.descriptiveName == params.name);
    };

    for (const auto& type : knownList.getTypes())
        if (juce::File (type.fileOrIdentifier) == file && matches (type))
            return std::make_unique<juce::PluginDescription> (type);

    // The bundle moved: the same plugin may live somewhere else on this machine.
    for (const auto& type : knownList.getTypes())
        if (matches (type))
            return std::make_unique<juce::PluginDescription> (type);

    return describeFile (file);
}

void PluginHostManager::prepareAll (double sampleRate, int maxBlockSize)
{
    const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto block = juce::jlimit (32, 16384, maxBlockSize);
    const auto settingsChanged = rate != currentSampleRate || block != currentBlockSize;

    // Re-preparing a plugin can make it re-initialise itself: when the audio device
    // restarts with a different rate or block size, the plugin is deactivated and
    // activated again, and some samplers (Kontakt above all) come back with their
    // instrument volume reset. The user changed an audio device, not their sound, so
    // the state is read before and put back if it moved - the same guard the export
    // uses, for the same reason.
    std::vector<std::pair<juce::int64, juce::MemoryBlock>> before;

    if (settingsChanged)
        for (const auto& entry : trackStates)
            if (entry.second.plugin != nullptr)
                before.emplace_back (entry.first, entry.second.plugin->captureState());

    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    currentBlockSize = juce::jlimit (32, 16384, maxBlockSize);

    for (auto& entry : trackStates)
        if (entry.second.plugin != nullptr)
        {
            entry.second.plugin->prepare (currentSampleRate, currentBlockSize);
            logEvent ("prepare   track=" + juce::String (entry.first) + "  rate="
                          + juce::String (currentSampleRate, 0) + "  block="
                          + juce::String (currentBlockSize) + "  layout=\""
                          + entry.second.plugin->getLayoutSummary() + "\"");
        }

    // Put the state back unconditionally, rather than comparing: a plugin that resets
    // itself in prepareToPlay tells the host about it through the parameter queue,
    // which has not been delivered yet at this point - the comparison would see "no
    // change" and let the reset stand. Writing the same state back is a no-op for a
    // plugin that behaved, and it is the only thing that is certain for one that did
    // not. It happens only when the rate or block size really changed.
    for (const auto& entry : before)
    {
        auto plugin = pluginFor (entry.first);

        if (plugin == nullptr || entry.second.getSize() == 0)
            continue;

        plugin->getInstance().setStateInformation (entry.second.getData(),
                                                  (int) entry.second.getSize());
        liveStateRestores().fetch_add (1);
        logEvent ("state-restored  track=" + juce::String (entry.first)
                      + "  (device settings changed: the plugin was re-prepared, so its"
                        " sound was put back)");
    }
}

PluginHostManager::TrackState& PluginHostManager::stateFor (juce::int64 trackId)
{
    for (auto& entry : trackStates)
        if (entry.first == trackId)
            return entry.second;

    trackStates.emplace_back (trackId, TrackState{});
    return trackStates.back().second;
}

std::shared_ptr<HostedInstrument> PluginHostManager::instrumentFor (juce::int64 trackId,
                                                                   const VstParams& params)
{
    if (! params.hasPlugin())
        return nullptr;

    const auto& state = stateFor (trackId);

    if (state.plugin != nullptr && state.matches (params))
        return state.plugin;

    // Not up yet (or the track switched plugins): start the load and let the
    // sound arrive when it is ready.
    requestInstrument (trackId, params);
    return nullptr;
}

void PluginHostManager::requestInstrument (juce::int64 trackId, const VstParams& params)
{
    if (! params.hasPlugin())
        return;

    auto& state = stateFor (trackId);

    if (state.pending)
        return;

    if (state.plugin != nullptr && state.matches (params))
        return;

    // A plugin that could not be loaded is not retried on every rebuild; the same
    // failure would be reported again and again (and the rebuild that reporting
    // triggers would try once more, forever).
    if (state.hasFailedFor (params))
        return;

    auto description = findDescription (params);

    if (description == nullptr)
    {
        state.plugin.reset();
        state.requestedPath = params.pluginPath;
        state.requestedUid = params.pluginUid;
        state.failedPath = params.pluginPath;
        state.failedUid = params.pluginUid;
        state.error = NI_TXT ("找不到插件：") + params.pluginPath;

        if (onInstrumentChanged)
            onInstrumentChanged();

        return;
    }

    state.pending = true;
    state.error.clear();
    state.requestedPath = params.pluginPath;
    state.requestedUid = params.pluginUid;
    state.failedPath.clear();
    state.failedUid.clear();

    const auto descriptionCopy = *description;
    auto guard = lifetimeGuard;
    const auto requestGeneration = generation;

    formatManager.createPluginInstanceAsync (descriptionCopy, currentSampleRate, currentBlockSize,
        [this, guard, trackId, params, descriptionCopy, requestGeneration]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (guard == nullptr || *guard != this)
                return;

            // The project changed while this plugin was loading: the instance belongs
            // to a project that is no longer open, so it is dropped here (the unique
            // pointer does that) rather than being handed to whatever now owns the id.
            if (requestGeneration != generation)
                return;

            auto& target = stateFor (trackId);
            target.pending = false;

            auto load = std::make_unique<Load>();
            load->trackId = trackId;
            load->params = params;

            if (descriptionCopy.name.isNotEmpty())
                load->params.name = descriptionCopy.name;

            if (instance != nullptr)
            {
                auto hosted = std::make_shared<HostedPlugin> (std::move (instance),
                                                              load->params.name);
                hosted->prepare (currentSampleRate, currentBlockSize);

                if (params.stateBase64.isNotEmpty())
                {
                    juce::MemoryOutputStream stream;

                    if (juce::Base64::convertFromBase64 (stream, params.stateBase64))
                    {
                        hosted->getInstance().setStateInformation (stream.getData(),
                                                                   (int) stream.getDataSize());
                        logEvent ("state-in  track=" + juce::String (trackId) + "  plugin=\""
                                      + load->params.name + "\"  bytes="
                                      + juce::String ((int) stream.getDataSize()));
                    }
                }

                target.plugin = hosted;
                target.error.clear();
                target.failedPath.clear();
                target.failedUid.clear();
                load->plugin = hosted;

                logEvent ("create    track=" + juce::String (trackId) + "  plugin=\""
                              + load->params.name + "\"  rate="
                              + juce::String (currentSampleRate, 0) + "  block="
                              + juce::String (currentBlockSize) + "  layout=\""
                              + hosted->getLayoutSummary() + "\"");
            }
            else
            {
                target.plugin.reset();
                target.error = describeError (error);
                target.failedPath = params.pluginPath;
                target.failedUid = params.pluginUid;
                load->error = target.error;

                logEvent ("create-failed  track=" + juce::String (trackId) + "  error="
                              + target.error);
            }

            // Kept for the life of the app: see the note on PluginHostManager.
            loads.push_back (std::move (load));

            if (onInstrumentChanged)
                onInstrumentChanged();
        });
}

std::shared_ptr<HostedPlugin> PluginHostManager::pluginFor (juce::int64 trackId) const
{
    for (const auto& entry : trackStates)
        if (entry.first == trackId)
            return entry.second.plugin;

    return nullptr;
}

juce::String PluginHostManager::getLoadError (juce::int64 trackId) const
{
    for (const auto& entry : trackStates)
        if (entry.first == trackId)
            return entry.second.error;

    return {};
}

void PluginHostManager::forgetTrack (juce::int64 trackId)
{
    // The instance is *detached* rather than destroyed: the plugin window may still
    // be showing its editor, and a plugin that is alive is exactly what the class
    // comment promises. It stays in `loads` (which is never emptied) and goes away
    // with the app - or with the next project load, which is the one place that is
    // allowed to take instances down.
    for (auto& entry : trackStates)
    {
        if (entry.first != trackId)
            continue;

        if (entry.second.plugin != nullptr)
        {
            auto retired = std::make_unique<Load>();
            retired->trackId = trackId;
            retired->plugin = entry.second.plugin;
            retired->error = NI_TXT ("轨道已删除");
            loads.push_back (std::move (retired));
        }

        entry.second.plugin.reset();
        entry.second.requestedPath.clear();
        entry.second.requestedUid.clear();
        entry.second.failedPath.clear();
        entry.second.failedUid.clear();
        entry.second.error.clear();
        entry.second.pending = false;
        break;
    }

    logEvent ("forget    track=" + juce::String (trackId));

    // The insert chain goes with the track.
    for (auto& entry : effectStates)
    {
        if (entry.first.first != trackId)
            continue;

        if (entry.second.plugin != nullptr)
        {
            auto retired = std::make_unique<Load>();
            retired->trackId = trackId;
            retired->effect = entry.second.plugin;
            retired->error = NI_TXT ("轨道已删除");
            loads.push_back (std::move (retired));
        }

        entry.second.plugin.reset();
        entry.second.requestedPath.clear();
        entry.second.requestedUid.clear();
        entry.second.failedPath.clear();
        entry.second.failedUid.clear();
        entry.second.error.clear();
        entry.second.pending = false;
    }
}

void PluginHostManager::resetForProject()
{
    // A project load is the one moment where the live instances are wrong by
    // definition: they belong to the project that was open before. Bumping the
    // generation first means a load that is already in flight is dropped when it
    // comes back (see requestInstrument) instead of installing an instance from the
    // old project into the new one.
    ++generation;
    scanCancelled.store (true);

    trackStates.clear();
    effectStates.clear();

    // Unlike forgetTrack, this really does unload: the editors are closed before
    // this is called (MainComponent does that), so nothing can be left pointing at a
    // plugin that is about to go.
    loads.clear();

    logEvent ("reset     project");
}

void PluginHostManager::captureStateFor (juce::int64 trackId, Project& project)
{
    auto* track = project.findTrack (trackId);

    if (track == nullptr || track->kind != TrackKind::vst)
        return;

    const auto plugin = pluginFor (trackId);

    if (plugin == nullptr)
    {
        track->vst.loadedOk = false;
        return;
    }

    const auto block = plugin->captureState();

    // An empty block is not a state: some plugins answer with nothing while they are
    // still opening a library, and writing that over a good state would take the
    // user's sound with it.
    if (block.getSize() == 0)
        return;

    track->vst.stateBase64 = juce::Base64::toBase64 (block.getData(), block.getSize());
    track->vst.name = plugin->getName();
    track->vst.loadedOk = true;

    logEvent ("state-out track=" + juce::String (trackId) + "  plugin=\"" + track->vst.name
                  + "\"  bytes=" + juce::String ((int) block.getSize())
                  + "  hash=" + stateHash (block));
}

void PluginHostManager::captureStates (Project& project)
{
    for (auto& track : project.tracks)
    {
        if (track.kind != TrackKind::vst)
            continue;

        auto* state = (TrackState*) nullptr;

        for (auto& entry : trackStates)
            if (entry.first == track.id)
                state = &entry.second;

        if (state == nullptr || state->plugin == nullptr)
        {
            track.vst.loadedOk = false;
            continue;
        }

        const auto block = state->plugin->captureState();

        if (block.getSize() == 0)
            continue;   // never replace a good state with an empty one

        track.vst.stateBase64 = juce::Base64::toBase64 (block.getData(), block.getSize());
        track.vst.name = state->plugin->getName();
        track.vst.loadedOk = true;

        logEvent ("state-out track=" + juce::String (track.id) + "  plugin=\""
                      + track.vst.name + "\"  bytes=" + juce::String ((int) block.getSize())
                      + "  hash=" + stateHash (block));
    }

    captureEffectStates (project);
}

int PluginHostManager::restoreStatesChangedSince (const Project& before)
{
    auto putBackIfMoved = [this] (const juce::String& label, const juce::String& wantedBase64,
                                  juce::AudioPluginInstance* instance) -> bool
    {
        if (instance == nullptr || wantedBase64.isEmpty())
            return false;

        juce::MemoryOutputStream wanted;

        if (! juce::Base64::convertFromBase64 (wanted, wantedBase64))
            return false;

        juce::MemoryBlock now;
        instance->getStateInformation (now);

        if (now.getSize() == wanted.getDataSize()
            && std::memcmp (now.getData(), wanted.getData(), now.getSize()) == 0)
            return false;   // nothing moved: the healthy case

        instance->setStateInformation (wanted.getData(), (int) wanted.getDataSize());
        liveStateRestores().fetch_add (1);
        logEvent ("state-restored  " + label + "  (the export had moved its state: "
                      + juce::String ((int) now.getSize()) + " bytes -> "
                      + juce::String ((int) wanted.getDataSize()) + ")");
        return true;
    };

    auto restored = 0;

    for (const auto& track : before.tracks)
    {
        if (track.kind == TrackKind::vst)
        {
            if (auto plugin = pluginFor (track.id))
                if (putBackIfMoved ("track=" + juce::String (track.id) + " \"" + track.name + "\"",
                                    track.vst.stateBase64, &plugin->getInstance()))
                    ++restored;
        }

        for (int i = 0; i < (int) track.effects.size(); ++i)
        {
            const auto& slot = track.effects[(size_t) i];

            if (slot.stateBase64.isEmpty())
                continue;

            if (auto effect = effectPluginFor (track.id, i))
                if (putBackIfMoved ("track=" + juce::String (track.id) + " slot "
                                        + juce::String (i),
                                    slot.stateBase64, &effect->getInstance()))
                    ++restored;
        }
    }

    return restored;
}

std::shared_ptr<HostedInstrument> PluginHostManager::makeOfflineInstrument (const VstParams& params,
                                                                           double sampleRate,
                                                                           int blockSize,
                                                                           juce::String& error) const
{
    error.clear();

    if (! params.hasPlugin())
        return nullptr;

    auto description = findDescription (params);

    if (description == nullptr)
    {
        error = NI_TXT ("找不到插件：") + params.pluginPath;
        return nullptr;
    }

    return makeOfflineInstrument (*description, params, sampleRate, blockSize, error);
}

std::shared_ptr<HostedInstrument> PluginHostManager::makeOfflineInstrument (
    const juce::PluginDescription& description, const VstParams& params,
    double sampleRate, int blockSize, juce::String& error) const
{
    error.clear();

    // Nothing about a plugin may be touched off the message thread: loading it,
    // preparing it and handing it its state are all message-thread work (JUCE's own
    // VST3 host asserts on prepareToPlay and setStateInformation). The export calls
    // back to the message thread for exactly this reason; the counter is here so a
    // regression shows up in the self test rather than in somebody's export.
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        offlineThreadViolations().fetch_add (1);

    logEvent ("offline-instrument  plugin=\"" + description.name + "\"  rate="
                  + juce::String (sampleRate, 0) + "  block=" + juce::String (blockSize)
                  + "  onMessageThread="
                  + (juce::MessageManager::getInstance()->isThisTheMessageThread() ? "yes"
                                                                                  : "no"));

    // The caller is responsible for being on the message thread (the export marshals
    // here with MessageManager::callSync); the render thread only ever calls
    // processBlock on the instance it was handed.
    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance == nullptr)
    {
        error = describeError (error);
        return nullptr;
    }

    auto hosted = std::make_shared<HostedPlugin> (std::move (instance), params.name);
    hosted->prepare (sampleRate, blockSize);

    if (params.stateBase64.isNotEmpty())
    {
        juce::MemoryOutputStream stream;

        if (juce::Base64::convertFromBase64 (stream, params.stateBase64))
            hosted->getInstance().setStateInformation (stream.getData(),
                                                       (int) stream.getDataSize());
    }

    return hosted;
}

//==============================================================================
PluginHostManager::EffectState& PluginHostManager::effectStateFor (juce::int64 trackId,
                                                                   int slotIndex)
{
    for (auto& entry : effectStates)
        if (entry.first.first == trackId && entry.first.second == slotIndex)
            return entry.second;

    effectStates.push_back ({ { trackId, slotIndex }, EffectState{} });
    return effectStates.back().second;
}

void PluginHostManager::requestEffect (juce::int64 trackId, int slotIndex, const EffectSlot& slot)
{
    if (! slot.hasPlugin())
        return;

    auto& state = effectStateFor (trackId, slotIndex);

    if (state.pending || (state.plugin != nullptr && state.matches (slot))
        || state.hasFailedFor (slot))
        return;

    auto description = findEffectDescription (slot);

    if (description == nullptr)
    {
        state.plugin.reset();
        state.requestedPath = slot.pluginPath;
        state.requestedUid = slot.pluginUid;
        state.failedPath = slot.pluginPath;
        state.failedUid = slot.pluginUid;
        state.error = NI_TXT ("找不到插件：") + describeSlot (slot);

        if (onInstrumentChanged)
            onInstrumentChanged();

        return;
    }

    state.pending = true;
    state.error.clear();
    state.requestedPath = slot.pluginPath;
    state.requestedUid = slot.pluginUid;
    state.failedPath.clear();
    state.failedUid.clear();

    const auto descriptionCopy = *description;
    auto guard = lifetimeGuard;
    const auto requestGeneration = generation;

    formatManager.createPluginInstanceAsync (descriptionCopy, currentSampleRate, currentBlockSize,
        [this, guard, trackId, slotIndex, slot, descriptionCopy, requestGeneration]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (guard == nullptr || *guard != this)
                return;

            if (requestGeneration != generation)
                return;   // the project changed while this was loading

            auto& target = effectStateFor (trackId, slotIndex);
            target.pending = false;

            auto load = std::make_unique<Load>();
            load->trackId = trackId;
            load->params.name = descriptionCopy.name.isNotEmpty() ? descriptionCopy.name : slot.name;

            if (instance != nullptr)
            {
                auto hosted = std::make_shared<HostedEffectPlugin> (std::move (instance),
                                                                    load->params.name,
                                                                    slot.bypass);
                hosted->prepare (currentSampleRate, currentBlockSize);

                if (slot.stateBase64.isNotEmpty())
                {
                    juce::MemoryOutputStream stream;

                    if (juce::Base64::convertFromBase64 (stream, slot.stateBase64))
                    {
                        hosted->getInstance().setStateInformation (stream.getData(),
                                                                   (int) stream.getDataSize());
                        logEvent ("fx-state-in  track=" + juce::String (trackId) + " slot="
                                      + juce::String (slotIndex) + "  bytes="
                                      + juce::String ((int) stream.getDataSize()));
                    }
                }

                target.plugin = hosted;
                target.error.clear();
                target.failedPath.clear();
                target.failedUid.clear();
                load->effect = hosted;

                logEvent ("fx-create  track=" + juce::String (trackId) + " slot="
                              + juce::String (slotIndex) + "  plugin=\""
                              + load->params.name + "\"");
            }
            else
            {
                target.plugin.reset();
                target.error = describeError (error);
                target.failedPath = slot.pluginPath;
                target.failedUid = slot.pluginUid;

                logEvent ("fx-create-failed  track=" + juce::String (trackId) + " slot="
                              + juce::String (slotIndex) + "  error=" + target.error);
            }

            // Kept for the life of the app, exactly like an instrument instance.
            loads.push_back (std::move (load));

            if (onInstrumentChanged)
                onInstrumentChanged();
        });
}

std::shared_ptr<HostedEffectPlugin> PluginHostManager::effectPluginFor (juce::int64 trackId,
                                                                        int slotIndex) const
{
    for (const auto& entry : effectStates)
        if (entry.first.first == trackId && entry.first.second == slotIndex)
            return entry.second.plugin;

    return nullptr;
}

juce::String PluginHostManager::getEffectError (juce::int64 trackId, int slotIndex) const
{
    for (const auto& entry : effectStates)
        if (entry.first.first == trackId && entry.first.second == slotIndex)
            return entry.second.error;

    return {};
}

void PluginHostManager::setEffectBypassed (juce::int64 trackId, int slotIndex, bool shouldBypass)
{
    if (auto plugin = effectPluginFor (trackId, slotIndex))
        plugin->setBypassed (shouldBypass);
}

std::shared_ptr<HostedEffect> PluginHostManager::effectFor (juce::int64 trackId, int slotIndex,
                                                           const EffectSlot& slot)
{
    if (! slot.hasPlugin())
        return nullptr;

    const auto& state = effectStateFor (trackId, slotIndex);

    if (state.plugin != nullptr && state.matches (slot))
    {
        // The slot's bypass is part of the project, not of the plugin instance, and
        // it can change without a reload.
        state.plugin->setBypassed (slot.bypass);
        return state.plugin;
    }

    requestEffect (trackId, slotIndex, slot);
    return nullptr;
}

void PluginHostManager::captureEffectStates (Project& project)
{
    for (auto& track : project.tracks)
    {
        for (int i = 0; i < (int) track.effects.size(); ++i)
        {
            auto plugin = effectPluginFor (track.id, i);

            if (plugin == nullptr)
                continue;

            const auto block = plugin->captureState();
            track.effects[(size_t) i].stateBase64 =
                juce::Base64::toBase64 (block.getData(), block.getSize());
            track.effects[(size_t) i].name = plugin->getName();
        }
    }
}

std::unique_ptr<juce::PluginDescription> PluginHostManager::findEffectDescription (
    const EffectSlot& slot) const
{
    if (! slot.hasPlugin())
        return nullptr;

    const juce::File file (slot.pluginPath);

    auto matches = [&slot] (const juce::PluginDescription& type)
    {
        if (slot.pluginUid.isNotEmpty()
            && juce::String ((juce::int64) type.uniqueId) == slot.pluginUid)
            return true;

        return slot.name.isNotEmpty()
                   && (type.name == slot.name || type.descriptiveName == slot.name);
    };

    for (const auto& type : knownList.getTypes())
        if (juce::File (type.fileOrIdentifier) == file && matches (type))
            return std::make_unique<juce::PluginDescription> (type);

    for (const auto& type : knownList.getTypes())
        if (! type.isInstrument && matches (type))
            return std::make_unique<juce::PluginDescription> (type);

    return describeFile (file);
}

std::shared_ptr<HostedEffect> PluginHostManager::makeOfflineEffect (const EffectSlot& slot,
                                                                    double sampleRate, int blockSize,
                                                                    juce::String& error) const
{
    error.clear();

    if (! slot.hasPlugin())
        return nullptr;

    auto description = findEffectDescription (slot);

    if (description == nullptr)
    {
        error = NI_TXT ("找不到插件：") + describeSlot (slot);
        return nullptr;
    }

    return makeOfflineEffect (*description, slot, sampleRate, blockSize, error);
}

std::shared_ptr<HostedEffect> PluginHostManager::makeOfflineEffect (
    const juce::PluginDescription& description, const EffectSlot& slot,
    double sampleRate, int blockSize, juce::String& error) const
{
    error.clear();

    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        offlineThreadViolations().fetch_add (1);

    logEvent ("offline-effect  plugin=\"" + description.name + "\"  rate="
                  + juce::String (sampleRate, 0) + "  block=" + juce::String (blockSize)
                  + "  onMessageThread="
                  + (juce::MessageManager::getInstance()->isThisTheMessageThread() ? "yes"
                                                                                  : "no"));

    trace ("fx create: " + description.name + " (" + description.pluginFormatName + ")");

    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance == nullptr)
    {
        error = describeError (error);
        trace ("fx create failed: " + error);
        return nullptr;
    }

    trace ("fx created: " + description.name);

    auto hosted = std::make_shared<HostedEffectPlugin> (std::move (instance), slot.name,
                                                        slot.bypass);
    hosted->prepare (sampleRate, blockSize);

    if (slot.stateBase64.isNotEmpty())
    {
        juce::MemoryOutputStream stream;

        if (juce::Base64::convertFromBase64 (stream, slot.stateBase64))
            hosted->getInstance().setStateInformation (stream.getData(),
                                                       (int) stream.getDataSize());
    }

    return hosted;
}

} // namespace hmdaw
