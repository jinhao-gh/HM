#include "EditorPanel.h"
#include "Theme.h"

namespace hmdaw
{

namespace
{
    constexpr int kHeaderHeight = 30;
    /** Extra height the note edit strip takes when listing every selected note. */
    constexpr int kMultiNoteRowHeight = 22;
    constexpr int kMultiNoteMaxRows = 4;
    constexpr int kMarkerTrackHeight = 26;
    constexpr int kRulerHeight = 24;
    constexpr int kOptionsWidth = 264;
    constexpr int kScrollBarThickness = 12;
    constexpr int kResizerWidth = 6;
    /** The strip at the top of each column that holds its fold arrow. */
    constexpr int kPanelArrowHeight = 18;
    constexpr int kPanelArrowWidth  = 18;
    /** Combo id of the "不吸附" entry: the subdivisions keep ids 1 / 2 / 4. */
    constexpr int kSnapOffItemId = 5;
    /** The drag handle above the controller lane, and the room the canvas keeps
        whatever the lane is dragged to. */
    constexpr int kLaneHandleHeight = 6;
    constexpr int kMinCanvasHeight = 110;
    /** The left end of the lane's header row, where its fold arrow sits. */
    constexpr int kLaneArrowWidth = 18;

    /** 侧视 picker: the master switch, the per-track ticks, 全选 / 全不选. */
    constexpr int kSideViewMenuToggle     = 1;
    constexpr int kSideViewMenuAll        = 2;
    constexpr int kSideViewMenuNone       = 3;
    constexpr int kSideViewMenuNoTracks   = 9;
    constexpr int kSideViewMenuFirstTrack = 100;
}


EditorPanel::EditorPanel (ProjectDocument& documentToUse, AppState& appStateToUse,
                          AudioEngine& audioEngine, PresetStore& presets)
    : document (documentToUse),
      appState (appStateToUse),
      snapControl (documentToUse, appStateToUse, SnapArea::editor),
      markerTrack (documentToUse, appStateToUse),
      ruler (documentToUse, appStateToUse, audioEngine),
      noteEditBar (documentToUse, appStateToUse),
      controllerLane (documentToUse, appStateToUse, audioEngine),
      optionsPanel (documentToUse, appStateToUse),
      grid (documentToUse, appStateToUse, audioEngine),
      referenceGroupPanel (documentToUse, appStateToUse),
      presetPanel (documentToUse, appStateToUse, presets),
      groupEditorPanel (documentToUse, appStateToUse, presets)
{
    addAndMakeVisible (markerTrack);
    addAndMakeVisible (ruler);
    addAndMakeVisible (noteEditBar);
    addAndMakeVisible (controllerLane);
    addAndMakeVisible (optionsPanel);
    addAndMakeVisible (grid);
    addAndMakeVisible (referenceGroupPanel);
    addChildComponent (presetPanel);
    addChildComponent (groupEditorPanel);


    referenceGroupPanel.onSavePreset = [this, &presets]
    {
        presets.addReferenceGroup (document.getProject().referenceGroup);
        presetPanel.refresh();
    };

    // ---- the three collapsible columns -------------------------------------
    // Each column has a drag handle on its inner edge and a little arrow in its
    // header; folding one hides the panel itself and leaves only the arrow, which is
    // why the arrow is a child of this panel rather than of the column.
    for (auto* handle : { &groupResizer, &sideResizer, &linesResizer })
    {
        addChildComponent (*handle);
        handle->onDragStart = [this, handle] { beginPanelDrag (sidePanelIdFor (handle)); };
        handle->onDrag = [this, handle] (int deltaX)
        { dragPanelForTest (sidePanelIdFor (handle), deltaX); };
        handle->onReset = [this, handle]
        {
            panelState (sidePanelIdFor (handle)) = {};
            resized();
        };
    }

    for (auto* toggle : { &groupToggle, &sideToggle, &linesToggle })
    {
        // addAndMakeVisible, not addChildComponent: a fresh Component starts out
        // invisible, so the arrows would never have been on screen at all.
        addAndMakeVisible (*toggle);
        toggle->onClick = [this, toggle]
        {
            const auto which = toggle == &groupToggle ? SidePanelId::referenceGroup
                                                       : (toggle == &sideToggle ? SidePanelId::presets
                                                                                : SidePanelId::referenceLines);
            togglePanelForTest (which);
        };
    }

    // ---- the controller lane at the bottom ---------------------------------
    // The same contract as the three columns: the handle reports the drag, the arrow
    // folds the lane down to a thin bar, and both are children of this panel (so a
    // folded lane still has something on screen to click).
    addChildComponent (laneResizer);
    laneResizer.onDragStart = [this]
    {
        laneDragStartHeight = panelWidthFor (document.viewState().controllerLane,
                                             ControllerLane::defaultHeight);
    };
    laneResizer.onDrag = [this] (int deltaY) { dragControllerLaneForTest (deltaY); };
    laneResizer.onReset = [this]
    {
        document.viewState().controllerLane = {};
        resized();
    };

    addAndMakeVisible (laneToggle);
    laneToggle.onClick = [this] { toggleControllerLaneForTest(); };

    addAndMakeVisible (noteLockButton);
    noteLockButton.setButtonText (NI_TXT ("锁定音符"));
    noteLockButton.setClickingTogglesState (true);
    noteLockButton.setTooltip (NI_TXT ("开启后，写入新音符不会改变当前选中的音符"));
    noteLockButton.onClick = [this]
    {
        appState.setNoteLock (noteLockButton.getToggleState());
    };

    addAndMakeVisible (locateButton);
    locateButton.setButtonText (NI_TXT ("定位选中音符"));
    locateButton.onClick = [this] { grid.centreOnSelectedNote(); };

    addAndMakeVisible (snapControl);
    addAndMakeVisible (autoFitLabel);
    autoFitLabel.setText (NI_TXT ("纵向取景"), juce::dontSendNotification);
    autoFitLabel.setFont (theme::font (11.5f));
    autoFitLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (autoFitBox);
    autoFitBox.addItem (NI_TXT ("跟随选中音符"), 1);
    autoFitBox.addItem (NI_TXT ("视图锁定"), 2);
    autoFitBox.onChange = [this]
    {
        if (updating)
            return;

        document.viewState().autoFitView = autoFitBox.getSelectedId() == 1;
        appState.sendChangeMessage();
    };

    addAndMakeVisible (closeButton);
    closeButton.setButtonText (NI_TXT ("关闭编辑栏"));
    closeButton.onClick = [this] { appState.setEditorOpen (false); };

    addAndMakeVisible (modeLabel);
    modeLabel.setText (NI_TXT ("模式"), juce::dontSendNotification);
    modeLabel.setFont (theme::font (11.5f));
    modeLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (modeBox);
    modeBox.addItem (NI_TXT ("参考线模式"), 1);
    modeBox.addItem (NI_TXT ("钢琴窗模式"), 2);
    modeBox.setTooltip (NI_TXT ("参考线模式在频率参考线上写音符；钢琴窗模式按半音行写音符"));
    modeBox.onChange = [this]
    {
        if (updating)
            return;

        const auto mode = modeBox.getSelectedId() == 2 ? EditorMode::piano
                                                       : EditorMode::reference;

        if (document.viewState().editorMode == mode)
            return;

        document.viewState().editorMode = mode;

        // Switching into the piano window lines the rows up with the canvas, so
        // no row is cut in half at the top or the bottom.
        if (mode == EditorMode::piano)
            grid.alignPianoView();

        appState.sendChangeMessage();
    };

    addAndMakeVisible (pianoLinesButton);
    pianoLinesButton.setButtonText (NI_TXT ("参考线显示"));
    pianoLinesButton.setClickingTogglesState (true);
    pianoLinesButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    pianoLinesButton.setTooltip (NI_TXT ("钢琴窗里是否画出参考线（只作参照，不能在线上写音符）"));
    pianoLinesButton.onClick = [this]
    {
        if (updating)
            return;

        document.viewState().showReferenceLinesInPianoMode = pianoLinesButton.getToggleState();
        appState.sendChangeMessage();
    };

    addAndMakeVisible (sideViewButton);
    sideViewButton.setButtonText (NI_TXT ("侧视"));
    sideViewButton.setClickingTogglesState (true);
    sideViewButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    sideViewButton.setTooltip (NI_TXT ("把其他轨道的音符以变暗的形式画在画布上，"
                                       "只作参照（不能点、不能选、不能编辑）；"
                                       "右边的小箭头可以挑要看哪些轨道"));
    sideViewButton.onClick = [this]
    {
        if (updating)
            return;

        document.viewState().sideView = sideViewButton.getToggleState();
        appState.sendChangeMessage();
    };

    addAndMakeVisible (sideViewArrow);
    sideViewArrow.setTooltip (NI_TXT ("选择侧视要看哪些轨道"));
    sideViewArrow.onClick = [this] { showSideViewTrackMenu(); };

    document.addChangeListener (this);
    appState.addChangeListener (this);
}

EditorPanel::~EditorPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void EditorPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void EditorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& view = document.viewState();

