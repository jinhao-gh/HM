#pragma once

#include "../Core/Types.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <set>
#include <vector>

namespace hmdaw
{

/** UI-only state: what is selected, and whether the editor is open.
    Not part of the project file and deliberately not undoable.
*/
class AppState : public juce::ChangeBroadcaster
{
public:
    /** Which "pick a dot, then pick a note" flow is armed. */
    enum class ReferencePick { none, reference, apply };

    /** Which of the two beside panels is showing, if any. */
    enum class SidePanel { none, history, presets };

    /** The group editor's session: what is being edited and where it came from.

        Session state, like the rest of AppState. It is not undoable and not saved:
        while the editor is open the *project's* reference group holds the working
        copy (that is what gives the live preview on the canvas), and this struct is
        what remembers how to put the previous group back on 返回.
    */
    struct GroupEditor
    {
        enum class Kind { none, referenceGroup, referenceLineGroup };

        Kind  kind = Kind::none;
        /** Where the group came from: a preset index, or a project entry's id. */
        int          presetIndex = -1;
        juce::int64  lineId = 0;
        /** The group as it was before the editor opened, put back on 返回. */
        ReferenceGroup previousGroup;
        /** A reference line group's anchor: the editor shows it, and uses it for the
            frequency labels beside the dots. */
        double anchorHz = 440.0;
        double anchorPosition = 0.0;

        bool isOpen() const noexcept { return kind != Kind::none; }
        bool isLineGroup() const noexcept { return kind == Kind::referenceLineGroup; }
    };

    GroupEditor groupEditor;

    void openGroupEditor (GroupEditor editor)
    {
        groupEditor = std::move (editor);
        sidePanel = SidePanel::none;
        sendChangeMessage();
    }

    void closeGroupEditor()
    {
        groupEditor = {};
        sendChangeMessage();
    }

    juce::int64 selectedTrackId = 0;
    juce::int64 primaryNoteId   = 0;
    juce::int64 selectedClipId  = 0;   // the clip currently open in the editor
    bool editorOpen = false;

    /** Which row of the reference control panel is highlighted, and the transient
        "solo" of the panel. Soloing is mutually exclusive and is not saved. */
    juce::int64 selectedReferenceLineId = 0;
    juce::int64 soloReferenceLineId     = 0;

    void selectReferenceLine (juce::int64 lineId)
    {
        if (selectedReferenceLineId == lineId)
            return;

        selectedReferenceLineId = lineId;
        sendChangeMessage();
    }

    void setSoloReferenceLine (juce::int64 lineId)
    {
        if (soloReferenceLineId == lineId)
            return;

        soloReferenceLineId = lineId;
        sendChangeMessage();
    }

    /** When set, writing a new note leaves the current selection alone. */
    bool noteLock = false;

    /** Which signal the editor's controller lane is showing: the velocity of the
        notes, or the drawn curve of one MIDI controller. Session state, like the
        rest of AppState - the lane's *height* and folded state travel with the
        project (EditorViewState::controllerLane).
    */
    bool laneShowsVelocity = true;
    /** The controller shown while `laneShowsVelocity` is false. Never one of the
        numbers the host uses for itself (see controllers::isReservedController). */
    int laneController = 11;

    void setLaneSignal (bool showsVelocity, int controller)
    {
        if (laneShowsVelocity == showsVelocity && laneController == controller)
            return;

        laneShowsVelocity = showsVelocity;
        laneController = controller;
        sendChangeMessage();
    }

    /** The frequency ratio the editor is showing right now.

        This is a lens on the current selection, not note data: every change of the
        note selection puts it back to the octave (1:2), and it is never saved.
    */
    double currentRatio = 2.0;

    static constexpr double defaultRatio = 2.0;

    enum class PitchEndpoint { head, tail };
    PitchEndpoint currentPitchEndpoint = PitchEndpoint::head;

    void setCurrentPitchEndpoint (PitchEndpoint endpoint)
    {
        if (currentPitchEndpoint == endpoint)
            return;

        currentPitchEndpoint = endpoint;
        sendChangeMessage();
    }

    std::set<juce::int64> selectedNoteIds;

    /** 参考线控制栏里「跟着选中音符走」的那一行（第二类 / 第四类）是派生出来的：
        每次选中变化都会重新生成一条。显示 / 隐藏与独显说的是「这一行」而不是
        「当时选中的那个音符」，所以在重新生成时（甚至选中被清空、这一行消失之后）
        要把它继承回来，就记在这里。和别的视图状态一样，只活在本次会话里。 */
    bool derivedEntryVisible = true;
    bool derivedEntrySoloed  = false;

    // ---- clipboards ----
    std::vector<Note> noteClipboard;
    bool hasNoteClipboard = false;

    Clip clipClipboard;                // notes stay clip-relative
    bool hasClipClipboard = false;

    /** Which clipboard Ctrl+V should paste from. Copying always makes its own
        kind current, so "paste" means "paste what I copied last". */
    enum class ClipboardKind { none, notes, clips };
    ClipboardKind lastClipboard = ClipboardKind::none;

