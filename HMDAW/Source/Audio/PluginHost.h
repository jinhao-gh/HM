#pragma once

#include "../Core/PluginRendering.h"
#include "../Core/PluginGrouping.h"
#include "../Core/PluginScanProtocol.h"
#include "../Core/Types.h"
#include "../Core/VstSupport.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <array>
#include <memory>
#include <vector>

namespace hmdaw
{

/** One VST3 track's live plugin.

    Wraps the JUCE instance and does the note-to-MIDI translation: every sounding
    note gets a MIDI channel of its own so it can carry its own pitch bend, which
    is what keeps a frequency that is not exactly on a semitone (660 Hz, say)
    sounding at the pitch the project asks for.
*/
class HostedPlugin : public HostedInstrument
{
public:
    HostedPlugin (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                  juce::String pluginName);

    /** The plugin this wraps. Message thread only. */
    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    juce::String getName() const { return name; }

    // HostedInstrument
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void noteOn (juce::int64 noteId, float frequencyHz, float velocity) override;
    void noteOff (juce::int64 noteId) override;
    void allNotesOff() override;
    /** A real note-off for each held note (no CC123), keeping the tail alive. */
    void releaseHeldNotes() override;
    /** One drawn controller value, sent to the channels that are playing. */
    void sendController (int controllerNumber, int value) override;
    void renderAdding (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       float gainLeft, float gainRight) override;
    bool isActive() const noexcept override;
    juce::MemoryBlock captureState() const override;

    /** Diagnostics: how many notes are currently held (self test). */
    int activeNoteCount() const noexcept { return channels.activeCount(); }

    /** Diagnostics: how many MIDI channels have been used since the plugin was
        prepared (self test: "the notes were spread out, not piled onto one"). */
    int usedChannelCountForTest() const noexcept { return channels.channelsUsedSinceReset(); }
    /** Diagnostics: how often an old note had to be cut short to free a channel. */
    int evictedNoteCountForTest() const noexcept { return evictedNoteCount; }

    /** What the plugin's buses ended up as, for the event log. */
    juce::String getLayoutSummary() const { return layoutSummary; }

private:
    /** Asks for a stereo output and no audio input, which is what an instrument
        is. Falls back to mono, then to whatever the plugin already had. */
    void applyPreferredBusesLayout();
    /** "in 0 / out 2" as it stands now, for the event log. */
    juce::String describeLayout() const
    {
        return "in " + juce::String (plugin->getTotalNumInputChannels())
                   + " / out " + juce::String (plugin->getTotalNumOutputChannels());
    }
    /** Sends the pitch bend range for a channel the first time it is used. */
    void sendPitchBendRange (int channel);
    /** Puts the controller state a drawn curve has set back onto a channel, so a
        pedal (say) that was pressed before this note reaches the channel it landed
        on. Notes live on channels of their own, so "the state of the instrument"
        has to travel with each of them. */
    void replayControllerState (int channel);

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String name;

    vst::ChannelAllocator channels;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> scratch;
    std::array<bool, vst::kMidiChannels> rangeSent {};
    /** The MIDI note each channel is currently holding, -1 when it is free.
        Kept so "all notes off" can name the notes it is releasing. */
    std::array<int, vst::kMidiChannels> channelNote {};
    /** The last value of every controller a drawn curve has sent, and which ones
        have ever been sent: replayed onto a channel when a note first lands on it. */
    std::array<int, 128> lastControllerValue {};
    std::array<bool, 128> controllerSent {};
    /** How often a channel had to be taken from a note that was still held. */
    int evictedNoteCount = 0;

    double currentSampleRate = 48000.0;
    /** Samples of release left after the last note ended, taken from the plugin's
        own tail length so a long reverb is not chopped off. */
    juce::int64 tailSamplesLeft = 0;
    juce::int64 tailSamplesPerNote = 0;
    bool prepared = false;
    /** True once the buses have been arranged the way HM wants them. Re-asking on
        every prepare would deactivate and reactivate the plugin, which is what made
        an audio device restart reset some instruments' own volume. */
    bool layoutApplied = false;
    /** "in 0 / out 2": the shape the plugin was left in. */
    juce::String layoutSummary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPlugin)
};

//==============================================================================
/** One insert slot's live plugin.

    An effect is not an instrument: it has audio coming in, no notes going out, and
    its own latency. The instance is wrapped so the render loop can treat it like
    any other effect in the chain.
*/
class HostedEffectPlugin : public HostedEffect
{
public:
    HostedEffectPlugin (std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                        juce::String pluginName, bool shouldBypass);

    /** The plugin this wraps. Message thread only. */
    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    juce::String getName() const { return name; }

