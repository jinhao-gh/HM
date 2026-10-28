#pragma once

#include "../Audio/AudioEngine.h"
#include "../Audio/MidiInputManager.h"
#include "../Audio/PluginHost.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "EditorPanel.h"
#include "EffectsBar.h"
#include "EffectsWindow.h"
#include "MixerWindow.h"
#include "MidiInputWindow.h"
#include "TopToolbar.h"
#include "TrackArea.h"
#include "TransportBar.h"
#include "VirtualKeyboardWindow.h"
#include "SynthWindow.h"
#include "PluginWindow.h"
#include "WavetableTableWindow.h"
#include "../Audio/Exporter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <map>
#include <array>
#include <vector>

namespace nidaw
{

class MainComponent : public juce::Component,
                      public juce::KeyListener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    /** showDialogs is false in the offscreen self test, where a modal alert
        would block the run. */
    /** `openAudioDevice` is false for the offscreen self test: it renders every block
        by hand, and a live device callback would render the same engine from its own
        thread - which made level and voice-count measurements flap. */
    explicit MainComponent (bool showDialogs = true, bool openAudioDevice = true);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    /** The app-wide key listener: every window HM opens has this added to it, so the
        space bar drives the transport even when the focus is in another window - see
        keyPressed(key, origin) below. */
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;
    void parentHierarchyChanged() override;

    void newProject();
    void openProject();
    /** Loads a project file and puts the editor back into a clean state (no note
        or clip selection, lane headers rebuilt). The file chooser calls this, and
        so does the self test - it cannot drive a modal chooser. */
    bool loadProjectFile (const juce::File& file);
    void saveProject (bool saveAs);
    void exportWav();
    /** Renders a batch of files (the master mix and/or one per track).
        Shared by the export dialog and the offscreen self test. */
    bool runExport (const ExportSettings& settings, const juce::File& target,
                    bool targetIsFolder, juce::String& errorMessage,
                    std::vector<juce::File>* writtenFiles = nullptr,
                    std::vector<juce::String>* skippedTracks = nullptr);
    /** True when it is all right to close: asks to save when there are changes.
        Never blocks while the self test is driving (showDialogs == false). */
    bool confirmQuit();
    /** Puts one clip on the clip clipboard, exactly like Ctrl+C does. */
    void copyClipToClipboard (juce::int64 clipId);
    void deleteSelectedNotes();
    void shiftSelectedNotesByOctave (int octaves);
    void openSynthWindow (juce::int64 trackId);
    /** 自定义波表: puts a table of the project's own together out of the loaded
        audio, or out of a built-in table. All three sources end up here. */
    juce::int64 addCustomWavetable (CustomWavetable table);
    /** A one-line note for the wavetable editor's status line (and for the self
        test): "that clip has no audio", "that file cannot be read", … */
    void setWavetableNotice (const juce::String& text);
    /** 效果 in a lane header: this track's insert chain, in its own window. */
    void openEffectsWindow (juce::int64 trackId);
    /** 混音台 (the top bar's button and F3). */
    void openMixerWindow();
    /** Drives the app-wide key handling the way the desktop does (self test).

        The focused component goes up the tree, and at each level the app-wide
        listener - this component - gets the first go and then the component's own
        keyPressed. JUCE does exactly the same from ComponentPeer::handleKeyPress,
        which a test cannot reach, so the two checks happen here instead.
    */
    bool sendKeyPressForTest (const juce::KeyPress& key, juce::Component* focused);
    /** True when this window carries the app-wide key listener (self test). */
    bool listensToWindowForTest (const juce::Component* window) const
    {
        return listensToWindow (window);
    }
    /** What the space-bar rule answers for a component that has the keyboard.

        The desktop cannot be told to focus a component that is not on screen - and
        the offscreen self test never puts the editor on screen - so the rule is
        asked with the component handed in instead.
    */
    bool isTransportSpaceKeyForTest (const juce::KeyPress& key, juce::Component* focused) const
    {
        return isTransportSpaceKeyFor (key, focused);
    }
    void copySelection();
    void pasteClipboard();

