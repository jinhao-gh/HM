#pragma once

#include "Types.h"

#include <optional>

namespace hmdaw::music
{

/** Maps between a logarithmic frequency axis and screen rows.

    y  = centreY - pixelsPerOctave * log2 (hz / centreHz)
    hz = centreHz * 2 ^ ((centreY - y) / pixelsPerOctave)

    Because the mapping is logarithmic, equal frequency ratios are always the same
    distance apart (220 to 440 and 440 to 880 are identical), which is the whole
    point of the editor's vertical axis.
*/
struct VerticalAxis
{
    double centreHz       = 440.0;
    double octavesVisible = 4.0;
    double height         = 600.0;

    double pixelsPerOctave() const noexcept { return height / juce::jmax (0.01, octavesVisible); }
    double centreY() const noexcept         { return height * 0.5; }

    double frequencyToY (double hz) const noexcept;
    double yToFrequency (double y) const noexcept;

    double topHz() const noexcept    { return yToFrequency (0.0); }
    double bottomHz() const noexcept { return yToFrequency (height); }

    static double clampHz (double hz) noexcept;
    static double clampOctaves (double octaves) noexcept;
};

//==============================================================================
/** Pitch shape stored on one note.

    frequencyHz is the head. The tail falls back to the head when
    tailFrequencyHz is 0 (old projects). Outside tiltStart..tiltEnd the pitch is
    flat; inside it moves monotonically in log-frequency space.
*/
double pitchAt (const Note& note, double normalisedTime) noexcept;
void clampNotePitchShape (Note& note) noexcept;
void transposeNotePitch (Note& note, double factor) noexcept;
/** Copies the pitch function over the normalised sub-range into `destination`. */
void sliceNotePitchShape (const Note& source, double from, double to,
                          Note& destination) noexcept;

//==============================================================================
/** 12 tone equal temperament, which is what the piano roll lays out.

    The project's notes are absolute frequencies; this is only the grid the piano
    window snaps to. A4 = 440 Hz = MIDI 69, C4 = 60.
*/
namespace piano
{
    constexpr int semitonesPerOctave = 12;
    constexpr int lowestNote  = 0;
    constexpr int highestNote = 127;

    /** The frequency of a MIDI note. */
    double frequencyOf (int midiNote) noexcept;

    /** The MIDI note nearest a frequency, clamped into the MIDI range. */
    int noteForFrequency (double hz) noexcept;

    /** The frequency of the semitone nearest `hz`: what the piano roll writes. */
    double quantize (double hz) noexcept;

    /** The pitch class of a note, 0 = C. */
    inline int pitchClassOf (int midiNote) noexcept
    {
        return ((midiNote % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave;
    }

    /** True for the five black keys of an octave (C# D# F# G# A#). */
    inline bool isBlackKey (int midiNote) noexcept
    {
        switch (pitchClassOf (midiNote))
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: break;
        }

        return false;
    }

    /** The name of a note, e.g. "C4" or "F#3" (middle C = C4, like the rest of
        the app's audition keyboards). */
    juce::String noteName (int midiNote);
}

struct ReferenceLine
{
    double hz          = 0.0;
    bool   dashed      = false;   // own octave family of a non-octave ratio: dimmed, not clickable
    bool   interactive = true;
    bool   isTarget    = false;   // belongs to one of the two target families
    bool   isManual    = false;   // belongs to a hand-typed (「赫兹」) entry
    int    colourIndex = -1;      // >= 0 -> use the reference palette, -1 -> default colour for the kind
    bool   isGroup     = false;   // produced by an applied reference line group
    juce::int64 entryId = 0;      // which 参考线状态 produced it (0 == none)