    // The three columns' widths and folded state travel with the project, so a
    // refresh (a project load, an undo) has to put the arrows back in step too.
    refreshPanelVisibility();

    const auto* track = document.getProject().findTrack (appState.selectedTrackId);

    noteLockButton.setToggleState (appState.noteLock, juce::dontSendNotification);
    locateButton.setEnabled (track != nullptr && appState.hasNoteSelection());
    snapControl.refresh();
    autoFitBox.setSelectedId (view.autoFitView ? 1 : 2, juce::dontSendNotification);
    // A drum track shows its pads instead of a frequency axis, so there is no
    // vertical view to fit.
    autoFitBox.setEnabled (track == nullptr || track->kind != TrackKind::drums);

    // The piano window needs a pitch axis and notes to write, so it is not on
    // offer for the drum grid or for an audio track's read-only waveform.
    const auto pianoAvailable = track == nullptr
                                    || (track->kind != TrackKind::drums
                                        && track->kind != TrackKind::audio);
    modeBox.setEnabled (pianoAvailable);
    modeLabel.setEnabled (pianoAvailable);
    modeBox.setSelectedId (view.editorMode == EditorMode::piano ? 2 : 1,
                           juce::dontSendNotification);

    const auto pianoMode = pianoAvailable && view.editorMode == EditorMode::piano;
    pianoLinesButton.setVisible (pianoMode);
    pianoLinesButton.setToggleState (view.showReferenceLinesInPianoMode,
                                     juce::dontSendNotification);