    /** Turns the left column into the group editor for a saved preset, or for one
        of the project's own reference line groups. Both are the "编辑" buttons:
        the preset panel's and the status row's. */
    void openGroupEditorForPreset (bool lineGroup, int presetIndex);
    void openGroupEditorForLineGroup (juce::int64 lineId);
    /** 返回 / 保存 / 另存为, the three buttons along the group editor's bottom. */
    void closeGroupEditor (bool restorePreviousGroup);
    void saveGroupEditor (const juce::String& name);
    void saveGroupEditorAs (const juce::String& name);

    /** Appends `count` copies of the open clip, laid out back to back after it.
        The source clip stays selected so it can be repeated again. */
    void repeatSelectedClip (int count);

    // ---- MIDI 输入 ----------------------------------------------------------
    /** Opens (or brings back) the MIDI input window. */
    void openMidiInputWindow();
    /** The frequency a MIDI note plays right now: the mapping, or twelve-tone equal
        temperament while the editor is in the piano window. */
    double frequencyForMidiNote (int midiNote) const;
    /** Every frequency that key plays (a key can be tied to several dots): what the
        engine sounds and what the recorder writes out. */
    std::vector<double> frequenciesForMidiNote (int midiNote) const;
    /** One MIDI message, from the device or the self test. */
    void handleMidiMessage (const juce::MidiMessage& message);
    /** A note going on or off on the **selected track**, from any live source: the
        hardware keyboard, the virtual keyboard's keys, or its computer-keyboard
        playing. Everything downstream (the mapping, the arpeggiator, recording,
        the anchor lesson and the on-screen highlighting) hangs off this one path,
        so a key played with the mouse behaves exactly like one played by hand.

        `trigger` says **which** physical thing it was (see `keyboard::kHardwareTrigger`
        and friends): the same note held by two sources sounds twice, and each source
        lets go of its own. A repeat from the same source is ignored. */
    void handleLiveNoteOn (int midiNote, float velocity, juce::int64 trigger);
    void handleLiveNoteOff (int midiNote, juce::int64 trigger);
    /** A controller value played *now*: the pedal, the expression pedal, the mod
        wheel. It sounds at once and - while recording - is written into the clip's
        controller curve (one gesture per controller, so a continuous pedal is one
        undo step rather than a hundred). */
    void handleLiveController (int controller, int value);
    /** Which track a live key/message is routed to, or nullptr when this track has
        no pitch to bind to (鼓机 / 音频). */
    const InstrumentTrack* liveTargetTrack() const;
    /** Releases every note the MIDI keyboard is holding. */
    void releaseAllLiveNotes();
    MidiInputWindow* getMidiInputWindowForTest() noexcept { return midiInputWindow.get(); }
    /** Opens (or brings back) the virtual keyboard window. */
    void openVirtualKeyboardWindow();
    /** True while the virtual keyboard is on screen. The 1-6 频率比 shortcuts stand
        down while it is (those keys are the accordion's bass row). */
    bool isVirtualKeyboardVisible() const noexcept;
    VirtualKeyboardWindow* getVirtualKeyboardWindowForTest() noexcept
    {
        return virtualKeyboardWindow.get();
    }
    /** Self test: what the last live note-on asked the engine for - which proves the
        virtual keys, the hardware keys and the arpeggiator all go one way. */
    struct LiveNoteRecordForTest
    {
        int         midiNote   = -1;
        double      frequencyHz = 0.0;
        float       velocity   = 0.0f;
        juce::int64 trackId    = 0;
    };

