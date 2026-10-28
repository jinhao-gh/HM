#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace nidaw
{

/** The editing canvas: logarithmic frequency axis (vertical) against musical
    time (horizontal), with reference lines you write notes onto.
*/
class PianoGrid : public juce::Component,
                  private juce::ChangeListener,
                  private juce::ScrollBar::Listener,
                  private juce::Timer
{
public:
    PianoGrid (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine);
    ~PianoGrid() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    /** Recomputes auto fit, scroll bars and repaints. */
    void refresh();

    /** Cheap repaint for the moving playhead (called from the UI timer). */
    void refreshPlayhead();

    /** Centres the view on the currently selected note (used by "定位音符"). */
    void centreOnSelectedNote();

    /** Diagnostic hooks for the offscreen self test: where a note and its comment
        are drawn, in grid-local coordinates. */
    juce::Rectangle<float> getNoteRectForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getNoteShapeBoundsForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getHeadHandleBoundsForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getTailHandleBoundsForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getTiltStartMarkerForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getTiltEndMarkerForTest (juce::int64 noteId) const;
    juce::Rectangle<float> getLinkBadgeBoundsForTest (juce::int64 noteId) const;
    juce::Rectangle<int> getCommentRectForTest (juce::int64 noteId) const;
    /** Drum mode diagnostics: how many pad rows are drawn, and where one of them
        is. The count is 0 when the open track is not a drum kit. */
    int getDrumRowCountForTest() const;
    juce::Rectangle<int> getDrumRowBoundsForTest (int index) const;
    /** A point inside the given pad's row, at the given tick. */
    juce::Point<float> pointForPadForTest (int pad, juce::int64 tick) const;
    /** The pad the drum grid would hit at this point (clamped to the kit). */
    int padForPointForTest (juce::Point<float> point) const;
    /** The hover ratio arrow, or an empty rectangle when nothing is drawn.
        In this component's coordinates, like pointFor(). */
    juce::Rectangle<int> getRatioArrowBoundsForTest() const;
    /** The caption plate that sits beside the arrow head (empty when there is no
        arrow). Used to check it is neither at the shaft's middle nor under the
        mouse pointer. */
    juce::Rectangle<int> getRatioArrowLabelBoundsForTest() const;
    /** The caption that goes with it: "正 1:3" / "负 1:3", empty when there is none. */
    juce::String getRatioArrowLabelForTest() const;

    /** Maps a tick / frequency onto a point in this component's coordinates.
        Exposed so the offscreen self test can drive real mouse drags. */
    juce::Point<float> pointFor (double hz, juce::int64 tick) const;

    /** Runs one auto-scroll step without waiting for the timer, so the self test
        can check the edge scrolling without a running message loop. */
    void tickAutoScrollForTest() { updateAutoScroll(); }

    /** The end tick of the live drag preview, in project ticks (self test). */
    juce::int64 getDragPreviewEndTickForTest() const;
    /** The rectangle the write preview is drawn in (self test): it has to sit
        exactly where the note will land, even when the clip does not start at 0. */
    juce::Rectangle<float> getDragPreviewNoteRectForTest() const;
    /** The raw (pre-snap) tick under the last drag position (self test). */
    juce::int64 getDragRawTickForTest() const;
    /** The pitch the note being dragged would land on right now (self test). */
    double getDragRawFrequencyForTest() const;
    /** Diagnostic hook: the 侧视 ghosts the painter would draw right now, in this
        component's coordinates (the canvas may sit inset in piano mode). */
    std::vector<juce::Rectangle<float>> getSideViewGhostRectsForTest() const;

    // ---- 钢琴窗模式 (self test) ----
    /** The keyboard strip the piano window draws inside the canvas' left edge. */
    static constexpr int kPianoKeysWidth = 44;

    /** True when the canvas is a piano roll: semitone rows, and the reference
        lines are only background. False on a drum track, on an audio clip, and
        in 参考线模式. */
    bool isPianoMode() const;
    /** How many semitone rows are on screen. */
    int getPianoRowCountForTest() const;
    /** The row a point falls in, as a MIDI note. */
    int getPianoRowForTest (juce::Point<float> point) const;
    /** A point inside a semitone row, at the given tick. */
    juce::Point<float> pointForSemitoneForTest (int midiNote, juce::int64 tick) const;
    /** The frequency a y coordinate stands for, before any quantization. */
    double frequencyForYForTest (float y) const;
    /** The height of one semitone row, in pixels. */
    float pianoRowHeightForTest() const;
    float yForPianoRowForTest (int midiNote) const
    {
        return (float) gridArea().getY() + yForPianoRow (midiNote);
    }
    /** Where the keyboard strip is drawn (empty outside 钢琴窗模式). */
    juce::Rectangle<int> getPianoKeysBoundsForTest() const;
    juce::Rectangle<float> getPianoRowRectForTest (int midiNote) const { return pianoRowRect (midiNote); }
    /** Lines the view up with the semitone rows. Called when the mode changes. */
    void alignPianoView();

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;
    void timerCallback() override;

    /** Scrolls the timeline while a drag is held against the left/right edge, so a
        note can be dragged past the end of the visible area. */
    void updateAutoScroll();
    /** Replays the current drag at a (possibly clamped) pointer position. */
    void replayDragAt (juce::Point<float> local);

    juce::Rectangle<int> gridArea() const;
    EditorViewState& view() noexcept { return document.viewState(); }
    const EditorViewState& view() const noexcept { return document.viewState(); }

    const InstrumentTrack* currentTrack() const;
    Clip* currentClip();
    const Clip* currentClip() const;
    /** Absolute tick of the open clip's start (0 when no clip is open). */
    juce::int64 clipStartTick() const;
    /** True when the note actually sounds: inside the clip window (and inside one
        period when the clip loops). Notes outside are kept but silent. */
    bool noteIsSounding (const Note& note) const;
    const Note* primarySelectedNote() const;

    music::VerticalAxis makeAxis() const;
    double xForTick (double tick) const;
    double tickForX (double x) const;

    std::vector<music::ReferenceLine> currentReferenceLines() const;
    /** The interactive reference line within the hit slop of this row. */
    std::optional<music::ReferenceLine> findReferenceLineAt (float y) const;
    /** The arrow for the line the mouse is hovering, or an invisible one.
        Recomputed from the live line list, so a line that has been hidden or
        re-rationed cannot leave a stale arrow behind. */
    music::RatioArrow currentRatioArrow() const;
    /** Where the hover arrow goes, in grid-local coordinates. */
    struct RatioArrowGeometry
    {
        bool  valid     = false;
        float shaftX    = 0.0f;
        float top       = 0.0f;
        float bottom    = 0.0f;
        float headY     = 0.0f;
        int   direction = 0;
        juce::Rectangle<float> plate;
        juce::Rectangle<int>   labelBounds;   // component coordinates
        juce::Rectangle<int>   bounds;   // component coordinates, for the self test
        juce::String label;
    };
    RatioArrowGeometry ratioArrowGeometry() const;
    const Note* findNoteAt (juce::Point<float> gridPoint) const;
    /** True when the open clip sits on a drum track: the vertical axis is then
        the kit's twelve pad rows instead of the frequency axis. */
    bool isDrumTrack() const;
    /** The semitone row a y coordinate falls in, as a MIDI note. */
    int pianoRowForY (float y) const;
    /** The centre of a semitone row, in canvas coordinates. */
    float yForPianoRow (int midiNote) const;
    juce::Rectangle<float> pianoRowRect (int midiNote) const;
    float drumRowHeight() const;
    /** The centre of a pad's row (pad 0 is the bottom row). */
    float yForPad (int pad) const;
    /** Which pad row the y coordinate falls in, clamped to the kit. */
    int padForY (float y) const;
    juce::Rectangle<float> noteRect (const Note& note, const music::VerticalAxis& axis) const;
    juce::Path noteShapePath (const Note& note, const music::VerticalAxis& axis) const;
    juce::Point<float> pitchPoint (const Note& note, double t,
                                   const music::VerticalAxis& axis) const;
    juce::Rectangle<float> endpointHandle (const Note& note, bool tail,
                                           const music::VerticalAxis& axis) const;
    juce::Rectangle<float> tiltMarker (const Note& note, bool tail,
                                       const music::VerticalAxis& axis) const;
    juce::Rectangle<float> linkBadge (const Note& note,
                                      const music::VerticalAxis& axis) const;
    /** The same rectangle for a note placed at an absolute tick. */
    juce::Rectangle<float> noteRectAt (const Note& note, juce::int64 startTickAbs,
                                       const music::VerticalAxis& axis) const;
    /** The comment sits just above the note rather than inside it. */
    juce::Rectangle<int> commentRectFor (const juce::Rectangle<float>& noteRect, int gridWidth) const;

    void paintGridLines (juce::Graphics& g, const music::TempoMap& tempoMap, int width, int height);
    /** 侧视: the other tracks' notes as a dim background board (paint only). */
    void paintOtherTrackNotes (juce::Graphics& g, const music::VerticalAxis& axis,
                               int width, int height) const;
    /** One 侧视 ghost: where it goes, whose colour it wears, and whether the note
        behind it is muted (those read as an outline rather than a block). */
    struct GhostNote
    {
        juce::Rectangle<float> rect;
        juce::Colour           colour;
        bool                   muted = false;
    };
    /** Every other-track note that belongs on this canvas, in paint order. The
        painter and the diagnostic hook below both walk this list. */
    std::vector<GhostNote> otherTrackNotes (const music::VerticalAxis& axis,
                                            int width, int height) const;
    void paintReferenceLines (juce::Graphics& g, int width, int height);
    void paintAudioWaveform (juce::Graphics& g, const Clip& clip, int width, int height);
    void paintNotes (juce::Graphics& g, int width, int height);
    /** The twelve pad rows, their names and the alternating backgrounds. */
    void paintDrumRows (juce::Graphics& g, int width, int height);
    /** The keyboard strip of the piano window (one key per semitone row). */
    void paintPianoKeys (juce::Graphics& g, int height);
    /** The alternating white / black semitone rows behind the notes. */
    void paintPianoRows (juce::Graphics& g, int width, int height);
    void paintDragPreview (juce::Graphics& g, const music::VerticalAxis& axis, int width);
    void paintRatioArrow (juce::Graphics& g);
    void paintPlayhead (juce::Graphics& g, int width, int height);

    void commitNoteCreation (juce::int64 startTick, juce::int64 endTick);
    void commitNoteMove();
    void commitNoteResize (bool fromStartEdge);
    /** Alt drag on a drum note: the one note under the pointer changes velocity. */
    void commitNoteVelocity();
    bool handleReferencePick (double noteHz);
    void deleteNote (const Note& note);
    /** Removes every selected note of the open clip, as one edit. */
    void deleteSelectedNotes();
    /** Plays a note the user has just written, so writing has a sound. It is one
        short live note on the open track (through its own instrument, chain and
        fader) and it never touches the document. */
    void auditionWrittenNote (double frequencyHz, int pad);
    /** True when a drag held against the top / bottom edge slides the pitch axis. */
    bool verticalScrollFollowsDrag() const;
    /** The rubber band as it stands right now.

        Its anchor corner is stored on the CONTENT (a tick and a frequency) rather
        than on the screen: the view scrolls under the band while it is held against
        an edge, and a corner pinned to the screen would keep sweeping the notes it
        had already covered back out again.
    */
    juce::Rectangle<float> marqueeBand() const;
    void applyAutoFit();
    void updateScrollBars();
    double snapGridTicksAt (juce::int64 tick) const;
    juce::int64 snappedTick (double tick) const;

    enum class DragMode { none,
                          /** Pressed on a write target (a drum row, or a reference
                              line); the first movement decides whether it becomes a
                              note or a rubber band. */
                          pendingWrite,
                          creatingNote, movingNotes, resizingStart, resizingEnd, marquee,
                          /** Alt drag on a drum note: change its velocity. */
                          velocity,
                          pitchHandle, tiltHandle, connecting };
    enum class NoteHit { none, body, leftEdge, rightEdge,
                         headHandle, tailHandle, tiltStartHandle, tiltEndHandle,
                         linkBadge };

    struct NoteHitResult
    {
        const Note* note = nullptr;
        NoteHit hit = NoteHit::none;
    };

    struct SeamFollower
    {
        Note note;
        juce::int64 anchorNoteId = 0;
        /** true follows the anchor's tail with this note's head; false follows
            the anchor's head with this note's tail. */
        bool followsStart = false;
    };

    NoteHitResult hitTestNote (juce::Point<float> gridPoint) const;
    void beginNoteDrag (const NoteHitResult& hit);
    void beginPitchHandleDrag (const Note& note, bool tail);
    void beginTiltHandleDrag (const Note& note, bool tail);
    void updatePitchHandlePreview (juce::Point<float> local);
    void updateTiltHandlePreview (juce::Point<float> local);
    void commitPitchHandleDrag();
    void commitTiltHandleDrag();
    void commitLink (const Note& target, bool targetTail);
    const Note* findLinkTargetAt (juce::Point<float> local, const Note* ignored,
                                  bool desiredTail, bool& targetTail) const;
    juce::int64 linkPredecessorFor (const Note& note) const;
    void updateMovePreview (juce::Point<float> local, bool lockToDominantAxis);
    void updateResizePreview (juce::Point<float> local);
    void updateMarqueeSelection();
    const Note* findOriginalNote (juce::int64 noteId) const;
    std::vector<Note> connectedChainFor (const Clip& clip, juce::int64 noteId) const;
    std::vector<Note> successorsFor (const Clip& clip, juce::int64 noteId) const;
    const Note* predecessorFor (const Clip& clip, const Note& note) const;
    const Note* successorFor (const Clip& clip, const Note& note) const;
    /** Tracks which reference line the mouse is on and repaints when it changes. */
    void updateRatioHover (juce::Point<float> local, bool inside);

    ProjectDocument& document;
    AppState& appState;
    AudioEngine& audio;

    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };

    DragMode dragMode = DragMode::none;
    bool dragActive = false;
    juce::Point<float> dragStartLocal;
    /** Last pointer position during a drag, in grid-local coordinates. */
    juce::Point<float> dragLastLocal;
    /** Ctrl was held on the press: a pending drag that turns into a rubber band
        then unions with the existing selection instead of replacing it. */
    bool dragPendingCtrl = false;
    /** Shift was held at the last drag event: it locks the dominant axis. */
    bool dragShiftDown = false;

    // note creation
    juce::int64 dragAnchorTick = 0;
    juce::int64 dragCurrentTick = 0;
    double dragFrequency = 440.0;
    /** Pad being written while creating a note on a drum track. */
    int    dragPad = 0;

    // note editing
    std::vector<Note> dragOriginals;
    std::vector<Note> resizeShiftOriginals;
    std::vector<SeamFollower> bodySeamFollowers;
    Note resizePredecessorOriginal;
    bool hasResizePredecessor = false;
    std::vector<std::pair<juce::int64, bool>> pitchSeamPartners;
    std::vector<music::ReferenceLine> frozenLines;
    juce::int64 resizeOriginalStart = 0;
    juce::int64 resizeOriginalEnd = 0;
    /** How far the dragged edge moves, in absolute ticks: the same delta is applied
        to every selected note, so a multi-note selection changes length together. */
    juce::int64 previewResizeDelta = 0;
    juce::int64 resizeNoteId = 0;
    juce::int64 pitchHandleNoteId = 0;
    bool pitchHandleIsTail = false;
    double pitchHandleOriginalHz = 0.0;
    double pitchHandlePreviewFactor = 1.0;
    juce::int64 pitchHandlePointerStartTick = 0;
    juce::int64 pitchHandlePreviewTickDelta = 0;
    juce::int64 tiltHandleNoteId = 0;
    bool tiltHandleIsTail = false;
    float tiltPreview = 0.0f;
    float tiltOriginal = 0.0f;
    juce::int64 linkTargetNoteId = 0;
    bool linkTargetIsTail = false;
    bool linkTargetValid = false;
    double linkTargetHz = 0.0;
    juce::int64 hoverHandleNoteId = 0;
    bool hoverHandleIsTail = false;
    juce::int64 hoverTiltNoteId = 0;
    bool hoverTiltIsTail = false;
    juce::int64 earliestOriginalStart = 0;
    double primaryOriginalHz = 0.0;
    double primaryOriginalTailHz = 0.0;
    /** How far the drag has moved the notes in pad rows (drum tracks only). */
    int    previewPadDelta = 0;
    /** Velocity drag (Alt) on a drum note. */
    juce::int64 velocityNoteId = 0;
    int    velocityOriginal = 100;
    int    previewVelocity = 100;

    // live preview while dragging
    juce::int64 previewTickDelta = 0;
    double previewFrequencyFactor = 1.0;
    music::NoteTiming previewTiming;
    bool hasPreviewTiming = false;

    // marquee (rubber band) selection
    juce::Point<float> marqueeStart;
    juce::Point<float> marqueeCurrent;
    /** The anchor corner as content, so it rides the view instead of the screen
        (see marqueeBand()). marqueeStart keeps the raw press point for the drum
        grid, whose vertical axis is the pads and never scrolls. */
    double marqueeAnchorTick = 0.0;
    double marqueeAnchorHz = 0.0;
    std::set<juce::int64> marqueeBaseline;

    juce::int64 lastFittedNoteId = -1;
    double lastPlayheadX = -1.0;
    /** Which mode the canvas was drawn in last, so entering the piano window can
        line the rows up exactly once (after that the view is the user's). */
    EditorMode lastEditorMode = EditorMode::reference;

    // hover ratio arrow
    double hoveredLineHz = 0.0;    // 0 == nothing hovered
    int    hoverMouseX   = 0;      // component coordinates
    bool updatingScrollBars = false;
    /** Hands every "just written" audition its own voice id (see auditions). */
    int auditionSerial = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoGrid)
};

} // namespace nidaw