    // Purely descriptive, never serialised: when this line is one of the two
    // target families of a single-note entry, these two say which one, so the
    // canvas can explain where the line came from.
    double ratio       = 0.0;     // the ratio that produced it; 0 when it is not a ratio target
    int    targetSide  = 0;       // +1 = the f0 * r family, -1 = the f0 / r family, 0 = neither
};

/** Every member of baseHz * 2^k that falls inside the visible range. */
std::vector<ReferenceLine> buildOctaveFamily (double baseHz,
                                              double topHz,
                                              double bottomHz,
                                              bool   dashed,
                                              bool   interactive,
                                              bool   isManual);

//==============================================================================
/** Collapses a value into [0, 1). */
double foldPosition (double position) noexcept;

/** Frequency ratio of a position inside the octave: 2^position, so [1, 2). */
double positionToRatio (double position) noexcept;

/** Position inside the octave for a frequency ratio, folded into [0, 1).
    ratioToPosition (3.0) == 0.585 and ratioToPosition (1.0 / 3.0) == 0.415. */
double ratioToPosition (double ratio) noexcept;

/** Adds a dot, folding the position and ignoring positions already taken.
    Returns true when a dot was actually added. */
bool addDotAt (ReferenceGroup& group, double position, int colourIndex,
               double tolerance = 1.0e-6);

/** Removes the dot nearest to position (within tolerance). */
bool removeDotAt (ReferenceGroup& group, double position, double tolerance = 1.0e-6);

/** Index of the dot nearest to position, or -1. Frame positions 0 and 1 are
    handled by the caller, not by this function. */
int findDotNear (const ReferenceGroup& group, double position, double tolerance) noexcept;

/** n-1 evenly spaced positions strictly between the two given positions.
    The positions are taken as given within [0, 1] and may be supplied in either
    order; the frame dots live at 0 and 1 so a full octave divides correctly. */
std::vector<double> dividePositions (double fromPosition, double toPosition, int parts);

/** True when the position is in the selection list.

    Comparison is exact, NOT folded: the two frame dots sit at 0 and 1, and
    folding them would make them the same entry, which makes it impossible to
    select both ends of the octave in order to divide it.
*/
bool isPositionSelected (const std::vector<double>& selected,
                         double position,
                         double tolerance = 1.0e-9) noexcept;

/** Adds the position to the selection, or removes it when already present. */
void toggleSelectedPosition (std::vector<double>& selected,
                             double position,
                             double tolerance = 1.0e-9);

/** Frequencies of every dot with the group pinned to anchorHz at anchorPosition. */
std::vector<double> referenceFrequencies (const std::vector<ReferenceDot>& dots,
                                          double anchorHz,
                                          double anchorPosition);

/** The frequency one ladder position stands for, once a group is pinned.

    `position` is a ladder position (an integer part is an octave index, the fraction
    is where it sits inside that octave); the result is the same number the canvas
    draws its line at, so the group editor can print it beside a dot without keeping
    a second copy of the rule.
*/
double groupFrequencyAt (double anchorHz, double anchorPosition, double position);

//==============================================================================
// Reference control panel ("参考线控制栏") - the four kinds of reference lines.

/** A 1st-kind entry for a hand-typed frequency. Its octave family is drawn. */
ReferenceLineEntry makeFixedHzEntry (juce::int64 id, double hz, const juce::String& name);

/** A 3rd-kind entry built from an applied reference group. */
ReferenceLineEntry makeGroupEntry (juce::int64 id, const ReferenceLineGroup& group);

/** Builds a reference group straight out of a note selection.

    The lowest note becomes the frame dot (position 0), so the group stores the
    intervals between the notes rather than absolute pitch: re-enabling it at
    another anchor reproduces the same ratios. Notes an octave away from the
    anchor fold onto position 0 and add no dot (the anchor's octave family
    already covers them). The name is left to the caller. Fewer than two notes
    gives an empty group.
*/
ReferenceLineGroup makeGroupFromNotes (const std::vector<Note>& notes);

/** A 2nd-kind entry for one selected note (own octaves + ratio targets).

    The ratio is the transient lens of the current selection, not note data.
*/
ReferenceLineEntry makeNoteRatioEntry (juce::int64 id, const Note& note, double ratio);

/** A 4th-kind entry for a multi-note selection (one octave family per note). */
ReferenceLineEntry makeMultiNoteEntry (juce::int64 id, const std::vector<Note>& notes);

/** The name a derived entry shows: the live frequency plus the ratio. */
juce::String derivedEntryName (ReferenceLineKind kind, const Note& note, double ratio);

/** Drops every unlocked entry except keepId and returns how many went.

    Unlocked entries are scratch: creating a new reference line clears them, which
    is what makes the panel feel like "what I am working on right now".
*/
int clearUnlockedEntries (std::vector<ReferenceLineEntry>& entries, juce::int64 keepId);

/** Moves one entry, clamped to the list. Returns where it ended up. */
int moveEntry (std::vector<ReferenceLineEntry>& entries, int from, int to);

/** The entries as they are actually drawn.

    Unlocked 2nd/4th entries take their frequencies from the current note selection
    (and drop out when the selection no longer matches their kind). Hidden entries
    and every entry but the soloed one drop out. Locked entries are returned as
    stored, so they stop following the selection.
*/
std::vector<ReferenceLineEntry> resolveReferenceEntries (const std::vector<ReferenceLineEntry>& entries,
                                                         const std::vector<Note>& selectedNotes,
                                                         double currentRatio,
                                                         juce::int64 soloEntryId);

/** Expands resolved entries into the lines the editor draws, deduplicated across
    entries and sorted by frequency. */
std::vector<ReferenceLine> buildReferenceLinesForEntries (const std::vector<ReferenceLineEntry>& resolved,
                                                          double topHz,
                                                          double bottomHz);

//==============================================================================
/** One note of a clip placed on the timeline, after looping and window cropping. */
struct ClipNotePlacement
{
    const Note* note      = nullptr;
    int         repeat    = 0;
    juce::int64 startTick = 0;   // absolute
    juce::int64 endTick   = 0;   // absolute
};

/** How many times a clip's content repeats inside its window (at least 1). */
int clipRepeatCount (const Clip& clip) noexcept;

/** Places every sounding note of a clip on the timeline.

    When the clip is looped the content is tiled once per period and only the
    notes inside one period take part; otherwise only the notes starting inside
    the window sound. Notes beyond the window are kept in the data but produce
    nothing, which is what makes "extend the clip to reveal more" work.
*/
std::vector<ClipNotePlacement> expandClip (const Clip& clip);

/** Erases every note of a clip whose id is in `ids`, and returns how many went.

    The one implementation behind "delete the selected notes": the Delete key and
    the notes' own right-click menu both come through here, so the two can never
    disagree about what a delete does.
*/
int removeNotesByIds (Clip& clip, const std::set<juce::int64>& ids);
/** Clears ties that no longer point at a unique, contiguous predecessor or
    that would form a cycle. */
void pruneInvalidNoteTies (Clip& clip);

/** First tick at or after desiredStart where a clip of that length fits. */
juce::int64 findFreeSlot (const InstrumentTrack& track,
                          juce::int64 desiredStart,
                          juce::int64 lengthTicks,
                          juce::int64 ignoreClipId = 0);

/** Appends `count` copies of a clip, laid out one after another after it.

    Each copy keeps the source's content, length and loop settings and its notes
    stay clip-relative, so the copies are independent of the source afterwards.
    A copy goes into the first free slot at or after the running end, so
    repeating never destroys what is already in the lane, and repeating again
    simply extends the run. `nextClipId` is advanced once per copy.

    Returns the ids of the new clips, in the order they were placed, so the
    caller can select them. An unknown clip id or a non-positive count does
    nothing and returns an empty list.
*/
std::vector<juce::int64> repeatClip (InstrumentTrack& track,
                                     juce::int64 sourceClipId,
                                     int count,
                                     juce::int64& nextClipId);

class TempoMap;   // defined below; splitClipAt only needs a reference to it

/** Cuts a clip in two at an absolute tick.

    The tick has to fall strictly inside the clip's window, at least `marginTicks`
    away from either end. `clip` keeps the left half (same id, name and loop
    settings) and `rightOut` receives the new right half with a fresh id and the
    same name.

    Notes that straddle the cut are split in two: the left half keeps the original
    id with a shortened length, the right half gets a new id. Notes are stored
    relative to their clip, so everything in the right half is rebased. An audio
    clip's right half has its in-file offset advanced by the cut's duration.
*/
bool splitClipAt (Clip& clip,
                  juce::int64 splitTick,
                  const TempoMap& tempoMap,
                  juce::int64 marginTicks,
                  juce::int64& nextNoteId,
                  juce::int64& nextClipId,
                  Clip& rightOut);

/** True when `next` can be glued onto the end of `clip`: it starts exactly where
    `clip` ends, and both are plain (unlooped) note clips. Audio clips are not
    glued - one clip can only hold one file. */
bool canGlueClips (const Clip& clip, const Clip& next);

/** Merges `next` into `clip`: the window grows to cover both and the notes of
    `next` are rebased onto `clip`'s origin with fresh ids. Returns false when
    canGlueClips() says no. */
bool glueClips (Clip& clip, const Clip& next, juce::int64& nextNoteId);

inline bool isOctaveRatio (double ratio) noexcept
{
    return std::abs (ratio - 2.0) < 1.0e-9;
}

/** Index of the member of lines whose frequency is nearest to hz, comparing in
    log space (identical to comparing on-screen distance on the log axis).
    Returns -1 when the list is empty. */
int findNearestLineIndex (const std::vector<ReferenceLine>& lines, double hz);

/** Timing of a note being dragged or resized. */
struct NoteTiming
{
    juce::int64 startTick     = 0;
    juce::int64 durationTicks = kTicksPerQuarter;

