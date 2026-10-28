#include "PianoGrid.h"
#include "Theme.h"
#include "AutoScroll.h"
#include "../Audio/SamplePlayer.h"

#include <limits>

namespace nidaw
{

namespace
{
    constexpr int   kScrollBarThickness = 12;
    constexpr float kNoteHeight         = 10.0f;
    constexpr int   kCommentHeight      = 12;
    constexpr float kCommentGap         = 2.0f;
    constexpr float kReferenceHitSlop   = 6.0f;
    constexpr int   kMaxGridLines       = 4000;
    /** The "you just wrote a note" audition: one short live note on this track. */
    constexpr int   kAuditionMilliseconds = 260;
    constexpr int   kAuditionIdBase       = -100000;

    // The hover ratio arrow.
    constexpr float kRatioArrowHeadWidth  = 9.0f;
    constexpr float kRatioArrowHeadLength = 11.0f;
    constexpr float kRatioArrowMinLength  = 16.0f;
    // The caption sits to the left of the shaft by default. The mouse is on the
    // shaft, and an arrow cursor's sprite reaches about 11 px to the right of its
    // tip, so the left side is the one that cannot be covered by the pointer.
    constexpr float kRatioArrowLabelGap      = 6.0f;
    constexpr float kRatioArrowLabelGapRight = 16.0f;   // clears the cursor sprite
    constexpr float kRatioArrowLabelHeight = 17.0f;

    Note makeSeamFollowerForBodyDrag (const Note& follower, const Note& anchor,
                                      juce::int64 tickDelta, double pitchFactor,
                                      bool followsStart)
    {
        auto movedAnchor = anchor;
        movedAnchor.startTick = juce::jmax ((juce::int64) 0,
                                            anchor.startTick + tickDelta);
        music::transposeNotePitch (movedAnchor, pitchFactor);

        auto result = follower;

        if (followsStart)
        {
            result.startTick = juce::jmax ((juce::int64) 0, movedAnchor.endTick());
            result.durationTicks = juce::jmax ((juce::int64) 1,
                                               follower.endTick() - result.startTick);
            result.frequencyHz = movedAnchor.frequencyHz;
        }
        else
        {
            result.durationTicks = juce::jmax ((juce::int64) 1,
                                               movedAnchor.startTick - result.startTick);
            result.tailFrequencyHz = movedAnchor.effectiveTailHz();
        }

        return result;
    }

}

PianoGrid::PianoGrid (ProjectDocument& documentToUse, AppState& appStateToUse, AudioEngine& audioToUse)
    : document (documentToUse), appState (appStateToUse), audio (audioToUse)
{
    addAndMakeVisible (horizontalScrollBar);
    addAndMakeVisible (verticalScrollBar);

    horizontalScrollBar.addListener (this);
    verticalScrollBar.addListener (this);
    horizontalScrollBar.setAutoHide (false);
    verticalScrollBar.setAutoHide (false);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    setWantsKeyboardFocus (true);
}

PianoGrid::~PianoGrid()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
    horizontalScrollBar.removeListener (this);
    verticalScrollBar.removeListener (this);
}

juce::Rectangle<int> PianoGrid::gridArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom (kScrollBarThickness);
    area.removeFromRight (kScrollBarThickness);

    // 钢琴窗模式 reserves the left edge for the keyboard, so the rows and the
    // notes start after it. The editor's ruler is inset by the same amount, which
    // keeps the two lining up.
    if (isPianoMode())
        area.removeFromLeft (juce::jmin (kPianoKeysWidth, juce::jmax (0, area.getWidth() - 40)));

    return area;
}

const InstrumentTrack* PianoGrid::currentTrack() const
{
    return document.getProject().findTrack (appState.selectedTrackId);
}

Clip* PianoGrid::currentClip()
{
    return const_cast<Project&> (document.getProject()).findClip (appState.selectedClipId);
}

const Clip* PianoGrid::currentClip() const
{
    return const_cast<PianoGrid*> (this)->currentClip();
}

juce::int64 PianoGrid::clipStartTick() const
{
    const auto* clip = currentClip();
    return clip != nullptr ? clip->startTick : 0;
}

bool PianoGrid::noteIsSounding (const Note& note) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return false;

    if (note.startTick < 0 || note.startTick >= clip->lengthTicks)
        return false;

    // While looping, only the content of one period takes part.
    if (clip->looped && note.startTick >= juce::jmax ((juce::int64) 1, clip->loopLengthTicks))
        return false;

    return true;
}

const Note* PianoGrid::primarySelectedNote() const
{
    const auto* clip = currentClip();

    if (clip == nullptr || appState.primaryNoteId == 0)
        return nullptr;

    for (const auto& note : clip->notes)
        if (note.id == appState.primaryNoteId)
            return &note;

    return nullptr;
}

music::VerticalAxis PianoGrid::makeAxis() const
{
    music::VerticalAxis axis;
    axis.height         = (double) juce::jmax (1, gridArea().getHeight());
    axis.centreHz       = view().centreHz;
    axis.octavesVisible = view().octavesVisible;
    return axis;
}

bool PianoGrid::isDrumTrack() const
{
    const auto* track = currentTrack();
    return track != nullptr && track->kind == TrackKind::drums;
}

float PianoGrid::drumRowHeight() const
{
    const auto area = gridArea();
    return (float) area.getHeight() / (float) kDrumPads;
}

float PianoGrid::yForPad (int pad) const
{
    const auto area = gridArea();
    const auto row = drumRowHeight();

    // Pad 0 (the kick) is the bottom row, pad 11 (the ride) the top one.
    return (float) area.getY() + ((float) (kDrumPads - 1 - juce::jlimit (0, kDrumPads - 1, pad)) + 0.5f) * row;
}

int PianoGrid::padForY (float y) const
{
    const auto area = gridArea();
    const auto row = juce::jmax (1.0f, drumRowHeight());
    const auto rowFromTop = (int) std::floor ((y - (float) area.getY()) / row);

    return juce::jlimit (0, kDrumPads - 1, kDrumPads - 1 - rowFromTop);
}

bool PianoGrid::isPianoMode() const
{
    if (view().editorMode != EditorMode::piano || isDrumTrack())
        return false;

    // An audio clip has no notes: the canvas stays the read-only waveform.
    const auto* clip = currentClip();
    return clip == nullptr || ! clip->isAudio();
}

int PianoGrid::pianoRowForY (float y) const
{
    const auto axis = makeAxis();
    return music::piano::noteForFrequency (axis.yToFrequency ((double) y));
}

float PianoGrid::yForPianoRow (int midiNote) const
{
    const auto axis = makeAxis();
    return (float) axis.frequencyToY (music::piano::frequencyOf (midiNote));
}

juce::Rectangle<float> PianoGrid::pianoRowRect (int midiNote) const
{
    const auto area = gridArea();
    const auto rows = juce::jmax (1, (int) std::lround (
        view().octavesVisible * music::piano::semitonesPerOctave));
    const auto rowHeight = (float) area.getHeight() / (float) rows;
    const auto centre = yForPianoRow (midiNote);

    return { (float) area.getX(), centre - rowHeight * 0.5f,
             (float) area.getWidth(), rowHeight };
}

void PianoGrid::alignPianoView()
{
    auto& state = view();

    // Whole semitones only: the top and bottom edges of the canvas then land on
    // semitone boundaries and every row comes out exactly the same height.
    const auto semitones = juce::jmax ((double) music::piano::semitonesPerOctave,
                                       std::round (state.octavesVisible
                                                   * music::piano::semitonesPerOctave));
    state.octavesVisible = music::VerticalAxis::clampOctaves (semitones
                                                              / music::piano::semitonesPerOctave);

    const auto steps = std::round (music::piano::semitonesPerOctave
                                   * std::log2 (juce::jmax (1.0, state.centreHz) / 440.0));
    // Half a semitone further: then the top edge of the canvas is the boundary
    // between two rows rather than a note's own line, so no row is cut in half.
    const auto aligned = steps + 0.5;
    state.centreHz = music::VerticalAxis::clampHz (
        440.0 * std::pow (2.0, aligned / music::piano::semitonesPerOctave));
}

int PianoGrid::getPianoRowCountForTest() const
{
    return isPianoMode()
               ? (int) std::lround (view().octavesVisible * music::piano::semitonesPerOctave)
               : 0;
}

int PianoGrid::getPianoRowForTest (juce::Point<float> point) const
{
    return pianoRowForY (point.y);
}

juce::Point<float> PianoGrid::pointForSemitoneForTest (int midiNote, juce::int64 tick) const
{
    const auto area = gridArea();

    return { (float) area.getX() + (float) xForTick ((double) tick),
             (float) area.getY() + yForPianoRow (midiNote) };
}

juce::Rectangle<int> PianoGrid::getPianoKeysBoundsForTest() const
{
    if (! isPianoMode())
        return {};

    auto area = getLocalBounds();
    area.removeFromBottom (kScrollBarThickness);
    area.removeFromRight (kScrollBarThickness);
    return area.removeFromLeft (juce::jmin (kPianoKeysWidth, juce::jmax (0, area.getWidth() - 40)));
}

double PianoGrid::frequencyForYForTest (float y) const
{
    return makeAxis().yToFrequency ((double) y);
}

float PianoGrid::pianoRowHeightForTest() const
{
    const auto rows = juce::jmax (1, (int) std::lround (view().octavesVisible
                                                        * music::piano::semitonesPerOctave));
    return (float) gridArea().getHeight() / (float) rows;
}

double PianoGrid::xForTick (double tick) const
{
    return (tick - view().horizontalOffsetTicks) * view().pixelsPerQuarter
           / (double) kTicksPerQuarter;
}

double PianoGrid::tickForX (double x) const
{
    return view().horizontalOffsetTicks
           + x * (double) kTicksPerQuarter / juce::jmax (1.0, view().pixelsPerQuarter);
}

double PianoGrid::snapGridTicksAt (juce::int64 tick) const
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto divisor = view().editorSnap.clampedDenominator();
    return juce::jmax (1.0, tempoMap.ticksPerBeatAt (tick) / (double) divisor);
}

juce::int64 PianoGrid::snappedTick (double tick) const
{
    const auto rounded = (juce::int64) std::llround (tick);

    // 不吸附: whatever tick the pointer is over, with no grid quantization. The
    // subdivision still sizes the default / minimum note lengths (snapGridTicksAt).
    if (! view().editorSnap.enabled)
        return rounded;

    return music::TempoMap::snapToGrid (rounded, snapGridTicksAt (rounded));
}

std::vector<music::ReferenceLine> PianoGrid::currentReferenceLines() const
{
    const auto axis = makeAxis();
    const auto& project = document.getProject();

    // The panel is the single source of truth: hidden and non-soloed entries drop
    // out here, so they neither draw nor take part in writing or snapping.
    const auto selected = selectedNotesOf (project, appState.selectedClipId, appState.selectedNoteIds);
    const auto resolved = music::resolveReferenceEntries (project.referenceLines, selected,
                                                          appState.currentRatio,
                                                          appState.soloReferenceLineId);

    return music::buildReferenceLinesForEntries (resolved, axis.topHz(), axis.bottomHz());
}

std::optional<music::ReferenceLine> PianoGrid::findReferenceLineAt (float y) const
{
    const auto axis = makeAxis();
    const auto lines = currentReferenceLines();

    const music::ReferenceLine* best = nullptr;
    auto bestDistance = kReferenceHitSlop + 1.0f;

    for (const auto& line : lines)
    {
        if (! line.interactive)
            continue;

        const auto distance = std::abs ((float) axis.frequencyToY (line.hz) - y);

        if (distance <= kReferenceHitSlop && distance < bestDistance)
        {
            bestDistance = distance;
            best = &line;
        }
    }

    // Returned by value: the list above is a local, so handing out a pointer into
    // it would leave the caller reading freed memory.
    return best != nullptr ? std::optional<music::ReferenceLine> (*best) : std::nullopt;
}

//==============================================================================






juce::Rectangle<float> PianoGrid::noteRect (const Note& note, const music::VerticalAxis& axis) const
{
    // Note ticks are relative to the clip, the axis is absolute.
    const auto start = clipStartTick() + note.startTick;
    const auto x1 = xForTick ((double) start);
    const auto x2 = xForTick ((double) (start + note.durationTicks));

    // A drum track has no frequency axis: the note sits in its pad's row.
    if (isDrumTrack())
    {
        const auto height = juce::jmin ((float) kNoteHeight, drumRowHeight() * 0.55f);
        const auto y = yForPad (note.pad < 0 ? 0 : note.pad);

        return { (float) x1, y - height * 0.5f, (float) juce::jmax (4.0, x2 - x1), height };
    }

    const auto headY = axis.frequencyToY (note.frequencyHz);
    const auto tailY = axis.frequencyToY (note.effectiveTailHz());
    const auto top = (float) juce::jmin (headY, tailY) - kNoteHeight * 0.5f;
    const auto bottom = (float) juce::jmax (headY, tailY) + kNoteHeight * 0.5f;

    return { (float) x1, top, (float) juce::jmax (4.0, x2 - x1), bottom - top };
}

juce::Rectangle<float> PianoGrid::noteRectAt (const Note& note, juce::int64 startTickAbs,
                                              const music::VerticalAxis& axis) const
{
    const auto x1 = xForTick ((double) startTickAbs);
    const auto x2 = xForTick ((double) (startTickAbs + note.durationTicks));

    if (isDrumTrack())
    {
        const auto height = juce::jmin ((float) kNoteHeight, drumRowHeight() * 0.55f);
        const auto y = yForPad (note.pad < 0 ? 0 : note.pad);

        return { (float) x1, y - height * 0.5f, (float) juce::jmax (4.0, x2 - x1), height };
    }

    const auto headY = axis.frequencyToY (note.frequencyHz);
    const auto tailY = axis.frequencyToY (note.effectiveTailHz());
    const auto top = (float) juce::jmin (headY, tailY) - kNoteHeight * 0.5f;
    const auto bottom = (float) juce::jmax (headY, tailY) + kNoteHeight * 0.5f;

    return { (float) x1, top, (float) juce::jmax (4.0, x2 - x1), bottom - top };
}

juce::Point<float> PianoGrid::pitchPoint (const Note& note, double t,
                                          const music::VerticalAxis& axis) const
{
    const auto start = clipStartTick() + note.startTick;
    const auto x1 = xForTick ((double) start);
    const auto x2 = xForTick ((double) (start + note.durationTicks));
    const auto x = x1 + (x2 - x1) * juce::jlimit (0.0, 1.0, t);
    return { (float) x, (float) axis.frequencyToY (music::pitchAt (note, t)) };
}

juce::Path PianoGrid::noteShapePath (const Note& note,
                                     const music::VerticalAxis& axis) const
{
    juce::Path path;

    if (isDrumTrack())
    {
        path.addRectangle (noteRect (note, axis));
        return path;
    }

    const auto bounds = noteRect (note, axis);
    const auto steps = juce::jmax (2, (int) std::ceil (bounds.getWidth() / 2.0f));
    const auto halfHeight = juce::jmin (kNoteHeight * 0.5f, bounds.getHeight() * 0.5f);
    const auto head = pitchPoint (note, 0.0, axis);

    path.startNewSubPath (head.x, head.y - halfHeight);

    for (int i = 1; i <= steps; ++i)
    {
        const auto point = pitchPoint (note, (double) i / (double) steps, axis);
        path.lineTo (point.x, point.y - halfHeight);
    }

    const auto tail = pitchPoint (note, 1.0, axis);
    path.lineTo (tail.x, tail.y + halfHeight);

    for (int i = steps - 1; i >= 0; --i)
    {
        const auto point = pitchPoint (note, (double) i / (double) steps, axis);
        path.lineTo (point.x, point.y + halfHeight);
    }

    path.closeSubPath();
    return path;
}