    /** Turns the slot's bypass on or off. Safe to call while the audio thread is
        running: the flag is atomic and the plugin is simply not called. */
    void setBypassed (bool shouldBypass) noexcept { bypass.store (shouldBypass); }

    /** What the plugin's buses ended up as, for the event log. */
    juce::String getLayoutSummary() const { return layoutSummary; }

    // HostedEffect
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;
    bool isBypassed() const noexcept override { return bypass.load(); }
    int getLatencySamples() const noexcept override { return latencySamples; }
    juce::MemoryBlock captureState() const override;

private:
    /** Asks for a stereo (or mono) input *and* output, which is what an effect is. */
    void applyPreferredBusesLayout();
    /** "in 2 / out 2" as it stands now, for the event log. */
    juce::String describeLayout() const
    {
        return "in " + juce::String (plugin->getTotalNumInputChannels())
                   + " / out " + juce::String (plugin->getTotalNumOutputChannels());
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String name;
    /** processBlock takes a MidiBuffer even for an effect. */
    juce::MidiBuffer midi;
    /** The plugin's channel bundle: all inputs first, then all outputs. */
    juce::AudioBuffer<float> scratch;
    std::atomic<bool> bypass { false };
    int latencySamples = 0;
    bool prepared = false;
    /** True once the buses have been arranged the way HM wants them. */
    bool layoutApplied = false;
    /** "in 2 / out 2": the shape the plugin was left in. */
    juce::String layoutSummary;
    /** Diagnostics: the first block an effect is asked to process is reported once. */
    bool firstBlockTraced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedEffectPlugin)
};

//==============================================================================
/** Finds the VST3 plugins on this machine, keeps the instances of the tracks that
    use them, and answers the audio engine's questions about both.

    Everything here runs on the message thread except where noted. Instances are
    deliberately never unloaded while the app runs: an instance that is still
    referenced by a block the audio thread is rendering must not be destroyed
    there, and letting the message thread hold the last reference is the simplest
    way to guarantee that. The cost is that a deleted track's plugin stays in
    memory until HM quits.
*/
class PluginHostManager : public LiveInstrumentProvider
{
public:
    PluginHostManager();
    ~PluginHostManager() override;

    /** The scan cache. The self test points this at its own output folder so a run
        never touches the real per-user list. */
    static juce::File getDefaultListFile();
    static void setDefaultListFileForTesting (juce::File file);

    juce::File getListFile() const { return listFile; }
    /** Where the extra plugin folders are remembered (VST3 目录). */
    juce::File getFoldersFile() const { return listFile.getSiblingFile ("vstfolders.json"); }

    /** Every plugin found so far; empty before the first scan or cache load. */
    juce::Array<juce::PluginDescription> getPlugins() const { return knownList.getTypes(); }
    int getPluginCount() const noexcept { return knownList.getNumTypes(); }

    juce::StringArray getExtraFolders() const { return extraFolders; }
    void addExtraFolder (const juce::File& folder);

    /** How the picker arranges the plugins it found. Remembered next to the extra
        folders, so it survives a restart. */
    PluginGroupAxis getGroupingAxis() const noexcept { return groupingAxis; }
    void setGroupingAxis (PluginGroupAxis axis);

    bool isScanning() const noexcept { return scanning; }
    /** What the picker shows under the list: progress, then the result. */
    juce::String getStatusText() const;

    bool loadCache();
    bool saveCache() const;

    /** Scans the standard folders plus the extra ones on a background thread.
        `onFinished` runs on the message thread. */
    void startScan (std::function<void()> onFinished = {});
    /** Scans one folder right now and returns how many plugins it held. Used by
        the self test, which must not depend on what is installed. `failedFiles`
        counts the bundles whose child process died or read nothing. */
    int scanFolderNow (const juce::File& folder, juce::String& error,
                       int* failedFiles = nullptr);
    /** Stops a running scan at the next file, killing its child process. */
    void cancelScan();

    /** The description a stored VstParams asks for: by path first, then by the
        plugin's unique id, then by its name. Null when it cannot be found. */
    std::unique_ptr<juce::PluginDescription> findDescription (const VstParams& params) const;
    /** Reads one .vst3 bundle's description, without a scan. */
    std::unique_ptr<juce::PluginDescription> describeFile (const juce::File& file) const;

    // ---- LiveInstrumentProvider ----
    void prepareAll (double sampleRate, int maxBlockSize) override;
    std::shared_ptr<HostedInstrument> instrumentFor (juce::int64 trackId,
                                                     const VstParams& params) override;