    // 侧视 draws other tracks' notes on the same frequency axis, so it wants the
    // same "there is a pitch axis here" rule as the mode switch.
    sideViewButton.setEnabled (pianoAvailable);
    sideViewButton.setToggleState (view.sideView, juce::dontSendNotification);
    sideViewButton.setTooltip (pianoAvailable
                                   ? NI_TXT ("把其他轨道的音符以变暗的形式画在画布上，"
                                             "只作参照（不能点、不能选、不能编辑）；"
                                             "右边的小箭头可以挑要看哪些轨道")
                                   : NI_TXT ("鼓机轨 / 音频轨的画布没有频率轴，用不了侧视"));

    // The picker only means anything where the ghosts can be drawn at all.
    sideViewArrow.setEnabled (pianoAvailable);
    sideViewArrow.setTooltip (pianoAvailable
                                  ? NI_TXT ("选择侧视要看哪些轨道")
                                  : NI_TXT ("鼓机轨 / 音频轨的画布没有频率轴，用不了侧视"));

    markerTrack.refresh();
    ruler.refresh();
    referenceGroupPanel.refresh();
    presetPanel.refresh();
    groupEditorPanel.refresh();
    noteEditBar.refresh();
    noteEditBar.setVisible (appState.hasNoteSelection());
    controllerLane.refresh();
    optionsPanel.refresh();

    resized();
}

void EditorPanel::refreshPlayhead()
{
    grid.refreshPlayhead();
    ruler.refreshPlayhead();
    controllerLane.refreshPlayhead();
}

void EditorPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    // The note-edit strip is always there (see resized()); while nothing is
    // selected the bar itself is hidden, so say what the strip is for.
    if (! appState.hasNoteSelection() && ! noteStripBounds.isEmpty())
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f));
        g.drawText (NI_TXT ("选中音符后，可以在这里改频率 / 力度 / 备注；"
                            "选中一个音符时下面还有频率比"),
                    noteStripBounds.reduced (10, 0), juce::Justification::centredLeft, true);
    }
}

