#include "MainComponent.h"
#include "Theme.h"
#include "PluginPicker.h"

#include "../Audio/Exporter.h"
#include "../Core/ControllerMath.h"
#include "../Core/WindowPlacement.h"
#include "../Core/WavetableTables.h"
#include "../Core/WaveformPresets.h"
#include "../Core/FmPresets.h"
#include "../Core/SubtractivePresets.h"
#include "../Core/WavetablePresets.h"
#include "../Core/MinimoogPresets.h"
#include "../Core/HybridPresets.h"
#include "../Core/GmPrograms.h"

namespace hmdaw
{

namespace
{
    /** Where the export log goes (the self test points it at its own folder). */
    juce::File& exportLogFileOverride()
    {
        static juce::File file;
        return file;
    }

    /** The export log stays small and readable. */
    constexpr juce::int64 kExportLogLimitBytes = 256 * 1024;

    /** How long a controller has to stay still before the next change counts as a
        new recording gesture (and so a new undo step). */
    constexpr juce::int64 kControllerGestureGapMs = 300;

    /** Serialises the log: the render thread writes to it as well. */
    juce::CriticalSection& exportLogLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    // The names double as the on-screen labels and as the keys AlertWindow uses
    // to hand the ComboBox back.
    const juce::String formatLabel = NI_TXT ("格式");
    const juce::String depthLabel  = NI_TXT ("位深");
    const juce::String rateLabel   = NI_TXT ("采样率");

    /** The bit-depth entries a format offers, as they read in the combo box. */
    juce::StringArray depthItemsFor (ExportSettings::Format format)
    {
        juce::StringArray items;

        for (auto depth : ExportSettings::bitDepthsFor (format))
            items.add (depth >= 32 ? NI_TXT ("32 位浮点")
                                   : juce::String (depth) + NI_TXT (" 位"));

        return items;
    }

    constexpr int kToolbarHeight = 44;
    constexpr int kTransportHeight = 68;

    /** Renders the project on a background thread with a progress window. */
    /** Renders in the background behind the usual progress window.

        `isFolder` means "write a batch" (one file per chosen track plus the master
        mix); otherwise a single file is written.
    */
    class ExportTask : public juce::ThreadWithProgressWindow
    {
    public:
        ExportTask (Project projectToRender, juce::File target, juce::String projectNameToUse,
                    ExportSettings settingsToUse, bool targetIsFolder,
                    HostedInstrumentFactory factoryToUse,
                    HostedEffectFactory effectFactoryToUse)
            : juce::ThreadWithProgressWindow (NI_TXT ("导出音频"), true, true),
              project (std::move (projectToRender)),
              destination (std::move (target)),
              baseName (projectNameToUse),
              settings (settingsToUse),
              isFolder (targetIsFolder),
              factory (std::move (factoryToUse)),
              effectFactory (std::move (effectFactoryToUse))
        {
        }

        void run() override
        {
            setStatusMessage (NI_TXT ("正在离线渲染，请稍候…"));

            // The live plugins' state has to be read on the message thread, and for a
            // big sampler that read can take seconds - or never come back. It happens
            // *inside* this window, with a message on screen, so a plugin that makes
            // the app look frozen at least says which step it is stuck on (and the
            // export log names the track).
            if (captureLiveStates != nullptr)
            {
                setStatusMessage (NI_TXT ("正在准备插件状态，请稍候…"));
                juce::Thread::sleep (220);   // let the progress window paint the message
                juce::MessageManager::callSync ([this] { captureLiveStates (project); });
                /** The state in `project` is now the one the guard compares against. */
                capturedLiveStates = true;
                setStatusMessage (NI_TXT ("正在离线渲染，请稍候…"));
            }

            auto onProgress = [this] (double fraction) { setProgress (fraction); };
            auto shouldAbort = [this] { return threadShouldExit(); };

            if (isFolder)
            {
                succeeded = Exporter::renderSelection (project, destination, baseName, settings,
                                                       onProgress, shouldAbort,
                                                       errorMessage, writtenFiles,
                                                       &skippedTracks, factory, effectFactory);
                return;
            }

            succeeded = Exporter::renderToFile (project, destination, settings, settings.trackIds,
                                                onProgress, shouldAbort, errorMessage, factory,
                                                effectFactory);

            if (succeeded)
                writtenFiles.push_back (destination);
        }

        bool succeeded = false;
        juce::String errorMessage;
        std::vector<juce::File> writtenFiles;
        /** Tracks the batch left out because mute / solo keeps them silent. */
        std::vector<juce::String> skippedTracks;

        /** Reads the live plugins' state into the project copy being rendered. Runs on
            the message thread, from inside this window. */
        std::function<void (Project&)> captureLiveStates;
        /** True once the block above has run: only then is the copy's state the one
            the live instances should be compared with (and put back to). */
        bool capturedLiveStates = false;
        /** The project the render used - i.e. the copy with the captured state in it. */
        const Project& getRenderedProject() const noexcept { return project; }

    private:
        Project project;
        juce::File destination;
        juce::String baseName;
        ExportSettings settings;
        bool isFolder = false;
        /** Makes a private plugin instance for each file the render writes. */
        HostedInstrumentFactory factory;
        /** The same for the insert chains. */
        HostedEffectFactory effectFactory;
    };

    /** What to export, as the dialog shows it: a master tick box plus one tick box
        per track, inside a scrolling list.
    */
    class ExportContentsList : public juce::Component
    {
    public:
        ExportContentsList (const Project& project, int maximumHeight)
        {
            addAndMakeVisible (masterButton);
            masterButton.setButtonText (NI_TXT ("总混音"));
            masterButton.setToggleState (true, juce::dontSendNotification);

            for (const auto& track : project.tracks)
            {
                auto* box = trackButtons.add (new juce::ToggleButton (track.name));
                box->setToggleState (true, juce::dontSendNotification);
                listHolder.addAndMakeVisible (box);
            }

            listHolder.setSize (200, juce::jmax (1, trackButtons.size() * kRowHeight));
            viewport.setViewedComponent (&listHolder, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);

            const auto rows = juce::jmax (1, trackButtons.size());

            // The 导出 / 取消 buttons sit below this list, and the alert window caps
            // its own height at the screen. On a short display the list gives way
            // (and scrolls) instead of pushing the buttons off the bottom.
            setSize (260, kRowHeight + juce::jlimit (kRowHeight, 200,
                                                     juce::jmin (rows * kRowHeight,
                                                                 maximumHeight - kRowHeight)));
        }

        void resized() override
        {
            auto area = getLocalBounds();
            masterButton.setBounds (area.removeFromTop (kRowHeight).reduced (2, 0));
            viewport.setBounds (area);

            auto y = 0;

            for (auto* box : trackButtons)
            {
                box->setBounds (2, y, juce::jmax (40, viewport.getMaximumVisibleWidth() - 8),
                                kRowHeight);
                y += kRowHeight;
            }

            listHolder.setSize (juce::jmax (40, viewport.getMaximumVisibleWidth() - 4), y);
        }

        bool wantsMaster() const { return masterButton.getToggleState(); }

        std::set<juce::int64> chosenTracks (const Project& project) const
        {
            std::set<juce::int64> ids;

            for (int i = 0; i < trackButtons.size() && i < (int) project.tracks.size(); ++i)
                if (trackButtons[i]->getToggleState())
                    ids.insert (project.tracks[(size_t) i].id);

            return ids;
        }

    private:
        static constexpr int kRowHeight = 22;

        juce::ToggleButton masterButton;
        juce::Viewport viewport;
        juce::Component listHolder;
        juce::OwnedArray<juce::ToggleButton> trackButtons;
    };
}

MainComponent::MainComponent (bool showDialogsToUse, bool openAudioDevice)
    : audio (document),
      toolbar (document),
      trackArea (document, appState, audio, pluginHost),
      effectsBar (document, appState, pluginHost),
     transportBar (document, appState, audio),
      editorPanel (document, appState, audio, presetStore),
      studioPage (document, audio, appState),
      routingPage(),
      mixerPage (document, audio, appState)
{
    showDialogs = showDialogsToUse;
    editorPanel.getOptionsPanel().showDialogs = showDialogs;
    editorPanel.showDialogs = showDialogs;

    // 琶音器: its pulse only runs while it has something to play (see arpTick).
    arpTicker = std::make_unique<ArpTicker> (*this);

    // MIDI 输入: the device list and the remembered device. The self test points
    // the settings file somewhere harmless first (see its run()), so this never
    // opens real hardware there.
    midiInput.setMessageCallback ([this] (const juce::MidiMessage& message)
    {
        handleMidiMessage (message);
    });
    midiInput.loadSettings();

    presetStore.load();

    // The engine asks the host for the instances of the VST tracks, and the host
    // tells the editor whenever one of them appears or fails.
    audio.setLiveProvider (&pluginHost);

    pluginHost.onInstrumentChanged = [this]
    {
        // A load can finish while the engine is in the middle of rebuilding its
        // schedule; going through the message queue keeps that from re-entering.
        juce::Component::SafePointer<MainComponent> safeThis (this);

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->refreshVstLoadedFlags();
            safeThis->audio.rebuildSchedule();

            if (safeThis->pluginWindow != nullptr && safeThis->pluginWindow->isVisible())
                safeThis->pluginWindow->refresh();
        });
    };

    addAndMakeVisible (toolbar);
    addAndMakeVisible (trackArea);
    addAndMakeVisible (transportBar);
    addChildComponent (editorPanel);
    addChildComponent (studioPage);
    addChildComponent (routingPage);
    addChildComponent (mixerPage);

    toolbar.onNewProject   = [this] { newProject(); };
    toolbar.onOpenProject  = [this] { openProject(); };
    toolbar.onSaveProject  = [this] { saveProject (false); };
    toolbar.onSaveProjectAs = [this] { saveProject (true); };
    toolbar.onExportWav    = [this] { exportWav(); };
    toolbar.onOpenMidiInput = [this] { openMidiInputWindow(); };
    toolbar.onWorkspaceChanged = [this] (AppState::Workspace workspace)
    {
        setWorkspace (workspace);
    };
    toolbar.setWorkspace (appState.workspace);

    transportBar.onOpenMidiInput = [this] { openMidiInputWindow(); };
    transportBar.onOpenVirtualKeyboard = [this] { openVirtualKeyboardWindow(); };

    trackArea.onEditTimbre = [this] (juce::int64 trackId) { openSynthWindow (trackId); };
    trackArea.onCopyClip  = [this] (juce::int64 clipId) { copyClipToClipboard (clipId); };
    trackArea.onEditEffects = [this] (juce::int64 trackId) { openEffectsWindow (trackId); };
    trackArea.onTrackMenu = [this] (juce::int64 trackId) { showTrackMenu (trackId); };
    trackArea.onReplaceInstrument = [this] (juce::int64 trackId) { replaceInstrument (trackId); };

    effectsBar.onAddEffect  = [this] (juce::int64 trackId) { pickEffectFor (trackId); };
    effectsBar.onEditEffect = [this] (juce::int64 trackId, int slotIndex)
    {
        openEffectSlot (trackId, slotIndex);
    };

    // The clip menu's two dialog items: 重复… and 重命名… (both used to live in the
    // clip strip down the right of the arrangement).
    trackArea.onRepeatClip = [this] (juce::int64) { showClipRepeatDialog(); };
    trackArea.onRenameClip = [this] (juce::int64 clipId) { showClipRenameDialog (clipId); };

    // The three 编辑 buttons: two in the preset panel (参考组 / 参考线组) and one on
    // each 参考线组 status row. All three turn the left column into the editor.
    editorPanel.getPresetPanel().onEditPreset = [this] (bool lineGroup, int index)
    {
        openGroupEditorForPreset (lineGroup, index);
    };

    editorPanel.getOptionsPanel().onEditLineGroup = [this] (juce::int64 lineId)
    {
        openGroupEditorForLineGroup (lineId);
    };

    {
        auto& groupEditor = editorPanel.getGroupEditorPanel();
        groupEditor.onBack   = [this] { closeGroupEditor (true); };
        groupEditor.onSave   = [this] (const juce::String& name) { saveGroupEditor (name); };
        groupEditor.onSaveAs = [this] (const juce::String& name) { saveGroupEditorAs (name); };
    }

    layout.setItemLayout (0, 120, -1.0, -0.5);
    layout.setItemLayout (1, 6, 6, 6);
    layout.setItemLayout (2, 140, -1.0, -0.5);

    // Item 1 sits between the arrangement and the editor, so it is a *horizontal*
    // bar: JUCE then reads the drag's vertical distance. Passing true here (a
    // vertical bar) made the editor height respond to left/right dragging.
    resizer = std::make_unique<juce::StretchableLayoutResizerBar> (&layout, 1, false);
    addChildComponent (*resizer);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    if (! document.getProject().tracks.empty())
        appState.selectedTrackId = document.getProject().tracks.front().id;

    // The offscreen self test drives the engine one block at a time; opening a real
    // device would have its callback render the same engine concurrently, which made
    // the measurements (levels, voice counts, channel allocation) flaky.
    const auto audioError = openAudioDevice ? audio.initialise() : juce::String();

    if (audioError.isNotEmpty() && showDialogs)
    {
        juce::MessageManager::callAsync ([this, audioError]
        {
            showMessage (NI_TXT ("音频设备"),
                         NI_TXT ("无法初始化音频设备：") + audioError
                             + NI_TXT ("\n\n界面仍可使用，插好声卡后可在底部「音频设置」中重新选择。"),
                         true);
        });
    }

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audio.shutdown();
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void MainComponent::parentHierarchyChanged()
{
    // The main window is the first one this exists inside, so this is where the
    // app-wide key listener gets hung for the very first time.
    attachKeyListenersToWindows();
}

bool MainComponent::listensToWindow (const juce::Component* window) const
{
    for (const auto& entry : keyListenerWindows)
        if (entry.getComponent() == window)
            return true;

    return false;
}