    LiveNoteRecordForTest getLastLiveNoteForTest() const { return lastLiveNote; }
    /** Self test: the notes the arpeggiator is cycling, in play order. */
    std::vector<int> getArpOrderForTest() const { return arp.order; }
    /** Self test: the note the arpeggiator is sounding right now, or -1. */
    int getArpSoundingNoteForTest() const noexcept { return arp.soundingNote; }
    /** Self test: how many voices that note has (one per cable of the key). */
    int getArpVoiceCountForTest() const noexcept { return (int) arp.soundingVoices.size(); }
    /** Self test: run one pulse of the arpeggiator (the 1 ms timer does this). */
    void arpTickForTest() { arpTick(); }
    /** Self test: how many steps the arpeggiator has fired since it was switched on. */
    int getArpFiredStepCountForTest() const noexcept { return arp.firedSteps; }
    /** Self test: pin the arpeggiator's clock, so the steps can be stepped through
        without waiting for real time (the music is the same either way). */
    void setArpClockForTest (juce::int64 ticks);
    void clearArpClockForTest() { arp.forcedClockTicks = -1; }
    /** Self test: write the whole 琶音 set at once. */
    void setArpSettingsForTest (const midiarp::Settings& settings);
    /** Self test: the keys the virtual keyboard should be drawing as held. */
    std::vector<int> getHeldKeysForTest() const;
    /** Self test: the arpeggiator's settings, straight from the project. */
    midiarp::Settings getArpSettingsForTest() const
    {
        return document.getProject().midiInput.arp;
    }
    void setArpEnabledForTest (bool shouldBeOn);
    /** The wavetable editor window, once something has opened it. */
    WavetableTableWindow* getWavetableTableWindowForTest() noexcept
    {
        return wavetableTableWindow.get();
    }
    /** Opens the wavetable editor on the current track's own table (and makes one
        out of the built-in it is using when it has none yet). */
    void openWavetableTableWindow (juce::int64 customTableId = 0);
    /** The settings a right click on a lane header offers (a VST track can be
        switched between exact frequencies and 按半音演奏). */
    void showTrackMenu (juce::int64 trackId);
    /** Self test: the items that menu holds for a track. */
    struct TrackMenuItemForTest
    {
        int itemId = 0;
        juce::String text;
        bool ticked = false;
    };

    std::vector<TrackMenuItemForTest> getTrackMenuItemsForTest (juce::int64 trackId) const;
    void performTrackMenuCommandForTest (juce::int64 trackId, int itemId);
    /** Self test: the three sources, without a dialog in the way. */
    juce::int64 importWavetableFromFile (const juce::File& file);
    juce::int64 sampleWavetableFromOpenClip();
    void copyBuiltInWavetable (int builtInIndex);
    void deleteCustomWavetable (juce::int64 tableId);
    juce::String getWavetableStatusNoticeForTest() const;
    /** How many *notes* are down (two sources on one note count once). */
    int getHeldMidiNoteCountForTest() const { return (int) heldNotes().size(); }
    /** Self test: how many voices are really sounding (a key with a stack has one
        per dot). */
    int getHeldMidiVoiceCountForTest() const;
    /** Self test: how many recorded notes are still waiting for their key release. */
    int getPendingRecordCountForTest() const { return (int) pendingRecords.size(); }
    /** Self test: pretend the machine has these MIDI input devices. */
    void setMidiDeviceListForTest (const std::vector<juce::String>& names);
    /** Self test: the device that is open, by the name it was opened with. */
    juce::String getMidiOpenDeviceNameForTest() const { return midiInput.getOpenName(); }
    bool isMidiDeviceOpenForTest() const { return midiInput.isDeviceOpenForTest(); }
    /** Self test: hand a message in through the manager's own delivery path (it
        arrives on the message thread, exactly like a real device's would). */
    void simulateMidiDeviceMessageForTest (const juce::MidiMessage& message)
    {
        midiInput.simulateMessageForTest (message);
    }