juce::Rectangle<float> PianoGrid::endpointHandle (const Note& note, bool tail,
                                                  const music::VerticalAxis& axis) const
{
    const auto bounds = noteRect (note, axis);
    const auto radius = juce::jlimit (2.5f, 4.0f, bounds.getWidth() * 0.16f);
    const auto inset = juce::jmin (radius + 1.5f, bounds.getWidth() * 0.28f);
    const auto point = pitchPoint (note, tail ? 1.0 : 0.0, axis);
    const auto x = tail ? bounds.getRight() - inset : bounds.getX() + inset;

    return { x - radius, point.y - radius, radius * 2.0f, radius * 2.0f };
}

juce::Rectangle<float> PianoGrid::tiltMarker (const Note& note, bool tail,
                                              const music::VerticalAxis& axis) const
{
    if (isDrumTrack() || note.durationTicks <= 0)
        return {};

    const auto bounds = noteRect (note, axis);
    const auto t = juce::jlimit (0.0f, 1.0f, tail ? note.tiltEnd : note.tiltStart);
    const auto x = juce::jlimit (bounds.getX() + 2.0f,
                                 bounds.getRight() - 2.0f,
                                 bounds.getX() + bounds.getWidth() * t);
    const auto centre = pitchPoint (note, (double) t, axis);
    const auto top = juce::jmax (bounds.getY() - 4.0f, centre.y - kNoteHeight * 0.5f - 4.0f);
    const auto bottom = juce::jmin (bounds.getBottom() + 4.0f,
                                    centre.y + kNoteHeight * 0.5f + 4.0f);
    return { x - 3.0f, top, 6.0f, bottom - top };
}

juce::int64 PianoGrid::linkPredecessorFor (const Note& note) const
{
    const auto* clip = currentClip();

    if (clip == nullptr || note.tieFromNoteId == 0)
        return 0;

    for (const auto& candidate : clip->notes)
        if (candidate.id == note.tieFromNoteId
            && candidate.endTick() == note.startTick)
            return candidate.id;

    return 0;
}

juce::Rectangle<float> PianoGrid::linkBadge (const Note& note,
                                             const music::VerticalAxis& axis) const
{
    if (linkPredecessorFor (note) == 0)
        return {};

    const auto bounds = noteRect (note, axis);
    return { bounds.getX() - 5.0f, bounds.getY() - 12.0f, 10.0f, 7.0f };
}

juce::Rectangle<int> PianoGrid::commentRectFor (const juce::Rectangle<float>& rect,
                                                int gridWidth) const
{
    const auto x = (int) std::floor (rect.getX());
    const auto y = (int) std::floor (rect.getY() - kCommentGap) - kCommentHeight;
    const auto width = juce::jmax (40, gridWidth - x);

    return { x, y, width, kCommentHeight };
}

juce::Rectangle<float> PianoGrid::getNoteRectForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    const auto axis = makeAxis();

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return noteRect (note, axis);

    return {};
}

juce::Rectangle<float> PianoGrid::getHeadHandleBoundsForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return endpointHandle (note, false, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<float> PianoGrid::getNoteShapeBoundsForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return noteRect (note, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<float> PianoGrid::getTailHandleBoundsForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return endpointHandle (note, true, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<float> PianoGrid::getTiltStartMarkerForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return tiltMarker (note, false, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<float> PianoGrid::getTiltEndMarkerForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return tiltMarker (note, true, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<float> PianoGrid::getLinkBadgeBoundsForTest (juce::int64 noteId) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return {};

    for (const auto& note : clip->notes)
        if (note.id == noteId)
            return linkBadge (note, makeAxis()).translated (
                (float) gridArea().getX(), (float) gridArea().getY());

    return {};
}

juce::Rectangle<int> PianoGrid::getCommentRectForTest (juce::int64 noteId) const
{
    const auto noteBounds = getNoteRectForTest (noteId);

    if (noteBounds.isEmpty())
        return {};

    return commentRectFor (noteBounds, gridArea().getWidth());
}

int PianoGrid::getDrumRowCountForTest() const
{
    return isDrumTrack() ? kDrumPads : 0;
}

juce::Rectangle<int> PianoGrid::getDrumRowBoundsForTest (int index) const
{
    if (! isDrumTrack() || index < 0 || index >= kDrumPads)
        return {};

    const auto area = gridArea();
    const auto row  = drumRowHeight();
    const auto top  = (float) area.getY()
                    + (float) (kDrumPads - 1 - index) * row;

    return { area.getX(), (int) std::round (top), area.getWidth(), (int) std::round (row - 1.0f) };
}

juce::Point<float> PianoGrid::pointForPadForTest (int pad, juce::int64 tick) const
{
    const auto area = gridArea();

    return { (float) area.getX() + (float) xForTick ((double) tick),
             yForPad (pad) };
}

int PianoGrid::padForPointForTest (juce::Point<float> point) const
{
    return padForY (point.y);
}

const Note* PianoGrid::findNoteAt (juce::Point<float> gridPoint) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return nullptr;

    const auto axis = makeAxis();

    for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
    {
        auto rect = noteRect (*it, axis);
        rect.expand (2.0f, 2.0f);

        if (rect.contains (gridPoint))
            return &(*it);
    }

    return nullptr;
}

//==============================================================================
void PianoGrid::refresh()
{
    // Entering the piano window (or opening a project that was saved in it) lines
    // the semitone rows up with the canvas once; after that the view belongs to
    // the user's scrolling, so this must not run on every refresh.
    if (view().editorMode != lastEditorMode)
    {
        lastEditorMode = view().editorMode;

        if (isPianoMode())
            alignPianoView();
    }

    applyAutoFit();
    updateScrollBars();
    repaint();
}

juce::Point<float> PianoGrid::pointFor (double hz, juce::int64 tick) const
{
    const auto area = gridArea();
    const auto axis = makeAxis();

    return { (float) area.getX() + (float) xForTick ((double) tick),
             (float) area.getY() + (float) axis.frequencyToY (hz) };
}

void PianoGrid::centreOnSelectedNote()
{
    if (const auto* note = primarySelectedNote())
    {
        view().centreHz = music::VerticalAxis::clampHz (note->frequencyHz);
        if (isPianoMode())
            alignPianoView();
        updateScrollBars();
        repaint();
    }
}

void PianoGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PianoGrid::applyAutoFit()
{
    const auto* note = primarySelectedNote();
    const auto noteId = note != nullptr ? note->id : (juce::int64) 0;

    if (noteId == lastFittedNoteId)
        return;

    lastFittedNoteId = noteId;

    // A drum track's vertical axis is the kit, so there is nothing to centre on.
    if (! view().autoFitView || note == nullptr || isDrumTrack())
        return;

    view().centreHz = music::VerticalAxis::clampHz (note->frequencyHz);
}

void PianoGrid::updateScrollBars()
{
    const juce::ScopedValueSetter<bool> guard (updatingScrollBars, true);
    const auto area = gridArea();

    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    const auto visibleTicks = (double) area.getWidth() * (double) kTicksPerQuarter
                              / juce::jmax (1.0, view().pixelsPerQuarter);
    const auto contentTicks = juce::jmax (view().horizontalOffsetTicks + visibleTicks,
                                          (double) document.projectEndTick() + 8.0 * (double) kTicksPerQuarter);

    horizontalScrollBar.setRangeLimits (0.0, juce::jmax (1.0, contentTicks),
                                        juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange (view().horizontalOffsetTicks, visibleTicks,
                                         juce::dontSendNotification);

    const auto bottomLog = std::log2 (kMinVisibleHz);
    const auto topLog    = std::log2 (kMaxVisibleHz);
    const auto axis = makeAxis();

    // A drum track shows its twelve rows all at once: there is nothing to scroll.
    verticalScrollBar.setVisible (! isDrumTrack());
    verticalScrollBar.setRangeLimits (bottomLog, topLog, juce::dontSendNotification);
    verticalScrollBar.setCurrentRange (std::log2 (axis.bottomHz()),
                                       juce::jmin (view().octavesVisible, topLog - bottomLog),
                                       juce::dontSendNotification);
}

void PianoGrid::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollBars)
        return;

    if (bar == &horizontalScrollBar)
        view().horizontalOffsetTicks = juce::jmax (0.0, newRangeStart);
    else
    {
        view().centreHz = music::VerticalAxis::clampHz (
            std::pow (2.0, newRangeStart + view().octavesVisible * 0.5));

        if (isPianoMode())
            alignPianoView();
    }

    repaint();
}

void PianoGrid::resized()
{
    auto area = getLocalBounds();
    horizontalScrollBar.setBounds (area.removeFromBottom (kScrollBarThickness)
                                       .withTrimmedRight (kScrollBarThickness));
    verticalScrollBar.setBounds (area.removeFromRight (kScrollBarThickness));

    updateScrollBars();
}

//==============================================================================
void PianoGrid::paint (juce::Graphics& g)
{
    const auto area = gridArea();

    g.setColour (theme::gridBackground);
    g.fillRect (area);

    // The keyboard is outside the canvas (which starts after it), so it is
    // painted in component coordinates rather than inside the transform below.
    if (isPianoMode())
        paintPianoKeys (g, area.getHeight());

    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    const auto axis = makeAxis();
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto width = area.getWidth();
    const auto height = area.getHeight();

    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::translation ((float) area.getX(), (float) area.getY()));
    g.reduceClipRegion (juce::Rectangle<int> (0, 0, width, height));

    const auto* openClip = currentClip();
    const auto audioClip = openClip != nullptr && openClip->isAudio();

    // 钢琴窗模式: the semitone rows go behind everything else. The reference lines
    // can be switched off, in which case the piano roll is left on its own.
    if (isPianoMode())
        paintPianoRows (g, width, height);

    paintGridLines (g, tempoMap, width, height);

    // 侧视: the other tracks' notes, as a dim background board. They go under the
    // reference lines and under the window shade, so they stay behind everything
    // the user actually works with, and they take no part in hit testing.
    paintOtherTrackNotes (g, axis, width, height);

    // An audio clip has no pitch to line up with, and a drum track's vertical
    // axis is the kit's rows, so the reference lines stay out of both.
    if (! audioClip && ! isDrumTrack()
        && (! isPianoMode() || view().showReferenceLinesInPianoMode))
        paintReferenceLines (g, width, height);

    // The clip is the window being edited: everything outside it is dimmed.
    if (const auto* clip = currentClip())
    {
        const auto windowLeft = (float) xForTick ((double) clip->startTick);
        const auto windowRight = (float) xForTick ((double) clip->endTick());

        g.setColour (juce::Colours::black.withAlpha (0.45f));

        if (windowLeft > 0.0f)
            g.fillRect (0.0f, 0.0f, windowLeft, (float) height);

        if (windowRight < (float) width)
            g.fillRect (windowRight, 0.0f, (float) width - windowRight, (float) height);
    }

    if (audioClip)
    {
        paintAudioWaveform (g, *openClip, width, height);
    }
    else
    {
        paintNotes (g, width, height);

    }

    paintDragPreview (g, axis, width);

    // The ratio arrow explains which line a hovered target came from; in the
    // piano window the reference lines are background, so there is nothing to
    // explain.
    if (! isDrumTrack() && ! isPianoMode())
        paintRatioArrow (g);

    paintPlayhead (g, width, height);

    if (audioClip)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f));
        g.drawText (NI_TXT ("音频片段（只读）"),
                    juce::Rectangle<int> (6, 4, width - 12, 16),
                    juce::Justification::topLeft);
    }

    if (currentTrack() == nullptr || currentClip() == nullptr)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (14.0f));
        g.drawText (currentTrack() == nullptr
                        ? NI_TXT ("请先在音轨栏选择或新建一条音轨")
                        : NI_TXT ("请在音轨栏点击一个片段进入编辑"),
                    juce::Rectangle<int> (0, 0, width, height),
                    juce::Justification::centred);
    }
}

void PianoGrid::paintGridLines (juce::Graphics& g, const music::TempoMap& tempoMap,
                                int width, int height)
{
    // A drum track's "grid lines" are its twelve pad rows: alternating bands with
    // the drum's name, so the horizontal beat lines still read on top of them.
    if (isDrumTrack())
    {
        const auto row = drumRowHeight();

        g.setFont (theme::font (11.0f));

        for (int pad = kDrumPads - 1; pad >= 0; --pad)
        {
            const auto top = (float) (kDrumPads - 1 - pad) * row;
            const auto striped = (pad % 2) == 1;

            g.setColour (striped ? theme::laneBackground : theme::gridBackground.brighter (0.04f));
            g.fillRect (0.0f, top, (float) width, row);

            g.setColour (theme::outline.withAlpha (0.7f));
            g.drawHorizontalLine ((int) (top + row), 0.0f, (float) width);

            g.setColour (theme::dimText);
            g.drawText (drumPadName (pad),
                        juce::Rectangle<int> (5, (int) top, 110, (int) row),
                        juce::Justification::centredLeft);
        }
    }

    const auto firstTick = tickForX (0.0);
    const auto lastTick  = tickForX ((double) width);
    const auto firstBar  = juce::jmax (1, tempoMap.barAtTick ((juce::int64) juce::jmax (0.0, firstTick)));
    int bar = firstBar;

    // Beat lines first, so the heavier bar lines sit on top of them.
    g.setColour (theme::gridBeatLine);

    auto ticksPerBeat = juce::jmax (1.0, tempoMap.ticksPerBeatAt ((juce::int64) juce::jmax (0.0, firstTick)));
    auto beatTick = (juce::int64) std::floor (juce::jmax (0.0, firstTick) / ticksPerBeat + 1.0e-9) * (juce::int64) std::llround (ticksPerBeat);
    const auto pixelsPerBeat = ticksPerBeat * view().pixelsPerQuarter / (double) kTicksPerQuarter;

    if (pixelsPerBeat >= 4.0)
    {
        for (int guard = 0; guard < kMaxGridLines && (double) beatTick <= lastTick; ++guard)
        {
            const auto x = (float) xForTick ((double) beatTick);

            if (x >= 0.0f && x <= (float) width)
                g.fillRect (x, 0.0f, 1.0f, (float) height);

            beatTick = tempoMap.nextBeatTick (beatTick);
            ticksPerBeat = juce::jmax (1.0, tempoMap.ticksPerBeatAt (beatTick));
        }
    }

    const auto& editorSnap = view().editorSnap;

    if (editorSnap.enabled && editorSnap.clampedDenominator() > 1)
    {
        auto snapTickStep = ticksPerBeat / (double) editorSnap.clampedDenominator();
        snapTickStep = juce::jmax (1.0, snapTickStep);
        const auto pixelsPerSnap = snapTickStep * view().pixelsPerQuarter
                                   / (double) kTicksPerQuarter;

        if (pixelsPerSnap >= 3.0)
        {
            const float dashes[] { 2.0f, 5.0f };
            g.setColour (theme::gridBeatLine.withAlpha (0.42f));
            auto tick = (juce::int64) std::floor (juce::jmax (0.0, firstTick) / snapTickStep)
                        * (juce::int64) std::llround (snapTickStep);

            for (int guard = 0; guard < kMaxGridLines && (double) tick <= lastTick; ++guard)
            {
                const auto thisBeat = juce::jmax (1.0, tempoMap.ticksPerBeatAt (tick));
                const auto isBeat = std::abs (std::fmod ((double) tick, thisBeat)) < 0.5;

                if (! isBeat)
                {
                    const auto x = (float) xForTick ((double) tick);

                    if (x >= 0.0f && x <= (float) width)
                        g.drawDashedLine ({ x, 0.0f, x, (float) height },
                                          dashes, 2, 1.0f);
                }

                tick += (juce::int64) std::llround (snapTickStep);
            }
        }
    }

    g.setColour (theme::gridBarLine);

    for (int guard = 0; guard < kMaxGridLines; ++guard)
    {
        const auto barTick = tempoMap.barStartTick (bar);

        if ((double) barTick > lastTick)
            break;

        const auto x = (float) xForTick ((double) barTick);

        if (x >= -40.0f && x <= (float) width)
            g.fillRect (x, 0.0f, 1.6f, (float) height);

        ++bar;
    }
}