    juce::int64 endTick() const noexcept { return startTick + durationTicks; }
};

/** Drags the start edge: the end stays where it was, and the length never drops
    below minDurationTicks. */
NoteTiming resizeNoteStart (juce::int64 originalEndTick, juce::int64 newStartTick,
                            juce::int64 minDurationTicks);

/** Drags the end edge: the start stays where it was. */
NoteTiming resizeNoteEnd (juce::int64 originalStartTick, juce::int64 newEndTick,
                          juce::int64 minDurationTicks);

/** Largest tick delta that can be applied to a group of notes without any of them
    starting before tick zero. */
juce::int64 clampTickDeltaForGroup (juce::int64 earliestStartTick, juce::int64 deltaTicks);

/** Scales a frequency, clamped to the visible range. */
double scaledFrequency (double hz, double factor);

/** Parses "3", "1:3", "3:1" or "1/3" into a multiplier. Returns 0 when unparsable. */
double parseRatioText (const juce::String& text);

/** Formats a multiplier as a "1:3" style label. */
juce::String formatRatioText (double ratio);

/** The vertical arrow drawn while the mouse hovers one of a single-note entry's
    two ratio target families.

    The arrowhead sits on the line under the mouse; the tail sits on the one
    reference line of the base note that the ratio came from, i.e. either
    `headHz / ratio` (the f0 * r family) or `headHz * ratio` (the f0 / r family).
    Because the families are octave families, that tail is always the base note's
    own octave line, and the arrow is exactly log2 (ratio) octaves long.
*/
struct RatioArrow
{
    bool   visible   = false;
    double headHz    = 0.0;    // the target line the mouse is on
    double tailHz    = 0.0;    // the line the ratio came from
    double ratio     = 0.0;
    int    direction = 0;      // +1 the arrow points up (tail below), -1 it points down
};

/** Describes the arrow for a hovered line. Invisible unless that line really is a
    target family of a non-octave ratio. */
RatioArrow describeRatioArrow (const ReferenceLine& hoveredLine);

/** The caption beside the arrow: "正 1:3" when it points up, "负 1:3" when it points
    down. 正/负 follows the arrow's direction, not the multiplication itself. */
juce::String ratioArrowLabel (const RatioArrow& arrow);

/** A copy of the line whose frequency matches hz within tolerance, so callers can
    never end up holding a pointer into a temporary list. */
std::optional<ReferenceLine> findLineNear (const std::vector<ReferenceLine>& lines,
                                           double hz,
                                           double tolerance = 1.0e-6);

/** Converts between musical time and seconds, honouring the tempo / time signature map. */
class TempoMap
{
public:
    TempoMap();
    explicit TempoMap (const std::vector<TempoMarker>& markers);