    ProjectDocument& getDocument() noexcept { return document; }
    AppState& getAppState() noexcept { return appState; }
    AudioEngine& getAudioEngine() noexcept { return audio; }
    PresetStore& getPresetStore() noexcept { return presetStore; }
    PluginHostManager& getPluginHost() noexcept { return pluginHost; }
    EditorPanel& getEditorPanel() noexcept { return editorPanel; }
    EffectsBar& getEffectsBar() noexcept { return effectsBar; }
    /** The effects window, once a lane header's 效果 button has opened it. */
    EffectsWindow* getEffectsWindowForTest() noexcept { return effectsWindow.get(); }
    MixerWindow* getMixerWindowForTest() noexcept { return mixerWindow.get(); }
    /** The sound window / the plugin window, once something has opened them. */
    SynthWindow* getSynthWindowForTest() noexcept { return synthWindow.get(); }
    PluginWindow* getPluginWindowForTest() noexcept { return pluginWindow.get(); }
    TrackArea& getTrackArea() noexcept { return trackArea; }
    TransportBar& getTransportBar() noexcept { return transportBar; }
    TopToolbar& getTopToolbar() noexcept { return toolbar; }
    /** Where the export narration goes (the self test points it at its own folder).
        Every step of Ctrl+E is appended here, so "the export does not work" can be
        answered with what actually happened instead of a guess. */
    static juce::File getExportLogFile();
    static void setExportLogFileForTesting (juce::File file);
    /** Diagnostic hook: the bar between the arrangement and the editor. */
    juce::StretchableLayoutResizerBar* getLayoutResizerForTest() const { return resizer.get(); }

private:
    /** The items of the header menu (empty for a track that has no settings). */
    juce::PopupMenu buildTrackMenu (juce::int64 trackId) const;
    /** Runs one item of that menu (1 = 按半音演奏 on/off). */
    void performTrackMenuCommand (juce::int64 trackId, int itemId);

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    bool confirmDiscardChanges();
    void showMessage (const juce::String& title, const juce::String& message, bool isWarning);
    void showClipRepeatDialog();
    /** 重命名… from the clip menu: one text field, then one edit. */
    void showClipRenameDialog (juce::int64 clipId);
    /** Asks which effect to add to a track, then adds it to its insert chain. */
    void pickEffectFor (juce::int64 trackId);
    /** Opens an insert slot's plugin window - or the picker again when its plugin
        could not be loaded. */
    void openEffectSlot (juce::int64 trackId, int slotIndex);
    /** The export settings dialog; `settings` comes back with what was chosen. */
    bool showExportDialog (ExportSettings& settings);
    /** Captures the live plugins' state and writes the project. */
    bool writeProject (const juce::File& file, juce::String& errorMessage);
    /** A new or loaded project starts with no plugins up: the instances that are
        running belong to the project that came before. */
    void unloadPluginsForProjectChange();
    /** Puts this KeyListener on every window HM opens (the main window, the sound
        window, the effects window, the mixer): the space bar drives the transport
        wherever the focus happens to be, which is what the user asked for. A
        plugin's own window is the exception - see keyPressed(key, origin). */
    void attachKeyListenersToWindows();
    /** True when this listener has already been added to that window. */
    bool listensToWindow (const juce::Component* window) const;
    /** True when this key press is the transport's space bar: no modifiers, and
        neither a text box (that is typing) nor a plugin's own interface (Kontakt
        and Serum audition with it) holds the key. Both keyPressed overloads ask
        this, so the rule is written once. */
    bool isTransportSpaceKey (const juce::KeyPress& key) const;
    /** The same rule, for a component that has the keyboard. */
    bool isTransportSpaceKeyFor (const juce::KeyPress& key, juce::Component* focused) const;
    /** True when that component is inside one of the plugin window's editors. */
    bool focusIsInsidePluginInterface (juce::Component* focused) const;
    /** Keeps `vst.loadedOk` in step with what is really loaded, which is what the
        lane header shows. */
    void refreshVstLoadedFlags();
    /** Every step of Ctrl+E - the dialog, the chosen destination, the render, the
        files written - is appended here, so a report of "the export does not work"
        can be answered with what actually happened instead of a guess. */
    static void logExport (const juce::String& text);
    /** Index of the matching entry in the sample-rate combo (0 = the project's own). */
    static int rateIndexFor (double sampleRate);
    /** Reads the last export settings so the dialog remembers them. */
    ExportSettings lastExportSettings;
    /** Set when the user chose "save" while closing, so a successful save quits. */
    bool quitAfterSave = false;

    /** Keeps the 2nd/4th reference line entries in step with the note selection.

        They are derived state, so this deliberately does not push an undo step.
    */
    void syncReferenceEntries();

    ProjectDocument document;
    PresetStore presetStore;
    PluginHostManager pluginHost;
    MidiInputManager midiInput;
    AppState appState;
    AudioEngine audio;