void PianoGrid::paintPianoRows (juce::Graphics& g, int width, int height)
{
    const auto area = gridArea();
    const auto rows = juce::jmax (1, (int) std::lround (view().octavesVisible
                                                        * music::piano::semitonesPerOctave));
    const auto rowHeight = (float) height / (float) rows;
    const auto topNote = pianoRowForY (rowHeight * 0.5f);

    for (int row = 0; row < rows; ++row)
    {
        const auto midiNote = juce::jlimit (music::piano::lowestNote,
                                            music::piano::highestNote, topNote - row);
        // This function paints inside the canvas transform, while pianoRowRect
        // uses component coordinates shared with the key strip. Translate to the
        // canvas' local space first; otherwise the first key-column width of every
        // row stayed unpainted and looked like a hole beside the piano keys.
        const auto rect = pianoRowRect (midiNote).translated (
            -(float) area.getX(), -(float) area.getY());
        const auto black = music::piano::isBlackKey (midiNote);

        // White key rows are the lighter rows; black keys are darker. Both use the
        // exact same rect as the key labels below.
        g.setColour (black ? juce::Colour (0xff15151b) : juce::Colour (0xff262631));
        g.fillRect (rect);

        // Every C gets a line, so an octave is easy to find on screen.
        if (music::piano::pitchClassOf (midiNote) == 0)
        {
            g.setColour (theme::gridBeatLine);
            g.fillRect (0.0f, rect.getY(), (float) width, 1.0f);
        }
    }
}

void PianoGrid::paintPianoKeys (juce::Graphics& g, int height)
{
    const auto keys = getPianoKeysBoundsForTest();

    if (keys.isEmpty() || height <= 0)
        return;

    g.setColour (theme::gridBackground);
    g.fillRect (keys);

    const auto rows = juce::jmax (1, (int) std::lround (view().octavesVisible
                                                        * music::piano::semitonesPerOctave));
    const auto rowHeight = (float) height / (float) rows;
    const auto topNote = pianoRowForY (rowHeight * 0.5f);

    g.setFont (theme::font (9.0f));

    for (int row = 0; row < rows; ++row)
    {
        const auto midiNote = juce::jlimit (music::piano::lowestNote,
                                            music::piano::highestNote, topNote - row);
        const auto rowRect = pianoRowRect (midiNote);
        const auto y = rowRect.getY();
        const auto black = music::piano::isBlackKey (midiNote);
        const auto keyHeight = juce::jmax (1.0f, rowHeight - (black ? 1.0f : 0.5f));

        g.setColour (black ? juce::Colour (0xff15151b) : juce::Colour (0xffe6e6ee));
        g.fillRect (juce::Rectangle<float> ((float) keys.getX(), y,
                                            (float) keys.getWidth() * (black ? 0.62f : 1.0f),
                                            keyHeight));

        if (! black && music::piano::pitchClassOf (midiNote) == 0)
        {
            g.setColour (theme::dimText);
            g.drawText (music::piano::noteName (midiNote),
                        juce::Rectangle<int> (keys.getX(), (int) y,
                                              keys.getWidth() - 3,
                                              (int) juce::jmax (1.0f, rowHeight)),
                        juce::Justification::centredRight, false);
        }
    }

    g.setColour (theme::outline);
    g.drawVerticalLine (keys.getRight() - 1, (float) keys.getY(), (float) keys.getBottom());
}

void PianoGrid::paintReferenceLines (juce::Graphics& g, int width, int height)
{
    const auto axis = makeAxis();
    const auto lines = currentReferenceLines();
    const float dashes[] = { 5.0f, 5.0f };

    g.setFont (theme::font (10.5f));

    for (const auto& line : lines)
    {
        const auto y = (float) axis.frequencyToY (line.hz);

        if (y < -2.0f || y > (float) height + 2.0f)
            continue;

        // A colour set on the status entry wins; otherwise the line keeps the
        // colour of its role (and groups fall back to their dot colours).
        auto colour = line.colourIndex >= 0 ? theme::trackColour (line.colourIndex)
                    : line.isManual        ? theme::referenceLineManual
                    : line.isTarget        ? theme::referenceLineTarget
                                           : theme::referenceLine;

        // Dashed lines are the dimmed, non-interactive octaves; keep that readable
        // whatever colour the entry carries.
        if (line.dashed)
            colour = line.colourIndex >= 0 ? colour.withAlpha (0.45f) : theme::referenceLineDim;

        // Snap to whole pixels so the lines stay crisp instead of being smeared
        // over two rows by anti-aliasing.
        const auto yPixel = std::round (y);

        if (line.dashed)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.drawDashedLine ({ 0.0f, yPixel, (float) width, yPixel }, dashes, 2, 1.0f);
        }
        else
        {
            g.setColour (colour);
            g.fillRect (0.0f, yPixel, (float) width, 1.0f);
        }

        g.setColour (colour.brighter (0.25f));
        g.drawText (formatHz (line.hz),
                    juce::Rectangle<int> (3, (int) yPixel - 13, 110, 12),
                    juce::Justification::centredLeft);
    }
}

void PianoGrid::paintAudioWaveform (juce::Graphics& g, const Clip& clip, int width, int height)
{
    juce::String error;
    auto sample = getCachedSample (clip.audioFilePath, error);

    if (sample == nullptr || sample->peaks.empty())
        return;

    const auto windowLeft = xForTick ((double) clip.startTick);
    const auto windowRight = xForTick ((double) clip.endTick());
    const auto left = (int) std::floor (windowLeft);
    const auto right = (int) std::ceil (windowRight);

    if (right <= 0 || left >= width)
        return;

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto clipSeconds = tempoMap.secondsAtTick (clip.endTick())
                                 - tempoMap.secondsAtTick (clip.startTick);
    const auto fileLength = sample->lengthSeconds();

    if (clipSeconds <= 0.0 || fileLength <= 0.0)
        return;

    const auto bucketCount = (int) sample->peaks.size();
    const auto centreY = (float) height * 0.5f;
    const auto halfHeight = (float) height * 0.42f;

    // A faint frame marks the clip window itself.
    g.setColour (theme::outline);
    g.drawRect (juce::Rectangle<float> ((float) left, 0.0f,
                                        (float) juce::jmax (1, right - left), (float) height), 1.0f);

    g.setColour (theme::referenceLine.withAlpha (0.85f));

    for (int x = juce::jmax (0, left); x < juce::jmin (width, right); ++x)
    {
        const auto clipTime = ((double) x - windowLeft) / juce::jmax (1.0, windowRight - windowLeft)
                                  * clipSeconds;
        const auto fileTime = clip.audioOffsetSeconds + clipTime;
        const auto bucket = waveformBucketFor (fileTime, fileLength, bucketCount);

        if (bucket < 0)
            continue;

        const auto peak = juce::jlimit (0.0f, 1.0f, sample->peaks[(size_t) bucket]);
        const auto barHeight = juce::jmax (1.0f, peak * halfHeight);
        g.fillRect (juce::Rectangle<float> ((float) x, centreY - barHeight, 1.0f, barHeight * 2.0f));
    }
}

std::vector<PianoGrid::GhostNote> PianoGrid::otherTrackNotes (const music::VerticalAxis& axis,
                                                              int width, int height) const
{
    std::vector<GhostNote> ghosts;

    // The same "there is a pitch axis on this canvas" rule the 侧视 button uses: a
    // drum track's rows are the kit, and an audio clip is a waveform, so neither
    // has anywhere to put another track's notes.
    if (! view().sideView || isDrumTrack())
        return ghosts;

    const auto* openClip = currentClip();

    if (openClip != nullptr && openClip->isAudio())
        return ghosts;

    const auto leftTick = tickForX (0.0);
    const auto rightTick = tickForX ((double) width);

    for (const auto& track : document.getProject().tracks)
    {
        // The open track is the one being edited: its own material is what the
        // canvas already shows, clip by clip.
        if (track.id == appState.selectedTrackId)
            continue;

        // A drum machine's notes are pad hits, not pitches, so they cannot be
        // placed on this axis at all.
        if (track.kind == TrackKind::drums)
            continue;

        // 侧视: the picker's un-ticked tracks drop out here, so the painter and the
        // diagnostic hook see exactly the same set.
        if (view().sideViewHiddenTracks.count (track.id) > 0)
            continue;

        const auto colour = theme::trackColour (track.colourIndex);

        for (const auto& clip : track.clips)
        {
            // Only the clips that can show something in this window are expanded -
            // that keeps the pass cheap enough to run on every repaint.
            if (clip.lengthTicks <= 0
                || (double) clip.endTick() < leftTick || (double) clip.startTick > rightTick)
                continue;

            for (const auto& placement : music::expandClip (clip))
            {
                if (placement.note == nullptr)
                    continue;

                const auto rect = noteRectAt (*placement.note, placement.startTick, axis);

                if (rect.getRight() < 0.0f || rect.getX() > (float) width
                    || rect.getBottom() < 0.0f || rect.getY() > (float) height)
                    continue;

                ghosts.push_back ({ rect, colour, placement.note->muted });
            }
        }
    }

    return ghosts;
}

void PianoGrid::paintOtherTrackNotes (juce::Graphics& g, const music::VerticalAxis& axis,
                                      int width, int height) const
{
    // The painter and the self test walk this one list, so "it is in the list" and
    // "it really got painted" cannot drift apart.
    for (const auto& ghost : otherTrackNotes (axis, width, height))
    {
        // One flat, dim colour per track: this is a background board, not a second
        // copy of the editor, so no velocity shading and no comments.
        if (ghost.muted)
        {
            g.setColour (ghost.colour.withAlpha (0.35f));
            g.drawRect (ghost.rect, 1.0f);
        }
        else
        {
            g.setColour (ghost.colour.withAlpha (0.30f));
            g.fillRect (ghost.rect);
        }
    }
}

std::vector<juce::Rectangle<float>> PianoGrid::getSideViewGhostRectsForTest() const
{
    const auto area = gridArea();
    const auto axis = makeAxis();
    const auto offset = juce::Point<float> ((float) area.getX(), (float) area.getY());
    std::vector<juce::Rectangle<float>> rects;

    for (const auto& ghost : otherTrackNotes (axis, area.getWidth(), area.getHeight()))
        rects.push_back (ghost.rect + offset);

    return rects;
}