void MainComponent::attachKeyListenersToWindows()
{
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        auto* window = juce::TopLevelWindow::getTopLevelWindow (i);

        if (window == nullptr || listensToWindow (window))
            continue;

        // A dialog owns its own space bar (it presses the focused button), and a
        // modal alert is running its own loop: neither is a place for "play".
        if (dynamic_cast<juce::AlertWindow*> (window) != nullptr)
            continue;

        window->addKeyListener (this);
        keyListenerWindows.push_back (window);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    juce::ignoreUnused (origin);

    // The app-wide half of the space bar: every window HM opens carries this
    // listener, so the transport answers wherever the focus is - except in a text
    // box (typing a space is typing) and inside a plugin's own interface (Kontakt
    // and Serum audition with the space bar, so that one is theirs).
    if (! isTransportSpaceKey (key))
        return false;

    audio.togglePlay();
    transportBar.refresh();
    return true;
}

bool MainComponent::keyStateChanged (bool isKeyDown, juce::Component* origin)
{
    juce::ignoreUnused (isKeyDown, origin);

    // The computer keys are read as a *poll* (see VirtualKeyboardPanel), which is
    // what lets the keyboard keep playing while another window holds the focus.
    // This forward starts the note with the key event instead of up to 33 ms later.
    // The key itself is never swallowed, so the focused control keeps working.
    if (virtualKeyboardWindow != nullptr && virtualKeyboardWindow->isVisible())
        virtualKeyboardWindow->getPanel().pollTypingNow();

    return false;
}

bool MainComponent::focusIsInsidePluginInterface (juce::Component* focused) const
{
    if (pluginWindow == nullptr || ! pluginWindow->isVisible())
        return false;

    auto* editor = pluginWindow->getEditorComponent();

    if (editor == nullptr)
        return false;

    return editor == focused || editor->isParentOf (focused);
}

bool MainComponent::isTransportSpaceKey (const juce::KeyPress& key) const
{
    return isTransportSpaceKeyFor (key, juce::Component::getCurrentlyFocusedComponent());
}

bool MainComponent::isTransportSpaceKeyFor (const juce::KeyPress& key,
                                            juce::Component* focused) const
{
    if (key.getKeyCode() != juce::KeyPress::spaceKey
        || key.getModifiers().isAnyModifierKeyDown())
        return false;

    // A text box keeps the space bar: that is typing, not transport. The desktop
    // only gets this far when the box did not want the key (a real key press
    // arrives with its character, so typing never reaches here at all), but a
    // synthetic key has no character - and being explicit keeps the rule the same
    // whichever way the key arrived.
    for (auto* component = focused; component != nullptr;
         component = component->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (component) != nullptr)
            return false;

    return ! focusIsInsidePluginInterface (focused);
}

bool MainComponent::sendKeyPressForTest (const juce::KeyPress& key, juce::Component* focused)
{
    if (focused == nullptr)
        focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused == nullptr)
        focused = this;

    for (auto* target = focused; target != nullptr; target = target->getParentComponent())
    {
        if (listensToWindow (target) && keyPressed (key, target))
            return true;

        if (target->keyPressed (key))
            return true;
    }

    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The 2nd and 4th kinds of reference line follow the note selection. The guard
    // stops the change message this emits from re-entering.
    if (! syncingReferenceEntries)
    {
        const juce::ScopedValueSetter<bool> guard (syncingReferenceEntries, true);
        syncReferenceEntries();
    }

    scheduleDirty = true;

    // Disarming 录制 (the button or the `*` key) closes the take: whatever is still
    // held gets written with the length it was held for.
    if (! appState.midiRecordArmed && ! pendingRecords.empty())
        flushPendingRecords();

    // The take is over: the next controller change starts a new undo step rather
    // than joining the one that was still open.
    if (! appState.midiRecordArmed)
        closeControllerGestures();

    resized();
}

//==============================================================================
// The group editor (参考组编辑栏 / 参考线组编辑栏).
//
// Three places open it: the 编辑 button on a 参考组预设 row, the same button on a
// 参考线组预设 row, and the 编辑 button on a 参考线组 status row. All three land
// here, because what the editor works on is the project's reference GROUP in every
// case: the segment and the canvas both read it, so the copy written there while
// the editor is open *is* the live preview. AppState::GroupEditor is the session
// that remembers where the copy came from and what 返回 puts back.

namespace
{
    /** A 参考线组 status entry as the group the editor works on. */
    ReferenceLineGroup lineGroupFromEntry (const ReferenceLineEntry& entry)
    {
        ReferenceLineGroup group;
        group.name           = entry.name;
        group.anchorHz       = entry.anchorHz;
        group.anchorPosition = entry.anchorPosition;
        group.dots           = entry.dots;
        group.visible        = entry.visible;
        return group;
    }
}

void MainComponent::openGroupEditorForPreset (bool lineGroup, int presetIndex)
{
    AppState::GroupEditor session;
    ReferenceGroup working;

    if (lineGroup)
    {
        const auto& list = presetStore.getReferenceLineGroups();

        if (presetIndex < 0 || presetIndex >= (int) list.size())
            return;

        const auto& group = list[(size_t) presetIndex];

        session.kind           = AppState::GroupEditor::Kind::referenceLineGroup;
        session.presetIndex    = presetIndex;
        session.previousGroup  = document.getProject().referenceGroup;
        session.anchorHz       = group.anchorHz;
        session.anchorPosition = group.anchorPosition;

        working.name = group.name;
        working.dots = group.dots;
    }
    else
    {
        const auto& list = presetStore.getReferenceGroups();

        if (presetIndex < 0 || presetIndex >= (int) list.size())
            return;

        working = list[(size_t) presetIndex];

        session.kind          = AppState::GroupEditor::Kind::referenceGroup;
        session.presetIndex   = presetIndex;
        session.previousGroup = document.getProject().referenceGroup;
    }

    // A derived edit, so the preview never lands in the undo stack: opening an
    // editor is looking, not editing.
    document.applyDerivedEdit ([working] (Project& project) { project.referenceGroup = working; });
    appState.openGroupEditor (session);
    editorPanel.refresh();
}

void MainComponent::openGroupEditorForLineGroup (juce::int64 lineId)
{
    const auto* entry = document.getProject().findReferenceLine (lineId);

    if (entry == nullptr || entry->kind != ReferenceLineKind::group)
        return;

    const auto group = lineGroupFromEntry (*entry);

    AppState::GroupEditor session;
    session.kind           = AppState::GroupEditor::Kind::referenceLineGroup;
    session.lineId         = lineId;
    session.previousGroup  = document.getProject().referenceGroup;
    session.anchorHz       = group.anchorHz;
    session.anchorPosition = group.anchorPosition;

    ReferenceGroup working;
    working.name = group.name;
    working.dots = group.dots;

    document.applyDerivedEdit ([working] (Project& project) { project.referenceGroup = working; });
    appState.openGroupEditor (session);
    editorPanel.refresh();
}

void MainComponent::closeGroupEditor (bool restorePreviousGroup)
{
    if (! appState.groupEditor.isOpen())
        return;

    if (restorePreviousGroup)
    {
        const auto previous = appState.groupEditor.previousGroup;

        // 返回 drops the preview the same way it was laid down: no undo step.
        document.applyDerivedEdit ([previous] (Project& project) { project.referenceGroup = previous; });
    }

    appState.closeGroupEditor();
    editorPanel.refresh();
}

void MainComponent::saveGroupEditor (const juce::String& name)
{
    if (! appState.groupEditor.isOpen())
        return;

    const auto session = appState.groupEditor;
    auto working = document.getProject().referenceGroup;

    if (name.isNotEmpty())
        working.name = name;

    if (session.isLineGroup())
    {
        ReferenceLineGroup group;
        group.name           = working.name;
        group.anchorHz       = session.anchorHz;
        group.anchorPosition = session.anchorPosition;
        group.dots           = working.dots;
        group.visible        = true;

        if (session.lineId != 0)
        {
            // One of the project's own entries: one edit, so one Ctrl+Z puts the
            // whole group back.
            document.applyEdit (NI_TXT ("编辑参考线组"),
                                [id = session.lineId, group] (Project& project)
            {
                if (auto* target = project.findReferenceLine (id))
                {
                    target->name           = group.name;
                    target->anchorHz       = group.anchorHz;
                    target->anchorPosition = group.anchorPosition;
                    target->dots           = group.dots;
                }
            });
        }
        else
        {
            presetStore.replaceReferenceLineGroup (session.presetIndex, group);
        }
    }
    else
    {
        presetStore.replaceReferenceGroup (session.presetIndex, working);
    }

    // The working copy stays on the project's reference group: it is the set of
    // lines the user was just looking at.
    document.applyDerivedEdit ([working] (Project& project) { project.referenceGroup = working; });

    appState.closeGroupEditor();
    editorPanel.refresh();
}

void MainComponent::saveGroupEditorAs (const juce::String& name)
{
    if (! appState.groupEditor.isOpen())
        return;

    const auto session = appState.groupEditor;
    auto working = document.getProject().referenceGroup;

    if (name.isNotEmpty())
        working.name = name;

    if (session.isLineGroup())
    {
        ReferenceLineGroup group;
        group.name           = working.name;
        group.anchorHz       = session.anchorHz;
        group.anchorPosition = session.anchorPosition;
        group.dots           = working.dots;
        group.visible        = true;

        presetStore.addReferenceLineGroup (std::move (group));
    }
    else
    {
        presetStore.addReferenceGroup (working);
    }

    document.applyDerivedEdit ([working] (Project& project) { project.referenceGroup = working; });

    appState.closeGroupEditor();
    editorPanel.refresh();
}

void MainComponent::syncReferenceEntries()
{
    const auto& project = document.getProject();

    // 独显 names a row by id. If that row is gone (another project was opened, the
    // row was deleted) the id has to go too: resolveReferenceEntries would keep
    // filtering every *other* row out and the canvas would end up empty.
    if (appState.soloReferenceLineId != 0
        && project.findReferenceLine (appState.soloReferenceLineId) == nullptr)
        appState.setSoloReferenceLine (0);

    // A drum track's notes carry a pad index instead of a meaningful frequency,
    // so the frequency driven kinds (2nd and 4th) have nothing to follow there.
    if (const auto* track = project.findTrack (appState.selectedTrackId);
        track != nullptr && track->kind == TrackKind::drums)
    {
        const auto isDerived = [] (const ReferenceLineEntry& entry)
        {
            return ! entry.locked && (entry.kind == ReferenceLineKind::noteRatio
                                      || entry.kind == ReferenceLineKind::multiNote);
        };

        auto hasDerived = false;

        for (const auto& entry : project.referenceLines)
            if (isDerived (entry))
                hasDerived = true;

        if (hasDerived)
            document.applyDerivedEdit ([isDerived] (Project& p)
            {
                auto& lines = p.referenceLines;
                lines.erase (std::remove_if (lines.begin(), lines.end(), isDerived), lines.end());
            });

        return;
    }

    const auto selected = selectedNotesOf (project, appState.selectedClipId,
                                           appState.selectedNoteIds);

    const auto hasLockedKind = [&project] (ReferenceLineKind kind)
    {
        for (const auto& entry : project.referenceLines)
            if (entry.kind == kind && entry.locked)
                return true;

        return false;
    };

    // Which derived entry the selection implies, if the exclusive rule allows it:
    // a sealed entry of the other kind wins and simply blocks the derived one.
    auto wanted = ReferenceLineKind::noteRatio;
    auto wantEntry = false;

    if (selected.size() == 1 && ! hasLockedKind (ReferenceLineKind::multiNote))
    {
        wanted = ReferenceLineKind::noteRatio;
        wantEntry = true;
    }
    else if (selected.size() >= 2 && ! hasLockedKind (ReferenceLineKind::noteRatio))
    {
        wanted = ReferenceLineKind::multiNote;
        wantEntry = true;
    }

    const auto isDerived = [] (const ReferenceLineEntry& entry)
    {
        return ! entry.locked && (entry.kind == ReferenceLineKind::noteRatio
                                  || entry.kind == ReferenceLineKind::multiNote);
    };

    // What the row that follows the selection was told last time. 显示 / 隐藏 and
    // 独显 are statements about that lens itself, not about the one note that
    // happened to be selected when it was set, so they survive a change of
    // selection - including the change from one note to several that replaces the
    // 2nd kind with the 4th.
    auto havePrevious = false;
    auto previousVisible = true;
    auto previousWasSoloed = false;

    // Keep the row where it is (and keep its id) while the kind does not change.
    juce::int64 keepId = 0;

    for (const auto& entry : project.referenceLines)
        if (isDerived (entry))
        {
            havePrevious = true;
            previousVisible = entry.visible;
            previousWasSoloed = appState.soloReferenceLineId == entry.id;

            appState.derivedEntryVisible = entry.visible;
            appState.derivedEntrySoloed  = previousWasSoloed;

            if (wantEntry && entry.kind == wanted)
                keepId = entry.id;

            break;
        }

    auto desired = ReferenceLineEntry();

    if (wantEntry)
    {
        if (wanted == ReferenceLineKind::multiNote)
        {
            desired = music::makeMultiNoteEntry (keepId, selected);
        }
        else
        {
                desired = music::makeNoteRatioEntry (keepId, selected.front(),
                                                     appState.currentRatio);
        }
    }

    // Inherit 显示 / 隐藏 from the row this one replaces, or - when there is no row
    // right now because the selection was cleared - from the last one there was.
    desired.visible = havePrevious ? previousVisible : appState.derivedEntryVisible;

    desired.id = keepId;

    // Nothing to do when the stored list already says exactly this.
    auto derivedCount = 0;

    for (const auto& entry : project.referenceLines)
        if (isDerived (entry))
            ++derivedCount;

    if (derivedCount == (wantEntry ? 1 : 0))
    {
        const auto* stored = project.findReferenceLine (keepId);

        if (! wantEntry || (stored != nullptr && stored->name == desired.name
                            && stored->visible == desired.visible))
            return;
    }

    const auto entry = desired;
    const auto wantedKind = wanted;
    const auto inheritSolo = havePrevious ? previousWasSoloed : appState.derivedEntrySoloed;
    juce::int64 freshId = 0;

    document.applyDerivedEdit ([entry, keepId, wantEntry, wantedKind, isDerived, &freshId]
                               (Project& p)
    {
        auto& lines = p.referenceLines;

        auto insertAt = (int) lines.size();

        for (int i = 0; i < (int) lines.size(); ++i)
            if (isDerived (lines[(size_t) i]))
            {
                insertAt = i;
                break;
            }

        lines.erase (std::remove_if (lines.begin(), lines.end(), isDerived), lines.end());

        if (! wantEntry)
            return;

        auto fresh = entry;
        fresh.kind    = wantedKind;
        fresh.locked  = false;
        fresh.id      = keepId != 0 ? keepId : p.nextReferenceLineId++;
        freshId = fresh.id;

        insertAt = juce::jlimit (0, (int) lines.size(), insertAt);
        lines.insert (lines.begin() + insertAt, std::move (fresh));
    });

    // Hand a 独显 over to the rebuilt row (its id changes when the kind does), or
    // drop it if the row it named is gone.
    if (inheritSolo && freshId != 0)
        appState.setSoloReferenceLine (freshId);
    else if (appState.soloReferenceLineId != 0
             && document.getProject().findReferenceLine (appState.soloReferenceLineId) == nullptr)
        appState.setSoloReferenceLine (0);
}