void EditorPanel::selectSnapForTest (int itemId)
{
    if (itemId != 1 && itemId != 2 && itemId != 4 && itemId != kSnapOffItemId)
        return;

    document.viewState().editorSnap.enabled = itemId != kSnapOffItemId;

    if (itemId != kSnapOffItemId)
        document.viewState().editorSnap.denominator = itemId;

    snapControl.refresh();
    appState.sendChangeMessage();
}

void EditorPanel::selectEditorModeForTest (int itemId)
{
    if (itemId != 1 && itemId != 2)
        return;

    modeBox.setSelectedId (itemId, juce::dontSendNotification);

    // Same path a real click takes.
    if (modeBox.onChange != nullptr)
        modeBox.onChange();
}

void EditorPanel::setShowReferenceLinesForTest (bool shouldShow)
{
    pianoLinesButton.setToggleState (shouldShow, juce::dontSendNotification);

    if (pianoLinesButton.onClick != nullptr)
        pianoLinesButton.onClick();
}

void EditorPanel::toggleSideViewForTest()
{
    sideViewButton.setToggleState (! sideViewButton.getToggleState(), juce::dontSendNotification);

    if (sideViewButton.onClick != nullptr)
        sideViewButton.onClick();
}

void EditorPanel::triggerSideViewArrowForTest()
{
    if (sideViewArrow.onClick != nullptr)
        sideViewArrow.onClick();
}

std::vector<juce::int64> EditorPanel::sideViewSelectableTracks() const
{
    std::vector<juce::int64> ids;

    for (const auto& track : document.getProject().tracks)
    {
        // The open track is the one being edited, and a drum machine's notes are pad
        // hits rather than pitches: neither can be a ghost on this canvas.
        if (track.id == appState.selectedTrackId || track.kind == TrackKind::drums)
            continue;

        ids.push_back (track.id);
    }

    return ids;
}

juce::PopupMenu EditorPanel::buildSideViewMenu() const
{
    // The ids: 1 the master switch, then one per selectable track from
    // kSideViewMenuFirstTrack up, then 全选 / 全不选.
    juce::PopupMenu menu;
    const auto& view = document.viewState();

    menu.addItem (kSideViewMenuToggle, NI_TXT ("显示侧视"), true, view.sideView);
    menu.addSeparator();

    const auto selectable = sideViewSelectableTracks();

    if (selectable.empty())
    {
        menu.addItem (kSideViewMenuNoTracks, NI_TXT ("没有别的轨道可以显示"), false, false);
        return menu;
    }

    for (int i = 0; i < (int) selectable.size(); ++i)
    {
        const auto* track = document.getProject().findTrack (selectable[(size_t) i]);

        if (track == nullptr)
            continue;

        menu.addItem (kSideViewMenuFirstTrack + i, track->name, true,
                      view.sideViewHiddenTracks.count (track->id) == 0);
    }

    menu.addSeparator();
    menu.addItem (kSideViewMenuAll, NI_TXT ("全选"), true, false);
    menu.addItem (kSideViewMenuNone, NI_TXT ("全不选"), true, false);
    return menu;
}

bool EditorPanel::performSideViewMenuCommand (int menuId)
{
    auto& view = document.viewState();
    const auto selectable = sideViewSelectableTracks();

    if (menuId == kSideViewMenuToggle)
    {
        view.sideView = ! view.sideView;
    }
    else if (menuId == kSideViewMenuAll || menuId == kSideViewMenuNone)
    {
        for (const auto id : selectable)
        {
            if (menuId == kSideViewMenuNone)
                view.sideViewHiddenTracks.insert (id);
            else
                view.sideViewHiddenTracks.erase (id);
        }
    }
    else if (menuId >= kSideViewMenuFirstTrack)
    {
        const auto index = menuId - kSideViewMenuFirstTrack;

        if (index < 0 || index >= (int) selectable.size())
            return false;

        const auto id = selectable[(size_t) index];

        if (! view.sideViewHiddenTracks.erase (id))
            view.sideViewHiddenTracks.insert (id);
    }
    else
    {
        return false;
    }

    // The picker only ever changes view state, which is not undoable - but the
    // canvas has to be told to redraw.
    appState.sendChangeMessage();
    repaint();
    return true;
}