void PianoGrid::paintNotes (juce::Graphics& g, int width, int height)
{
    const auto* track = currentTrack();
    const auto* clip = currentClip();

    if (track == nullptr || clip == nullptr)
        return;

    const auto axis = makeAxis();
    const auto trackColour = theme::trackColour (track->colourIndex);

    g.setFont (theme::font (10.0f));

    for (const auto& note : clip->notes)
    {
        auto rect = noteRect (note, axis);
        const auto shape = noteShapePath (note, axis);

        if (rect.getRight() < 0.0f || rect.getX() > (float) width
            || rect.getBottom() < 0.0f || rect.getY() > (float) height)
            continue;

        const auto selected = appState.isNoteSelected (note.id);
        // Notes outside the clip window are kept but silent, so they read dimmer.
        const auto sounding = noteIsSounding (note);

        if (note.muted)
        {
            // A muted note is a coloured hollow outline: it keeps its place and its
            // track colour, but it is visibly empty, so "it is there but silent" is
            // readable at a glance.
            g.setColour (trackColour.withAlpha (sounding ? 1.0f : 0.4f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }
        else
        {
            // On a drum track the fill brightness is the velocity, so accents are
            // readable at a glance: 0 is a dark block, 127 the full track colour.
            auto fill = trackColour;

            if (isDrumTrack())
            {
                const auto velocity = juce::jlimit (0, 127, note.velocity) / 127.0f;
                fill = trackColour.withMultipliedBrightness (0.45f + 0.55f * velocity);
            }

            g.setColour (fill.withAlpha (sounding ? (selected ? 1.0f : 0.82f) : 0.32f));
            g.fillPath (shape);

            g.setColour (trackColour.darker (0.6f).withAlpha (sounding ? 1.0f : 0.4f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        if (selected)
        {
            g.setColour (theme::noteSelectedOutline);
            g.strokePath (shape, juce::PathStrokeType (1.5f));
        }

        const auto showEndpoint = selected || (hoverHandleNoteId == note.id);

        if (showEndpoint && ! isDrumTrack() && note.durationTicks > 0)
        {
            const auto headHandle = endpointHandle (note, false, axis);
            const auto tailHandle = endpointHandle (note, true, axis);

            if (headHandle.getWidth() >= 5.0f)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (headHandle, 1.2f);
            }

            if (tailHandle.getWidth() >= 5.0f)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (tailHandle, 1.2f);
            }
        }

        if (! isDrumTrack() && (selected || note.hasPitchTilt()))
        {
            const auto start = tiltMarker (note, false, axis);
            const auto end = tiltMarker (note, true, axis);

            g.setColour (theme::accentDim);
            g.fillRect (start.withWidth (1.5f));
            g.fillRect (end.withX (end.getRight() - 1.5f));
        }

        if (const auto badge = linkBadge (note, axis); ! badge.isEmpty())
        {
            juce::Path triangle;
            triangle.addTriangle (badge.getCentreX(), badge.getBottom(),
                                  badge.getX(), badge.getY(),
                                  badge.getRight(), badge.getY());
            g.setColour (theme::accent);
            g.fillPath (triangle);
            g.setColour (theme::outline);
            g.strokePath (triangle, juce::PathStrokeType (0.8f));
        }

        // The comment sits just above the note, out of the way of its own shape.
        if (note.comment.isNotEmpty())
        {
            const auto commentRect = commentRectFor (rect, width);

            g.setColour (note.muted ? theme::dimText.withAlpha (0.6f)
                                    : theme::text.withAlpha (sounding ? 0.92f : 0.5f));
            g.setFont (theme::font (9.5f));
            g.drawText (note.comment,
                        commentRect,
                        juce::Justification::centredLeft,
                        true);
        }
    }
}

void PianoGrid::paintDragPreview (juce::Graphics& g, const music::VerticalAxis& axis, int width)
{
    if (dragMode == DragMode::creatingNote)
    {
        const auto start = juce::jmin (dragAnchorTick, dragCurrentTick);
        const auto end   = juce::jmax (dragAnchorTick, dragCurrentTick);

        Note preview;
        // dragAnchorTick/dragCurrentTick are absolute; noteRect() adds the clip's
        // start again, so the preview has to be stored the way a note is.
        preview.startTick = juce::jmax ((juce::int64) 0, start - clipStartTick());
        preview.durationTicks = juce::jmax ((juce::int64) 1, end - start);
        preview.frequencyHz = isDrumTrack() ? drumPadBaseHz (dragPad) : dragFrequency;
        preview.pad = isDrumTrack() ? dragPad : -1;

        g.setColour (theme::accent.withAlpha (0.55f));
        g.fillRect (noteRect (preview, axis));
        return;
    }

    if (dragMode == DragMode::marquee)
    {
        if (dragActive)
        {
            const auto rectangle = marqueeBand();

            g.setColour (theme::accent.withAlpha (0.16f));
            g.fillRect (rectangle);
            g.setColour (theme::accent);
            g.drawRect (rectangle, 1.0f);
        }

        return;
    }

    if (! dragActive)
        return;

    if (dragMode == DragMode::pitchHandle || dragMode == DragMode::tiltHandle)
    {
        for (const auto& original : dragOriginals)
        {
            Note preview = original;

            if (dragMode == DragMode::pitchHandle)
            {
                if (! linkTargetValid || original.id == pitchHandleNoteId)
                {
                    if (pitchHandleIsTail)
                    {
                        preview.tailFrequencyHz = juce::jlimit (
                            kMinVisibleHz, kMaxVisibleHz,
                            original.effectiveTailHz() * pitchHandlePreviewFactor);
                        preview.durationTicks = juce::jmax (
                            (juce::int64) 1,
                            original.endTick() + pitchHandlePreviewTickDelta
                                - original.startTick);
                    }
                    else
                    {
                        preview.frequencyHz = juce::jlimit (
                            kMinVisibleHz, kMaxVisibleHz,
                            original.frequencyHz * pitchHandlePreviewFactor);
                        preview.startTick = juce::jmax (
                            (juce::int64) 0,
                            original.startTick + pitchHandlePreviewTickDelta);
                        preview.durationTicks = juce::jmax (
                            (juce::int64) 1,
                            original.endTick() - preview.startTick);
                    }
                }
            }
            else
            {
                const auto delta = tiltPreview - tiltOriginal;

                if (tiltHandleIsTail)
                    preview.tiltEnd = juce::jlimit (preview.tiltStart + 0.001f, 1.0f,
                                                    original.tiltEnd + delta);
                else
                    preview.tiltStart = juce::jlimit (0.0f, preview.tiltEnd - 0.001f,
                                                      original.tiltStart + delta);
            }

            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillPath (noteShapePath (preview, axis));
            g.setColour (theme::accent);
            g.strokePath (noteShapePath (preview, axis), juce::PathStrokeType (1.0f));
        }

        if (dragMode == DragMode::pitchHandle && ! linkTargetValid)
        {
            const auto* clip = currentClip();

            if (clip != nullptr)
                for (const auto& [partnerId, partnerTail] : pitchSeamPartners)
                    for (const auto& note : clip->notes)
                        if (note.id == partnerId)
                        {
                            Note preview = note;

                            if (partnerTail)
                            {
                                preview.tailFrequencyHz = juce::jlimit (
                                    kMinVisibleHz, kMaxVisibleHz,
                                    note.effectiveTailHz() * pitchHandlePreviewFactor);
                                preview.durationTicks = juce::jmax (
                                    (juce::int64) 1,
                                    note.endTick() + pitchHandlePreviewTickDelta
                                        - note.startTick);
                            }
                            else
                            {
                                preview.frequencyHz = juce::jlimit (
                                    kMinVisibleHz, kMaxVisibleHz,
                                    note.frequencyHz * pitchHandlePreviewFactor);
                                preview.startTick = juce::jmax (
                                    (juce::int64) 0,
                                    note.startTick + pitchHandlePreviewTickDelta);
                                preview.durationTicks = juce::jmax (
                                    (juce::int64) 1,
                                    note.endTick() - preview.startTick);
                            }

                            g.setColour (juce::Colours::white.withAlpha (0.45f));
                            g.fillPath (noteShapePath (preview, axis));
                            g.setColour (juce::Colours::white.withAlpha (0.8f));
                            g.strokePath (noteShapePath (preview, axis),
                                          juce::PathStrokeType (1.0f));
                            break;
                        }
        }

        if (dragMode == DragMode::pitchHandle && linkTargetValid && linkTargetNoteId != 0)
        {
            const auto* dragged = findOriginalNote (pitchHandleNoteId);

            if (dragged != nullptr)
            {
                const auto from = pitchPoint (*dragged, pitchHandleIsTail ? 1.0 : 0.0, axis);

                if (const auto* clip = currentClip())
                    for (const auto& candidate : clip->notes)
                        if (candidate.id == linkTargetNoteId)
                        {
                            const auto targetPoint = pitchPoint (
                                candidate, linkTargetIsTail ? 1.0 : 0.0, axis);
                            g.setColour (theme::accent.withAlpha (0.65f));
                            g.drawLine (from.x, from.y, targetPoint.x, targetPoint.y, 1.5f);
                            break;
                        }
            }
        }

        return;
    }

    if (dragMode == DragMode::movingNotes)
    {
        for (const auto& original : dragOriginals)
        {
            Note moved = original;
            moved.startTick = juce::jmax ((juce::int64) 0, original.startTick + previewTickDelta);

            if (isDrumTrack())
            {
                const auto from = original.pad < 0 ? 0 : original.pad;
                moved.pad = juce::jlimit (0, kDrumPads - 1, from + previewPadDelta);
                moved.frequencyHz = drumPadBaseHz (moved.pad);
            }
            else
            {
                music::transposeNotePitch (moved, previewFrequencyFactor);
            }

            auto rect = noteRect (moved, axis);

            if (rect.getRight() < 0.0f || rect.getX() > (float) width)
                continue;

            const auto shape = noteShapePath (moved, axis);
            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillPath (shape);
            g.setColour (theme::accent);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        for (const auto& follower : bodySeamFollowers)
        {
            const auto* anchor = [&]() -> const Note*
            {
                for (const auto& original : dragOriginals)
                    if (original.id == follower.anchorNoteId)
                        return &original;

                return nullptr;
            }();

            if (anchor == nullptr)
                continue;

            const auto movedFollower = makeSeamFollowerForBodyDrag (
                follower.note, *anchor, previewTickDelta, previewFrequencyFactor,
                follower.followsStart);
            const auto shape = noteShapePath (movedFollower, axis);

            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillPath (shape);
            g.setColour (theme::accent);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        // Highlight the reference line the group would snap onto.
        if (std::abs (previewFrequencyFactor - 1.0) > 1.0e-12 && primaryOriginalHz > 0.0)
        {
            const auto target = music::scaledFrequency (primaryOriginalHz, previewFrequencyFactor);
            const auto y = (float) std::round (axis.frequencyToY (target));

            g.setColour (theme::accent.withAlpha (0.85f));
            g.fillRect (0.0f, y, (float) width, 1.6f);
        }

        return;
    }

    if (hasPreviewTiming)
    {
        const auto fromStart = dragMode == DragMode::resizingStart;

        // Every selected note previews with the same edge delta.
        for (const auto& original : dragOriginals)
        {
            Note resized = original;

            if (fromStart)
            {
                resized.startTick = juce::jmax ((juce::int64) 0,
                                                original.startTick + previewResizeDelta);
                resized.durationTicks = juce::jmax ((juce::int64) 1,
                                                    original.endTick() - resized.startTick);
            }
            else
            {
                resized.durationTicks = juce::jmax ((juce::int64) 1,
                                                    original.endTick() + previewResizeDelta
                                                        - original.startTick);
            }

            const auto rect = noteRect (resized, axis);
            const auto shape = noteShapePath (resized, axis);

            if (rect.getRight() < 0.0f || rect.getX() > (float) width)
                continue;

            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillPath (shape);
            g.setColour (theme::accent);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }

        if (fromStart && hasResizePredecessor)
        {
            Note predecessor = resizePredecessorOriginal;
            predecessor.durationTicks = juce::jmax (
                (juce::int64) 1, predecessor.durationTicks + previewResizeDelta);
            const auto shape = noteShapePath (predecessor, axis);
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillPath (shape);
            g.setColour (theme::accent);
            g.strokePath (shape, juce::PathStrokeType (1.0f));
        }
        else if (! fromStart)
        {
            for (const auto& successor : resizeShiftOriginals)
            {
                Note moved = successor;
                moved.startTick = juce::jmax ((juce::int64) 0,
                                              successor.startTick + previewResizeDelta);
                const auto shape = noteShapePath (moved, axis);
                g.setColour (theme::accent.withAlpha (0.35f));
                g.fillPath (shape);
                g.setColour (theme::accent);
                g.strokePath (shape, juce::PathStrokeType (1.0f));
            }
        }
    }
}

void PianoGrid::paintRatioArrow (juce::Graphics& g)
{
    const auto geometry = ratioArrowGeometry();

    if (! geometry.valid)
        return;

    g.setColour (theme::accent);
    g.fillRect (geometry.shaftX - 1.0f, geometry.top, 2.0f, geometry.bottom - geometry.top);

    // The head sits on the line under the mouse; the shaft runs from the line the
    // ratio came from, so the arrow reads "this one came from that one".
    const auto baseY = geometry.headY + (geometry.direction > 0 ? kRatioArrowHeadLength
                                                               : -kRatioArrowHeadLength);
    juce::Path head;
    head.addTriangle (geometry.shaftX, geometry.headY,
                      geometry.shaftX - kRatioArrowHeadWidth * 0.5f, baseY,
                      geometry.shaftX + kRatioArrowHeadWidth * 0.5f, baseY);
    g.fillPath (head);

    if (geometry.label.isNotEmpty())
    {
        // A plate behind the caption keeps it readable on top of notes.
        g.setColour (theme::gridBackground.withAlpha (0.9f));
        g.fillRoundedRectangle (geometry.plate, 3.0f);
        g.setColour (theme::accent);
        g.setFont (theme::font (11.5f, true));
        g.drawText (geometry.label, geometry.plate, juce::Justification::centred);
    }
}

void PianoGrid::paintPlayhead (juce::Graphics& g, int width, int height)
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto tick = tempoMap.tickAtSeconds (audio.getPositionSeconds());
    const auto x = (float) xForTick ((double) tick);

    lastPlayheadX = (double) x;

    if (x < -2.0f || x > (float) width + 2.0f)
        return;

    g.setColour (theme::playhead);
    g.fillRect (x, 0.0f, 1.6f, (float) height);
}

void PianoGrid::refreshPlayhead()
{
    const auto area = gridArea();
    const auto x = lastPlayheadX;

    auto repaintStrip = [this, &area] (double stripX)
    {
        if (stripX < -4.0 || stripX > (double) area.getWidth() + 4.0)
            return;

        repaint (juce::Rectangle<int> (area.getX() + (int) stripX - 3, area.getY(),
                                       8, area.getHeight()));
    };

    repaintStrip (x);

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    repaintStrip (xForTick ((double) tempoMap.tickAtSeconds (audio.getPositionSeconds())));

}

//==============================================================================
/** Classifies what sits under the mouse: nothing, a note body, or one of its edges. */
PianoGrid::NoteHitResult PianoGrid::hitTestNote (juce::Point<float> gridPoint) const
{
    NoteHitResult result;
    const auto* clip = currentClip();

    if (clip == nullptr)
        return result;

    const auto axis = makeAxis();

    for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
    {
        const auto& note = *it;
        const auto rect = noteRect (note, axis);

        if (const auto badge = linkBadge (note, axis);
            ! badge.isEmpty() && badge.expanded (2.0f).contains (gridPoint))
        {
            result.note = &note;
            result.hit = NoteHit::linkBadge;
            return result;
        }

        if (! isDrumTrack())
        {
            const auto head = endpointHandle (note, false, axis);
            const auto tail = endpointHandle (note, true, axis);

            const auto insideOuterEdge = gridPoint.x > rect.getX() + 3.0f
                                         && gridPoint.x < rect.getRight() - 3.0f;

            if (insideOuterEdge && head.contains (gridPoint))
            {
                result.note = &note;
                result.hit = NoteHit::headHandle;
                return result;
            }

            if (insideOuterEdge && tail.contains (gridPoint))
            {
                result.note = &note;
                result.hit = NoteHit::tailHandle;
                return result;
            }

            // The tilt handles extend a few pixels above and below the note. Use
            // those exposed tips to grab them; the strip inside the note itself
            // remains available for duration resizing.
            const auto normalizedX = rect.getWidth() > 0.0f
                                         ? juce::jlimit (
                                               0.0f, 1.0f,
                                               (gridPoint.x - rect.getX()) / rect.getWidth())
                                         : 0.0f;
            const auto curveY = pitchPoint (note, (double) normalizedX, axis).y;
            const auto halfNoteHeight = juce::jmin (kNoteHeight * 0.5f,
                                                    rect.getHeight() * 0.5f);
            const auto onTiltTip = std::abs (gridPoint.y - curveY)
                                   > halfNoteHeight + 0.75f;

            if (onTiltTip
                && (appState.isNoteSelected (note.id) || note.hasPitchTilt()))
            {
                const auto start = tiltMarker (note, false, axis);
                const auto end = tiltMarker (note, true, axis);

                if (! start.isEmpty() && start.contains (gridPoint))
                {
                    result.note = &note;
                    result.hit = NoteHit::tiltStartHandle;
                    return result;
                }

                if (! end.isEmpty() && end.contains (gridPoint))
                {
                    result.note = &note;
                    result.hit = NoteHit::tiltEndHandle;
                    return result;
                }
            }
        }

        // Controls drawn on the note win over the outer duration strips. That
        // keeps the endpoint circles usable on narrow notes and also means one
        // handle drag can change both time and pitch.
        if (rect.expanded (2.0f, 2.0f).contains (gridPoint))
        {
            if (gridPoint.x <= rect.getX() + 3.0f)
            {
                result.note = &note;
                result.hit = NoteHit::leftEdge;
                return result;
            }

            if (gridPoint.x >= rect.getRight() - 3.0f)
            {
                result.note = &note;
                result.hit = NoteHit::rightEdge;
                return result;
            }
        }

        if (! rect.expanded (2.0f, 2.0f).contains (gridPoint))
            continue;

        if (! isDrumTrack() && ! noteShapePath (note, axis).contains (gridPoint))
            continue;

        result.note = &note;

        // Narrow notes would otherwise be all edge, so the grab zone shrinks.
        const auto edge = juce::jmin (5.0f, rect.getWidth() * 0.34f);

        if (gridPoint.x <= rect.getX() + edge)
            result.hit = NoteHit::leftEdge;
        else if (gridPoint.x >= rect.getRight() - edge)
            result.hit = NoteHit::rightEdge;
        else
            result.hit = NoteHit::body;

        return result;
    }

    return result;
}

const Note* PianoGrid::findOriginalNote (juce::int64 noteId) const
{
    for (const auto& note : dragOriginals)
        if (note.id == noteId)
            return &note;

    return nullptr;
}

const Note* PianoGrid::predecessorFor (const Clip& clip, const Note& note) const
{
    if (note.tieFromNoteId == 0)
        return nullptr;

    for (const auto& candidate : clip.notes)
        if (candidate.id == note.tieFromNoteId && candidate.endTick() == note.startTick)
            return &candidate;

    return nullptr;
}

const Note* PianoGrid::successorFor (const Clip& clip, const Note& note) const
{
    for (const auto& candidate : clip.notes)
        if (candidate.tieFromNoteId == note.id && candidate.startTick == note.endTick())
            return &candidate;

    return nullptr;
}

std::vector<Note> PianoGrid::successorsFor (const Clip& clip, juce::int64 noteId) const
{
    std::vector<Note> result;
    std::set<juce::int64> seen;
    const auto* cursorNote = [&]() -> const Note*
    {
        for (const auto& note : clip.notes)
            if (note.id == noteId)
                return &note;
        return nullptr;
    }();

    for (int guard = 0; cursorNote != nullptr && guard < (int) clip.notes.size(); ++guard)
    {
        const auto* next = successorFor (clip, *cursorNote);

        if (next == nullptr || ! seen.insert (next->id).second)
            break;

        result.push_back (*next);
        cursorNote = next;
    }

    return result;
}

std::vector<Note> PianoGrid::connectedChainFor (const Clip& clip, juce::int64 noteId) const
{
    std::set<juce::int64> ids { noteId };
    bool changed = true;

    for (int guard = 0; changed && guard < (int) clip.notes.size() + 1; ++guard)
    {
        changed = false;

        for (const auto& note : clip.notes)
        {
            if (ids.find (note.id) == ids.end())
                continue;

            if (const auto* pred = predecessorFor (clip, note);
                pred != nullptr && ids.insert (pred->id).second)
                changed = true;

            if (const auto* next = successorFor (clip, note);
                next != nullptr && ids.insert (next->id).second)
                changed = true;
        }
    }

    std::vector<Note> chain;

    for (const auto& note : clip.notes)
        if (ids.find (note.id) != ids.end())
            chain.push_back (note);

    return chain;
}

void PianoGrid::beginNoteDrag (const NoteHitResult& hit)
{
    const auto* track = currentTrack();
    const auto* clip = currentClip();

    if (track == nullptr || clip == nullptr || hit.note == nullptr)
    {
        dragMode = DragMode::none;
        return;
    }

    dragOriginals.clear();
    earliestOriginalStart = std::numeric_limits<juce::int64>::max();

    for (const auto& note : clip->notes)
    {
        if (! appState.isNoteSelected (note.id))
            continue;

        dragOriginals.push_back (note);
        earliestOriginalStart = juce::jmin (earliestOriginalStart, note.startTick);
    }

    if (dragOriginals.empty())
    {
        dragOriginals.push_back (*hit.note);
        earliestOriginalStart = hit.note->startTick;
    }

    bodySeamFollowers.clear();

    if ((hit.hit == NoteHit::body || hit.hit == NoteHit::none) && ! isDrumTrack())
    {
        std::set<juce::int64> selectedIds;

        for (const auto& original : dragOriginals)
            selectedIds.insert (original.id);

        std::set<std::pair<juce::int64, bool>> followerKeys;

        for (const auto& original : dragOriginals)
        {
            if (const auto* predecessor = predecessorFor (*clip, original);
                predecessor != nullptr
                && selectedIds.find (predecessor->id) == selectedIds.end()
                && followerKeys.insert ({ predecessor->id, false }).second)
            {
                bodySeamFollowers.push_back ({ *predecessor, original.id, false });
            }

            if (const auto* successor = successorFor (*clip, original);
                successor != nullptr
                && selectedIds.find (successor->id) == selectedIds.end()
                && followerKeys.insert ({ successor->id, true }).second)
            {
                bodySeamFollowers.push_back ({ *successor, original.id, true });
            }
        }
    }

    // Freeze the reference lines. While dragging, the selected note's own family
    // must not chase the note, and this is the set of snap targets.
    // A drum track has no frequency axis to snap against.
    // A drum track has no frequency axis to snap against, and the piano window
    // snaps to its own semitone rows instead.
    frozenLines = (isDrumTrack() || isPianoMode()) ? std::vector<music::ReferenceLine>()
                                                   : currentReferenceLines();

    const auto* primary = findOriginalNote (appState.primaryNoteId);

    if (primary == nullptr)
        primary = &dragOriginals.front();

    primaryOriginalHz = primary->frequencyHz;
    primaryOriginalTailHz = primary->effectiveTailHz();
    resizeNoteId = hit.note->id;
    // The resize path works in absolute ticks end to end: the pointer tick it is
    // compared against is absolute, and both the preview and the commit take the
    // clip's start back out again. Storing the note's clip-relative ticks here used
    // to make the whole path drift by the clip's start.
    resizeOriginalStart = clipStartTick() + hit.note->startTick;
    resizeOriginalEnd = clipStartTick() + hit.note->endTick();

    previewTickDelta = 0;
    previewFrequencyFactor = 1.0;
    previewTiming = { resizeOriginalStart, hit.note->durationTicks };
    hasPreviewTiming = true;
    resizeShiftOriginals.clear();
    hasResizePredecessor = false;

    if (hit.hit == NoteHit::leftEdge)
    {
        if (const auto* predecessor = predecessorFor (*clip, *hit.note))
        {
            resizePredecessorOriginal = *predecessor;
            hasResizePredecessor = true;
        }
    }
    else if (hit.hit == NoteHit::rightEdge)
    {
        resizeShiftOriginals = successorsFor (*clip, hit.note->id);
    }

    switch (hit.hit)
    {
        case NoteHit::leftEdge:  dragMode = DragMode::resizingStart; break;
        case NoteHit::rightEdge: dragMode = DragMode::resizingEnd;   break;
        case NoteHit::body:
        case NoteHit::none:      dragMode = DragMode::movingNotes;   break;
    }
}

void PianoGrid::beginPitchHandleDrag (const Note& note, bool tail)
{
    dragOriginals.clear();
    const auto* clip = currentClip();

    if (clip == nullptr)
        return;

    for (const auto& candidate : clip->notes)
        if (appState.isNoteSelected (candidate.id))
            dragOriginals.push_back (candidate);

    if (dragOriginals.empty())
        dragOriginals.push_back (note);

    pitchHandleNoteId = note.id;
    pitchHandleIsTail = tail;
    pitchHandleOriginalHz = tail ? note.effectiveTailHz() : note.frequencyHz;
    pitchHandlePreviewFactor = 1.0;
    pitchHandlePointerStartTick = (juce::int64) std::llround (
        tickForX ((double) dragStartLocal.x));
    pitchHandlePreviewTickDelta = 0;
    pitchSeamPartners.clear();
    linkTargetNoteId = 0;
    linkTargetValid = false;
    linkTargetHz = 0.0;
    frozenLines = (isDrumTrack() || isPianoMode()) ? std::vector<music::ReferenceLine>()
                                                   : currentReferenceLines();

    std::set<juce::int64> selectedIds;

    for (const auto& original : dragOriginals)
        selectedIds.insert (original.id);

    for (const auto& original : dragOriginals)
    {
        const Note* partner = nullptr;
        auto partnerTail = false;

        if (tail)
        {
            static_cast<void> (partnerTail);
            partner = successorFor (*clip, original);
        }
        else
        {
            partner = predecessorFor (*clip, original);
            partnerTail = true;
        }

        if (partner != nullptr && selectedIds.find (partner->id) == selectedIds.end()
            && std::find_if (pitchSeamPartners.begin(), pitchSeamPartners.end(),
                             [&] (const auto& entry) { return entry.first == partner->id; })
                   == pitchSeamPartners.end())
            pitchSeamPartners.emplace_back (partner->id, partnerTail);
    }

    dragMode = DragMode::pitchHandle;
    dragActive = false;
    repaint();
}

void PianoGrid::beginTiltHandleDrag (const Note& note, bool tail)
{
    dragOriginals.clear();
    const auto* clip = currentClip();

    if (clip == nullptr)
        return;

    for (const auto& candidate : clip->notes)
        if (appState.isNoteSelected (candidate.id))
            dragOriginals.push_back (candidate);

    if (dragOriginals.empty())
        dragOriginals.push_back (note);

    tiltHandleNoteId = note.id;
    tiltHandleIsTail = tail;
    tiltOriginal = tail ? note.tiltEnd : note.tiltStart;
    tiltPreview = tiltOriginal;
    dragMode = DragMode::tiltHandle;
    dragActive = false;
    repaint();
}

const Note* PianoGrid::findLinkTargetAt (juce::Point<float> local, const Note* ignored,
                                         bool desiredTail, bool& targetTail) const
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return nullptr;

    const auto axis = makeAxis();
    const Note* best = nullptr;
    auto bestDistance = std::numeric_limits<float>::max();

    for (const auto& candidate : clip->notes)
    {
        if (candidate.id == ignored->id || candidate.muted || candidate.durationTicks <= 0)
            continue;

        const auto handle = endpointHandle (candidate, desiredTail, axis);

        if (! handle.expanded (6.0f).contains (local))
            continue;

        const auto distance = handle.getCentre().getDistanceFrom (local);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = &candidate;
            targetTail = desiredTail;
        }
    }

    return best;
}

void PianoGrid::updatePitchHandlePreview (juce::Point<float> local)
{
    const auto axis = makeAxis();
    const auto* dragged = findOriginalNote (pitchHandleNoteId);

    if (dragged == nullptr)
        return;

    const auto desiredTargetTail = ! pitchHandleIsTail;
    bool targetTail = desiredTargetTail;
    const auto* target = findLinkTargetAt (local, dragged, desiredTargetTail, targetTail);

    if (target != nullptr)
    {
        linkTargetNoteId = target->id;
        linkTargetIsTail = targetTail;
        linkTargetValid = true;
        linkTargetHz = targetTail ? target->effectiveTailHz() : target->frequencyHz;
        pitchHandlePreviewFactor = linkTargetHz > 0.0 && pitchHandleOriginalHz > 0.0
                                       ? linkTargetHz / pitchHandleOriginalHz : 1.0;
        pitchHandlePreviewTickDelta = 0;
        repaint();
        return;
    }

    linkTargetNoteId = 0;
    linkTargetValid = false;
    linkTargetHz = 0.0;

    const auto rawDelta = (juce::int64) std::llround (
        tickForX ((double) local.x) - (double) pitchHandlePointerStartTick);
    const auto originalEdgeTick = clipStartTick()
                                  + (pitchHandleIsTail ? dragged->endTick()
                                                       : dragged->startTick);
    auto wantedTick = originalEdgeTick + rawDelta;

    if (view().editorSnap.enabled)
        wantedTick = snappedTick ((double) wantedTick);

    auto timingDelta = wantedTick - originalEdgeTick;
    auto minimumDelta = std::numeric_limits<juce::int64>::min() / 4;
    auto maximumDelta = std::numeric_limits<juce::int64>::max() / 4;

    for (const auto& original : dragOriginals)
    {
        maximumDelta = juce::jmin (maximumDelta, original.durationTicks - 1);

        if (! pitchHandleIsTail)
            minimumDelta = juce::jmax (minimumDelta, -original.startTick);
    }

    if (const auto* clip = currentClip())
        for (const auto& [partnerId, partnerTail] : pitchSeamPartners)
            for (const auto& partner : clip->notes)
                if (partner.id == partnerId)
                {
                    maximumDelta = juce::jmin (maximumDelta,
                                               partner.durationTicks - 1);

                    if (! partnerTail)
                        minimumDelta = juce::jmax (minimumDelta,
                                                   -partner.startTick);
                    break;
                }

    if (minimumDelta <= maximumDelta)
        timingDelta = juce::jlimit (minimumDelta, maximumDelta, timingDelta);

    pitchHandlePreviewTickDelta = timingDelta;

    auto targetHz = axis.yToFrequency ((double) local.y);

    if (! frozenLines.empty())
    {
        const auto index = music::findNearestLineIndex (frozenLines, targetHz);

        if (index >= 0)
            targetHz = frozenLines[(size_t) index].hz;
    }
    else if (isPianoMode())
    {
        targetHz = music::piano::quantize (targetHz);
    }

    pitchHandlePreviewFactor = pitchHandleOriginalHz > 0.0
                                   ? targetHz / pitchHandleOriginalHz : 1.0;
    repaint();
}

void PianoGrid::updateTiltHandlePreview (juce::Point<float> local)
{
    const auto* primary = findOriginalNote (tiltHandleNoteId);

    if (primary == nullptr || primary->durationTicks <= 0)
        return;

    const auto* clip = currentClip();

    if (clip == nullptr)
        return;

    const auto absoluteStart = clipStartTick() + primary->startTick;
    const auto absoluteEnd = absoluteStart + primary->durationTicks;
    const auto tick = juce::jlimit ((double) absoluteStart, (double) absoluteEnd,
                                    tickForX ((double) local.x));
    const auto snapped = view().editorSnap.enabled ? (double) snappedTick (tick) : tick;
    auto value = (float) juce::jlimit (0.0, 1.0,
                                       (snapped - (double) absoluteStart)
                                           / (double) primary->durationTicks);

    if (tiltHandleIsTail)
        value = juce::jmax (primary->tiltStart + 0.001f, value);
    else
        value = juce::jmin (primary->tiltEnd - 0.001f, value);

    tiltPreview = juce::jlimit (0.0f, 1.0f, value);
    repaint();
}

void PianoGrid::commitPitchHandleDrag()
{
    const auto clipId = appState.selectedClipId;
    const auto originals = dragOriginals;
    const auto handleId = pitchHandleNoteId;
    const auto tail = pitchHandleIsTail;
    const auto factor = pitchHandlePreviewFactor;
    const auto tickDelta = pitchHandlePreviewTickDelta;
    const auto targetId = linkTargetNoteId;
    const auto targetTail = linkTargetIsTail;

    if (targetId != 0 && linkTargetValid)
    {
        if (const auto* dragged = findOriginalNote (handleId))
            commitLink (*dragged, targetTail);

        return;
    }

    if (std::abs (factor - 1.0) < 1.0e-12 && tickDelta == 0)
        return;

    document.applyEdit (NI_TXT ("调整音符端点"),
                        [clipId, originals, handleId, tail, factor, tickDelta,
                         partners = pitchSeamPartners] (Project& project)
    {
        auto* clip = project.findClip (clipId);

        if (clip == nullptr)
            return;

        for (auto& note : clip->notes)
        {
            const auto* original = [&]() -> const Note*
            {
                for (const auto& candidate : originals)
                    if (candidate.id == note.id)
                        return &candidate;
                return nullptr;
            }();

            if (original == nullptr)
                continue;

            if (tail)
            {
                const auto current = original->effectiveTailHz();
                note.tailFrequencyHz = juce::jlimit (kMinVisibleHz, kMaxVisibleHz,
                                                    current * factor);
                note.durationTicks = juce::jmax (
                    (juce::int64) 1,
                    original->endTick() + tickDelta - original->startTick);
            }
            else
            {
                note.frequencyHz = juce::jlimit (kMinVisibleHz, kMaxVisibleHz,
                                                 original->frequencyHz * factor);
                note.startTick = juce::jmax ((juce::int64) 0,
                                             original->startTick + tickDelta);
                note.durationTicks = juce::jmax (
                    (juce::int64) 1,
                    original->endTick() - note.startTick);
            }
        }

        for (const auto& [partnerId, partnerTail] : partners)
            for (auto& note : clip->notes)
                if (note.id == partnerId)
                {
                    const auto partnerOriginalEnd = note.endTick();

                    if (partnerTail)
                    {
                        note.tailFrequencyHz = juce::jlimit (
                            kMinVisibleHz, kMaxVisibleHz,
                            note.effectiveTailHz() * factor);
                        note.durationTicks = juce::jmax (
                            (juce::int64) 1,
                            note.durationTicks + tickDelta);
                    }
                    else
                    {
                        note.frequencyHz = juce::jlimit (
                            kMinVisibleHz, kMaxVisibleHz,
                            note.frequencyHz * factor);
                        note.startTick = juce::jmax ((juce::int64) 0,
                                                     note.startTick + tickDelta);
                        note.durationTicks = juce::jmax (
                            (juce::int64) 1,
                            partnerOriginalEnd - note.startTick);
                    }
                    break;
                }

        music::pruneInvalidNoteTies (*clip);
    });
}

void PianoGrid::commitTiltHandleDrag()
{
    if (std::abs (tiltPreview - tiltOriginal) < 1.0e-6f)
        return;

    const auto clipId = appState.selectedClipId;
    const auto originals = dragOriginals;
    const auto tail = tiltHandleIsTail;
    const auto value = tiltPreview;
    const auto delta = value - tiltOriginal;

    document.applyEdit (NI_TXT ("调整倾斜区间"),
                        [clipId, originals, tail, delta] (Project& project)
    {
        auto* clip = project.findClip (clipId);

        if (clip == nullptr)
            return;

        for (auto& note : clip->notes)
        {
            const auto* original = [&]() -> const Note*
            {
                for (const auto& candidate : originals)
                    if (candidate.id == note.id)
                        return &candidate;
                return nullptr;
            }();

            if (original == nullptr)
                continue;

            auto next = tail ? original->tiltEnd + delta : original->tiltStart + delta;

            if (tail)
                note.tiltEnd = juce::jlimit (original->tiltStart + 0.001f, 1.0f, next);
            else
                note.tiltStart = juce::jlimit (0.0f, original->tiltEnd - 0.001f, next);

        }
    });
}

void PianoGrid::commitLink (const Note& dragged, bool targetTail)
{
    const auto* clip = currentClip();
    const auto targetId = linkTargetNoteId;

    if (clip == nullptr || targetId == 0 || dragged.id == 0)
        return;

    Note target;
    auto found = false;

    for (const auto& note : clip->notes)
        if (note.id == targetId)
        {
            target = note;
            found = true;
            break;
        }

    if (! found)
        return;

    const auto clipId = appState.selectedClipId;
    const auto draggedId = dragged.id;
    const auto draggedTail = pitchHandleIsTail;
    const auto targetHz = targetTail ? target.effectiveTailHz() : target.frequencyHz;

    document.applyEdit (NI_TXT ("连接音符"),
                        [clipId, draggedId, targetId, draggedTail, targetTail, targetHz]
                        (Project& project)
    {
        auto* editClip = project.findClip (clipId);

        if (editClip == nullptr)
            return;

        Note* first = nullptr;
        Note* second = nullptr;

        for (auto& note : editClip->notes)
        {
            if (note.id == draggedId)
                first = &note;
            if (note.id == targetId)
                second = &note;
        }

        if (first == nullptr || second == nullptr || first == second)
            return;

        // Only opposite endpoints are legal.
        if (draggedTail == targetTail)
            return;

        for (auto& note : editClip->notes)
            if (note.id != second->id && note.tieFromNoteId == first->id)
                note.tieFromNoteId = 0;

        if (draggedTail)
        {
            const auto newDuration = second->startTick - first->startTick;

            if (newDuration <= 0)
                return;

            first->durationTicks = newDuration;
            first->tailFrequencyHz = targetHz;
            second->tieFromNoteId = first->id;
        }
        else
        {
            const auto newStart = second->endTick();
            const auto newEnd = first->endTick();

            if (newEnd <= newStart)
                return;

            first->startTick = newStart;
            first->durationTicks = newEnd - newStart;
            first->frequencyHz = targetHz;
            first->tieFromNoteId = second->id;
        }

        music::pruneInvalidNoteTies (*editClip);
    });
}

void PianoGrid::updateMovePreview (juce::Point<float> local, bool lockToDominantAxis)
{
    const auto axis = makeAxis();
    const auto startTickAtDown = tickForX ((double) dragStartLocal.x);
    const auto startTickNow = tickForX ((double) local.x);

    const auto snappedDelta = snappedTick (startTickNow) - snappedTick (startTickAtDown);
    auto tickDelta = snappedDelta;
    auto frequencyFactor = 1.0;

    // A drum track moves between pad rows instead of snapping to a frequency.
    if (isDrumTrack())
    {
        previewPadDelta = padForY (local.y) - padForY (dragStartLocal.y);

        if (lockToDominantAxis)
        {
            const auto dx = std::abs (local.x - dragStartLocal.x);
            const auto dy = std::abs (local.y - dragStartLocal.y);

            if (dy >= dx)
                tickDelta = 0;
            else
                previewPadDelta = 0;
        }

        previewTickDelta = music::clampTickDeltaForGroup (clipStartTick() + earliestOriginalStart,
                                                          tickDelta);
        hasPreviewTiming = false;
        return;
    }

    if (primaryOriginalHz > 0.0 && primaryOriginalTailHz > 0.0)
    {
        const auto pointerHz = axis.yToFrequency ((double) local.y);
        auto headDistance = std::numeric_limits<double>::max();
        auto tailDistance = std::numeric_limits<double>::max();
        auto headTarget = 0.0;
        auto tailTarget = 0.0;

        if (! frozenLines.empty())
        {
            const auto headIndex = music::findNearestLineIndex (frozenLines, pointerHz);
            const auto tailIndex = music::findNearestLineIndex (frozenLines, pointerHz);

            if (headIndex >= 0)
            {
                headTarget = frozenLines[(size_t) headIndex].hz;
                headDistance = std::abs (axis.frequencyToY (headTarget) - (double) local.y);
            }

            if (tailIndex >= 0)
            {
                tailTarget = frozenLines[(size_t) tailIndex].hz;
                tailDistance = std::abs (axis.frequencyToY (tailTarget) - (double) local.y);
            }
        }
        else if (isPianoMode())
        {
            headTarget = music::piano::quantize (pointerHz);
            tailTarget = headTarget;
            headDistance = std::abs (axis.frequencyToY (headTarget) - (double) local.y);
            tailDistance = std::abs (axis.frequencyToY (tailTarget) - (double) local.y);
        }

        const auto headOriginalDistance = std::abs (
            axis.frequencyToY (primaryOriginalHz) - (double) local.y);
        const auto tailOriginalDistance = std::abs (
            axis.frequencyToY (primaryOriginalTailHz) - (double) local.y);
        const auto headWins = headDistance < tailDistance
                              || (std::abs (headDistance - tailDistance) < 1.0e-9
                                  && headOriginalDistance <= tailOriginalDistance);

        if (headTarget > 0.0 && headWins)
            frequencyFactor = headTarget / primaryOriginalHz;
        else if (tailTarget > 0.0)
            frequencyFactor = tailTarget / primaryOriginalTailHz;
    }

    if (lockToDominantAxis)
    {
        const auto dx = std::abs (local.x - dragStartLocal.x);
        const auto dy = std::abs (local.y - dragStartLocal.y);

        if (dx >= dy)
            frequencyFactor = 1.0;
        else
            tickDelta = 0;
    }

    // A linked neighbour follows only the seam edge. Keep that outer edge legal
    // while the dragged note moves across it.
    if (bodySeamFollowers.size() > 0)
    {
        auto minimumDelta = std::numeric_limits<juce::int64>::min() / 4;
        auto maximumDelta = std::numeric_limits<juce::int64>::max() / 4;

        for (const auto& follower : bodySeamFollowers)
        {
            const auto* anchor = [&]() -> const Note*
            {
                for (const auto& original : dragOriginals)
                    if (original.id == follower.anchorNoteId)
                        return &original;

                return nullptr;
            }();

            if (anchor == nullptr)
                continue;

            if (follower.followsStart)
            {
                maximumDelta = juce::jmin (
                    maximumDelta,
                    follower.note.endTick() - anchor->endTick() - 1);
            }
            else
            {
                minimumDelta = juce::jmax (
                    minimumDelta,
                    follower.note.startTick - anchor->startTick + 1);
            }
        }

        if (minimumDelta <= maximumDelta)
            tickDelta = juce::jlimit (minimumDelta, maximumDelta, tickDelta);
    }

    // The clamp keeps notes from moving before tick zero, so it has to work on
    // absolute time even though the note positions are clip-relative.
    previewTickDelta = music::clampTickDeltaForGroup (clipStartTick() + earliestOriginalStart,
                                                      tickDelta);
    previewFrequencyFactor = frequencyFactor;
    hasPreviewTiming = false;
}

void PianoGrid::updateResizePreview (juce::Point<float> local)
{
    const auto rawTick = tickForX ((double) local.x);
    const auto rounded = (juce::int64) std::llround (juce::jmax (0.0, rawTick));
    const auto gridTicks = snapGridTicksAt (rounded);
    // 不吸附: the edge follows the pointer exactly; the subdivision is still the
    // shortest the note may become.
    const auto newTick = view().editorSnap.enabled ? music::TempoMap::snapToGrid (rounded, gridTicks)
                                            : rounded;
    const auto minDuration = (juce::int64) std::llround (gridTicks);

    // The dragged edge moves the whole selection: every selected note's edge moves
    // by the same amount, so the delta is clamped once for the group (a note may not
    // get shorter than one grid step, and a left edge may not cross tick zero).
    const auto origin = clipStartTick();
    const auto rawTiming = (dragMode == DragMode::resizingStart)
                               ? music::resizeNoteStart (resizeOriginalEnd, newTick, minDuration)
                               : music::resizeNoteEnd (resizeOriginalStart, newTick, minDuration);
    auto delta = (dragMode == DragMode::resizingStart)
                     ? rawTiming.startTick - resizeOriginalStart
                     : rawTiming.endTick() - resizeOriginalEnd;

    for (const auto& note : dragOriginals)
    {
        const auto noteStart = origin + note.startTick;
        const auto noteEnd   = origin + note.endTick();

        if (dragMode == DragMode::resizingStart)
        {
            delta = juce::jmin (delta, (noteEnd - minDuration) - noteStart);
            delta = juce::jmax (delta, (juce::int64) 0 - noteStart);
        }
        else
        {
            delta = juce::jmax (delta, (noteStart + minDuration) - noteEnd);
        }
    }

    if (dragMode == DragMode::resizingStart && hasResizePredecessor)
        delta = juce::jmax (delta, minDuration - resizePredecessorOriginal.durationTicks);

    previewResizeDelta = delta;
    previewTiming = (dragMode == DragMode::resizingStart)
                        ? music::resizeNoteStart (resizeOriginalEnd, resizeOriginalStart + delta,
                                                  minDuration)
                        : music::resizeNoteEnd (resizeOriginalStart, resizeOriginalEnd + delta,
                                                minDuration);

    previewTickDelta = 0;
    previewFrequencyFactor = 1.0;
    hasPreviewTiming = true;
}

void PianoGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto area = gridArea();
    const auto point = e.position;

    dragActive = false;
    dragMode = DragMode::none;

    // An audio clip is read only here: there are no notes to write, drag or select.
    if (const auto* clip = currentClip(); clip != nullptr && clip->isAudio())
        return;

    if (! area.toFloat().contains (point))
        return;

    const juce::Point<float> local (point.x - (float) area.getX(), point.y - (float) area.getY());
    dragStartLocal = local;

    const auto hit = hitTestNote (local);

    // Nothing can be written or selected until a clip is open in the editor.
    if (currentClip() == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        if (hit.hit == NoteHit::linkBadge && hit.note != nullptr)
        {
            const auto clipId = appState.selectedClipId;
            const auto noteId = hit.note->id;
            document.applyEdit (NI_TXT ("取消音符连接"), [clipId, noteId] (Project& project)
            {
                if (auto* clip = project.findClip (clipId))
                    for (auto& note : clip->notes)
                        if (note.id == noteId)
                            note.tieFromNoteId = 0;
            });
            repaint();
            return;
        }

        const auto* note = findNoteAt (local);

        if (note == nullptr)
            return;

        // A right click on a note that is part of a multiple selection removes the
        // whole selection (one edit) - "select several notes, right click one of
        // them, they all go". A note outside the selection still goes on its own.
        if (appState.selectedNoteIds.size() > 1 && appState.isNoteSelected (note->id))
            deleteSelectedNotes();
        else
            deleteNote (*note);

        return;
    }

    if (hit.note != nullptr)
    {
        const auto noteId = hit.note->id;

        if (hit.hit == NoteHit::headHandle || hit.hit == NoteHit::tailHandle)
        {
            if (! appState.isNoteSelected (noteId))
                appState.selectNote (noteId, e.mods.isCommandDown());

            beginPitchHandleDrag (*hit.note, hit.hit == NoteHit::tailHandle);
            return;
        }

        if (hit.hit == NoteHit::tiltStartHandle || hit.hit == NoteHit::tiltEndHandle)
        {
            if (! appState.isNoteSelected (noteId))
                appState.selectNote (noteId, e.mods.isCommandDown());

            beginTiltHandleDrag (*hit.note, hit.hit == NoteHit::tiltEndHandle);
            return;
        }

        // Ctrl toggles multi selection; Shift is reserved for axis locking while dragging.
        if (e.mods.isCommandDown() && hit.hit == NoteHit::body)
            appState.selectNote (noteId, true);
        else if (! appState.isNoteSelected (noteId))
            appState.selectNote (noteId, false);

        if (handleReferencePick (hit.note->frequencyHz))
            return;

        // Alt drag on a drum note changes its velocity; on a tonal note Alt is
        // unused, so it falls through to the normal move.
        if (isDrumTrack() && e.mods.isAltDown())
        {
            dragMode = DragMode::velocity;
            velocityNoteId = noteId;
            velocityOriginal = hit.note->velocity;
            previewVelocity = velocityOriginal;
            repaint();
            return;
        }

        if (appState.isNoteSelected (noteId))
            beginNoteDrag (hit);

        return;
    }

    if (isDrumTrack())
    {
        // A drum row is a write target, but which gesture this is only becomes
        // clear on the first movement: horizontally it writes a hit, vertically
        // it is a rubber band. So remember the row and wait (see replayDragAt).
        dragMode = DragMode::pendingWrite;
        dragPendingCtrl = e.mods.isCommandDown();
        dragPad = padForY (local.y);
        dragAnchorTick = snappedTick (tickForX ((double) local.x));
        dragCurrentTick = dragAnchorTick;
        repaint();
        return;
    }

    if (isPianoMode())
    {
        // Every point of a piano roll is a write target: the row under the pointer
        // decides the pitch (quantized to the semitone on the way in), and the
        // horizontal / vertical split decides whether this drag writes a note or
        // rubber-bands, exactly like the drum grid and the reference lines.
        dragMode = DragMode::pendingWrite;
        dragPendingCtrl = e.mods.isCommandDown();
        dragFrequency = music::piano::quantize (makeAxis().yToFrequency ((double) local.y));
        dragAnchorTick = snappedTick (tickForX ((double) local.x));
        dragCurrentTick = dragAnchorTick;
        repaint();
        return;
    }

    if (const auto line = findReferenceLineAt (local.y))
    {
        // Same rule in the pitch grid: from a reference line, a horizontal drag
        // writes a note and a vertical one rubber-bands the selection.
        dragMode = DragMode::pendingWrite;
        dragPendingCtrl = e.mods.isCommandDown();
        dragFrequency = line->hz;
        dragAnchorTick = snappedTick (tickForX ((double) local.x));
        dragCurrentTick = dragAnchorTick;
        repaint();
        return;
    }

    // Empty space: start a rubber band selection. Ctrl keeps what was already
    // selected and unions the band into it.
    dragMode = DragMode::marquee;
    marqueeStart = local;
    marqueeCurrent = local;
    marqueeAnchorTick = tickForX ((double) local.x);
    marqueeAnchorHz = makeAxis().yToFrequency ((double) local.y);
    marqueeBaseline = e.mods.isCommandDown() ? appState.selectedNoteIds : std::set<juce::int64>();

    if (! e.mods.isCommandDown())
        appState.clearNotes();

    repaint();
}

juce::Rectangle<float> PianoGrid::marqueeBand() const
{
    auto anchor = marqueeStart;

    // Both corners have to live in the same space as the notes they are compared
    // against, and that space slides: the anchor is therefore stored as a tick and
    // a frequency and mapped back through the view in force right now.
    anchor.x = (float) xForTick (marqueeAnchorTick);

    // A drum track's vertical axis is the twelve pads, which always fit: there the
    // press row is already a content position and nothing ever slides it.
    if (! isDrumTrack())
        anchor.y = (float) makeAxis().frequencyToY (marqueeAnchorHz);

    return juce::Rectangle<float> (anchor, marqueeCurrent);
}

void PianoGrid::updateMarqueeSelection()
{
    const auto* clip = currentClip();

    if (clip == nullptr)
        return;

    const auto axis = makeAxis();
    const auto rectangle = marqueeBand();
    auto hits = marqueeBaseline;

    for (const auto& note : clip->notes)
        if (noteRect (note, axis).intersects (rectangle))
            hits.insert (note.id);

    if (hits != appState.selectedNoteIds)
        appState.setSelectedNotes (std::move (hits));
}

void PianoGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    const auto area = gridArea();
    const juce::Point<float> local (e.position.x - (float) area.getX(),
                                    e.position.y - (float) area.getY());

    if (! dragActive && (local - dragStartLocal).getDistanceFromOrigin() > 3.0f)
        dragActive = true;

    dragShiftDown = e.mods.isShiftDown();

    // A drag is under way: keep a repeating tick so the view scrolls when the
    // pointer is held against an edge.
    if (dragMode != DragMode::none)
        startTimerHz (30);

    replayDragAt (local);
}