void MainComponent::timerCallback()
{
    if (scheduleDirty)
    {
        audio.rebuildSchedule();
        scheduleDirty = false;
    }

    // Windows come and go (the sound window, the effects window, the mixer, the
    // plugin windows), so the app-wide key listener is re-hung here: the list is
    // tiny and each window is only added to once.
    attachKeyListenersToWindows();

    transportBar.refresh();
    editorPanel.refreshPlayhead();

    const auto playingNow = audio.isPlaying();

    if (transportWasPlaying && ! playingNow)
    {
        // The take is over even if 录制 is still armed: a controller gesture that
        // was still open ends here. The arpeggiator does not stop - it swaps to its
        // own clock, so a chord held across the stop carries on playing.
        closeControllerGestures();
        arpTransportStopped();
    }

    transportWasPlaying = playingNow;

    if (playingNow)
        trackArea.repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBackground);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (kToolbarHeight));
    transportBar.setBounds (area.removeFromBottom (kTransportHeight));

    const auto workspace = appState.workspace;
    const auto arrangement = workspace == AppState::Workspace::arrange;

    trackArea.setVisible (arrangement);
    editorPanel.setVisible (arrangement && appState.editorOpen);
    resizer->setVisible (arrangement && appState.editorOpen);
    studioPage.setVisible (workspace == AppState::Workspace::studio);
    routingPage.setVisible (workspace == AppState::Workspace::routing);
    mixerPage.setVisible (workspace == AppState::Workspace::mixer);

    if (workspace == AppState::Workspace::studio)
    {
        studioPage.setBounds (area);
        return;
    }

    if (workspace == AppState::Workspace::routing)
    {
        routingPage.setBounds (area);
        return;
    }

    if (workspace == AppState::Workspace::mixer)
    {
        mixerPage.setBounds (area);
        return;
    }

    if (appState.editorOpen)
    {
        juce::Component* components[] = { &trackArea, resizer.get(), &editorPanel };
        layout.layOutComponents (components, 3, area.getX(), area.getY(),
                                 area.getWidth(), area.getHeight(), true, true);
    }
    else
    {
        trackArea.setBounds (area);
    }
}