void EditorPanel::showSideViewTrackMenu()
{
    ++sideViewPickerRequests;

    if (! showDialogs)
        return;   // the offscreen self test drives the menu through its items instead

    buildSideViewMenu().showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&sideViewArrow),
        [this] (int result)
        {
            if (result != 0)
                performSideViewMenuCommand (result);
        });
}

std::vector<EditorPanel::SideViewMenuItemForTest> EditorPanel::getSideViewMenuItemsForTest() const
{
    std::vector<SideViewMenuItemForTest> items;
    const auto selectable = sideViewSelectableTracks();
    auto trackIndex = 0;

    // The iterator holds a reference to the menu, so it has to outlive the loop:
    // iterating a temporary here read freed memory (and took the process with it).
    const auto menu = buildSideViewMenu();

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();

        if (item.isSeparator)
            continue;

        juce::int64 trackId = 0;

        if (item.itemID >= kSideViewMenuFirstTrack && trackIndex < (int) selectable.size())
            trackId = selectable[(size_t) trackIndex++];

        items.push_back ({ item.itemID, item.text, item.isEnabled, item.isTicked, trackId });
    }

    return items;
}

bool EditorPanel::performSideViewMenuCommandForTest (int menuId)
{
    return performSideViewMenuCommand (menuId);
}