void PianoGrid::replayDragAt (juce::Point<float> local)
{
    dragLastLocal = local;

    // A drag that started on a write target is still undecided: the first real
    // movement picks the gesture, and it never changes afterwards. The same
    // dominant-axis test the Shift axis lock uses: |dy| >= |dx| counts as
    // vertical, so an exact 45 degrees rubber-bands.
    if (dragMode == DragMode::pendingWrite)
    {
        const auto dx = std::abs (local.x - dragStartLocal.x);
        const auto dy = std::abs (local.y - dragStartLocal.y);

        // Same threshold as dragActive (Euclidean 3 px), so the moment a drag is
        // "real" it also has a direction and never falls through undecided.
        if ((local - dragStartLocal).getDistanceFromOrigin() > 3.0f)
        {
            if (dy >= dx)
            {
                dragMode = DragMode::marquee;
                marqueeStart = dragStartLocal;
                marqueeCurrent = local;
                marqueeAnchorTick = tickForX ((double) dragStartLocal.x);
                marqueeAnchorHz = makeAxis().yToFrequency ((double) dragStartLocal.y);
                marqueeBaseline = dragPendingCtrl ? appState.selectedNoteIds
                                                  : std::set<juce::int64>();

                if (! dragPendingCtrl)
                    appState.clearNotes();
            }
            else
            {
                dragMode = DragMode::creatingNote;
            }
        }
    }

    if (dragMode == DragMode::creatingNote)
    {
        dragCurrentTick = snappedTick (tickForX ((double) local.x));

        // The pad was fixed on the press: a horizontal drag writes the row it
        // started in, and a vertical one became a rubber band instead, so there
        // is no "slide to another drum" while writing any more.

        repaint();
        return;
    }

    if (dragMode == DragMode::velocity)
    {
        // Up is louder. Three pixels per velocity step feels about right.
        const auto delta = (int) std::round ((dragStartLocal.y - local.y) / 3.0f);
        previewVelocity = juce::jlimit (0, 127, velocityOriginal + delta);
        repaint();
        return;
    }

    if (dragMode == DragMode::marquee)
    {
        marqueeCurrent = local;

        if (dragActive)
            updateMarqueeSelection();

        repaint();
        return;
    }

    if (! dragActive)
        return;

    // A pending drag that never crossed the direction threshold has nothing to
    // preview (it will commit as a click on mouse up).
    if (dragMode == DragMode::pendingWrite)
        return;

    if (dragMode == DragMode::pitchHandle)
        updatePitchHandlePreview (local);
    else if (dragMode == DragMode::tiltHandle)
        updateTiltHandlePreview (local);
    else if (dragMode == DragMode::movingNotes)
        updateMovePreview (local, dragShiftDown);
    else
        updateResizePreview (local);

    repaint();
}