    TopToolbar toolbar;
    TrackArea trackArea;
    EffectsBar effectsBar;
    TransportBar transportBar;
    EditorPanel editorPanel;

    juce::StretchableLayoutManager layout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizer;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<SynthWindow> synthWindow;
    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<EffectsWindow> effectsWindow;
    std::unique_ptr<MixerWindow> mixerWindow;
    std::unique_ptr<MidiInputWindow> midiInputWindow;
    std::unique_ptr<VirtualKeyboardWindow> virtualKeyboardWindow;
    std::unique_ptr<WavetableTableWindow> wavetableTableWindow;
    /** The last note the wavetable actions produced (shown in the editor window). */
    juce::String wavetableNotice;

    /** Which track and which voice each held MIDI key started, so its note-off goes
        to the instrument that is really playing it. */
    struct HeldMidiNote
    {
        juce::int64 trackId = 0;
        juce::int64 liveId  = 0;
    };

    /** One key that is down right now, **one entry per source**: the same note held by
        a melody key and a bass key (or by the hardware keyboard and the on-screen
        piano) is two entries, each with its own voices, and each lets go of its own.
        While the arpeggiator is on the key has no voices of its own - the arpeggiator
        plays the set instead - but it is still "held": that is what the key count,
        the highlighting and the latch read. */
    struct HeldKey
    {
        juce::int64 trigger = keyboard::kHardwareTrigger;
        int   midiNote = 0;
        float velocity = 100.0f;
        std::vector<HeldMidiNote> voices;
    };

    /** Every key that is down, in the order it went down (one entry per source). */
    std::vector<HeldKey> heldKeys;
    juce::int64 nextLiveNoteId = 1;

    HeldKey* findHeldKey (juce::int64 trigger, int midiNote);
    void releaseHeldKeyVoices (HeldKey& key);
    /** The notes that are down, deduplicated and in the order they went down: what
        the highlighting, the arpeggiator's held set and the counts read. */
    std::vector<int> heldNotes() const;
    /** Tells the two windows that draw live keys (the mapping canvas and the
        virtual keyboard) that something went down. */
    void showLiveKeyOnMidiCanvas (int midiNote, float velocity);

    /** What the last live note-on did (self test only). */
    LiveNoteRecordForTest lastLiveNote;

    // ---- MIDI 录制 ----------------------------------------------------------
    /** A note that has been pressed (and written) but not yet released. */
    struct PendingRecord
    {
        juce::int64 trackId   = 0;
        juce::int64 startTick = 0;    // absolute
        juce::int64 clipId    = 0;    // 片段内: the clip it goes into; 片段外: 0 until made
        /** The velocity the key was struck with (the old code wrote a flat 100). */
        int         velocity  = 100;
        /** The arpeggiator's own note length for this step, or 0 to measure the
            time the key was really held down (a hand-played note). */
        juce::int64 fixedLengthTicks = 0;
    };

    std::map<int, PendingRecord> pendingRecords;

    /** One open controller-recording gesture per controller number: the first
        change pushes the undo snapshot, a change after 300 ms of quiet starts the
        next one. */
    struct ControllerGesture
    {
        bool      open = false;
        juce::int64 lastMs = 0;
    };

    std::array<ControllerGesture, 128> controllerGestures {};