void EditorPanel::resized()
{
    auto area = getLocalBounds();

    auto& view = document.viewState();
    const auto sideVisible = appState.sidePanel != AppState::SidePanel::none;

    // The three columns want the widths the user dragged them to. If the row cannot
    // hold them, the widest one that is not folded gives way first, so the canvas
    // keeps its minimum width and nothing ever collapses to nothing.
    laidOutGroupWidth = panelWidthFor (view.referenceGroupPanel, ReferenceGroupPanel::panelWidth);
    laidOutSideWidth  = panelWidthFor (view.presetPanel, PresetPanel::defaultWidth);
    laidOutLinesWidth = panelWidthFor (view.referenceLinesPanel, kOptionsWidth);

    {
        std::vector<int*> widths { &laidOutGroupWidth, &laidOutLinesWidth };

        if (sideVisible)
            widths.push_back (&laidOutSideWidth);

        fitPanelsToCanvas (area.getWidth(), (sideVisible ? 2 : 1) * kResizerWidth, widths);
    }

    // ---- left: 参考组栏 ----
    {
        auto column = area.removeFromLeft (laidOutGroupWidth);
        const auto folded = view.referenceGroupPanel.collapsed;
        // While a group is being edited the same slot shows that group's editor,
        // so the column the user clicked 编辑 in is the one that changes.
        const auto editingGroup = appState.groupEditor.isOpen();

        if (folded)
        {
            referenceGroupPanel.setVisible (false);
            groupEditorPanel.setVisible (false);
            groupToggle.setBounds (column);
        }
        else
        {
            // The arrow gets a strip of its own along the top, so it never sits on
            // top of the column's own buttons.
            auto arrowRow = column.removeFromTop (kPanelArrowHeight);
            groupToggle.setBounds (arrowRow.removeFromRight (kPanelArrowWidth));
            referenceGroupPanel.setVisible (! editingGroup);
            groupEditorPanel.setVisible (editingGroup);

            if (editingGroup)
                groupEditorPanel.setBounds (column);
            else
                referenceGroupPanel.setBounds (column);
        }

        groupToggle.setCollapsed (folded);
        groupResizer.setVisible (! folded);
        groupResizer.setBounds (area.removeFromLeft (folded ? 0 : kResizerWidth));
    }

    // ---- left inner: 预设 / 历史栏 ----
    if (sideVisible)
    {
        auto column = area.removeFromLeft (laidOutSideWidth);
        const auto folded = view.presetPanel.collapsed;

        if (folded)
        {
            presetPanel.setVisible (false);
            sideToggle.setBounds (column);
        }
        else
        {
            auto arrowRow = column.removeFromTop (kPanelArrowHeight);
            sideToggle.setBounds (arrowRow.removeFromRight (kPanelArrowWidth));
            presetPanel.setVisible (true);
            presetPanel.setBounds (column);
        }

        sideToggle.setCollapsed (folded);
        sideResizer.setVisible (! folded);
        sideResizer.setBounds (area.removeFromLeft (folded ? 0 : kResizerWidth));
    }
    else
    {
        presetPanel.setVisible (false);
        sideToggle.setVisible (false);
        sideResizer.setVisible (false);
    }

    // ---- right: 参考线状态栏 ----
    {
        auto column = area.removeFromRight (laidOutLinesWidth);
        const auto folded = view.referenceLinesPanel.collapsed;

        if (folded)
        {
            optionsPanel.setVisible (false);
            linesToggle.setBounds (column);
        }
        else
        {
            auto arrowRow = column.removeFromTop (kPanelArrowHeight);
            linesToggle.setBounds (arrowRow.removeFromLeft (kPanelArrowWidth));
            optionsPanel.setVisible (true);
            optionsPanel.setBounds (column);
        }

        linesToggle.setCollapsed (folded);
        linesResizer.setVisible (! folded);
        linesResizer.setBounds (area.removeFromRight (folded ? 0 : kResizerWidth));
    }

    auto header = area.removeFromTop (kHeaderHeight);

    noteLockButton.setBounds (header.removeFromLeft (92).reduced (6, 4));
    header.removeFromLeft (4);
    snapControl.setBounds (header.removeFromLeft (106).reduced (0, 2));
    header.removeFromLeft (8);
    autoFitLabel.setBounds (header.removeFromLeft (58));
    autoFitBox.setBounds (header.removeFromLeft (100).reduced (0, 3));
    header.removeFromLeft (8);
    modeLabel.setBounds (header.removeFromLeft (30));
    modeBox.setBounds (header.removeFromLeft (96).reduced (0, 3));

    // 侧视 sits right after the mode switch and is on screen in both modes - the
    // canvas view options belong together. The three combos above gave up 26 px so
    // this button fits without squeezing anything else at the usual window size.
    header.removeFromLeft (8);
    sideViewButton.setBounds (header.removeFromLeft (64).reduced (0, 4));
    header.removeFromLeft (2);
    // The little arrow opens the picker: "which tracks do I want to see?".
    sideViewArrow.setBounds (header.removeFromLeft (18).reduced (0, 4));

    // The piano window's 参考线 toggle only exists in that mode, so it never
    // takes room from the reference view.
    if (pianoLinesButton.isVisible())
    {
        header.removeFromLeft (8);
        pianoLinesButton.setBounds (header.removeFromLeft (110).reduced (0, 4));
    }

    closeButton.setBounds (header.removeFromRight (100).reduced (6, 4));
    header.removeFromRight (6);
    locateButton.setBounds (header.removeFromRight (120).reduced (6, 4));

    // The strip is reserved whether or not anything is selected, and the bar never
    // grows beyond it: with several notes selected its per-note list hangs over the
    // canvas instead. Resizing the canvas with the selection would move the note
    // under the pointer (and the drag's own maths) every time the selection changed.
    auto strip = area.removeFromTop (NoteEditBar::slotHeight);
    noteStripBounds = strip;
    noteEditBar.setBounds (strip.withHeight (NoteEditBar::slotHeight
                                             + noteEditBar.getExtraHeight()));
    noteEditBar.toFront (false);

    // The marker track is aligned with the grid's drawing area, i.e. it stops
    // where the grid's vertical scroll bar starts.
    //
    // 钢琴窗模式 里画布左边让出一条键盘，标尺与标记轨跟着让同样的宽度，
    // 这样上面画的刻度仍然落在画布的同一列上。
    const auto pianoInset = grid.isPianoMode() ? PianoGrid::kPianoKeysWidth : 0;

    markerTrack.setBounds (area.removeFromTop (kMarkerTrackHeight)
                               .withTrimmedRight (kScrollBarThickness)
                               .withTrimmedLeft (pianoInset));

    ruler.setBounds (area.removeFromTop (kRulerHeight)
                         .withTrimmedRight (kScrollBarThickness)
                         .withTrimmedLeft (pianoInset));

    // ---- the controller lane, at the very bottom ---------------------------
    {
        const auto& saved = view.controllerLane;
        const auto wantsHeight = panelWidthFor (saved, ControllerLane::defaultHeight);

        // It may never eat the canvas: the grid keeps a usable height whatever the
        // window (and whatever the lane was dragged to on a bigger screen).
        const auto room = juce::jmax (ControllerLane::minHeight,
                                      area.getHeight() - kMinCanvasHeight);
        const auto laneHeight = saved.collapsed ? kCollapsedPanelWidth
                                                : juce::jmin (wantsHeight, room);

        // The lane is trimmed by the piano window's key column, exactly like the
        // ruler and the marker track above it: its "tick to x" is then the canvas's
        // own mapping with nothing added or taken away.
        auto laneArea = area.removeFromBottom (juce::jmin (laneHeight, area.getHeight()))
                            .withTrimmedLeft (pianoInset)
                            .withTrimmedRight (kScrollBarThickness);

        if (saved.collapsed)
        {
            // Folded away: the thin bar is the arrow itself, exactly like a folded
            // side column (nothing else of the lane is on screen).
            controllerLane.setVisible (false);
            laneResizer.setVisible (false);
            laneToggle.setBounds (laneArea);
        }
        else
        {
            controllerLane.setVisible (true);
            laneResizer.setVisible (true);
            laneResizer.setBounds (area.removeFromBottom (kLaneHandleHeight)
                                       .withTrimmedLeft (pianoInset)
                                       .withTrimmedRight (kScrollBarThickness));

            // The lane starts where the ruler and the canvas start, so its ticks line
            // up with them; its own header leaves the first 18 px free for the arrow,
            // which is this panel's child (so a folded lane still has a way back).
            controllerLane.setBounds (laneArea);
            laneToggle.setBounds (laneArea.getX(), laneArea.getY(),
                                  kLaneArrowWidth,
                                  juce::jmin (ControllerLane::headerHeight, laneArea.getHeight()));
        }

        laneToggle.setCollapsed (saved.collapsed);
    }

    grid.setBounds (area);
}