void PianoGrid::timerCallback()
{
    if (! dragActive && dragMode == DragMode::none)
    {
        stopTimer();
        return;
    }

    updateAutoScroll();
}

bool PianoGrid::verticalScrollFollowsDrag() const
{
    // Only the gestures that move in pitch follow the top and bottom edge: a note
    // being moved, and a rubber band. Writing a note pins the row it started in -
    // the row *is* the pitch - and resizing a note is a time-axis gesture, so for
    // those two the view sliding vertically would only move the target out from
    // under the pointer. A drum track's vertical axis is the twelve pads and always
    // fits, so there is nothing to slide there either.
    if (isDrumTrack())
        return false;

    return dragMode == DragMode::movingNotes || dragMode == DragMode::marquee
           || dragMode == DragMode::pitchHandle;
}

void PianoGrid::updateAutoScroll()
{
    if (dragMode == DragMode::none || ! dragActive)
        return;   // nothing is being dragged, so there is nothing to keep going

    const auto area = gridArea();

    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    // The time axis: a drag held against an edge slides the timeline. The
    // edge/step gesture itself is shared with the arrangement.
    const auto pixels = (float) autoscroll::stepFor ((double) dragLastLocal.x, 0.0,
                                                     (double) area.getWidth());

    // The pitch axis: dragging a note up and down - or banding a selection - holds
    // against the top and bottom edge the same way, so a pitch that is not on
    // screen yet is still reachable.
    const auto verticalPixels = verticalScrollFollowsDrag()
                                    ? (float) autoscroll::stepFor ((double) dragLastLocal.y, 0.0,
                                                                   (double) area.getHeight())
                                    : 0.0f;

    if (pixels == 0.0f && verticalPixels == 0.0f)
        return;

    auto scrolled = false;

    if (pixels != 0.0f)
    {
        const auto currentOffset = view().horizontalOffsetTicks;
        const auto deltaTicks = pixels * kTicksPerQuarter
                                / juce::jmax (1.0, view().pixelsPerQuarter);
        const auto newOffset = juce::jmax (0.0, currentOffset + deltaTicks);

        if (! juce::approximatelyEqual (newOffset, currentOffset))
        {
            view().horizontalOffsetTicks = newOffset;
            scrolled = true;
        }
    }

    if (verticalPixels != 0.0f)
    {
        const auto pixelsPerOctave = (double) area.getHeight()
                                     / juce::jmax (0.05, view().octavesVisible);
        const auto newCentre = music::VerticalAxis::clampHz (
            view().centreHz * std::pow (2.0, -(double) verticalPixels / pixelsPerOctave));

        if (! juce::approximatelyEqual (newCentre, view().centreHz))
        {
            view().centreHz = newCentre;

            // The piano window's rows have to stay whole, and in the reference view
            // the snap targets are "the lines you can see": both are re-taken for
            // the view that just slid, otherwise a note could never be dragged to a
            // pitch that was off screen when the drag started.
            if (isPianoMode())
                alignPianoView();
            else
                frozenLines = currentReferenceLines();

            scrolled = true;
        }
    }

    if (! scrolled)
        return;

    updateScrollBars();

    // Replay the drag with the pointer pinned just inside the edge it is held
    // against, so it keeps following the mouse while the canvas slides underneath.
    const auto pin = (float) autoscroll::kEdgePixels * 0.5f;
    auto clamped = dragLastLocal;

    if (pixels != 0.0f)
        clamped.x = juce::jlimit (pin, juce::jmax (pin, (float) area.getWidth() - 1.0f),
                                  clamped.x);

    if (verticalPixels != 0.0f)
        clamped.y = juce::jlimit (pin, juce::jmax (pin, (float) area.getHeight() - 1.0f),
                                  clamped.y);

    replayDragAt (clamped);
}