    // ---- 琶音器 --------------------------------------------------------------
    /** What the arpeggiator is doing right now.

        The settings themselves live in the project (`midiInput.arp`); everything
        here is the run: which keys are held, the phrase they make, where in the
        phrase we are, and the note that is sounding. The arpeggiator produces
        ordinary **live notes**, so what is heard and what a take records are the
        same thing.
    */
    struct ArpRun
    {
        /** The notes the phrase cycles (the latch's set, not the held keys). */
        midiarp::LatchState latch;
        /** The phrase in play order, or empty when the arpeggiator is off. */
        std::vector<int> order;
        /** The grid slot the phrase started on: step 0 is the first slot after it. */
        juce::int64 phraseStartSlot = 0;
        /** The tick the phrase started on, so step 0 records where the player
            pressed rather than the slot line before it. */
        juce::int64 phraseStartTick = 0;
        /** The last slot that has been played, so a slot fires exactly once. */
        juce::int64 lastSlot = -1;
        /** Its own clock while the transport is stopped: ticks, and the wall clock
            reading they were last advanced from (0 = seed it on the next tick). */
        juce::int64 freeTicks = 0;
        double lastWallSeconds = 0.0;
        /** The note sounding right now, its voices, and when it must stop. */
        int    soundingNote = -1;
        std::vector<HeldMidiNote> soundingVoices;
        juce::int64 soundingOffTick = 0;
        /** Where that note started and how long it is meant to be, in ticks: what
            the recorder writes down (the note-on itself lands on an audio block
            boundary, the tick does not). */
        juce::int64 soundingStartTick = 0;
        juce::int64 soundingLengthTicks = 0;
        /** The velocity the phrase was struck with. */
        int lastPlayedVelocity = 100;
        /** How many steps have been played since the arpeggiator started (the self
            test counts with it). */
        int firedSteps = 0;
        /** When >= 0 the clock is pinned here instead of being read from the
            transport (self test only). */
        juce::int64 forcedClockTicks = -1;
    };

    ArpRun arp;
    juce::uint32 arpSeed = 1;

    /** A 1 ms pulse for the arpeggiator. It only runs while the arpeggiator has
        something to play, so an idle project costs nothing (the same rule the
        wavetable cursor and the meters follow). */
    class ArpTicker : private juce::Timer
    {
    public:
        explicit ArpTicker (MainComponent& ownerToUse) : owner (ownerToUse) {}
        ~ArpTicker() override { stopTimer(); }

        void setRunning (bool shouldRun)
        {
            if (shouldRun == isTimerRunning())
                return;

            if (shouldRun)
                startTimer (1);
            else
                stopTimer();
        }

    private:
        void timerCallback() override { owner.arpTick(); }

        MainComponent& owner;
    };

    std::unique_ptr<ArpTicker> arpTicker;

    /** One pulse of the arpeggiator: let the sounding note go when its gate is up,
        then play the step of the slot the clock is in. */
    void arpTick();
    /** Releases the note that is sounding (one live note-off per voice). */
    void arpNoteOff();
    /** Silences the arpeggiator and forgets where it was. */
    void stopArpeggiatorNotes();
    /** Rebuilds the phrase from the keys that are down (and the latch) and restarts
        it: called whenever the held keys or the settings change. */
    void refreshArpeggiator();
    /** The clock the steps are counted on: the transport's own tick while it runs,
        the arpeggiator's own tick counter while it does not. */
    juce::int64 arpClockTicks (const music::TempoMap& map);
    /** The transport has stopped: the arpeggiator keeps going, on its own clock. */
    void arpTransportStopped();
    /** A settings change from either window's 琶音 controls. */
    void arpSettingsChanged();
    /** The windows that draw the arpeggiator's controls, so a change in one is
        reflected in the other. */
    void refreshArpWindows();

    /** Writes one controller value into the clip this take is landing in. */
    void recordControllerValue (int controller, int value);
    /** Puts every open controller gesture back to "closed" (recording stopped). */
    void closeControllerGestures();

    /** The transport state the last timer tick saw, so "it has just stopped" is
        noticed exactly once (the controller gesture and the arpeggiator both care). */
    bool transportWasPlaying = false;

    /** Commits one recorded note at the moment its key is released (one edit). */
    void finishRecordedNote (int midiNote, juce::int64 endTick);
    /** Drops (or commits) whatever is still held when recording is switched off. */
    void flushPendingRecords();
    /** Where the playhead is right now, in ticks, snapped like a written note. */
    juce::int64 recordTickNow() const;

    /** The windows this app-wide key listener lives on, so each one is given it
        exactly once (new windows appear all the time: the sound window, the
        effects window, the mixer and the plugin windows are all made on demand). */
    std::vector<juce::Component::SafePointer<juce::Component>> keyListenerWindows;

    juce::TooltipWindow tooltipWindow { this, 700 };

    bool scheduleDirty = true;
    bool showDialogs = true;
    bool syncingReferenceEntries = false;
    bool updatingVstFlags = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace nidaw