    void selectClip (juce::int64 clipId)
    {
        if (selectedClipId == clipId)
            return;

        selectedClipId = clipId;
        selectedNoteIds.clear();
        primaryNoteId = 0;
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    /** Changing the ratio is a view change, not an edit: it never touches the document. */
    void setCurrentRatio (double ratio)
    {
        if (ratio <= 0.0 || std::abs (ratio - currentRatio) < 1.0e-9)
            return;

        currentRatio = ratio;
        sendChangeMessage();
    }

    // ---- reference group ----
    ReferencePick referencePick = ReferencePick::none;

    bool   armedDotValid    = false;   // a dot has been chosen for the armed flow
    double armedDotPosition = 0.0;

    bool   referenceAnchorValid    = false;   // basline association from 选择基准音
    double referenceAnchorHz       = 0.0;
    double referenceAnchorPosition = 0.0;

    /** Monotonic counter used to name newly created reference line groups. */
    int referenceGroupSerial = 0;


    SidePanel sidePanel = SidePanel::none;
    bool sidePanelShowsLineGroups = false;   // which tab the preset panel shows

    /** Reference line groups built this session, newest first. */
    std::vector<ReferenceLineGroup> referenceLineHistory;

    static constexpr int maxHistoryEntries = 50;

    void setReferencePick (ReferencePick mode)
    {
        referencePick = mode;
        armedDotValid = false;

        if (mode != ReferencePick::reference)
        {
            referenceAnchorValid = false;
            referenceAnchorHz = 0.0;
            referenceAnchorPosition = 0.0;
        }

        sendChangeMessage();
    }

    void armDot (double position)
    {
        armedDotValid = true;
        armedDotPosition = position;
        // Arming a new dot starts a fresh association.
        referenceAnchorValid = false;
        sendChangeMessage();
    }

    void setReferenceAnchor (double frequencyHz, double dotPosition)
    {
        referenceAnchorValid = true;
        referenceAnchorHz = frequencyHz;
        referenceAnchorPosition = dotPosition;
        sendChangeMessage();
    }

    void pushReferenceLineHistory (const ReferenceLineGroup& group)
    {
        referenceLineHistory.insert (referenceLineHistory.begin(), group);

        if ((int) referenceLineHistory.size() > maxHistoryEntries)
            referenceLineHistory.resize ((size_t) maxHistoryEntries);

        sendChangeMessage();
    }

    void toggleSidePanel (SidePanel which)
    {
        sidePanel = (sidePanel == which) ? SidePanel::none : which;
        sendChangeMessage();
    }

    void setPresetTab (bool showsLineGroups)
    {
        sidePanelShowsLineGroups = showsLineGroups;
        sendChangeMessage();
    }

    bool isNoteSelected (juce::int64 noteId) const
    {
        return selectedNoteIds.find (noteId) != selectedNoteIds.end();
    }

    bool hasNoteSelection() const { return ! selectedNoteIds.empty(); }

    void selectTrack (juce::int64 trackId)
    {
        if (selectedTrackId == trackId)
            return;

        selectedTrackId = trackId;
        selectedNoteIds.clear();
        primaryNoteId = 0;
        selectedClipId = 0;
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    /** addToSelection is the ctrl-click case. */
    void selectNote (juce::int64 noteId, bool addToSelection)
    {
        if (addToSelection && noteId != 0)
        {
            if (auto it = selectedNoteIds.find (noteId); it != selectedNoteIds.end())
            {
                selectedNoteIds.erase (it);

                if (primaryNoteId == noteId)
                    primaryNoteId = selectedNoteIds.empty() ? 0 : *selectedNoteIds.rbegin();
            }
            else
            {
                selectedNoteIds.insert (noteId);
                primaryNoteId = noteId;
            }
        }
        else
        {
            selectedNoteIds.clear();
            primaryNoteId = noteId;

            if (noteId != 0)
                selectedNoteIds.insert (noteId);
        }

        // Selecting a note resets the lens back to the 1:2 octave.
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    void setPrimaryNote (juce::int64 noteId)
    {
        selectedNoteIds.clear();

        if (noteId != 0)
            selectedNoteIds.insert (noteId);

        primaryNoteId = noteId;
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    void clearNotes()
    {
        if (selectedNoteIds.empty() && primaryNoteId == 0)
            return;

        selectedNoteIds.clear();
        primaryNoteId = 0;
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    /** Replaces the whole note selection in one go (used by marquee selection). */
    void setSelectedNotes (std::set<juce::int64> noteIds)
    {
        selectedNoteIds = std::move (noteIds);
        primaryNoteId = selectedNoteIds.empty() ? 0 : *selectedNoteIds.rbegin();
        currentRatio = defaultRatio;
        sendChangeMessage();
    }

    void setEditorOpen (bool shouldBeOpen)
    {
        if (editorOpen == shouldBeOpen)
            return;

        editorOpen = shouldBeOpen;
        sendChangeMessage();
    }

    void setNoteLock (bool shouldLock)
    {
        if (noteLock == shouldLock)
            return;

        noteLock = shouldLock;
        sendChangeMessage();
    }

    // ---- MIDI 录制 ----------------------------------------------------------
    /** 录制 armed? Session state: it is a mode the user is in, not project data. */
    bool midiRecordArmed = false;
    /** True: notes go into the clip that is open. False: they build a new clip. */
    bool midiRecordIntoClip = true;

    void setMidiRecordArmed (bool shouldArm)
    {
        if (midiRecordArmed == shouldArm)
            return;

        midiRecordArmed = shouldArm;
        sendChangeMessage();
    }

    void setMidiRecordIntoClip (bool shouldRecordIntoClip)
    {
        if (midiRecordIntoClip == shouldRecordIntoClip)
            return;

        midiRecordIntoClip = shouldRecordIntoClip;
        sendChangeMessage();
    }
};

} // namespace hmdaw