void PianoGrid::mouseUp (const juce::MouseEvent&)
{
    // The drag is over however it ends. Leaving dragActive set would keep the
    // hover ratio arrow away until the next mouse-down - which is exactly what
    // happened after writing a note (writing is a drag) and then hovering a
    // target line.
    const auto wasDragging = dragActive;
    dragActive = false;
    stopTimer();

    if (dragMode == DragMode::pendingWrite)
    {
        // The pointer never left the click threshold: pressing a drum row or a
        // reference line and letting go still writes one subdivision-length note.
        dragMode = DragMode::none;
        commitNoteCreation (dragAnchorTick, dragAnchorTick);
        repaint();
        return;
    }

    if (dragMode == DragMode::creatingNote)
    {
        dragMode = DragMode::none;
        commitNoteCreation (juce::jmin (dragAnchorTick, dragCurrentTick),
                            juce::jmax (dragAnchorTick, dragCurrentTick));
        repaint();
        return;
    }

    const auto finished = dragMode;
    dragMode = DragMode::none;

    if (finished == DragMode::marquee)
    {
        // The selection was updated live while dragging; this catches the final
        // rectangle if the mouse was released mid-move.
        if (wasDragging)
            updateMarqueeSelection();

        repaint();
        return;
    }

    if (finished == DragMode::velocity)
    {
        commitNoteVelocity();
        velocityNoteId = 0;
        previewVelocity = velocityOriginal;
        repaint();
        return;
    }

    if (finished == DragMode::none || ! wasDragging)
    {
        repaint();
        return;
    }

    if (finished == DragMode::movingNotes)
        commitNoteMove();
    else if (finished == DragMode::pitchHandle)
        commitPitchHandleDrag();
    else if (finished == DragMode::tiltHandle)
        commitTiltHandleDrag();
    else
        commitNoteResize (finished == DragMode::resizingStart);

    previewTickDelta = 0;
    previewFrequencyFactor = 1.0;
    previewPadDelta = 0;
    hasPreviewTiming = false;
    dragOriginals.clear();
    resizeShiftOriginals.clear();
    bodySeamFollowers.clear();
    pitchSeamPartners.clear();
    hasResizePredecessor = false;
    frozenLines.clear();
    pitchHandleNoteId = 0;
    pitchHandlePreviewTickDelta = 0;
    tiltHandleNoteId = 0;
    linkTargetNoteId = 0;
    linkTargetValid = false;
    repaint();
}

void PianoGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto area = gridArea();
    const auto point = e.position;

    if (! area.toFloat().contains (point))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        updateRatioHover ({}, false);
        return;
    }

    const juce::Point<float> local (point.x - (float) area.getX(), point.y - (float) area.getY());
    updateRatioHover (local, true);

    const auto hit = hitTestNote (local);
    const auto oldHoverId = hoverHandleNoteId;
    const auto oldHoverTail = hoverHandleIsTail;
    const auto oldTiltId = hoverTiltNoteId;
    const auto oldTiltTail = hoverTiltIsTail;

    hoverHandleNoteId = (hit.hit == NoteHit::headHandle || hit.hit == NoteHit::tailHandle)
                            && hit.note != nullptr ? hit.note->id : 0;
    hoverHandleIsTail = hit.hit == NoteHit::tailHandle;
    hoverTiltNoteId = (hit.hit == NoteHit::tiltStartHandle
                       || hit.hit == NoteHit::tiltEndHandle)
                          && hit.note != nullptr ? hit.note->id : 0;
    hoverTiltIsTail = hit.hit == NoteHit::tiltEndHandle;

    if (oldHoverId != hoverHandleNoteId || oldHoverTail != hoverHandleIsTail
        || oldTiltId != hoverTiltNoteId || oldTiltTail != hoverTiltIsTail)
        repaint();

    if (hit.hit == NoteHit::headHandle || hit.hit == NoteHit::tailHandle)
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (hit.hit == NoteHit::tiltStartHandle || hit.hit == NoteHit::tiltEndHandle)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (hit.hit == NoteHit::linkBadge)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (hit.hit == NoteHit::leftEdge || hit.hit == NoteHit::rightEdge)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (hit.note != nullptr)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoGrid::mouseExit (const juce::MouseEvent&)
{
    hoverHandleNoteId = 0;
    hoverTiltNoteId = 0;
    if (hoveredLineHz != 0.0)
    {
        hoveredLineHz = 0.0;
        repaint();
    }
}

void PianoGrid::updateRatioHover (juce::Point<float> local, bool inside)
{
    double newHz = 0.0;
    auto newX = hoverMouseX;

    const auto* clip = currentClip();

    if (inside && ! dragActive && clip != nullptr && ! clip->isAudio())
    {
        const auto line = findReferenceLineAt (local.y);

        if (line.has_value() && music::describeRatioArrow (*line).visible)
        {
            newHz = line->hz;
            newX  = (int) std::round (local.x) + gridArea().getX();
        }
    }

    // Repaint when the hovered line changes, and while hovering also when the
    // pointer slides sideways: the arrow is drawn beside the pointer.
    if (newHz == hoveredLineHz && (newHz <= 0.0 || newX == hoverMouseX))
        return;

    hoveredLineHz = newHz;
    hoverMouseX   = newX;

    // While the 指示线 switch is off nothing is drawn, so the repaint would be
    // wasted - and the arrow still comes back the instant it is switched on
    // again, because the tracked line is left as it is.
    if (view().ratioArrowVisible)
        repaint();
}

music::RatioArrow PianoGrid::currentRatioArrow() const
{
    const auto* clip = currentClip();

    // Mid-drag the preview owns the canvas, an audio clip has no pitch axis, and
    // the 指示线 switch in the control panel can turn the arrow off entirely. The
    // piano window has no ratio targets at all, so no arrow there either.
    if (! view().ratioArrowVisible || dragActive || hoveredLineHz <= 0.0
        || clip == nullptr || clip->isAudio() || isPianoMode())
        return {};

    const auto lines = currentReferenceLines();
    const auto line = music::findLineNear (lines, hoveredLineHz);

    return line.has_value() ? music::describeRatioArrow (*line) : music::RatioArrow {};
}

PianoGrid::RatioArrowGeometry PianoGrid::ratioArrowGeometry() const
{
    RatioArrowGeometry geometry;

    const auto arrow = currentRatioArrow();

    if (! arrow.visible)
        return geometry;

    const auto area = gridArea();
    const auto width = area.getWidth();
    const auto height = area.getHeight();

    if (width < 60 || height < 40)
        return geometry;

    const auto axis = makeAxis();
    const auto headY = (float) axis.frequencyToY (arrow.headHz);
    const auto tailY = (float) axis.frequencyToY (arrow.tailHz);

    // A 1:9 arrow spans more than three octaves, so one end is often scrolled out
    // of view: clamp the shaft to the visible strip and keep drawing, which is
    // what says "the line this came from is further off".
    geometry.top    = juce::jlimit (0.0f, (float) height, juce::jmin (headY, tailY));
    geometry.bottom = juce::jlimit (0.0f, (float) height, juce::jmax (headY, tailY));
    geometry.headY  = headY;
    geometry.direction = arrow.direction;

    // The head is always on screen (the mouse is on the line), so an arrow whose
    // visible run is tiny would just be a blob: skip it.
    if (geometry.bottom - geometry.top < kRatioArrowMinLength
        || headY < 0.0f || headY > (float) height)
        return geometry;

    const auto localMouseX = (float) (hoverMouseX - area.getX());
    // The shaft sits on the pointer's own column; the clamp only keeps it inside
    // the canvas when the pointer is within a few pixels of an edge.
    geometry.shaftX = juce::jlimit (10.0f, (float) width - 10.0f, localMouseX);
    geometry.label = music::ratioArrowLabel (arrow);

    // Sized from the character count rather than a font measurement: the caption
    // mixes one CJK token with digits, and this estimate errs wide, which only
    // ever gives the plate a little more room than it needs.
    const auto plateWidth = 12.0f + (float) geometry.label.length() * 9.0f;
    auto plateX = geometry.shaftX - kRatioArrowLabelGap - plateWidth;

    if (plateX < 2.0f)
        plateX = geometry.shaftX + kRatioArrowLabelGapRight;

    plateX = juce::jlimit (2.0f, juce::jmax (2.0f, (float) width - plateWidth - 2.0f), plateX);

    // The caption belongs beside the arrow head, not at the middle of the shaft.
    const auto plateY = juce::jlimit (2.0f, (float) height - kRatioArrowLabelHeight - 2.0f,
                                      geometry.headY - kRatioArrowLabelHeight * 0.5f);
    geometry.plate = { plateX, plateY, plateWidth, kRatioArrowLabelHeight };
    geometry.labelBounds = geometry.plate.getSmallestIntegerContainer()
                               .translated (area.getX(), area.getY());

    // The arrow proper, without the caption: its top and bottom are the two lines
    // the arrow spans, which is what the self test checks.
    geometry.bounds = juce::Rectangle<float> (geometry.shaftX - kRatioArrowHeadWidth * 0.5f,
                                              geometry.top,
                                              kRatioArrowHeadWidth,
                                              geometry.bottom - geometry.top)
                          .getSmallestIntegerContainer()
                          .translated (area.getX(), area.getY());
    geometry.valid = true;
    return geometry;
}