bool MainComponent::confirmDiscardChanges()
{
    if (! document.isModified())
        return true;

    juce::AlertWindow window (NI_TXT ("未保存的修改"),
                              NI_TXT ("当前工程有未保存的修改，确定要放弃吗？"),
                              juce::MessageBoxIconType::WarningIcon, this);
    window.addButton (NI_TXT ("放弃修改"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    return window.runModalLoop() == 1;
}

void MainComponent::showMessage (const juce::String& title, const juce::String& message, bool isWarning)
{
    juce::AlertWindow window (title, message,
                              isWarning ? juce::MessageBoxIconType::WarningIcon
                                        : juce::MessageBoxIconType::InfoIcon,
                              this);
    window.addButton (NI_TXT ("好"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.runModalLoop();
}

void MainComponent::newProject()
{
    if (! confirmDiscardChanges())
        return;

    audio.stop();
    document.newProject();
    unloadPluginsForProjectChange();
    appState.clearNotes();
    appState.selectClip (0);
    appState.selectTrack (document.getProject().tracks.empty()
                              ? (juce::int64) 0
                              : document.getProject().tracks.front().id);
    appState.setEditorOpen (false);
    trackArea.reloadForNewProject();
    scheduleDirty = true;
    resized();
}

void MainComponent::unloadPluginsForProjectChange()
{
    // Order matters: the plugin window's editor is a child of a plugin instance, so
    // it goes first, and only then may the instances be unloaded.
    if (pluginWindow != nullptr)
        pluginWindow->closeForProjectChange();

    pluginHost.resetForProject();
}

void MainComponent::openProject()
{
    if (! confirmDiscardChanges())
        return;

    chooser = std::make_unique<juce::FileChooser> (
        NI_TXT ("打开 HM 工程"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.nprj");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fileChooser)
    {
        const auto result = fileChooser.getResult();

        if (result == juce::File())
            return;

        loadProjectFile (result);
    });
}

bool MainComponent::loadProjectFile (const juce::File& file)
{
    juce::String error;

    if (! document.loadFromFile (file, error))
    {
        showMessage (NI_TXT ("打开失败"), error, true);
        return false;
    }

    audio.stop();
    // The instances that are up belong to the project that was open before: they have
    // to go, or the tracks in the file would play the previous project's sound -
    // volume and timbre included.
    unloadPluginsForProjectChange();
    appState.clearNotes();
    // The old clip id can name a clip in the project that was just loaded (their
    // ids both start at 1), which would leave the clip inspector showing the
    // previous project's clip.
    appState.selectClip (0);
    appState.selectTrack (document.getProject().tracks.empty()
                              ? (juce::int64) 0
                              : document.getProject().tracks.front().id);
    // The lane headers are built from the project, and two projects often use the
    // same track ids: without the forced rebuild the instrument column would keep
    // showing the project that was open before.
    trackArea.reloadForNewProject();
    appState.setEditorOpen (false);
    scheduleDirty = true;
    resized();
    return true;
}

void MainComponent::saveProject (bool saveAs)
{
    const auto existing = document.getCurrentFile();

    if (! saveAs && existing != juce::File())
    {
        juce::String error;

        if (! writeProject (existing, error))
            showMessage (NI_TXT ("保存失败"), error, true);

        return;
    }

    chooser = std::make_unique<juce::FileChooser> (
        NI_TXT ("保存 HM 工程"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (document.getProjectName() + ".nprj"),
        "*.nprj");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fileChooser)
    {
        const auto result = fileChooser.getResult();

        if (result == juce::File())
        {
            // Cancelling the save-as also cancels a pending "save then quit".
            quitAfterSave = false;
            return;
        }

        juce::String error;

        if (! writeProject (result, error))
        {
            showMessage (NI_TXT ("保存失败"), error, true);
            quitAfterSave = false;
            return;
        }

        if (quitAfterSave)
        {
            quitAfterSave = false;

            if (auto* app = juce::JUCEApplication::getInstance())
                app->quit();
        }
    });
}

bool MainComponent::confirmQuit()
{
    if (! document.isModified())
        return true;

    // The offscreen self test must never block on a modal window.
    if (! showDialogs)
        return true;

    juce::AlertWindow window (NI_TXT ("未保存的修改"),
                              NI_TXT ("关闭前要保存「") + document.getProjectName() + NI_TXT ("」吗？"),
                              juce::MessageBoxIconType::WarningIcon, this);
    window.addButton (NI_TXT ("保存"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (NI_TXT ("不保存"), 2);
    window.addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    switch (window.runModalLoop())
    {
        case 1:
        {
            const auto existing = document.getCurrentFile();

            if (existing == juce::File())
            {
                // No file yet: the save-as dialog is asynchronous, so the app quits
                // from that callback once the file really is on disk.
                quitAfterSave = true;
                saveProject (true);
                return false;
            }

            juce::String error;

            if (! writeProject (existing, error))
            {
                showMessage (NI_TXT ("保存失败"), error, true);
                return false;
            }

            return true;
        }

        case 2:
            return true;   // discard

        default:
            return false;  // cancelled
    }
}

void MainComponent::copyClipToClipboard (juce::int64 clipId)
{
    const auto* clip = document.getProject().findClip (clipId);

    if (clip == nullptr)
        return;

    // The same clipboard Ctrl+C fills, so Ctrl+V pastes it at the playhead.
    appState.selectClip (clipId);
    appState.clearNotes();
    appState.clipClipboard = *clip;
    appState.hasClipClipboard = true;
    appState.lastClipboard = AppState::ClipboardKind::clips;
}

bool MainComponent::showExportDialog (ExportSettings& settings)
{
    const auto& project = document.getProject();

    // The dialog has three combo rows above the track list and the 导出 / 取消
    // buttons below it; on a short screen the list is what gives way, so the
    // buttons stay on it (see ExportContentsList).
    auto* contents = new ExportContentsList (project,
                                             juce::jmax (120, placement::displayAreaFor (this)
                                                                  .getHeight() - 300));

    juce::AlertWindow window (NI_TXT ("导出音频"),
                              NI_TXT ("选择格式，以及要导出的内容（勾选轨道会每轨导出一个文件）。"),
                              juce::MessageBoxIconType::NoIcon, this);

    const auto formatItems = juce::StringArray { "WAV", "AIFF", "FLAC" };
    window.addComboBox (formatLabel, formatItems, {});

    window.addComboBox (depthLabel, depthItemsFor (settings.format), {});

    const juce::StringArray rateItems { NI_TXT ("工程采样率"), "44100 Hz", "48000 Hz",
                                        "88200 Hz", "96000 Hz", "192000 Hz" };
    window.addComboBox (rateLabel, rateItems, {});

    auto* formatBox = window.getComboBoxComponent (formatLabel);
    auto* depthBox  = window.getComboBoxComponent (depthLabel);
    auto* rateBox   = window.getComboBoxComponent (rateLabel);

    if (formatBox != nullptr)
        formatBox->setSelectedItemIndex ((int) settings.format, juce::dontSendNotification);

    if (depthBox != nullptr)
    {
        const auto depths = ExportSettings::bitDepthsFor (settings.format);
        const auto index = depths.indexOf (settings.bitsPerSample);
        depthBox->setSelectedItemIndex (index >= 0 ? index : depths.size() - 1,
                                        juce::dontSendNotification);
    }

    if (rateBox != nullptr)
        rateBox->setSelectedItemIndex (rateIndexFor (settings.sampleRate), juce::dontSendNotification);

    // Keep the bit depths legal when the format changes: FLAC has no float.
    if (formatBox != nullptr && depthBox != nullptr)
        formatBox->onChange = [formatBox, depthBox]
        {
            const auto allowed = ExportSettings::bitDepthsFor ((ExportSettings::Format)
                                                                   (formatBox->getSelectedItemIndex()));
            depthBox->clear (juce::dontSendNotification);

            for (int i = 0; i < allowed.size(); ++i)
                depthBox->addItem (depthItemsFor ((ExportSettings::Format)
                                                      formatBox->getSelectedItemIndex())[i], i + 1);

            depthBox->setSelectedItemIndex (allowed.size() - 1, juce::dontSendNotification);
        };

    window.addCustomComponent (contents);
    window.addButton (NI_TXT ("导出"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const auto chosen = window.runModalLoop();
    const auto master = contents->wantsMaster();
    const auto tracks = contents->chosenTracks (project);

    // The window does not own a custom component, so the caller cleans it up.
    delete contents;

    if (chosen != 1)
        return false;

    if (! master && tracks.empty())
    {
        showMessage (NI_TXT ("无法导出"), NI_TXT ("请至少勾选总混音或一条轨道。"), true);
        return false;
    }

    if (formatBox != nullptr)
        settings.format = (ExportSettings::Format) formatBox->getSelectedItemIndex();

    if (depthBox != nullptr)
    {
        const auto depths = ExportSettings::bitDepthsFor (settings.format);
        settings.bitsPerSample = depths[juce::jlimit (0, depths.size() - 1,
                                                      depthBox->getSelectedItemIndex())];
    }

    if (rateBox != nullptr)
    {
        static const double rates[] = { 0.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const auto index = juce::jlimit (0, 5, rateBox->getSelectedItemIndex());
        settings.sampleRate = rates[index];
    }

    settings.master = master;
    settings.trackIds = tracks;
    settings.clampToFormat();
    return true;
}

juce::File MainComponent::getExportLogFile()
{
    if (exportLogFileOverride() != juce::File())
        return exportLogFileOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HMDAW")
               .getChildFile ("export.log");
}

void MainComponent::setExportLogFileForTesting (juce::File file)
{
    exportLogFileOverride() = std::move (file);
}

void MainComponent::logExport (const juce::String& text)
{
    // Written from whichever thread is doing the step, so it is locked - and it is
    // written *before* the step it describes, so the last line of the file after a
    // crash is the step that died.
    const juce::ScopedLock sl (exportLogLock());

    const auto file = getExportLogFile();

    if (file.getSize() > kExportLogLimitBytes)
        file.deleteFile();

    file.getParentDirectory().createDirectory();
    file.appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  "
                         + text + juce::newLine);
}

int MainComponent::rateIndexFor (double sampleRate)
{
    if (sampleRate <= 0.0)
        return 0;

    static const double rates[] = { 0.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

    for (int i = 1; i < (int) (sizeof (rates) / sizeof (rates[0])); ++i)
        if (std::abs (rates[i] - sampleRate) < 1.0)
            return i;

    return 0;
}

void MainComponent::exportWav()
{
    logExport ("export asked (project end tick " + juce::String (document.projectEndTick()) + ")");

    if (document.projectEndTick() <= 0)
    {
        logExport ("refused: the project has no notes");
        showMessage (NI_TXT ("无法导出"),
                     NI_TXT ("工程里还没有音符。在编辑栏中按住参考线拖动即可写入音符。"),
                     true);
        return;
    }

    auto settings = lastExportSettings;

    // The self test drives the export directly, so it never opens a dialog.
    if (showDialogs)
    {
        if (! showExportDialog (settings))
        {
            logExport ("the export dialog was cancelled");
            return;   // cancelled
        }
    }

    settings.clampToFormat();
    lastExportSettings = settings;

    logExport ("settings: format " + juce::String ((int) settings.format) + ", "
                   + juce::String (settings.bitsPerSample) + " bit, rate "
                   + (settings.sampleRate > 0.0 ? juce::String (settings.sampleRate, 0)
                                                : juce::String (NI_TXT ("project")))
                   + ", master " + (settings.master ? "yes" : "no") + ", "
                   + juce::String ((int) settings.trackIds.size()) + " tracks");

    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto currentProject = document.getCurrentFile();
    const auto exportFolder = currentProject != juce::File()
                                      && currentProject.getFileName() == "project.nprj"
                                  ? currentProject.getParentDirectory().getChildFile ("exports")
                                  : documents;
    const auto needsFolder = ! settings.trackIds.empty();

    if (needsFolder)
    {
        chooser = std::make_unique<juce::FileChooser> (NI_TXT ("选择导出文件夹"), exportFolder, "");

        logExport ("asking for a folder (one file per chosen track, plus the master)");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, settings] (const juce::FileChooser& fileChooser)
        {
            const auto folder = fileChooser.getResult();

            if (folder == juce::File())
            {
                logExport ("the folder chooser was cancelled");
                return;
            }

            logExport ("folder: " + folder.getFullPathName());

            juce::String error;
            std::vector<juce::File> written;
            std::vector<juce::String> skipped;

            if (! runExport (settings, folder, true, error, &written, &skipped))
                showMessage (NI_TXT ("导出失败"), error, true);
            else
            {
                auto message = NI_TXT ("已写入 ") + juce::String ((int) written.size())
                                   + NI_TXT (" 个文件到：\n") + folder.getFullPathName();

                if (! skipped.empty())
                {
                    juce::StringArray names;

                    for (const auto& name : skipped)
                        names.add (name);

                    message += "\n\n" + NI_TXT ("（跳过 ") + juce::String ((int) skipped.size())
                                   + NI_TXT (" 条静音 / 未独奏的轨道：")
                                   + names.joinIntoString (NI_TXT ("、"))
                                   + NI_TXT ("）");
                }

                showMessage (NI_TXT ("导出完成"), message, false);
            }
        });

        return;
    }

    chooser = std::make_unique<juce::FileChooser> (
        NI_TXT ("导出音频"),
        exportFolder.getChildFile (document.getProjectName() + "."
                                    + ExportSettings::extensionFor (settings.format)),
        "*." + ExportSettings::extensionFor (settings.format));

    logExport ("asking for a file name (master mix only)");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, settings] (const juce::FileChooser& fileChooser)
    {
        auto result = fileChooser.getResult();

        if (result == juce::File())
        {
            logExport ("the file chooser was cancelled");
            return;
        }

        result = result.withFileExtension (ExportSettings::extensionFor (settings.format));

        logExport ("file: " + result.getFullPathName());

        juce::String error;

        if (! runExport (settings, result, false, error))
            showMessage (NI_TXT ("导出失败"), error, true);
        else
            showMessage (NI_TXT ("导出完成"), NI_TXT ("已写入：\n") + result.getFullPathName(), false);
    });
}

bool MainComponent::runExport (const ExportSettings& settings, const juce::File& target,
                               bool targetIsFolder, juce::String& errorMessage,
                               std::vector<juce::File>* writtenFiles,
                               std::vector<juce::String>* skippedTracks)
{
    audio.stop();

    // A plugin's state is only knowable while it is loaded, and the instance the
    // user is listening to is the live one: the document only holds whatever was
    // there the last time the project was saved. Without that capture an export of a
    // project that has not been saved since the plugin was loaded would build the
    // offline instance from nothing at all, and a sampler would render an empty rack.
    //
    // The capture itself happens later, on the message thread, from inside the
    // progress window (see ExportTask::run): for a big sampler it can take seconds,
    // and having the main window freeze with nothing on screen is what "导出时未响应"
    // looks like.
    auto project = document.getProject();

    logExport ("render starting: " + juce::String ((int) project.tracks.size())
                   + " tracks, target " + (targetIsFolder ? juce::String ("folder")
                                                          : target.getFullPathName()));

    // What each VST track should render with is resolved here, on the message
    // thread: the render thread must not read the plugin list while the picker's
    // scan may be rewriting it. Each file then gets its own private instance, so a
    // stem sounds exactly like the master mix it came from.
    const auto exportRate = settings.sampleRate > 0.0
                                ? settings.sampleRate
                                : (project.sampleRate > 0.0
                                       ? project.sampleRate
                                       : 48000.0);

    std::vector<std::pair<juce::int64, std::pair<juce::PluginDescription, VstParams>>> resolved;
    std::vector<std::pair<std::pair<juce::int64, int>, std::pair<juce::PluginDescription,
                                                                 EffectSlot>>> resolvedEffects;

    // A plugin that cannot be found leaves its track silent, and a silent stem is
    // indistinguishable from a broken one: what the user gets is "the export has no
    // sound". The tracks and slots below are the ones that would be written and
    // that would be missing their plugin, so the export stops with their names
    // instead of writing a file that quietly left them out.
    juce::StringArray missingPlugins;

    for (const auto& track : project.tracks)
    {
        // Only what this export is going to write matters: a track the user did not
        // tick (or one mute / solo keeps silent) may miss its plugin in peace.
        const auto contributes = isTrackAudible (project, track)
                                     && (settings.trackIds.empty()
                                             || settings.trackIds.count (track.id) > 0);

        if (track.kind != TrackKind::vst || ! track.vst.hasPlugin())
        {
            // A VST instrument, or nothing: the effects below are independent of
            // the track kind.
        }
        else if (auto description = pluginHost.findDescription (track.vst))
        {
            resolved.emplace_back (track.id, std::make_pair (*description, track.vst));
        }
        else if (contributes)
        {
            missingPlugins.add (NI_TXT ("「") + track.name + NI_TXT ("」的乐器"));
        }

        // Each insert slot is resolved here too, so the render thread never reads
        // the plugin list while a scan may be rewriting it.
        for (int i = 0; i < (int) track.effects.size(); ++i)
            if (track.effects[(size_t) i].hasPlugin())
                if (auto description = pluginHost.findEffectDescription (track.effects[(size_t) i]))
                    resolvedEffects.emplace_back (std::make_pair (track.id, i),
                                                  std::make_pair (*description,
                                                                  track.effects[(size_t) i]));
                else if (contributes && ! track.effects[(size_t) i].bypass)
                {
                    const auto& slot = track.effects[(size_t) i];

                    missingPlugins.add (NI_TXT ("「") + track.name + NI_TXT ("」的效果器「")
                                            + (slot.name.isNotEmpty() ? slot.name
                                                                      : slot.pluginPath)
                                            + NI_TXT ("」"));
                }
    }

    if (! missingPlugins.isEmpty())
    {
        errorMessage = NI_TXT ("这些插件没有加载，导出的文件里会少掉它们的声音，所以没有开始导出：\n")
                           + missingPlugins.joinIntoString (NI_TXT ("、"))
                           + NI_TXT ("\n\n请在插件的行头或效果器栏里点「插件界面」重新定位插件，然后再导出。");
        logExport ("refused: " + missingPlugins.joinIntoString (", "));
        return false;
    }

    auto& host = pluginHost;

    // The factories talk to the progress window, which is created after them: this
    // holder is filled in as soon as it exists. It is how a slow plugin ends up
    // saying "正在准备插件「Kontakt 8」…" on screen instead of looking like a freeze.
    auto status = std::make_shared<std::function<void (const juce::String&)>>();

    HostedInstrumentFactory factory =
        [&host, exportRate, resolved, status] (juce::int64 trackId, const VstParams& params)
            -> std::shared_ptr<HostedInstrument>
        {
            for (const auto& entry : resolved)
            {
                if (entry.first != trackId)
                    continue;

                juce::String error;
                std::shared_ptr<HostedInstrument> instrument;

                // The render thread is the one that asks, but loading a plugin,
                // preparing it and handing it its state all have to happen on the
                // message thread - that is what every plugin (and JUCE's own VST3
                // host) assumes, and doing it on the render thread is exactly the
                // kind of thing that takes the process down with a real sampler.
                logExport ("offline instance for track " + juce::String (trackId) + " ("
                               + entry.second.second.name + ")");
                if (*status != nullptr)
                {
                    (*status) (NI_TXT ("正在准备插件「") + entry.second.second.name
                                   + NI_TXT ("」…"));
                    juce::Thread::sleep (120);   // let the window paint it
                }

                juce::MessageManager::callSync ([&]
                {
                    instrument = host.makeOfflineInstrument (entry.second.first, params,
                                                             exportRate,
                                                             Exporter::kRenderBlockSize,
                                                             error);
                });
                logExport (instrument != nullptr
                               ? juce::String ("offline instance ready for track ")
                                     + juce::String (trackId)
                               : juce::String ("offline instance FAILED for track ")
                                     + juce::String (trackId) + ": " + error);
                return instrument;
            }

            return nullptr;
        };

    HostedEffectFactory effectFactory =
        [&host, exportRate, resolvedEffects, status] (juce::int64 trackId, int slotIndex,
                                                     const EffectSlot& slot)
            -> std::shared_ptr<HostedEffect>
        {
            for (const auto& entry : resolvedEffects)
            {
                if (entry.first.first != trackId || entry.first.second != slotIndex)
                    continue;

                juce::String error;
                std::shared_ptr<HostedEffect> effect;

                logExport ("offline effect for track " + juce::String (trackId) + " slot "
                               + juce::String (slotIndex));
                if (*status != nullptr)
                {
                    (*status) (NI_TXT ("正在准备效果器「")
                                   + (slot.name.isNotEmpty() ? slot.name : slot.pluginPath)
                                   + NI_TXT ("」…"));
                    juce::Thread::sleep (120);
                }

                juce::MessageManager::callSync ([&]
                {
                    effect = host.makeOfflineEffect (entry.second.first, slot, exportRate,
                                                     Exporter::kRenderBlockSize, error);
                });

                if (effect == nullptr)
                    logExport ("offline effect FAILED for track " + juce::String (trackId)
                                   + ": " + error);

                return effect;
            }

            return nullptr;
        };

    ExportTask task (project, target, document.getProjectName(),
                     settings, targetIsFolder, std::move (factory), std::move (effectFactory));

    task.captureLiveStates = [this] (Project& toFill)
    {
        logExport ("capturing the live plugin states");
        pluginHost.captureStates (toFill);
        logExport ("live plugin states captured");
    };
    *status = [&task] (const juce::String& text) { task.setStatusMessage (text); };

    if (! task.runThread())
    {
        errorMessage = NI_TXT ("导出被取消。");
        logExport ("the render was cancelled");

        // Everything below this line happens whether the render worked or not: the
        // live instrument has to be back where the user left it either way.
        if (task.capturedLiveStates)
            pluginHost.restoreStatesChangedSince (task.getRenderedProject());
        return false;
    }

    if (! task.succeeded)
    {
        errorMessage = task.errorMessage;
        logExport ("the render failed: " + task.errorMessage);
        if (task.capturedLiveStates)
            pluginHost.restoreStatesChangedSince (task.getRenderedProject());
        return false;
    }

    // Building a private instance of a plugin can disturb the one that is already
    // loaded (Kontakt's instrument volume going to zero just from having a sibling is
    // the reported case), so the state captured before the render is put back when it
    // moved. The log says whether that was needed.
    if (task.capturedLiveStates)
    {
        const auto putBack = pluginHost.restoreStatesChangedSince (task.getRenderedProject());

        if (putBack > 0)
            logExport ("the export had moved live plugin state; put back " + juce::String (putBack)
                           + " instance(s)");
        else
            logExport ("live plugin state unchanged by the export");
    }

    logExport ("render finished: " + juce::String ((int) task.writtenFiles.size())
                   + " file(s) written");

    if (writtenFiles != nullptr)
        *writtenFiles = task.writtenFiles;

    if (skippedTracks != nullptr)
        *skippedTracks = task.skippedTracks;

    return true;
}

void MainComponent::deleteSelectedNotes()
{
    const auto ids = appState.selectedNoteIds;

    if (ids.empty())
        return;

    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("删除音符"), [clipId, ids] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            music::removeNotesByIds (*clip, ids);
    });

    appState.clearNotes();
}

void MainComponent::shiftSelectedNotesByOctave (int octaves)
{
    const auto ids = appState.selectedNoteIds;

    if (ids.empty() || octaves == 0)
        return;

    const auto clipId = appState.selectedClipId;
    const auto factor = std::pow (2.0, (double) octaves);

    document.applyEdit (octaves > 0 ? NI_TXT ("音符上移八度") : NI_TXT ("音符下移八度"),
                        [clipId, ids, factor] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            for (auto& note : clip->notes)
                if (ids.find (note.id) != ids.end())
                    music::transposeNotePitch (note, factor);
    });
}

void MainComponent::copySelection()
{
    // Notes win when they are selected: clicking a clip clears the note selection,
    // so "copy the clip" is simply "copy with nothing selected in the editor".
    if (const auto* clip = document.getProject().findClip (appState.selectedClipId))
    {
        if (! appState.selectedNoteIds.empty())
        {
            appState.noteClipboard.clear();

            for (const auto& note : clip->notes)
                if (appState.selectedNoteIds.find (note.id) != appState.selectedNoteIds.end())
                    appState.noteClipboard.push_back (note);

            if (! appState.noteClipboard.empty())
            {
                appState.hasNoteClipboard = true;
                appState.lastClipboard = AppState::ClipboardKind::notes;
                return;
            }
        }
    }

    if (const auto* clip = document.getProject().findClip (appState.selectedClipId))
    {
        appState.clipClipboard = *clip;
        appState.hasClipClipboard = true;
        appState.lastClipboard = AppState::ClipboardKind::clips;
    }
}

void MainComponent::pasteClipboard()
{
    const auto playheadTick = document.makeTempoMap()
                                  .tickAtSeconds (audio.getPositionSeconds());

    if (appState.lastClipboard == AppState::ClipboardKind::notes && appState.hasNoteClipboard)
    {
        const auto clipId = appState.selectedClipId;

        if (clipId == 0)
            return;   // notes only paste into the editor

        const auto copied = appState.noteClipboard;

        juce::int64 firstTick = copied.front().startTick;
        juce::int64 lastTick = copied.front().endTick();

        for (const auto& note : copied)
        {
            firstTick = juce::jmin (firstTick, note.startTick);
            lastTick = juce::jmax (lastTick, note.endTick());
        }

        std::vector<juce::int64> newIds;

        document.applyEdit (NI_TXT ("粘贴音符"), [clipId, copied, firstTick, lastTick,
                                                  playheadTick, &newIds] (Project& project)
        {
            auto* clip = project.findClip (clipId);

            if (clip == nullptr)
                return;

            const auto offset = (playheadTick - clip->startTick) - firstTick;

            std::vector<std::pair<juce::int64, juce::int64>> idMap;
            std::vector<Note> pastedNotes;
            pastedNotes.reserve (copied.size());

            for (const auto& note : copied)
            {
                auto pasted = note;
                const auto oldId = pasted.id;
                pasted.id = project.nextNoteId++;
                pasted.startTick = note.startTick + offset;
                idMap.emplace_back (oldId, pasted.id);
                newIds.push_back (pasted.id);
                pastedNotes.push_back (std::move (pasted));
            }

            for (size_t i = 0; i < pastedNotes.size(); ++i)
            {
                auto& pasted = pastedNotes[i];
                const auto oldTie = copied[i].tieFromNoteId;
                pasted.tieFromNoteId = 0;

                if (oldTie != 0)
                {
                    for (const auto& [oldId, newId] : idMap)
                        if (oldId == oldTie)
                        {
                            pasted.tieFromNoteId = newId;
                            break;
                        }
                }

                clip->notes.push_back (std::move (pasted));
            }

            music::pruneInvalidNoteTies (*clip);

            // Grow the window so everything that was pasted actually sounds.
            clip->lengthTicks = juce::jmax (clip->lengthTicks, lastTick + offset);
        });

        if (! newIds.empty())
        {
            std::set<juce::int64> ids (newIds.begin(), newIds.end());
            appState.setSelectedNotes (std::move (ids));
        }

        return;
    }

    if (appState.lastClipboard == AppState::ClipboardKind::clips && appState.hasClipClipboard)
    {
        const auto trackId = appState.selectedTrackId;

        if (trackId == 0)
            return;   // clips only paste into the track lane

        const auto copied = appState.clipClipboard;
        juce::int64 createdId = 0;

        document.applyEdit (NI_TXT ("粘贴片段"), [trackId, copied, playheadTick, &createdId] (Project& project)
        {
            auto* track = project.findTrack (trackId);

            if (track == nullptr)
                return;

            auto clip = copied;
            clip.id = project.nextClipId++;
            clip.name = NI_TXT ("片段 ") + juce::String (clip.id);
            clip.startTick = music::findFreeSlot (*track, playheadTick, clip.lengthTicks);

            std::vector<std::pair<juce::int64, juce::int64>> idMap;

            for (auto& note : clip.notes)
            {
                const auto oldId = note.id;
                note.id = project.nextNoteId++;
                idMap.emplace_back (oldId, note.id);
            }

            for (auto& note : clip.notes)
            {
                const auto oldTie = note.tieFromNoteId;
                note.tieFromNoteId = 0;

                for (const auto& [oldId, newId] : idMap)
                    if (oldId == oldTie)
                    {
                        note.tieFromNoteId = newId;
                        break;
                    }
            }

            music::pruneInvalidNoteTies (clip);

            createdId = clip.id;
            track->clips.push_back (std::move (clip));
        });

        if (createdId != 0)
            appState.selectClip (createdId);
    }
}

void MainComponent::repeatSelectedClip (int count)
{
    const auto clipId = appState.selectedClipId;

    if (clipId == 0 || count <= 0)
        return;

    const auto* source = document.getProject().findClip (clipId);
    const auto* sourceTrack = document.getProject().findTrackOfClip (clipId);

    if (source == nullptr || sourceTrack == nullptr)
        return;

    // Bound it so a stray value cannot fill the lane with thousands of copies.
    const auto copies = juce::jlimit (1, 64, count);
    const auto trackId = sourceTrack->id;

    // One edit for the whole batch, so a repeat of N copies is one undo step.
    document.applyEdit (NI_TXT ("重复片段"), [trackId, clipId, copies] (Project& project)
    {
        if (auto* track = project.findTrack (trackId))
            music::repeatClip (*track, clipId, copies, project.nextClipId);
    });
}

void MainComponent::showClipRepeatDialog()
{
    if (document.getProject().findClip (appState.selectedClipId) == nullptr)
        return;

    if (! showDialogs)
    {
        repeatSelectedClip (1);
        return;
    }

    auto* window = new juce::AlertWindow (NI_TXT ("重复片段"),
                                          NI_TXT ("把当前片段向后连续复制多少份？"),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("count", "4", NI_TXT ("份数"));
    window->addButton (NI_TXT ("确定"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            if (result != 1)
                return;

            const auto entered = window->getTextEditorContents ("count").getIntValue();

            if (entered > 0)
                repeatSelectedClip (entered);
        }), true);
}

void MainComponent::openEffectsWindow (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    if (effectsWindow == nullptr)
        effectsWindow = std::make_unique<EffectsWindow> (effectsBar, this);

    // The bar's rows act on the track it was opened for; the window is reused, so
    // opening another track's 效果 just re-points it.
    appState.selectTrack (trackId);
    effectsWindow->showForTrack (trackId, track->name);
    attachKeyListenersToWindows();
}

//==============================================================================
// 自定义波表：导入 / 取样 / 拷内置 / 编辑 / 删除。
//
// The tables live in the project (so a project carries its own sound), which means
// every action here is an ordinary undoable edit; the editor window only draws and
// writes into the table it was opened on.

namespace
{
    /** One frame per 2048 source samples, capped: a single-cycle file becomes a
        one-frame table, a couple of seconds become a full eight-frame morph. */
    int wavetableFramesForLength (juce::int64 numSamples)
    {
        const auto frames = numSamples / (juce::int64) kCustomWavetableFrameSize;

        return juce::jlimit (1, kMaxCustomWavetableFrames, (int) juce::jmax ((juce::int64) 1, frames));
    }

    /** The mono mix of a loaded sample, which is what the table builder wants. */
    std::vector<float> monoMixOf (const LoadedSample& sample)
    {
        const auto channels = juce::jmax (1, sample.audio.getNumChannels());
        const auto count    = sample.audio.getNumSamples();
        std::vector<float> mono ((size_t) juce::jmax (0, count), 0.0f);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* source = sample.audio.getReadPointer (channel);

            for (int i = 0; i < count; ++i)
                mono[(size_t) i] += source[i] / (float) channels;
        }

        return mono;
    }
}

void MainComponent::setWavetableNotice (const juce::String& text)
{
    wavetableNotice = text;

    if (wavetableTableWindow != nullptr)
        wavetableTableWindow->getPanel().setNotice (text);
}

juce::String MainComponent::getWavetableStatusNoticeForTest() const
{
    return wavetableNotice;
}

juce::int64 MainComponent::addCustomWavetable (CustomWavetable table)
{
    juce::int64 id = 0;
    const auto trackId = appState.selectedTrackId;

    document.applyEdit (NI_TXT ("新建自定义波表"), [&id, &table, trackId] (Project& project)
    {
        table.id = project.nextCustomWavetableId++;
        id = table.id;
        project.customWavetables.push_back (std::move (table));

        // The table was made for the track the user is working on: point its first
        // oscillator at it, so what they hear is what they just made.
        if (auto* track = project.findTrack (trackId);
            track != nullptr && track->kind == TrackKind::wavetable)
            track->wavetable.oscs[0].customTableId = id;
    });

    if (id != 0)
    {
        wavetableNotice.clear();
        openWavetableTableWindow (id);
    }

    return id;
}

void MainComponent::openWavetableTableWindow (juce::int64 customTableId)
{
    const auto* track = document.getProject().findTrack (appState.selectedTrackId);

    if (track == nullptr || track->kind != TrackKind::wavetable)
    {
        if (showDialogs)
            showMessage (NI_TXT ("波表编辑器"),
                         NI_TXT ("先选中一条波表轨（「＋ 新建轨道」→ 波表合成器），再打开波表编辑器。"),
                         false);
        return;
    }

    auto id = customTableId;

    if (id == 0 || document.getProject().findCustomWavetable (id) == nullptr)
    {
        // Nothing of its own yet: copy the built-in the first oscillator is using,
        // so there is something to edit. (The button's tooltip says so.)
        const auto seed = track->wavetable.oscs[0].tableIndex;

        setWavetableNotice ({});
        id = addCustomWavetable (wavetabletables::copyBuiltIn (seed, kMaxCustomWavetableFrames, 0,
                                                              wavetable::tableName (seed)
                                                                  + NI_TXT (" 副本")));
        return;   // addCustomWavetable opens the window on the new table
    }

    if (wavetableTableWindow == nullptr)
    {
        wavetableTableWindow = std::make_unique<WavetableTableWindow> (document, appState, audio, this);

        auto& panel = wavetableTableWindow->getPanel();

        panel.onImportWav = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (
                NI_TXT ("导入一张波表（音频文件）"),
                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                "*.wav;*.aif;*.aiff;*.flac");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fileChooser)
            {
                const auto result = fileChooser.getResult();

                if (result != juce::File())
                    importWavetableFromFile (result);
            });
        };

        panel.onSampleFromClip = [this] { sampleWavetableFromOpenClip(); };
        panel.onCopyBuiltIn    = [this] (int builtInIndex) { copyBuiltInWavetable (builtInIndex); };
        panel.onWaveformPreset = [this] (int presetIndex)
        {
            addCustomWavetable (waveformpresets::build (presetIndex, 0));
        };

        panel.onRenameTable = [this] (const juce::String& newName)
        {
            const auto tableId = wavetableTableWindow != nullptr
                                     ? wavetableTableWindow->getPanel().getTableId() : (juce::int64) 0;
            const auto name = newName.trim();

            if (tableId == 0 || name.isEmpty())
                return;

            document.applyEdit (NI_TXT ("重命名波表"), [tableId, name] (Project& project)
            {
                if (auto* table = project.findCustomWavetable (tableId))
                    table->name = name;
            });

            wavetableTableWindow->refresh();
        };

        panel.onDeleteTable = [this]
        {
            if (wavetableTableWindow != nullptr)
                deleteCustomWavetable (wavetableTableWindow->getPanel().getTableId());
        };

        panel.onAudition = [this]
        {
            const auto* auditionTrack = document.getProject().findTrack (appState.selectedTrackId);

            if (auditionTrack == nullptr || auditionTrack->kind != TrackKind::wavetable)
                return;

            const auto& project = document.getProject();

            audio.updatePreviewWaveTables (
                wavetabletables::playableTableFor (project, auditionTrack->wavetable.oscs[0].customTableId),
                wavetabletables::playableTableFor (project, auditionTrack->wavetable.oscs[1].customTableId));
            audio.triggerPreviewWaveNote (auditionTrack->wavetable, 261.6255653, 100.0f);

            juce::Component::SafePointer<MainComponent> safeThis (this);
            juce::Timer::callAfterDelay (700, [safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->audio.releasePreviewNote();
            });
        };
    }

    wavetableTableWindow->show (id);
    attachKeyListenersToWindows();
}

