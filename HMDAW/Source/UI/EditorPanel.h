#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "ControllerLane.h"
#include "GroupEditorPanel.h"
#include "NoteEditBar.h"
#include "OptionsPanel.h"
#include "PanelHandle.h"
#include "PianoGrid.h"
#include "PresetPanel.h"
#include "ReferenceGroupPanel.h"
#include "SnapControl.h"
#include "TempoMarkerTrack.h"
#include "TimelineRuler.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hmdaw
{

/** The editor that expands below the track area: header strip, note edit strip,
    tempo marker track, the grid itself, and the options column.
*/
class EditorPanel : public juce::Component,
                    private juce::ChangeListener
{
public:
    EditorPanel (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine,
                 PresetStore& presets);
    ~EditorPanel() override;

    /** False in the offscreen self test: a real popup menu would linger there. */
    bool showDialogs = true;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();
    void refreshPlayhead();

    PianoGrid& getGrid() noexcept { return grid; }
    /** The controller lane under the canvas (velocity / drawn CC curves). */
    ControllerLane& getControllerLane() noexcept { return controllerLane; }
    TimelineRuler& getRuler() noexcept { return ruler; }
    NoteEditBar& getNoteEditBar() noexcept { return noteEditBar; }
    /** Diagnostic hook: where the note-edit strip is (self test). */
    juce::Rectangle<int> getNoteStripBoundsForTest() const { return noteStripBounds; }
    ReferenceGroupPanel& getReferenceGroupPanel() noexcept { return referenceGroupPanel; }
    /** The left column while a group is being edited (参考组编辑栏 / 参考线组编辑栏). */
    GroupEditorPanel& getGroupEditorPanel() noexcept { return groupEditorPanel; }
    bool isGroupEditorOpenForTest() const { return appState.groupEditor.isOpen(); }
    PresetPanel& getPresetPanel() noexcept { return presetPanel; }
    OptionsPanel& getOptionsPanel() noexcept { return optionsPanel; }

    /** Which 吸附 entry the combo shows (1 / 2 / 4 = subdivisions, 5 = 不吸附). */
    int getSnapBoxIdForTest() const
    {
        return document.viewState().editorSnap.enabled
                   ? document.viewState().editorSnap.clampedDenominator() : 5;
    }
    SnapControl& getSnapControlForTest() noexcept { return snapControl; }
    /** Picks a 吸附 entry through the same path the mouse uses. */
    void selectSnapForTest (int itemId);

    // ---- 模式 (参考线模式 / 钢琴窗模式) ----
    /** 1 = 参考线模式, 2 = 钢琴窗模式. */
    int getEditorModeForTest() const { return modeBox.getSelectedId(); }
    /** Picks a mode through the same path the mouse uses. */
    void selectEditorModeForTest (int itemId);
    /** False while the open track has no semitone axis (drums, audio). */
    bool isModeBoxEnabledForTest() const { return modeBox.isEnabled(); }
    /** Whether the piano window's 参考线 toggle is on screen. */
    bool isPianoLinesButtonVisibleForTest() const { return pianoLinesButton.isVisible(); }
    /** Flips it through the same path the mouse uses. */
    void setShowReferenceLinesForTest (bool shouldShow);
    juce::Rectangle<int> getModeBoxBoundsForTest() const { return modeBox.getBounds(); }
    juce::Rectangle<int> getPianoLinesButtonBoundsForTest() const { return pianoLinesButton.getBounds(); }

    // ---- 侧视 (other tracks as a dim background board) ----------------------
    /** The toggle lives in the header and is on screen in both editor modes. */
    juce::Rectangle<int> getSideViewButtonBoundsForTest() const { return sideViewButton.getBounds(); }
    juce::String getSideViewButtonTextForTest() const { return sideViewButton.getButtonText(); }
    bool isSideViewButtonEnabledForTest() const { return sideViewButton.isEnabled(); }
    bool isSideViewOnForTest() const { return document.viewState().sideView; }
    /** Clicks it through the same path the mouse uses. */
    void toggleSideViewForTest();
    /** The little arrow next to it, which opens the track picker. */
    juce::Rectangle<int> getSideViewArrowBoundsForTest() const { return sideViewArrow.getBounds(); }
    /** Clicks that arrow. A synthetic mouse click cannot reach a real Button in the
        offscreen test (its "is the pointer over me" state stays false), so this goes
        through the same onClick the mouse would. */
    void triggerSideViewArrowForTest();
    /** How many times the picker was asked for (mouse events too, dialogs or not). */
    int getSideViewPickerRequestCountForTest() const noexcept { return sideViewPickerRequests; }
    /** Opens the track picker the way a right click does. */
    void showSideViewPickerForTest() { showSideViewTrackMenu(); }
    /** One entry of that picker, as the offscreen self test reads it. */
    struct SideViewMenuItemForTest
    {
        int          menuId  = 0;
        juce::String text;
        bool         enabled = false;
        bool         ticked  = false;
        juce::int64  trackId = 0;   // 0 for the master / 全选 / 全不选 entries
    };
    std::vector<SideViewMenuItemForTest> getSideViewMenuItemsForTest() const;
    /** Picks one of those entries by its menu id. */
    bool performSideViewMenuCommandForTest (int menuId);
    /** The other tracks the picker offers, in menu order. */
    std::vector<juce::int64> getSideViewSelectableTracksForTest() const
    {
        return sideViewSelectableTracks();
    }

    // ---- 栏位拖动 / 折叠 ------------------------------------------------
    /** The controller lane at the bottom of the editor. */
    int getControllerLaneHeightForTest() const;
    bool isControllerLaneCollapsedForTest() const;
    bool isControllerLaneVisibleForTest() const { return controllerLane.isVisible(); }
    juce::Rectangle<int> getControllerLaneToggleBoundsForTest() const
    {
        return laneToggle.getBounds();
    }
    bool isControllerLaneToggleVisibleForTest() const { return laneToggle.isVisible(); }
    juce::Rectangle<int> getControllerLaneResizerBoundsForTest() const
    {
        return laneResizer.getBounds();
    }
    /** Toggles through the same path the arrow uses. */
    void toggleControllerLaneForTest();
    /** Drags the lane's handle by this many pixels, as the mouse would. */
    void dragControllerLaneForTest (int deltaY);

    /** which: 0 = 参考组栏, 1 = 预设/历史栏, 2 = 参考线状态栏. */
    enum class SidePanelId { referenceGroup = 0, presets, referenceLines };

    /** The width the column is laid out at right now (18 when it is folded). */
    int getPanelWidthForTest (SidePanelId which) const;
    bool isPanelCollapsedForTest (SidePanelId which) const;
    /** True while the panel itself is on screen (false when it is folded away). */
    bool isPanelVisibleForTest (SidePanelId which) const;
    juce::Rectangle<int> getPanelToggleBoundsForTest (SidePanelId which) const;
    /** Whether the arrow itself is on screen (a fresh Component starts invisible). */
    bool isPanelToggleVisibleForTest (SidePanelId which) const;
    juce::Rectangle<int> getPanelResizerBoundsForTest (SidePanelId which) const;
    /** Toggles through the same path the arrow uses. */
    void togglePanelForTest (SidePanelId which);
    /** Drags the column's handle by this many pixels, as the mouse would. */
    void dragPanelForTest (SidePanelId which, int deltaX);

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** 侧视: the other tracks its picker can offer (not the open one, and not a
        drum machine, whose notes are pads rather than pitches). */
    std::vector<juce::int64> sideViewSelectableTracks() const;
    /** The picker: master switch, one tick per selectable track, 全选 / 全不选. */
    juce::PopupMenu buildSideViewMenu() const;
    void showSideViewTrackMenu();
    bool performSideViewMenuCommand (int menuId);

    ProjectDocument& document;
    AppState& appState;

    juce::TextButton noteLockButton;
    juce::TextButton locateButton;
    juce::TextButton closeButton;
    SnapControl snapControl;
    juce::Label autoFitLabel;
    juce::ComboBox autoFitBox;
    juce::Label modeLabel;
    juce::ComboBox modeBox;
    juce::TextButton pianoLinesButton;
    juce::TextButton sideViewButton;
    /** The arrow beside it: click to pick which tracks the 侧视 shows. */
    PanelToggleButton sideViewArrow { PanelToggleButton::Side::menu };

    TempoMarkerTrack markerTrack;
    TimelineRuler ruler;
    NoteEditBar noteEditBar;
    ControllerLane controllerLane;
    OptionsPanel optionsPanel;
    PianoGrid grid;
    ReferenceGroupPanel referenceGroupPanel;
    PresetPanel presetPanel;
    GroupEditorPanel groupEditorPanel;

    EditorViewState::PanelSize& panelState (SidePanelId which);
    juce::Component& panelComponent (SidePanelId which);
    int panelDefaultWidth (SidePanelId which) const;
    /** Remembers the width a drag starts from (a drag reports the total distance). */
    void beginPanelDrag (SidePanelId which);
    /** Which column one of the three handles belongs to. */
    SidePanelId sidePanelIdFor (const juce::Component* handle) const;
    /** Shows/hides the columns and their arrows to match the saved state. */
    void refreshPanelVisibility();

    PanelResizer groupResizer, sideResizer, linesResizer;
    PanelResizer laneResizer { PanelResizer::Orientation::vertical };
    PanelToggleButton groupToggle { PanelToggleButton::Side::left };
    PanelToggleButton sideToggle { PanelToggleButton::Side::left };
    PanelToggleButton linesToggle { PanelToggleButton::Side::right };
    PanelToggleButton laneToggle { PanelToggleButton::Side::bottom };
    /** Where each drag began, since a drag reports the distance from the press. */
    int groupDragStartWidth = 0, sideDragStartWidth = 0, linesDragStartWidth = 0;
    /** How tall the lane was when its handle was grabbed. */
    int laneDragStartHeight = 0;
    /** The width each column is laid out at right now, for the self test. */
    int laidOutGroupWidth = 0, laidOutSideWidth = 0, laidOutLinesWidth = 0;

    /** Where the note-edit strip sits, so paint() can put a hint in it while the
        bar itself is hidden (nothing selected). */
    juce::Rectangle<int> noteStripBounds;

    bool updating = false;
    /** How many times the 侧视 picker has been asked for (self test). */
    int sideViewPickerRequests = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorPanel)
};

} // namespace hmdaw