juce::Rectangle<int> PianoGrid::getRatioArrowBoundsForTest() const
{
    return ratioArrowGeometry().bounds;
}

juce::Rectangle<int> PianoGrid::getRatioArrowLabelBoundsForTest() const
{
    return ratioArrowGeometry().labelBounds;
}

juce::int64 PianoGrid::getDragPreviewEndTickForTest() const
{
    // Mirrors paintDragPreview's "creatingNote" branch, so a test can compare the
    // preview against where the note really lands.
    if (dragMode == DragMode::creatingNote || dragMode == DragMode::pendingWrite)
        // Both are already absolute ticks.
        return juce::jmax (dragAnchorTick, dragCurrentTick);

    // A move preview is stored as a delta rather than as absolute timing.
    if (dragMode == DragMode::movingNotes && ! dragOriginals.empty())
    {
        auto end = (juce::int64) 0;

        for (const auto& note : dragOriginals)
            end = juce::jmax (end, note.endTick() + previewTickDelta);

        return clipStartTick() + end;
    }

    if (hasPreviewTiming)
        // previewTiming is absolute (see the resize drag setup).
        return previewTiming.endTick();

    return 0;
}

juce::int64 PianoGrid::getDragRawTickForTest() const
{
    return (juce::int64) std::llround (tickForX ((double) dragLastLocal.x));
}

double PianoGrid::getDragRawFrequencyForTest() const
{
    // Mirrors what updateMovePreview computed for the note being dragged: the
    // pointer's pitch snapped to the frozen reference lines (or to a semitone row
    // in the piano window), which is what the drop commits.
    if (dragMode != DragMode::movingNotes || primaryOriginalHz <= 0.0)
        return primaryOriginalHz;

    return music::scaledFrequency (primaryOriginalHz, previewFrequencyFactor);
}

juce::Rectangle<float> PianoGrid::getDragPreviewNoteRectForTest() const
{
    if (dragMode != DragMode::creatingNote && dragMode != DragMode::pendingWrite)
        return {};

    const auto start = juce::jmin (dragAnchorTick, dragCurrentTick);
    const auto end   = juce::jmax (dragAnchorTick, dragCurrentTick);

    Note preview;
    preview.startTick = juce::jmax ((juce::int64) 0, start - clipStartTick());
    preview.durationTicks = juce::jmax ((juce::int64) 1, end - start);
    preview.frequencyHz = isDrumTrack() ? drumPadBaseHz (dragPad) : dragFrequency;
    preview.pad = isDrumTrack() ? dragPad : -1;

    return noteRect (preview, makeAxis());
}

juce::String PianoGrid::getRatioArrowLabelForTest() const
{
    return music::ratioArrowLabel (currentRatioArrow());
}

void PianoGrid::commitNoteCreation (juce::int64 startTick, juce::int64 endTick)
{
    const auto gridTicks = snapGridTicksAt (startTick);
    // 不吸附: the note lands exactly where the pointer was. The subdivision still
    // gives a plain click its length, so it never writes a one-tick note.
    const auto snapping = view().editorSnap.enabled;
    auto start = snapping ? music::TempoMap::snapToGrid (startTick, gridTicks) : startTick;
    auto end   = snapping ? music::TempoMap::snapToGrid (endTick, gridTicks) : endTick;

    if (end <= start)
        end = start + (juce::int64) std::llround (gridTicks);

    juce::int64 newNoteId = 0;

    // start/end arrive as absolute ticks; notes are stored relative to the clip.
    const auto origin = clipStartTick();
    const auto clipId = appState.selectedClipId;
    const auto pad = dragPad;
    const auto drum = isDrumTrack();

    document.applyEdit (NI_TXT ("写入音符"), [this, start, end, origin, clipId, pad, drum, &newNoteId] (Project& project)
    {
        auto* clip = project.findClip (clipId);

        if (clip == nullptr)
            return;

        Note note;
        note.id            = project.nextNoteId++;
        note.startTick     = start - origin;
        note.durationTicks = juce::jmax ((juce::int64) 1, end - start);
        // On a drum track the pad picks the drum; the frequency is only a
        // readable placeholder there, so it takes the drum's base pitch.
        note.pad           = drum ? pad : -1;
        note.frequencyHz   = drum ? drumPadBaseHz (pad) : dragFrequency;
        note.tailFrequencyHz = note.frequencyHz;
        note.velocity      = 100;

        newNoteId = note.id;
        clip->notes.push_back (note);
    });

    if (newNoteId != 0 && ! appState.noteLock)
        appState.selectNote (newNoteId, false);

    if (newNoteId != 0)
    {
        // Writing a note should be audible: one short live note on this track's own
        // instrument (drum tracks hit the pad that was written).
        const auto hz = drum ? drumPadBaseHz (pad) : dragFrequency;
        auditionWrittenNote (hz, drum ? pad : -1);
    }
}

void PianoGrid::auditionWrittenNote (double frequencyHz, int pad)
{
    const auto* track = currentTrack();

    if (track == nullptr || frequencyHz <= 0.0)
        return;

    const auto trackId = track->id;

    // Auditions live in their own id space (negative, and far from anything else):
    // a live voice must never collide with the id of the note that was written.
    const auto liveId = (juce::int64) kAuditionIdBase - (juce::int64) ++auditionSerial;

    audio.noteOnTrack (trackId, liveId, frequencyHz, 100.0f, pad);

    juce::Component::SafePointer<PianoGrid> safeThis (this);
    juce::Timer::callAfterDelay (kAuditionMilliseconds, [safeThis, trackId, liveId]
    {
        if (safeThis != nullptr)
            safeThis->audio.noteOffTrack (trackId, liveId);
    });
}

bool PianoGrid::handleReferencePick (double noteFrequency)
{
    if (appState.referencePick == AppState::ReferencePick::none || ! appState.armedDotValid)
        return false;

    if (appState.referencePick == AppState::ReferencePick::reference)
    {
        // The first note click after arming establishes the association; further
        // selection changes just re-project onto the segment.
        if (! appState.referenceAnchorValid)
            appState.setReferenceAnchor (noteFrequency, appState.armedDotPosition);

        return true;
    }

    // Apply: pin the current reference group to this note.
    ReferenceLineGroup created;
    created.name = NI_TXT ("参考线组 ") + juce::String (appState.referenceGroupSerial + 1);
    created.anchorHz = noteFrequency;
    created.anchorPosition = appState.armedDotPosition;
    created.visible = true;

    // The frame dot (position 0) is part of every reference group.
    ReferenceDot frameDot;
    frameDot.position = 0.0;
    frameDot.colourIndex = document.getProject().referenceGroup.frameColourIndex;
    created.dots.push_back (frameDot);

    for (const auto& dot : document.getProject().referenceGroup.dots)
        created.dots.push_back (dot);

    juce::int64 newEntryId = 0;

    document.applyEdit (NI_TXT ("应用参考组"), [created, &newEntryId] (Project& project)
    {
        auto entry = music::makeGroupEntry (project.nextReferenceLineId++, created);
        newEntryId = entry.id;

        // A new reference line clears the scratch (unlocked) entries.
        music::clearUnlockedEntries (project.referenceLines, entry.id);
        project.referenceLines.push_back (std::move (entry));
    });

    if (newEntryId != 0)
        appState.selectReferenceLine (newEntryId);

    appState.referenceGroupSerial++;
    appState.pushReferenceLineHistory (created);
    appState.setReferencePick (AppState::ReferencePick::none);
    return true;
}

void PianoGrid::commitNoteMove()
{
    const auto nothingChanged = previewTickDelta == 0 && previewPadDelta == 0
                                && std::abs (previewFrequencyFactor - 1.0) < 1.0e-12;

    if (nothingChanged)
        return;

    const auto originals = dragOriginals;
    const auto tickDelta = previewTickDelta;
    const auto factor = previewFrequencyFactor;
    const auto padDelta = previewPadDelta;
    const auto clipId = appState.selectedClipId;
    const auto drum = isDrumTrack();

    document.applyEdit (NI_TXT ("移动音符"),
                        [clipId, originals, followers = bodySeamFollowers,
                         tickDelta, factor, padDelta, drum] (Project& project)
    {
        auto* clip = project.findClip (clipId);

        if (clip == nullptr)
            return;

        for (auto& note : clip->notes)
        {
            for (const auto& original : originals)
            {
                if (note.id != original.id)
                    continue;

                // Notes keep their relative intervals: everything is scaled by the
                // same factor and shifted by the same tick delta.
                note.startTick = juce::jmax ((juce::int64) 0, original.startTick + tickDelta);

                if (drum)
                {
                    // Vertical movement picks another drum rather than another pitch.
                    const auto from = original.pad < 0 ? 0 : original.pad;
                    const auto to = juce::jlimit (0, kDrumPads - 1, from + padDelta);
                    note.pad = to;
                    note.frequencyHz = drumPadBaseHz (to);
                }
                else
                {
                    music::transposeNotePitch (note, factor);
                }

                break;
            }
        }

        if (! drum)
            for (const auto& follower : followers)
            {
                const auto* anchor = [&]() -> const Note*
                {
                    for (const auto& original : originals)
                        if (original.id == follower.anchorNoteId)
                            return &original;

                    return nullptr;
                }();

                if (anchor == nullptr)
                    continue;

                const auto movedFollower = makeSeamFollowerForBodyDrag (
                    follower.note, *anchor, tickDelta, factor,
                    follower.followsStart);

                for (auto& note : clip->notes)
                    if (note.id == follower.note.id)
                    {
                        note.startTick = movedFollower.startTick;
                        note.durationTicks = movedFollower.durationTicks;

                        if (follower.followsStart)
                            note.frequencyHz = movedFollower.frequencyHz;
                        else
                            note.tailFrequencyHz = movedFollower.tailFrequencyHz;

                        break;
                    }
            }

        music::pruneInvalidNoteTies (*clip);
    });
}

void PianoGrid::commitNoteResize (bool fromStartEdge)
{
    if (! hasPreviewTiming)
        return;

    const auto clipId = appState.selectedClipId;
    const auto delta = previewResizeDelta;
    const auto originals = dragOriginals;

    document.applyEdit (fromStartEdge ? NI_TXT ("调整音符起点") : NI_TXT ("调整音符长度"),
                        [clipId, delta, fromStartEdge, originals,
                         hasPred = hasResizePredecessor,
                         predOriginal = resizePredecessorOriginal,
                         successors = resizeShiftOriginals] (Project& project)
    {
        auto* clip = project.findClip (clipId);

        if (clip == nullptr)
            return;

        // The whole selection changes length together - one undo step, exactly like
        // dragging a group of notes moves them together.
        for (const auto& original : originals)
        {
            for (auto& note : clip->notes)
            {
                if (note.id != original.id)
                    continue;

                if (fromStartEdge)
                {
                    note.startTick = juce::jmax ((juce::int64) 0,
                                                 original.startTick + delta);
                    note.durationTicks = juce::jmax ((juce::int64) 1,
                                                     original.endTick() - note.startTick);
                }
                else
                {
                    note.startTick = original.startTick;
                    note.durationTicks = juce::jmax ((juce::int64) 1,
                                                     original.endTick() + delta
                                                         - original.startTick);
                }

                break;
            }
        }

        std::set<juce::int64> selectedIds;

        for (const auto& original : originals)
            selectedIds.insert (original.id);

        if (fromStartEdge && hasPred
            && selectedIds.find (predOriginal.id) == selectedIds.end())
        {
            for (auto& note : clip->notes)
                if (note.id == predOriginal.id)
                {
                    note.durationTicks = juce::jmax (
                        (juce::int64) 1, predOriginal.durationTicks + delta);
                    break;
                }
        }
        else if (! fromStartEdge)
        {
            for (const auto& successor : successors)
            {
                if (selectedIds.find (successor.id) != selectedIds.end())
                    continue;

                for (auto& note : clip->notes)
                    if (note.id == successor.id)
                    {
                        note.startTick = juce::jmax ((juce::int64) 0,
                                                     successor.startTick + delta);
                        break;
                    }
            }
        }

        music::pruneInvalidNoteTies (*clip);
    });
}

void PianoGrid::commitNoteVelocity()
{
    if (velocityNoteId == 0 || previewVelocity == velocityOriginal)
        return;

    const auto noteId = velocityNoteId;
    const auto velocity = previewVelocity;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("调整力度"), [clipId, noteId, velocity] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            for (auto& note : clip->notes)
                if (note.id == noteId)
                    note.velocity = velocity;
    });
}

void PianoGrid::deleteNote (const Note& note)
{
    const auto noteId = note.id;
    const auto wasSelected = appState.isNoteSelected (noteId);
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("删除音符"), [clipId, noteId] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            music::removeNotesByIds (*clip, { noteId });
    });

    if (wasSelected)
        appState.selectNote (noteId, true);
}

void PianoGrid::deleteSelectedNotes()
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

void PianoGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto area = gridArea();
    const auto point = e.position;

    if (! area.toFloat().contains (point))
        return;

    const juce::Point<float> local (point.x - (float) area.getX(), point.y - (float) area.getY());

    const auto alt = e.mods.isAltDown();
    const auto shift = e.mods.isShiftDown();

    const auto rawDelta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY
                                                                             : wheel.deltaX;

    if (rawDelta == 0.0f)
        return;

    if (alt)
    {
        const auto factor = std::pow (2.0, (double) rawDelta * 0.6);

        if (shift)
        {
            // Horizontal zoom, anchored on the tick under the mouse.
            const auto anchorTick = tickForX ((double) local.x);
            view().pixelsPerQuarter = juce::jlimit (8.0, 2000.0, view().pixelsPerQuarter * factor);
            view().horizontalOffsetTicks = juce::jmax (0.0, anchorTick
                - (double) local.x * (double) kTicksPerQuarter / view().pixelsPerQuarter);
        }
        else
        {
            // Vertical zoom, anchored on the frequency under the mouse.
            const auto axis = makeAxis();
            const auto anchorHz = axis.yToFrequency ((double) local.y);

            view().octavesVisible = music::VerticalAxis::clampOctaves (view().octavesVisible / factor);

            const auto newPixelsPerOctave = (double) area.getHeight() / view().octavesVisible;
            const auto offsetFromCentre = ((double) area.getHeight() * 0.5 - (double) local.y)
                                          / newPixelsPerOctave;

            view().centreHz = music::VerticalAxis::clampHz (anchorHz * std::pow (2.0, -offsetFromCentre));
        }
    }
    else if (shift)
    {
        const auto visibleTicks = (double) area.getWidth() * (double) kTicksPerQuarter
                                  / juce::jmax (1.0, view().pixelsPerQuarter);

        view().horizontalOffsetTicks = juce::jmax (0.0, view().horizontalOffsetTicks
                                                        - (double) rawDelta * visibleTicks * 0.35);
    }
    else
    {
        view().centreHz = music::VerticalAxis::clampHz (
            view().centreHz * std::pow (2.0, (double) rawDelta * 0.6));
    }

    applyAutoFit();

    if (isPianoMode())
        alignPianoView();

    updateScrollBars();
    repaint();
}

} // namespace nidaw