juce::int64 MainComponent::importWavetableFromFile (const juce::File& file)
{
    juce::String error;
    auto sample = getCachedSample (file.getFullPathName(), error);

    if (sample == nullptr)
    {
        setWavetableNotice (NI_TXT ("这个文件读不了：") + error);
        return 0;
    }

    const auto mono  = monoMixOf (*sample);
    const auto name  = file.getFileNameWithoutExtension();
    auto table = wavetabletables::makeFromAudio (mono.data(), (juce::int64) mono.size(),
                                                 wavetableFramesForLength ((juce::int64) mono.size()),
                                                 0, name.isNotEmpty() ? name : NI_TXT ("导入的波表"));

    return addCustomWavetable (std::move (table));
}

juce::int64 MainComponent::sampleWavetableFromOpenClip()
{
    const auto* clip = document.getProject().findClip (appState.selectedClipId);

    if (clip == nullptr)
    {
        setWavetableNotice (NI_TXT ("先在编辑栏里打开一个音频片段（点片段就打开）"));
        return 0;
    }

    if (clip->audioFilePath.isEmpty())
    {
        setWavetableNotice (NI_TXT ("这个片段不是音频片段：只有音频片段（有音频文件的）才能取样"));
        return 0;
    }

    juce::String error;
    auto sample = getCachedSample (clip->audioFilePath, error);

    if (sample == nullptr)
    {
        setWavetableNotice (NI_TXT ("片段的音频读不了：") + error);
        return 0;
    }

    const auto mono = monoMixOf (*sample);
    const auto name = clip->name.isNotEmpty() ? clip->name : clip->audioFileName;
    auto table = wavetabletables::makeFromAudio (mono.data(), (juce::int64) mono.size(),
                                                 wavetableFramesForLength ((juce::int64) mono.size()),
                                                 0, name);

    return addCustomWavetable (std::move (table));
}