    /** Starts loading a track's plugin if it is not up yet. */
    void requestInstrument (juce::int64 trackId, const VstParams& params);
    /** The live plugin for a track, when one is up. */
    std::shared_ptr<HostedPlugin> pluginFor (juce::int64 trackId) const;
    /** Why a track's plugin could not be loaded; empty when it is fine. */
    juce::String getLoadError (juce::int64 trackId) const;
    /** Forgets a track's plugin: the track is gone or its plugin was replaced. */
    void forgetTrack (juce::int64 trackId);
    /** Copies every live plugin's state into the project, before saving it. */
    void captureStates (Project& project);
    /** The same, for one track: used before an edit that is about to take the
        instance away (deleting a track, which the user can undo). */
    void captureStateFor (juce::int64 trackId, Project& project);
    /** Drops every live instance so the next rebuild re-creates each one from the
        project that was just loaded. A project load is a fresh start: reusing the
        previous project's instances is what made a track play the sound - volume
        included - of the project that came before it. */
    void resetForProject();

    // ---- insert chains ----
    /** Starts loading a slot's effect if it is not up yet. */
    void requestEffect (juce::int64 trackId, int slotIndex, const EffectSlot& slot);
    /** The live effect for one slot, when it is up. */
    std::shared_ptr<HostedEffectPlugin> effectPluginFor (juce::int64 trackId, int slotIndex) const;
    /** Why a slot's effect could not be loaded; empty when it is fine. */
    juce::String getEffectError (juce::int64 trackId, int slotIndex) const;
    /** Applies a slot's bypass to the live instance, when there is one. */
    void setEffectBypassed (juce::int64 trackId, int slotIndex, bool shouldBypass);
    /** Copies every live effect's state into the project. */
    void captureEffectStates (Project& project);

    /** Puts the live instances back to the state they had at `before`.

        The export builds a private instance per track, and for some plugins a
        second copy in the same process disturbs - or resets - the one the user is
        listening to (Kontakt's own engine is the usual suspect: its instrument
        volume ends up at zero just from having a sibling). Nobody should lose the
        sound they were working on because they exported it, so the state captured
        before the render is compared with the state now and put back when it moved.

        Returns the number of instances that had to be restored (0 is the normal,
        healthy answer). Message thread only.
    */
    int restoreStatesChangedSince (const Project& before);

    /** Self test: how many instances the guard above has had to put back. */
    static int getLiveStateRestoresForTest() noexcept;

    // LiveInstrumentProvider
    std::shared_ptr<HostedEffect> effectFor (juce::int64 trackId, int slotIndex,
                                             const EffectSlot& slot) override;

    /** A private instance for the offline export, prepared at the export rate.
        The caller renders with it and lets it go when the file is written. */
    std::shared_ptr<HostedInstrument> makeOfflineInstrument (const VstParams& params,
                                                             double sampleRate, int blockSize,
                                                             juce::String& error) const;
    /** The same, for a description that was resolved on the message thread. The
        export resolves what it needs before the render starts, so the background
        thread never reads the plugin list while a scan may be rewriting it. */
    std::shared_ptr<HostedInstrument> makeOfflineInstrument (const juce::PluginDescription& description,
                                                             const VstParams& params,
                                                             double sampleRate, int blockSize,
                                                             juce::String& error) const;
    /** A private effect instance for the offline export, prepared at the export
        rate, with the slot's stored state restored. */
    std::shared_ptr<HostedEffect> makeOfflineEffect (const EffectSlot& slot,
                                                     double sampleRate, int blockSize,
                                                     juce::String& error) const;
    std::shared_ptr<HostedEffect> makeOfflineEffect (const juce::PluginDescription& description,
                                                     const EffectSlot& slot,
                                                     double sampleRate, int blockSize,
                                                     juce::String& error) const;
    /** The description an EffectSlot asks for (by path, uid or name). */
    std::unique_ptr<juce::PluginDescription> findEffectDescription (const EffectSlot& slot) const;

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int getCurrentBlockSize() const noexcept { return currentBlockSize; }

    // ---- self test hooks ----
    /** Pretends a scan found this plugin, so the cache and the picker can be
        exercised without one being installed. */
    void addPluginForTest (const juce::PluginDescription& description)
    {
        knownList.addType (description);
    }
    /** Writes the cache now, instead of waiting for a scan to finish. */
    bool saveCacheForTest() const { return saveCache(); }