//==============================================================================
EditorViewState::PanelSize& EditorPanel::panelState (SidePanelId which)
{
    auto& view = document.viewState();

    switch (which)
    {
        case SidePanelId::presets:       return view.presetPanel;
        case SidePanelId::referenceLines: return view.referenceLinesPanel;
        case SidePanelId::referenceGroup:
        default:                          return view.referenceGroupPanel;
    }
}

juce::Component& EditorPanel::panelComponent (SidePanelId which)
{
    switch (which)
    {
        case SidePanelId::presets:        return presetPanel;
        case SidePanelId::referenceLines: return optionsPanel;
        case SidePanelId::referenceGroup:
        default:                          return referenceGroupPanel;
    }
}

int EditorPanel::panelDefaultWidth (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return PresetPanel::defaultWidth;
        case SidePanelId::referenceLines: return kOptionsWidth;
        case SidePanelId::referenceGroup:
        default:                          return ReferenceGroupPanel::panelWidth;
    }
}

int EditorPanel::getPanelWidthForTest (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return laidOutSideWidth;
        case SidePanelId::referenceLines: return laidOutLinesWidth;
        case SidePanelId::referenceGroup:
        default:                          return laidOutGroupWidth;
    }
}

bool EditorPanel::isPanelCollapsedForTest (SidePanelId which) const
{
    const auto& view = document.viewState();

    switch (which)
    {
        case SidePanelId::presets:        return view.presetPanel.collapsed;
        case SidePanelId::referenceLines: return view.referenceLinesPanel.collapsed;
        case SidePanelId::referenceGroup:
        default:                          return view.referenceGroupPanel.collapsed;
    }
}

void EditorPanel::beginPanelDrag (SidePanelId which)
{
    const auto width = getPanelWidthForTest (which);

    switch (which)
    {
        case SidePanelId::presets:        sideDragStartWidth = width; break;
        case SidePanelId::referenceLines: linesDragStartWidth = width; break;
        case SidePanelId::referenceGroup:
        default:                          groupDragStartWidth = width; break;
    }
}

bool EditorPanel::isPanelVisibleForTest (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return presetPanel.isVisible();
        case SidePanelId::referenceLines: return optionsPanel.isVisible();
        case SidePanelId::referenceGroup:
        default:                          return referenceGroupPanel.isVisible();
    }
}

bool EditorPanel::isPanelToggleVisibleForTest (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return sideToggle.isVisible();
        case SidePanelId::referenceLines: return linesToggle.isVisible();
        case SidePanelId::referenceGroup:
        default:                          return groupToggle.isVisible();
    }
}