void MainComponent::copyBuiltInWavetable (int builtInIndex)
{
    const auto name = wavetable::tableName (builtInIndex) + NI_TXT (" 副本");

    addCustomWavetable (wavetabletables::copyBuiltIn (builtInIndex, kMaxCustomWavetableFrames, 0, name));
}

void MainComponent::deleteCustomWavetable (juce::int64 tableId)
{
    if (tableId == 0)
        return;

    document.applyEdit (NI_TXT ("删除自定义波表"), [tableId] (Project& project)
    {
        auto& tables = project.customWavetables;
        tables.erase (std::remove_if (tables.begin(), tables.end(),
                                      [tableId] (const CustomWavetable& table)
                                      { return table.id == tableId; }),
                      tables.end());

        // Anything still pointing at it falls back to its built-in table, so the
        // track keeps playing instead of going silent.
        for (auto& track : project.tracks)
            for (auto& osc : track.wavetable.oscs)
                if (osc.customTableId == tableId)
                    osc.customTableId = 0;
    });

    setWavetableNotice (NI_TXT ("这张表已经从工程里删掉了，用到它的振荡器退回内置表"));

    if (wavetableTableWindow != nullptr)
        wavetableTableWindow->setVisible (false);
}

//==============================================================================
// MIDI 输入.

void MainComponent::openMidiInputWindow()
{
    if (midiInputWindow == nullptr)
    {
        midiInputWindow = std::make_unique<MidiInputWindow> (document, appState, presetStore,
                                                            audio, midiInput, this);

        midiInputWindow->getPanel().onArpSettingsChanged = [this] { arpSettingsChanged(); };
    }

    midiInputWindow->show();
    midiInputWindow->getPanel().refreshArpControls();
}

double MainComponent::frequencyForMidiNote (int midiNote) const
{
    const auto all = frequenciesForMidiNote (midiNote);

    return all.empty() ? 0.0 : all.front();
}

std::vector<double> MainComponent::frequenciesForMidiNote (int midiNote) const
{
    // 钢琴窗模式 is plain twelve-tone equal temperament: the mapping is a thing of
    // the reference-line view, where a pitch class is a ratio rather than a semitone.
    if (document.viewState().editorMode == EditorMode::piano)
        return { music::piano::frequencyOf (midiNote) };

    return document.getProject().midiInput.map.frequenciesFor (midiNote);
}

int MainComponent::getHeldMidiVoiceCountForTest() const
{
    auto count = 0;

    for (const auto& key : heldKeys)
        count += (int) key.voices.size();

    return count;
}

std::vector<int> MainComponent::getHeldKeysForTest() const
{
    return heldNotes();
}

std::vector<int> MainComponent::heldNotes() const
{
    std::vector<int> notes;
    notes.reserve (heldKeys.size());

    for (const auto& key : heldKeys)
        if (std::find (notes.begin(), notes.end(), key.midiNote) == notes.end())
            notes.push_back (key.midiNote);

    return notes;
}

MainComponent::HeldKey* MainComponent::findHeldKey (juce::int64 trigger, int midiNote)
{
    for (auto& key : heldKeys)
        if (key.trigger == trigger && key.midiNote == midiNote)
            return &key;

    return nullptr;
}

void MainComponent::releaseHeldKeyVoices (HeldKey& key)
{
    for (const auto& voice : key.voices)
        audio.noteOffTrack (voice.trackId, voice.liveId);

    key.voices.clear();
}

void MainComponent::handleMidiMessage (const juce::MidiMessage& message)
{
    // CC 123 / all sound off: let go of everything the keyboard is holding.
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        releaseAllLiveNotes();
        return;
    }

    // Pedals and wheels: they sound at once (and, while recording, are written
    // into the clip's controller curve). This used to be dropped on the floor, so
    // a hardware sustain pedal did nothing at all.
    if (message.isController())
    {
        handleLiveController (message.getControllerNumber(), message.getControllerValue());
        return;
    }

    if (! message.isNoteOnOrOff())
        return;

    const auto note = message.getNoteNumber();

    // The MIDI input window gets the first look: while it is teaching its anchor a
    // key press means "the tile starts here". The note still sounds below.
    if (message.isNoteOn() && midiInputWindow != nullptr && midiInputWindow->isVisible())
        midiInputWindow->getPanel().handleMidiMessage (message);

    if (message.isNoteOff())
    {
        handleLiveNoteOff (note, keyboard::kHardwareTrigger);
        return;
    }

    handleLiveNoteOn (note, juce::jlimit (1.0f, 127.0f, (float) message.getVelocity()),
                      keyboard::kHardwareTrigger);
}

const InstrumentTrack* MainComponent::liveTargetTrack() const
{
    const auto* track = document.getProject().findTrack (appState.selectedTrackId);

    // 鼓机 / 音频: there is no pitch on those canvases to bind a key to.
    if (track == nullptr || track->kind == TrackKind::drums || track->kind == TrackKind::audio)
        return nullptr;

    return track;
}

void MainComponent::handleLiveNoteOn (int midiNote, float velocity, juce::int64 trigger)
{
    if (findHeldKey (trigger, midiNote) != nullptr)
        return;   // the same key again while it is still held

    const auto* track = liveTargetTrack();

    if (track == nullptr)
        return;   // no pitch on this track: nothing to bind a key to

    const auto playedVelocity = juce::jlimit (1.0f, 127.0f, velocity);

    // The key is down whether or not it has anything bound to it: the window draws
    // it held (that is how "空" is visible while playing) and 琶音 counts it.
    heldKeys.push_back ({ trigger, midiNote, playedVelocity, {} });

    // 琶音: with it on the key does not sound by itself - the arpeggiator plays the
    // set of held keys, one step at a time.
    if (document.getProject().midiInput.arp.enabled)
    {
        arp.lastPlayedVelocity = (int) playedVelocity;
        refreshArpeggiator();
        showLiveKeyOnMidiCanvas (midiNote, playedVelocity);
        return;
    }

    const auto frequencies = frequenciesForMidiNote (midiNote);

    if (frequencies.empty())
    {
        showLiveKeyOnMidiCanvas (midiNote, playedVelocity);
        return;   // this key has nothing bound to it
    }

    std::vector<HeldMidiNote> voices;
    voices.reserve (frequencies.size());

    // A key can be tied to several dots: every one of them sounds at once, exactly
    // like the stack of cables drawn in the MIDI input window.
    for (const auto frequency : frequencies)
    {
        const auto liveId = nextLiveNoteId++;
        audio.noteOnTrack (track->id, liveId, frequency, playedVelocity);
        voices.push_back ({ track->id, liveId });

        lastLiveNote = { midiNote, frequency, playedVelocity, track->id };
    }

    if (auto* key = findHeldKey (trigger, midiNote))
        key->voices = std::move (voices);

    // 录制: only while the transport is actually running - a note played while it
    // is stopped still sounds, it just has nowhere to land.
    if (appState.midiRecordArmed && audio.isPlaying())
        pendingRecords[midiNote] = { track->id, recordTickNow(), 0, (int) playedVelocity, 0 };

    showLiveKeyOnMidiCanvas (midiNote, playedVelocity);
}

void MainComponent::handleLiveNoteOff (int midiNote, juce::int64 trigger)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end(); ++it)
        if (it->trigger == trigger && it->midiNote == midiNote)
        {
            releaseHeldKeyVoices (*it);
            heldKeys.erase (it);
            break;
        }

    if (appState.midiRecordArmed)
        finishRecordedNote (midiNote, recordTickNow());

    // The set the arpeggiator plays is what is *held*, so letting go is a change to
    // it - unless the latch is holding the set, which updateLatch knows about.
    if (document.getProject().midiInput.arp.enabled)
        refreshArpeggiator();

    // The virtual keyboard draws what is held.
    if (virtualKeyboardWindow != nullptr)
        virtualKeyboardWindow->setKeysDown (getHeldKeysForTest());
}

void MainComponent::showLiveKeyOnMidiCanvas (int midiNote, float velocity)
{
    if (midiInputWindow != nullptr)
        midiInputWindow->getPanel().getCanvas().showPlayedNote (midiNote);

    if (virtualKeyboardWindow != nullptr)
    {
        virtualKeyboardWindow->setKeysDown (getHeldKeysForTest());
        virtualKeyboardWindow->setLastVelocity ((int) std::lround (velocity));
    }
}

void MainComponent::handleLiveController (int controller, int value)
{
    controller = juce::jlimit (0, 127, controller);
    value = juce::jlimit (0, 127, value);

    const auto* track = liveTargetTrack();

    if (track == nullptr)
        return;

    audio.sendControllerToTrack (track->id, controller, value);

    // Only the controllers this build really reads are worth writing down: the
    // pedal, the expression pedal and the mod wheel.
    if (appState.midiRecordArmed && audio.isPlaying()
        && (controller == 1 || controller == 11 || controller == 64))
        recordControllerValue (controller, value);
}

void MainComponent::recordControllerValue (int controller, int value)
{
    auto clipId = appState.midiRecordIntoClip ? appState.selectedClipId : (juce::int64) 0;

    if (! appState.midiRecordIntoClip)
    {
        // 片段外: the controller joins whatever clip this take has made so far. A
        // pedal pressed before the first note has nowhere to go yet.
        for (const auto& [note, record] : pendingRecords)
        {
            juce::ignoreUnused (note);

            if (record.clipId != 0)
            {
                clipId = record.clipId;
                break;
            }
        }
    }

    const auto* clip = document.getProject().findClip (clipId);

    if (clip == nullptr)
        return;

    const auto localTick = juce::jmax ((juce::int64) 0, recordTickNow() - clip->startTick);

    // One gesture per controller: the first change of a gesture pushes the undo
    // snapshot, a change after 300 ms of quiet starts the next one. A continuous
    // expression pedal is therefore one Ctrl+Z, not a hundred.
    auto& gesture = controllerGestures[(size_t) controller];
    const auto now = (juce::int64) juce::Time::getMillisecondCounter();

    if (! gesture.open || now - gesture.lastMs > kControllerGestureGapMs)
        document.beginEdit (NI_TXT ("录制控制器"));

    gesture.open = true;
    gesture.lastMs = now;

    document.applyDerivedEdit ([clipId, controller, localTick, value] (Project& project)
    {
        if (auto* target = project.findClip (clipId))
            controllers::upsertControllerPoint (controllers::laneFor (*target, controller),
                                                localTick, value);
    });
    document.notifyEdit();
}

void MainComponent::closeControllerGestures()
{
    for (auto& gesture : controllerGestures)
        gesture.open = false;
}

juce::int64 MainComponent::recordTickNow() const
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto tick = tempoMap.tickAtSeconds (audio.getPositionSeconds());
    const auto& view = document.viewState();

    if (! view.editorSnap.enabled)
        return tick;

    const auto grid = tempoMap.ticksPerBeatAt (tick) / view.editorSnap.clampedDenominator();
    return music::TempoMap::snapToGrid (tick, juce::jmax (1.0, grid));
}

void MainComponent::finishRecordedNote (int midiNote, juce::int64 endTick)
{
    const auto found = pendingRecords.find (midiNote);

    if (found == pendingRecords.end())
        return;

    const auto pending = found->second;
    pendingRecords.erase (found);

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto grid = juce::jmax (
        1.0, tempoMap.ticksPerBeatAt (pending.startTick)
                 / document.viewState().editorSnap.clampedDenominator());
    // The arpeggiator already knows how long its step sounds; a note played by hand
    // is measured from the key going down to it coming up (with one grid step as
    // the floor, so a stab of a few milliseconds is still a note).
    const auto length = pending.fixedLengthTicks > 0
                            ? pending.fixedLengthTicks
                            : juce::jmax ((juce::int64) std::llround (grid),
                                          juce::jmax ((juce::int64) 1,
                                                      endTick - pending.startTick));

    const auto trackId = pending.trackId;
    const auto startTick = pending.startTick;
    const auto velocity = juce::jlimit (1, 127, pending.velocity);
    const auto existingClipId = pending.clipId;
    const auto insideClipId = appState.midiRecordIntoClip ? appState.selectedClipId : (juce::int64) 0;
    // One note per cable: what was heard is what lands in the clip.
    const auto frequencies = frequenciesForMidiNote (midiNote);

    juce::int64 madeClipId = 0;

    document.applyEdit (NI_TXT ("录制 MIDI"), [&] (Project& project)
    {
        auto* track = project.findTrack (trackId);

        if (track == nullptr)
            return;

        auto* clip = project.findClip (existingClipId != 0 ? existingClipId : insideClipId);

        if (clip == nullptr)
        {
            // 片段外: the first note of the take makes the clip it lives in.
            auto created = makeDefaultClip (project);
            created.startTick = startTick;
            created.lengthTicks = juce::jmax (kMinClipTicks, length);
            created.loopLengthTicks = created.lengthTicks;
            created.name = NI_TXT ("录制 ") + juce::String ((int) track->clips.size() + 1);
            madeClipId = created.id;
            track->clips.push_back (std::move (created));
            clip = &track->clips.back();
        }

        auto written = juce::jmax ((juce::int64) 0, startTick - clip->startTick);
        auto needed = written;

        for (const auto frequency : frequencies)
        {
            Note note;
            note.id = project.nextNoteId++;
            note.startTick = written;
            note.durationTicks = length;
            note.frequencyHz = frequency;
            note.tailFrequencyHz = frequency;
            note.velocity = velocity;
            note.pad = clip->isAudio() ? 0 : -1;
            clip->notes.push_back (note);

            needed = juce::jmax (needed, note.startTick + note.durationTicks);
        }

        // A take that runs past the clip's end makes the clip longer, exactly like
        // "keep dragging and it reveals more" does for a written note.
        if (needed > clip->lengthTicks)
            clip->lengthTicks = needed;

        if (clip->loopLengthTicks > clip->lengthTicks)
            clip->loopLengthTicks = clip->lengthTicks;
    });

    if (madeClipId != 0)
    {
        appState.selectClip (madeClipId);

        // The take continues in the clip it just made.
        for (auto& [note, record] : pendingRecords)
            if (record.trackId == trackId && record.clipId == 0)
                record.clipId = madeClipId;
    }
    else if (existingClipId == 0 && insideClipId != 0)
    {
        for (auto& [note, record] : pendingRecords)
            if (record.trackId == trackId && record.clipId == 0)
                record.clipId = insideClipId;
    }
}