    /** Self test: extra switches handed to the scan child (which can be told to die
        or to hang), and how long a child gets before it is killed. */
    void setScanChildArgumentsForTest (const juce::StringArray& extraArguments)
    {
        scanChildExtraArgs = extraArguments;
    }
    void setScanTimeoutForTest (int milliseconds) { scanTimeoutMs = juce::jmax (100, milliseconds); }
    /** Where the host narrates what it does with the plugins. The self test points
        this at its own output folder so a run never touches the real file. */
    static juce::File getEventsLogFile();
    static void setEventsLogFileForTesting (juce::File file);
    /** Self test: how often an offline instance was built off the message thread.
        Loading a plugin, preparing it and handing it its state all belong on the
        message thread; the export calls back here to prove it (see runExport). */
    static int getOfflineThreadViolationsForTest() noexcept;

    /** Called on the message thread whenever an instance appears or fails, so the
        app can rebuild the schedule. */
    std::function<void()> onInstrumentChanged;

    /** Diagnostics: when set, the host narrates what it does with the instances it
        builds. The offscreen self test points this at its trace file, which is how
        a plugin that takes the process down can be pinned to the step it died in. */
    static std::function<void (const juce::String&)> traceHook;

private:
    /** One attempt at loading one track's plugin. */
    struct Load
    {
        juce::int64 trackId = 0;
        VstParams   params;
        std::shared_ptr<HostedPlugin> plugin;   // null when the attempt failed
        /** The same idea for an insert slot's plugin. */
        std::shared_ptr<HostedEffectPlugin> effect;
        juce::String error;
    };

    /** Where a track's instance currently stands. */
    struct TrackState
    {
        std::shared_ptr<HostedPlugin> plugin;
        juce::String error;
        juce::String requestedPath;
        juce::String requestedUid;
        /** The plugin this track already tried and could not load. Asking again
            would only report the same thing, and a rebuild of the schedule asks
            every time the document changes - so a failure is remembered. */
        juce::String failedPath;
        juce::String failedUid;
        bool pending = false;

        bool matches (const VstParams& params) const
        {
            return requestedPath == params.pluginPath && requestedUid == params.pluginUid;
        }

        bool hasFailedFor (const VstParams& params) const
        {
            return failedPath.isNotEmpty() && failedPath == params.pluginPath
                       && failedUid == params.pluginUid;
        }
    };

    TrackState& stateFor (juce::int64 trackId);

    /** Where one insert slot's effect currently stands. */
    struct EffectState
    {
        std::shared_ptr<HostedEffectPlugin> plugin;
        juce::String error;
        juce::String requestedPath;
        juce::String requestedUid;
        juce::String failedPath;
        juce::String failedUid;
        bool pending = false;

        bool matches (const EffectSlot& slot) const
        {
            return requestedPath == slot.pluginPath && requestedUid == slot.pluginUid;
        }

        bool hasFailedFor (const EffectSlot& slot) const
        {
            return failedPath.isNotEmpty() && failedPath == slot.pluginPath
                       && failedUid == slot.pluginUid;
        }
    };

    EffectState& effectStateFor (juce::int64 trackId, int slotIndex);
    juce::FileSearchPath searchPath() const;
    void mergeScannedPlugins (const juce::Array<juce::PluginDescription>& found,
                              const juce::StringArray& failedFiles);
    /** Reads one .vst3 through a child of this process. */
    scanproto::Result scanFileWithChild (const juce::File& file, bool honourCancel) const;
    /** The shared body of the background scan and of scanFolderNow. */
    juce::Array<juce::PluginDescription> scanWithChildProcesses (const juce::FileSearchPath& folders,
                                                                 bool recursive,
                                                                 juce::StringArray& failedFiles);
    void setScanStatus (const juce::String& text);
    /** One line in plugin-events.log. Message thread only. */
    void logEvent (const juce::String& text) const;

    juce::AudioPluginFormatManager formatManager;
    juce::VST3PluginFormat* vst3Format = nullptr;   // owned by the format manager
    juce::KnownPluginList knownList;
    juce::File listFile;
    juce::StringArray extraFolders;
    PluginGroupAxis groupingAxis = PluginGroupAxis::vendor;
    std::vector<std::unique_ptr<Load>> loads;
    std::vector<std::pair<juce::int64, TrackState>> trackStates;
    std::vector<std::pair<std::pair<juce::int64, int>, EffectState>> effectStates;

    /** Keeps a background scan from touching a manager that has gone away. */
    std::shared_ptr<PluginHostManager*> lifetimeGuard;

    bool scanning = false;
    juce::String statusText;
    mutable juce::CriticalSection statusLock;
    std::atomic<bool> scanCancelled { false };
    juce::StringArray scanChildExtraArgs;
    int scanTimeoutMs = scanproto::kDefaultTimeoutMs;
    /** Bumped whenever the instances are thrown away, so a load that was already in
        flight cannot install an instance that belongs to the previous project. */
    int generation = 0;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostManager)
};

} // namespace hmdaw