juce::Rectangle<int> EditorPanel::getPanelToggleBoundsForTest (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return sideToggle.getBounds();
        case SidePanelId::referenceLines: return linesToggle.getBounds();
        case SidePanelId::referenceGroup:
        default:                          return groupToggle.getBounds();
    }
}

juce::Rectangle<int> EditorPanel::getPanelResizerBoundsForTest (SidePanelId which) const
{
    switch (which)
    {
        case SidePanelId::presets:        return sideResizer.getBounds();
        case SidePanelId::referenceLines: return linesResizer.getBounds();
        case SidePanelId::referenceGroup:
        default:                          return groupResizer.getBounds();
    }
}

void EditorPanel::togglePanelForTest (SidePanelId which)
{
    panelState (which).collapsed = ! panelState (which).collapsed;
    resized();
    repaint();
}

//==============================================================================
int EditorPanel::getControllerLaneHeightForTest() const
{
    const auto& saved = document.viewState().controllerLane;

    return saved.collapsed ? kCollapsedPanelWidth
                           : panelWidthFor (saved, ControllerLane::defaultHeight);
}

bool EditorPanel::isControllerLaneCollapsedForTest() const
{
    return document.viewState().controllerLane.collapsed;
}

void EditorPanel::toggleControllerLaneForTest()
{
    auto& saved = document.viewState().controllerLane;

    // A lane that has never been dragged has no width of its own yet: remember the
    // one it is showing, so unfolding comes back to the same height.
    if (! saved.collapsed && saved.size == 0)
        saved.size = ControllerLane::defaultHeight;

    saved.collapsed = ! saved.collapsed;
    controllerLane.refresh();
    resized();
    repaint();
}

void EditorPanel::dragControllerLaneForTest (int deltaY)
{
    auto& saved = document.viewState().controllerLane;

    // The handle sits on the lane's *top* edge, so dragging upwards makes the lane
    // taller - the delta is the other way round.
    const auto origin = laneDragStartHeight > 0 ? laneDragStartHeight
                                                : getControllerLaneHeightForTest();
    const auto tallest = juce::jmax (ControllerLane::minHeight,
                                     getHeight() - kMinCanvasHeight - kLaneHandleHeight);

    saved.size = juce::jlimit (ControllerLane::minHeight, tallest, origin - deltaY);
    saved.collapsed = false;
    resized();
    repaint();
}

void EditorPanel::dragPanelForTest (SidePanelId which, int deltaX)
{
    auto& state = panelState (which);

    // A drag reports the distance from the press, so the width it started at matters.
    const auto startWidth = [this, which]
    {
        switch (which)
        {
            case SidePanelId::presets:        return sideDragStartWidth;
            case SidePanelId::referenceLines: return linesDragStartWidth;
            case SidePanelId::referenceGroup:
            default:                          return groupDragStartWidth;
        }
    }();

    // A drag that never went through onDragStart (the self test drives the same
    // callback) measures from where the column is now.
    const auto origin = startWidth > 0 ? startWidth : getPanelWidthForTest (which);

    // The right-hand column's handle sits on its left edge, so dragging it to the
    // left makes the column *wider*: the delta is the other way round there.
    const auto grow = which == SidePanelId::referenceLines ? -deltaX : deltaX;

    state.size = juce::jlimit (kMinPanelWidth, 2000, origin + grow);
    state.collapsed = false;
    resized();
    repaint();
}

void EditorPanel::refreshPanelVisibility()
{
    groupToggle.setCollapsed (document.viewState().referenceGroupPanel.collapsed);
    sideToggle.setCollapsed (document.viewState().presetPanel.collapsed);
    linesToggle.setCollapsed (document.viewState().referenceLinesPanel.collapsed);
    laneToggle.setCollapsed (document.viewState().controllerLane.collapsed);
    resized();
}

EditorPanel::SidePanelId EditorPanel::sidePanelIdFor (const juce::Component* handle) const
{
    if (handle == &sideResizer)
        return SidePanelId::presets;

    if (handle == &linesResizer)
        return SidePanelId::referenceLines;

    return SidePanelId::referenceGroup;
}

} // namespace hmdaw