    void rebuild (const std::vector<TempoMarker>& markers);

    double      bpmAt (juce::int64 tick) const;
    int         numeratorAt (juce::int64 tick) const;
    int         denominatorAt (juce::int64 tick) const;
    double      ticksPerBeatAt (juce::int64 tick) const;
    double      secondsAtTick (juce::int64 tick) const;
    juce::int64 tickAtSeconds (double seconds) const;

    void        tickToBarBeat (juce::int64 tick, int& bar, int& beat, int& tickInBeat) const;
    juce::int64 barBeatToTick (int bar, int beat) const;
    juce::int64 barStartTick (int bar) const;
    int         barAtTick (juce::int64 tick) const;
    juce::int64 nextBeatTick (juce::int64 tick) const;

    int segmentCount() const noexcept { return (int) segments.size(); }

    static juce::String formatPosition (int bar, int beat, int tickInBeat);
    static juce::int64  snapToGrid (juce::int64 tick, double gridTicks);

private:
    struct Segment
    {
        juce::int64 startTick    = 0;
        int         startBar     = 1;
        double      startSeconds = 0.0;
        double      ticksPerBeat = kTicksPerQuarter;
        int         numerator    = 4;
        int         denominator  = 4;
        double      bpm          = 120.0;

        double secondsPerTick() const noexcept
        {
            return 60.0 / (juce::jmax (1.0, bpm) * juce::jmax (1.0, ticksPerBeat));
        }

        double ticksPerBar() const noexcept
        {
            return ticksPerBeat * (double) juce::jmax (1, numerator);
        }
    };

    int segmentIndexFor (juce::int64 tick) const;

    std::vector<Segment> segments;
};

/** One beat of the metronome grid. */
struct MetronomeClick
{
    juce::int64 tick     = 0;
    bool        downbeat = false;   // first beat of a bar -> accented click
};

/** The first click at or after tick (the downbeat of a bar, or a plain beat). */
MetronomeClick firstClickAtOrAfter (const TempoMap& tempoMap, juce::int64 tick);

/** The first click strictly after tick: what the click cursor walks by. */
MetronomeClick nextClickAfter (const TempoMap& tempoMap, juce::int64 tick);

} // namespace hmdaw::music