void MainComponent::flushPendingRecords()
{
    // Keys still held when recording is switched off are written with the length
    // they have been held for: they were played, so they belong in the take.
    const auto endTick = recordTickNow();

    std::vector<int> held;

    for (const auto& [note, record] : pendingRecords)
        held.push_back (note);

    for (const auto note : held)
        finishRecordedNote (note, endTick);
}

void MainComponent::releaseAllLiveNotes()
{
    for (auto& key : heldKeys)
        releaseHeldKeyVoices (key);

    heldKeys.clear();

    // Whatever the arpeggiator was sounding goes too, and the phrase it was in the
    // middle of is over (the next key press starts a new one).
    stopArpeggiatorNotes();
    arp.latch.notes.clear();
    arp.latch.startFreshOnNextPress = false;

    if (virtualKeyboardWindow != nullptr)
        virtualKeyboardWindow->setKeysDown ({});
}

//==============================================================================
// 琶音器.
//
// The arpeggiator is a *player*, not a track effect: it turns the keys that are
// held into a stream of ordinary live notes on the selected track. That is what
// makes "what you hear is what a take records" true, and it is why it can run with
// the transport stopped (its own clock) as well as locked to the project's grid
// (when the transport is the clock).

void MainComponent::arpTick()
{
    const auto& settings = document.getProject().midiInput.arp;

    if (! settings.enabled || arp.order.empty() || liveTargetTrack() == nullptr)
    {
        stopArpeggiatorNotes();

        if (arpTicker != nullptr)
            arpTicker->setRunning (false);

        return;
    }

    const music::TempoMap map (document.getProject().tempoMarkers);
    const auto ticksNow = arpClockTicks (map);
    const auto stepTicks = midiarp::stepTicksFor (settings, map.bpmAt (ticksNow),
                                                  kTicksPerQuarter);

    // The gate first: the note whose length is up stops before the next one starts.
    if (arp.soundingNote >= 0 && ticksNow >= arp.soundingOffTick)
        arpNoteOff();

    // Which slot of the grid we are in. While the transport runs this is the
    // project's own grid, so the arpeggio sits exactly on the bars and follows a
    // tempo change; while it is stopped it is the arpeggiator's own clock, which
    // started when the first key went down.
    const auto slot = ticksNow / stepTicks;

    if (slot <= arp.lastSlot)
        return;   // this slot has already been played

    const auto stepIndex = (int) (slot - arp.phraseStartSlot);

    // Swing: the off-beats wait a little before they fire.
    if (midiarp::offsetTicksForStep (settings, stepTicks, stepIndex)
        > ticksNow - slot * stepTicks)
        return;

    arp.lastSlot = slot;

    const auto note = midiarp::noteForStep (arp.order, stepIndex);

    if (note < 0)
        return;

    const auto frequencies = frequenciesForMidiNote (note);

    if (frequencies.empty())
        return;   // an empty step of the tile: the slot passes in silence

    const auto* track = liveTargetTrack();

    if (track == nullptr)
        return;

    const auto velocity = midiarp::velocityForStep (settings, arp.lastPlayedVelocity, stepIndex,
                                                    (int) arp.order.size());
    const auto lengthTicks = midiarp::gateTicksFor (settings, stepTicks);
    // The phrase may have started in the middle of a slot: step 0 then starts where
    // the player pressed rather than back at the slot line.
    const auto startTick = stepIndex == 0 ? juce::jmax (slot * stepTicks, arp.phraseStartTick)
                                          : slot * stepTicks;

    std::vector<HeldMidiNote> voices;

    for (const auto frequency : frequencies)
    {
        const auto liveId = nextLiveNoteId++;
        audio.noteOnTrack (track->id, liveId, frequency, (float) velocity);
        voices.push_back ({ track->id, liveId });
        lastLiveNote = { note, frequency, (float) velocity, track->id };
    }

    arp.soundingNote = note;
    arp.soundingVoices = std::move (voices);
    arp.soundingOffTick = startTick + lengthTicks;
    arp.soundingStartTick = startTick;
    arp.soundingLengthTicks = lengthTicks;
    ++arp.firedSteps;

    // 录制: the take gets the note the arpeggiator *meant* to play - its own tick
    // and its gate - rather than wherever the audio block happened to land.
    if (appState.midiRecordArmed && audio.isPlaying())
        pendingRecords[note] = { track->id, startTick, 0, velocity, lengthTicks };
}

void MainComponent::arpNoteOff()
{
    for (const auto& voice : arp.soundingVoices)
        audio.noteOffTrack (voice.trackId, voice.liveId);

    arp.soundingVoices.clear();

    if (arp.soundingNote >= 0)
    {
        if (appState.midiRecordArmed)
            finishRecordedNote (arp.soundingNote,
                                arp.soundingStartTick + arp.soundingLengthTicks);

        arp.soundingNote = -1;
    }
}

void MainComponent::stopArpeggiatorNotes()
{
    arpNoteOff();

    arp.order.clear();
    arp.latch.notes.clear();
    arp.latch.startFreshOnNextPress = false;
    arp.lastSlot = -1;
    arp.phraseStartSlot = 0;
    arp.phraseStartTick = 0;
    arp.freeTicks = 0;
    arp.lastWallSeconds = 0.0;
}

juce::int64 MainComponent::arpClockTicks (const music::TempoMap& map)
{
    if (arp.forcedClockTicks >= 0)
        return arp.forcedClockTicks;   // self test: the clock is pinned

    if (audio.isPlaying())
        return map.tickAtSeconds (audio.getPositionSeconds());

    // Its own clock: the wall clock turned into ticks at the tempo the playhead is
    // sitting at, so the arpeggio keeps the same feel with the transport stopped.
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto bpm = map.bpmAt (map.tickAtSeconds (audio.getPositionSeconds()));

    if (arp.lastWallSeconds > 0.0)
        arp.freeTicks += (juce::int64) std::llround ((now - arp.lastWallSeconds)
                                                     * bpm / 60.0 * (double) kTicksPerQuarter);

    arp.lastWallSeconds = now;
    return arp.freeTicks;
}

void MainComponent::arpTransportStopped()
{
    arpNoteOff();

    // From here the arpeggiator counts its own ticks, starting now.
    arp.freeTicks = 0;
    arp.lastWallSeconds = 0.0;
    arp.phraseStartSlot = 0;
    arp.phraseStartTick = 0;
    arp.lastSlot = -1;

    if (arpTicker != nullptr && ! arp.order.empty())
        arpTicker->setRunning (true);
}

void MainComponent::refreshArpeggiator()
{
    if (arpTicker == nullptr)
        return;

    const auto& settings = document.getProject().midiInput.arp;

    if (! settings.enabled || liveTargetTrack() == nullptr)
    {
        stopArpeggiatorNotes();
        arpTicker->setRunning (false);
        refreshArpWindows();
        return;
    }

    // The set the phrase cycles: without Hold it is exactly what is down; with it,
    // the set keeps running after you let go and the next key starts a new one.
    midiarp::updateLatch (arp.latch, heldNotes(), settings.hold);

    arp.order = midiarp::orderFor (settings, arp.latch.notes, ++arpSeed);

    if (arp.order.empty())
    {
        arpNoteOff();
        arpTicker->setRunning (false);
        refreshArpWindows();
        return;
    }

    // A new set starts the phrase from its own beginning.
    const music::TempoMap map (document.getProject().tempoMarkers);

    if (! audio.isPlaying())
    {
        arp.freeTicks = 0;
        arp.lastWallSeconds = 0.0;
    }

    const auto ticksNow = arpClockTicks (map);
    const auto stepTicks = midiarp::stepTicksFor (settings, map.bpmAt (ticksNow),
                                                  kTicksPerQuarter);

    arp.phraseStartSlot = ticksNow / stepTicks;
    arp.phraseStartTick = ticksNow;
    arp.lastSlot = arp.phraseStartSlot - 1;

    arpTicker->setRunning (true);
    refreshArpWindows();
}

void MainComponent::arpSettingsChanged()
{
    const auto& settings = document.getProject().midiInput.arp;
    const auto wasRunning = ! arp.order.empty();

    refreshArpeggiator();

    // 琶音 just went off with keys still down: they sound on their own now, exactly
    // as they would have if it had never been on.
    if (wasRunning && ! settings.enabled)
    {
        const auto* track = liveTargetTrack();

        for (auto& key : heldKeys)
        {
            if (track == nullptr || ! key.voices.empty())
                continue;

            for (const auto frequency : frequenciesForMidiNote (key.midiNote))
            {
                const auto liveId = nextLiveNoteId++;
                audio.noteOnTrack (track->id, liveId, frequency, key.velocity);
                key.voices.push_back ({ track->id, liveId });
            }
        }
    }
}

void MainComponent::refreshArpWindows()
{
    if (midiInputWindow != nullptr)
        midiInputWindow->getPanel().refreshArpControls();

    if (virtualKeyboardWindow != nullptr)
        virtualKeyboardWindow->refreshArpControls();
}

void MainComponent::setArpEnabledForTest (bool shouldBeOn)
{
    document.applyDerivedEdit ([shouldBeOn] (Project& project)
    {
        project.midiInput.arp.enabled = shouldBeOn;
    });

    arpSettingsChanged();
}

void MainComponent::setArpClockForTest (juce::int64 ticks)
{
    arp.forcedClockTicks = juce::jmax ((juce::int64) 0, ticks);
}

void MainComponent::setArpSettingsForTest (const midiarp::Settings& settings)
{
    document.applyDerivedEdit ([&settings] (Project& project)
    {
        project.midiInput.arp = settings;
        midiarp::clamp (project.midiInput.arp);
    });

    arpSettingsChanged();
}

void MainComponent::openVirtualKeyboardWindow()
{
    if (virtualKeyboardWindow == nullptr)
    {
        virtualKeyboardWindow = std::make_unique<VirtualKeyboardWindow> (document, appState);

        virtualKeyboardWindow->onNoteOn = [this] (int midiNote, float velocity,
                                                  juce::int64 trigger)
        {
            handleLiveNoteOn (midiNote, velocity, trigger);
        };

        virtualKeyboardWindow->onNoteOff = [this] (int midiNote, juce::int64 trigger)
        {
            handleLiveNoteOff (midiNote, trigger);
        };

        virtualKeyboardWindow->onArpSettingsChanged = [this] { arpSettingsChanged(); };
    }

    virtualKeyboardWindow->show();
    virtualKeyboardWindow->setKeysDown (getHeldKeysForTest());
}

bool MainComponent::isVirtualKeyboardVisible() const noexcept
{
    return virtualKeyboardWindow != nullptr && virtualKeyboardWindow->isVisible();
}

void MainComponent::setMidiDeviceListForTest (const std::vector<juce::String>& names)
{
    std::vector<MidiInputManager::DeviceInfo> list;

    for (int i = 0; i < (int) names.size(); ++i)
        list.push_back ({ names[(size_t) i], "test-midi-device-" + juce::String (i) });

    midiInput.setDeviceListForTest (std::move (list));

    if (midiInputWindow != nullptr)
        midiInputWindow->refresh();
}

juce::PopupMenu MainComponent::buildTrackMenu (juce::int64 trackId) const
{
    juce::PopupMenu menu;
    const auto* track = document.getProject().findTrack (trackId);

    // Only a VST track has a setting here today: it is the one that has to choose
    // between exact frequencies (a pitch bend of its own per note) and plain
    // semitones, because some instruments bend the whole patch and drag every
    // sounding note onto the last bend they were sent.
    if (track == nullptr || track->kind != TrackKind::vst || ! track->vst.hasPlugin())
        return menu;

    menu.addItem (1, NI_TXT ("按半音演奏（插件把弯音当整机时用）"), true,
                  track->vst.equalTemperament);

    return menu;
}

void MainComponent::showTrackMenu (juce::int64 trackId)
{
    auto menu = buildTrackMenu (trackId);

    if (menu.getNumItems() == 0)
        return;

    if (! showDialogs)
        return;   // the offscreen self test drives the menu through its items instead

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trackArea),
                        [this, trackId] (int result)
                        {
                            if (result != 0)
                                performTrackMenuCommand (trackId, result);
                        });
}

void MainComponent::performTrackMenuCommand (juce::int64 trackId, int itemId)
{
    if (itemId != 1)
        return;

    // One undoable edit. Nothing about the plugin instance changes: the flag decides
    // which frequency a note is played at, and the engine re-reads it with the next
    // schedule rebuild (which this edit triggers).
    document.applyEdit (NI_TXT ("VST 按半音演奏"), [trackId] (Project& project)
    {
        if (auto* track = project.findTrack (trackId))
            if (track->kind == TrackKind::vst)
                track->vst.equalTemperament = ! track->vst.equalTemperament;
    });
}

std::vector<MainComponent::TrackMenuItemForTest>
MainComponent::getTrackMenuItemsForTest (juce::int64 trackId) const
{
    std::vector<TrackMenuItemForTest> items;

    // The iterator holds a reference to the menu, so it has to outlive the loop.
    const auto menu = buildTrackMenu (trackId);

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();

        if (item.isSeparator)
            continue;

        items.push_back ({ item.itemID, item.text, item.isTicked });
    }

    return items;
}

void MainComponent::performTrackMenuCommandForTest (juce::int64 trackId, int itemId)
{
    performTrackMenuCommand (trackId, itemId);
}

void MainComponent::setWorkspace (AppState::Workspace workspace)
{
    appState.setWorkspace (workspace);
    toolbar.setWorkspace (workspace);
    resized();
}

void MainComponent::openMixerWindow()
{
    if (! showDialogs)
    {
        // The legacy offscreen self-test still drives the old mixer window. The
        // production UI uses the mixer page; keeping this path means the old
        // checks continue to exercise their intended track strips.
        if (mixerWindow == nullptr)
        {
            mixerWindow = std::make_unique<MixerWindow> (document, appState, audio, this);
            mixerWindow->onOpenInstrument = [this] (juce::int64 trackId) { openSynthWindow (trackId); };
            mixerWindow->onOpenEffect = [this] (juce::int64 trackId, int slot) { openEffectSlot (trackId, slot); };
            mixerWindow->onAddEffect = [this] (juce::int64 trackId) { pickEffectFor (trackId); };
            mixerWindow->onPlayStop = [this] { audio.togglePlay(); transportBar.refresh(); };
            mixerWindow->onReturnToStart = [this] { audio.returnToZero(); transportBar.refresh(); };
        }

        mixerWindow->showForProject();
        attachKeyListenersToWindows();
        return;
    }

    setWorkspace (AppState::Workspace::mixer);
}

void MainComponent::showClipRenameDialog (juce::int64 clipId)
{
    const auto* clip = document.getProject().findClip (clipId);

    if (clip == nullptr)
        return;

    if (! showDialogs)
    {
        // The offscreen self test cannot type into a dialog, so this path marks the
        // name instead: the rename still goes through the same edit.
        document.applyEdit (NI_TXT ("重命名片段"), [clipId] (Project& project)
        {
            if (auto* target = project.findClip (clipId))
                target->name += NI_TXT ("（重命名）");
        });

        return;
    }

    auto* window = new juce::AlertWindow (NI_TXT ("重命名片段"),
                                          NI_TXT ("给这个片段起个名字："),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", clip->name, NI_TXT ("名字"));
    window->addButton (NI_TXT ("改名"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, clipId] (int result)
        {
            if (result != 1)
                return;

            const auto name = window->getTextEditorContents ("name").trim();

            if (name.isEmpty())
                return;

            document.applyEdit (NI_TXT ("重命名片段"), [clipId, &name] (Project& project)
            {
                if (auto* target = project.findClip (clipId))
                    target->name = name;
            });
        }), true);
}

void MainComponent::replaceInstrument (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    const auto kind = track->kind;

    document.applyEdit (NI_TXT ("替换同类乐器"), [this, trackId] (Project& project)
    {
        auto* target = project.findTrack (trackId);

        if (target == nullptr)
            return;

        switch (target->kind)
        {
            case TrackKind::synth:
            {
                int count = 0;
                const auto* bank = fm::bank (count);
                int current = 0;

                for (int i = 0; i < count; ++i)
                    if (target->synth.patch.name == juce::String::fromUTF8 (bank[i].name))
                        current = i;

                if (count > 0)
                    fm::applyPreset (target->synth.patch, (current + 1) % count);
                break;
            }

            case TrackKind::subtractive:
            {
                int count = 0;
                const auto* bank = subtractive::bank (count);
                int current = 0;

                for (int i = 0; i < count; ++i)
                    if (target->subtractive.name == juce::String::fromUTF8 (bank[i].name))
                        current = i;

                if (count > 0)
                    subtractive::applyPreset (target->subtractive, (current + 1) % count);
                break;
            }

            case TrackKind::wavetable:
            {
                int count = 0;
                const auto* bank = wavetable::bank (count);
                int current = 0;

                for (int i = 0; i < count; ++i)
                    if (target->wavetable.name == juce::String::fromUTF8 (bank[i].name))
                        current = i;

                if (count > 0)
                    wavetable::applyPreset (target->wavetable, (current + 1) % count);
                break;
            }

            case TrackKind::minimoog:
            {
                int count = 0;
                const auto* bank = minimoog::bank (count);
                int current = 0;

                for (int i = 0; i < count; ++i)
                    if (target->minimoog.name == juce::String::fromUTF8 (bank[i].name))
                        current = i;

                if (count > 0)
                    minimoog::applyPreset (target->minimoog, (current + 1) % count);
                break;
            }

            case TrackKind::hybrid:
            {
                const auto count = hybridpresets::factoryPresetCount();
                int current = 0;

                for (int i = 0; i < count; ++i)
                    if (target->hybrid.name == hybridpresets::bank()[(size_t) i].name)
                        current = i;

                if (count > 0)
                    hybridpresets::apply (target->hybrid, (current + 1) % count);
                break;
            }

            case TrackKind::gm:
                target->gm.program = (target->gm.program + 1) % 128;
                break;

            case TrackKind::drums:
            case TrackKind::sampler:
            case TrackKind::granular:
            case TrackKind::vst:
            case TrackKind::audio:
                break;
        }
    });

    if (kind == TrackKind::vst || kind == TrackKind::sampler
        || kind == TrackKind::granular || kind == TrackKind::audio
        || kind == TrackKind::drums)
        openSynthWindow (trackId);
}

void MainComponent::openSynthWindow (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    if (track->kind == TrackKind::vst)
    {
        if (pluginWindow == nullptr)
            pluginWindow = std::make_unique<PluginWindow> (pluginHost, this);

        // A track that has just been opened may still need its plugin: ask, then
        // show. The window fills itself in when the instance arrives.
        pluginHost.requestInstrument (trackId, track->vst);
        pluginWindow->showForTrack (trackId, track->name);
        attachKeyListenersToWindows();
        return;
    }

    if (synthWindow == nullptr)
    {
        synthWindow = std::make_unique<SynthWindow> (document, audio, presetStore,
                                                     showDialogs, this, &appState);

        // 波表编辑器… in the wavetable page: the window that makes and draws the
        // project's own tables.
        synthWindow->getWavetableEditor().onEditTable = [this] { openWavetableTableWindow(); };
        // 删除波表 right next to it: the same action the editor window's own button
        // does, for the table the first oscillator is using.
        synthWindow->getWavetableEditor().onDeleteTable = [this]
        {
            const auto* track = document.getProject().findTrack (appState.selectedTrackId);

            if (track != nullptr && track->kind == TrackKind::wavetable)
                deleteCustomWavetable (track->wavetable.oscs[0].customTableId);
        };
    }

    synthWindow->showForTrack (trackId, track->name, track->kind);
    attachKeyListenersToWindows();
}

void MainComponent::pickEffectFor (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    if (! showDialogs)
        return;   // the offscreen self test adds effects through EffectsBar instead

    auto* picker = new PluginPicker (pluginHost, NI_TXT ("选择效果器"),
                                     PluginPicker::Kind::effect);

    picker->onChosen = [this, trackId] (const juce::PluginDescription& description)
    {
        // The bar adds it to whichever track is selected, so make sure that is the
        // one the user asked for.
        appState.selectTrack (trackId);
        effectsBar.addEffect (description);
    };

    picker->launch (getTopLevelComponent());
}

void MainComponent::openEffectSlot (juce::int64 trackId, int slotIndex)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr || slotIndex < 0 || slotIndex >= (int) track->effects.size())
        return;

    const auto& slot = track->effects[(size_t) slotIndex];

    // A slot whose plugin never loaded has no window to show, so the picker comes
    // back: that is how the user relocates or replaces it.
    if (pluginHost.effectPluginFor (trackId, slotIndex) == nullptr)
    {
        if (! showDialogs)
            return;

        auto* picker = new PluginPicker (pluginHost, NI_TXT ("重新选择效果器"),
                                         PluginPicker::Kind::effect);

        picker->onChosen = [this, trackId, slotIndex] (const juce::PluginDescription& description)
        {
            document.applyEdit (NI_TXT ("更换效果器"), [trackId, slotIndex, &description]
                                                      (Project& project)
            {
                if (auto* target = project.findTrack (trackId))
                    if (slotIndex < (int) target->effects.size())
                    {
                        auto& replaced = target->effects[(size_t) slotIndex];
                        replaced.kind       = 0;
                        replaced.name       = description.name;
                        replaced.pluginPath = description.fileOrIdentifier;
                        replaced.pluginUid  = juce::String ((juce::int64) description.uniqueId);
                        replaced.stateBase64.clear();
                    }
            });
        };

        picker->launch (getTopLevelComponent());
        return;
    }

    if (pluginWindow == nullptr)
        pluginWindow = std::make_unique<PluginWindow> (pluginHost, this);

    pluginHost.requestEffect (trackId, slotIndex, slot);
    pluginWindow->showForEffect (trackId, slotIndex,
                                 slot.name.isNotEmpty() ? slot.name : NI_TXT ("效果器"));
    attachKeyListenersToWindows();
}

void MainComponent::refreshVstLoadedFlags()
{
    if (updatingVstFlags)
        return;

    const auto& project = document.getProject();
    auto needsUpdate = false;

    for (const auto& track : project.tracks)
    {
        if (track.kind != TrackKind::vst)
            continue;

        const auto isUp = pluginHost.pluginFor (track.id) != nullptr;

        if (track.vst.loadedOk != isUp)
        {
            needsUpdate = true;
            break;
        }
    }

    if (! needsUpdate)
        return;

    const juce::ScopedValueSetter<bool> guard (updatingVstFlags, true);
    auto& host = pluginHost;

    document.applyDerivedEdit ([&host] (Project& state)
    {
        for (auto& track : state.tracks)
            if (track.kind == TrackKind::vst)
                track.vst.loadedOk = host.pluginFor (track.id) != nullptr;
    });
}

bool MainComponent::writeProject (const juce::File& file, juce::String& errorMessage)
{
    // A plugin's state is only knowable while it is loaded, so it is captured on
    // the way out rather than being stored as the user works.
    auto& host = pluginHost;
    document.applyDerivedEdit ([&host] (Project& project) { host.captureStates (project); });

    return document.saveToProjectFolder (file, errorMessage);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (mods.isCommandDown())
    {
        switch (code)
        {
            case 'Z': case 'z':
                if (mods.isShiftDown()) document.redo(); else document.undo();
                return true;

            case 'Y': case 'y':
                document.redo();
                return true;

            case 'N': case 'n':
                newProject();
                return true;

            case 'O': case 'o':
                openProject();
                return true;

            case 'S': case 's':
                saveProject (mods.isShiftDown());
                return true;

            case 'E': case 'e':
                exportWav();
                return true;

            case 'C': case 'c':
                copySelection();
                return true;

            case 'V': case 'v':
                pasteClipboard();
                return true;

            case 'D': case 'd':
                // Ctrl+D duplicates the open clip once, back to back.
                repeatSelectedClip (1);
                return true;

            default:
                return false;
        }
    }

    // Shift + up/down transposes the selection by whole octaves. This has to be
    // handled before the blanket "any modifier -> not ours" bail out below.
    if (mods.isShiftDown() && ! mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == juce::KeyPress::upKey)
        {
            shiftSelectedNotesByOctave (1);
            return true;
        }

        if (code == juce::KeyPress::downKey)
        {
            shiftSelectedNotesByOctave (-1);
            return true;
        }
    }

    // Alt+K: the virtual keyboard (the same way Cubase opens its on-screen one).
    if (mods.isAltDown() && ! mods.isCommandDown())
    {
        if (code == 'K' || code == 'k')
        {
            openVirtualKeyboardWindow();
            return true;
        }

        return false;
    }

    if (mods.isAnyModifierKeyDown())
        return false;

    // With the virtual keyboard on screen, a key the current layout plays is the
    // instrument: it must not also fire a single-key shortcut ("n" seals a reference
    // line; Backspace is a bass note in the accordion layout). The digits below
    // already follow the same rule. Real key events arrive with an upper-case letter
    // code; the lower-case form is accepted too, like the "n" shortcut below.
    const auto typingCode = (code >= 'a' && code <= 'z') ? code - ('a' - 'A') : code;

    if (isVirtualKeyboardVisible() && virtualKeyboardWindow != nullptr
        && virtualKeyboardWindow->getPanel().isTypingKey (typingCode))
        return false;

    if (code == juce::KeyPress::F3Key)
    {
        setWorkspace (AppState::Workspace::mixer);
        return true;
    }

    // Plain arrows walk the reference line status list, and "n" seals it.
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        editorPanel.getOptionsPanel().moveSelection (code == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }

    if (code == 'N' || code == 'n')
    {
        editorPanel.getOptionsPanel().toggleSelectedLock();
        return true;
    }

    // 频率比 keys (1-6). They are off while the virtual keyboard is on screen: in
    // the accordion layout those very keys are the bass row (and in the two-row
    // layout the digits 2 3 5 6 7 already play notes), so a player with the keyboard
    // up must not be changing a note's ratio by accident. The ratio *buttons* in the
    // note edit bar still work with the mouse.
    if (code >= '1' && code <= '6' && ! isVirtualKeyboardVisible())
    {
        const auto index = code - '1';

        if (index < 5)
            editorPanel.getNoteEditBar().applyPresetRatio (index);
        else
            editorPanel.getNoteEditBar().applyCustomRatio();

        return true;
    }

    if (isTransportSpaceKey (key))
    {
        audio.togglePlay();
        transportBar.refresh();
        return true;
    }

    if (code == juce::KeyPress::homeKey)
    {
        audio.returnToZero();
        transportBar.refresh();
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNotes();
        return true;
    }

    // `*` (the shifted 8 on most layouts, and the numpad star) arms MIDI recording.
    if (code == '*' || code == juce::KeyPress::numberPadMultiply)
    {
        appState.setMidiRecordArmed (! appState.midiRecordArmed);
        transportBar.refresh();
        return true;
    }

    return false;
}

} // namespace hmdaw
