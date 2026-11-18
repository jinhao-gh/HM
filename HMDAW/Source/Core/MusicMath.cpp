#include "MusicMath.h"

namespace hmdaw::music
{

double pitchAt (const Note& note, double normalisedTime) noexcept
{
    const auto head = VerticalAxis::clampHz (note.frequencyHz);
    const auto tail = VerticalAxis::clampHz (
        note.tailFrequencyHz > 0.0 ? note.tailFrequencyHz : note.frequencyHz);
    const auto start = juce::jlimit (0.0, 0.999, (double) note.tiltStart);
    const auto end = juce::jlimit (start, 1.0, (double) note.tiltEnd);
    const auto t = juce::jlimit (0.0, 1.0, normalisedTime);

    if (t <= start)
        return head;

    if (t >= end || end - start <= 1.0e-12)
        return tail;

    const auto u = (t - start) / (end - start);
    const auto curve = juce::jlimit (-1.0, 1.0, (double) note.pitchCurve);
    const auto eased = juce::jlimit (0.0, 1.0, u + curve * u * (1.0 - u));
    return head * std::pow (tail / head, eased);
}

void clampNotePitchShape (Note& note) noexcept
{
    note.frequencyHz = VerticalAxis::clampHz (note.frequencyHz);

    if (note.tailFrequencyHz > 0.0)
        note.tailFrequencyHz = VerticalAxis::clampHz (note.tailFrequencyHz);

    note.pitchCurve = (float) juce::jlimit (-1.0, 1.0, (double) note.pitchCurve);
    note.tiltStart = (float) juce::jlimit (0.0, 0.999, (double) note.tiltStart);
    note.tiltEnd = (float) juce::jlimit ((double) note.tiltStart, 1.0,
                                         (double) note.tiltEnd);

    if (note.tiltEnd <= note.tiltStart)
        note.tiltEnd = juce::jmin (1.0f, note.tiltStart + 0.001f);
}

void transposeNotePitch (Note& note, double factor) noexcept
{
    if (! (factor > 0.0))
        return;

    note.frequencyHz = VerticalAxis::clampHz (note.frequencyHz * factor);

    if (note.tailFrequencyHz > 0.0)
        note.tailFrequencyHz = VerticalAxis::clampHz (note.tailFrequencyHz * factor);
}

void sliceNotePitchShape (const Note& source, double from, double to,
                          Note& destination) noexcept
{
    const auto u0 = juce::jlimit (0.0, 0.999999, from);
    const auto u1 = juce::jlimit (u0 + 1.0e-9, 1.0, to);
    destination.frequencyHz = pitchAt (source, u0);
    destination.tailFrequencyHz = pitchAt (source, u1);

    const auto sourceStart = juce::jlimit (0.0, 0.999, (double) source.tiltStart);
    const auto sourceEnd = juce::jlimit (sourceStart, 1.0, (double) source.tiltEnd);
    const auto span = u1 - u0;
    const auto sourceTiltSpan = juce::jmax (1.0e-12, sourceEnd - sourceStart);
    const auto overlapStart = juce::jmax (u0, sourceStart);
    const auto overlapEnd = juce::jmin (u1, sourceEnd);

    if (overlapEnd <= overlapStart || span <= 1.0e-12)
    {
        destination.tiltStart = 0.0f;
        destination.tiltEnd = 1.0f;
        destination.pitchCurve = 0.0f;
        destination.tailFrequencyHz = destination.frequencyHz;
        return;
    }

    destination.tiltStart = (float) juce::jlimit (0.0, 0.999,
                                                  (overlapStart - u0) / span);
    destination.tiltEnd = (float) juce::jlimit ((double) destination.tiltStart, 1.0,
                                                (overlapEnd - u0) / span);

    const auto curveStart = juce::jlimit (0.0, 1.0,
                                          (overlapStart - sourceStart) / sourceTiltSpan);
    const auto curveEnd = juce::jlimit (curveStart, 1.0,
                                        (overlapEnd - sourceStart) / sourceTiltSpan);
    const auto d = curveEnd - curveStart;
    const auto c = (double) juce::jlimit (-1.0f, 1.0f, source.pitchCurve);
    const auto slopeAtStart = 1.0 + c * (1.0 - 2.0 * curveStart);
    const auto denominator = slopeAtStart - c * d;
    destination.pitchCurve = denominator > 1.0e-12
                                 ? (float) juce::jlimit (-1.0, 1.0, c * d / denominator)
                                 : 0.0f;
}

double VerticalAxis::clampHz (double hz) noexcept
{
    return juce::jlimit (kMinVisibleHz, kMaxVisibleHz, hz);
}

double VerticalAxis::clampOctaves (double octaves) noexcept
{
    return juce::jlimit (0.5, 10.0, octaves);
}

double VerticalAxis::frequencyToY (double hz) const noexcept
{
    const auto safeHz = clampHz (hz);
    const auto centre = clampHz (centreHz);

    return centreY() - pixelsPerOctave() * std::log2 (safeHz / centre);
}

double VerticalAxis::yToFrequency (double y) const noexcept
{
    const auto centre = clampHz (centreHz);

    return clampHz (centre * std::pow (2.0, (centreY() - y) / pixelsPerOctave()));
}

namespace piano
{
    double frequencyOf (int midiNote) noexcept
    {
        return 440.0 * std::pow (2.0, (juce::jlimit (lowestNote, highestNote, midiNote) - 69) / 12.0);
    }

    int noteForFrequency (double hz) noexcept
    {
        if (! (hz > 0.0))
            hz = 440.0;

        return juce::jlimit (lowestNote, highestNote,
                             (int) std::llround (69.0 + 12.0 * std::log2 (hz / 440.0)));
    }

    double quantize (double hz) noexcept
    {
        return frequencyOf (noteForFrequency (hz));
    }

    juce::String noteName (int midiNote)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

        const auto note = juce::jlimit (lowestNote, highestNote, midiNote);
        // Middle C (MIDI 60) is C4, the same octave numbering the audition
        // keyboards use.
        const auto octave = note / 12 - 1;

        return juce::String (names[pitchClassOf (note)]) + juce::String (octave);
    }
} // namespace piano

std::vector<ReferenceLine> buildOctaveFamily (double baseHz,
                                              double topHz,
                                              double bottomHz,
                                              bool   dashed,
                                              bool   interactive,
                                              bool   isManual)
{
    std::vector<ReferenceLine> lines;

    if (baseHz <= 0.0 || topHz <= 0.0 || bottomHz <= 0.0 || topHz <= bottomHz)
        return lines;

    const auto lowestExponent  = (int) std::ceil (std::log2 (bottomHz / baseHz) - 1.0e-9);
    const auto highestExponent = (int) std::floor (std::log2 (topHz / baseHz) + 1.0e-9);

    if (highestExponent < lowestExponent)
        return lines;

    if (highestExponent - lowestExponent + 1 > kMaxReferenceLines)
        return lines;

    lines.reserve ((size_t) (highestExponent - lowestExponent + 1));

    for (int k = lowestExponent; k <= highestExponent; ++k)
    {
        const auto hz = baseHz * std::pow (2.0, (double) k);

        if (hz >= bottomHz * 0.999999 && hz <= topHz * 1.000001)
        {
            ReferenceLine line;
            line.hz          = hz;
            line.dashed      = dashed;
            line.interactive = interactive;
            line.isManual    = isManual;
            lines.push_back (line);
        }
    }

    return lines;
}

//==============================================================================
int clipRepeatCount (const Clip& clip) noexcept
{
    if (! clip.looped)
        return 1;

    const auto period = juce::jmax ((juce::int64) 1, clip.loopLengthTicks);
    return (int) juce::jmax ((juce::int64) 1, (clip.lengthTicks + period - 1) / period);
}

std::vector<ClipNotePlacement> expandClip (const Clip& clip)
{
    std::vector<ClipNotePlacement> placements;

    if (clip.lengthTicks <= 0)
        return placements;

    const auto period = clip.looped ? juce::jmax ((juce::int64) 1, clip.loopLengthTicks)
                                    : clip.lengthTicks;
    const auto repeats = clipRepeatCount (clip);
    const auto windowEnd = clip.endTick();

    placements.reserve (clip.notes.size() * (size_t) repeats);

    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        const auto offset = clip.startTick + (juce::int64) repeat * period;

        for (const auto& note : clip.notes)
        {
            // While looping, only the content of one period takes part.
            if (clip.looped && note.startTick >= period)
                continue;

            const auto start = offset + note.startTick;

            if (start < clip.startTick || start >= windowEnd)
                continue;

            const auto end = juce::jmin (start + juce::jmax ((juce::int64) 1, note.durationTicks),
                                         windowEnd);

            if (end <= start)
                continue;

            placements.push_back ({ &note, repeat, start, end });
        }
    }

    std::sort (placements.begin(), placements.end(),
               [] (const ClipNotePlacement& a, const ClipNotePlacement& b)
               { return a.startTick < b.startTick; });

    return placements;
}

int removeNotesByIds (Clip& clip, const std::set<juce::int64>& ids)
{
    if (ids.empty())
        return 0;

    auto& notes = clip.notes;
    const auto before = notes.size();

    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [&ids] (const Note& note) { return ids.find (note.id) != ids.end(); }),
                 notes.end());

    pruneInvalidNoteTies (clip);
    return (int) (before - notes.size());
}

void pruneInvalidNoteTies (Clip& clip)
{
    auto findById = [&clip] (juce::int64 id) -> Note*
    {
        for (auto& note : clip.notes)
            if (note.id == id)
                return &note;

        return nullptr;
    };

    for (auto& note : clip.notes)
    {
        if (note.tieFromNoteId == 0)
            continue;

        auto* predecessor = findById (note.tieFromNoteId);

        if (predecessor == nullptr || predecessor == &note
            || predecessor->endTick() != note.startTick)
        {
            note.tieFromNoteId = 0;
            continue;
        }

        int successors = 0;

        for (const auto& other : clip.notes)
            if (other.tieFromNoteId == predecessor->id)
                ++successors;

        if (successors != 1)
        {
            note.tieFromNoteId = 0;
            continue;
        }

        auto* cursor = predecessor;
        bool cyclic = false;

        for (int guard = 0; cursor != nullptr && guard <= (int) clip.notes.size(); ++guard)
        {
            if (cursor->id == note.id)
            {
                cyclic = true;
                break;
            }

            cursor = cursor->tieFromNoteId != 0 ? findById (cursor->tieFromNoteId)
                                                : nullptr;
        }

        if (cyclic)
            note.tieFromNoteId = 0;
    }
}

juce::int64 findFreeSlot (const InstrumentTrack& track, juce::int64 desiredStart,
                          juce::int64 lengthTicks, juce::int64 ignoreClipId)
{
    const auto span = juce::jmax (kMinClipTicks, lengthTicks);
    auto start = juce::jmax ((juce::int64) 0, desiredStart);

    for (int guard = 0; guard < 512; ++guard)
    {
        auto next = start;

        for (const auto& clip : track.clips)
        {
            if (clip.id == ignoreClipId)
                continue;

            if (start < clip.endTick() && clip.startTick < start + span)
                next = juce::jmax (next, clip.endTick());
        }

        if (next == start)
            return start;

        start = next;
    }

    return start;
}

std::vector<juce::int64> repeatClip (InstrumentTrack& track, juce::int64 sourceClipId,
                                     int count, juce::int64& nextClipId)
{
    std::vector<juce::int64> created;

    const auto* source = track.findClip (sourceClipId);

    if (source == nullptr || count <= 0 || source->lengthTicks <= 0)
        return created;

    // Take a copy first: pushing into the clip list can reallocate it, and the
    // source pointer would dangle.
    const Clip original = *source;

    created.reserve ((size_t) count);

    // Each copy goes into the first free slot at or after the running end. That
    // keeps the command non-destructive when the lane is busy, and pressing
    // "repeat" again just extends the run instead of landing on top of itself.
    auto cursor = original.endTick();

    for (int i = 0; i < count; ++i)
    {
        auto copy = original;
        copy.id   = nextClipId++;
        copy.name = original.name + "." + juce::String (copy.id);
        copy.startTick = findFreeSlot (track, cursor, original.lengthTicks);
        cursor = copy.startTick + original.lengthTicks;

        created.push_back (copy.id);
        track.clips.push_back (std::move (copy));
    }

    return created;
}

bool splitClipAt (Clip& clip,
                  juce::int64 splitTick,
                  const TempoMap& tempoMap,
                  juce::int64 marginTicks,
                  juce::int64& nextNoteId,
                  juce::int64& nextClipId,
                  Clip& rightOut)
{
    const auto leftStart = clip.startTick;
    const auto rightEnd = clip.endTick();
    const auto margin = juce::jmax ((juce::int64) 1, marginTicks);

    // The cut has to leave something on both sides, otherwise one of the halves
    // would be empty.
    if (splitTick - leftStart < margin || rightEnd - splitTick < margin)
        return false;

    Clip right = clip;
    right.id        = nextClipId++;
    right.startTick = splitTick;
    right.lengthTicks = rightEnd - splitTick;
    // The right half keeps the same name: both halves are the same part of the
    // arrangement, exactly like Cubase's split.

    std::vector<Note> leftNotes, rightNotes;
    const auto cutOffset = splitTick - leftStart;   // in clip-relative ticks

    for (const auto& note : clip.notes)
    {
        const auto noteStart = note.startTick;
        const auto noteEnd   = note.endTick();

        if (noteEnd <= cutOffset)
        {
            leftNotes.push_back (note);
            continue;
        }

        if (noteStart >= cutOffset)
        {
            auto moved = note;
            moved.startTick = noteStart - cutOffset;
            rightNotes.push_back (moved);
            continue;
        }

        // Straddling the cut: keep the head on the left and give the tail a new
        // note. Note ids are unique across the project, so the tail needs a fresh
        // one even though the pitch and velocity are the same.
        const auto fraction = (double) (cutOffset - noteStart)
                              / (double) juce::jmax ((juce::int64) 1, noteEnd - noteStart);
        auto head = note;
        head.durationTicks = cutOffset - noteStart;
        sliceNotePitchShape (note, 0.0, fraction, head);

        Note tail = note;
        tail.id            = nextNoteId++;
        tail.startTick     = 0;
        tail.durationTicks = noteEnd - cutOffset;
        sliceNotePitchShape (note, fraction, 1.0, tail);
        tail.tieFromNoteId = 0;

        leftNotes.push_back (head);
        rightNotes.push_back (tail);
    }

    clip.notes = std::move (leftNotes);
    clip.lengthTicks = cutOffset;
    right.notes = std::move (rightNotes);
    pruneInvalidNoteTies (clip);
    pruneInvalidNoteTies (right);

    // Both halves keep their notes in time order, so the lists stay canonical
    // whatever order the original happened to be in.
    auto sortNotes = [] (std::vector<Note>& notes)
    {
        std::sort (notes.begin(), notes.end(),
                   [] (const Note& a, const Note& b)
                   {
                       if (a.startTick != b.startTick)
                           return a.startTick < b.startTick;

                       return a.id < b.id;
                   });
    };

    sortNotes (clip.notes);
    sortNotes (right.notes);

    if (clip.isAudio())
    {
        // The right half starts later inside the same file.
        const auto startSeconds = tempoMap.secondsAtTick (leftStart);
        const auto cutSeconds   = tempoMap.secondsAtTick (splitTick);
        right.audioOffsetSeconds = clip.audioOffsetSeconds + juce::jmax (0.0, cutSeconds - startSeconds);
    }

    rightOut = std::move (right);
    return true;
}

bool canGlueClips (const Clip& clip, const Clip& next)
{
    if (clip.isAudio() || next.isAudio())
        return false;               // one clip holds one file, so they cannot merge

    if (clip.looped || next.looped)
        return false;               // tiled content has no single timeline to join

    return next.startTick == clip.endTick() && next.lengthTicks > 0;
}

bool glueClips (Clip& clip, const Clip& next, juce::int64& nextNoteId)
{
    if (! canGlueClips (clip, next))
        return false;

    const auto offset = next.startTick - clip.startTick;

    for (auto note : next.notes)
    {
        note.id = nextNoteId++;
        note.startTick += offset;
        clip.notes.push_back (std::move (note));
    }

    clip.lengthTicks = next.endTick() - clip.startTick;

    std::sort (clip.notes.begin(), clip.notes.end(),
               [] (const Note& a, const Note& b)
               {
                   if (a.startTick != b.startTick)
                       return a.startTick < b.startTick;

                   return a.id < b.id;
               });

    return true;
}

double foldPosition (double position) noexcept
{
    return position - std::floor (position);
}

double positionToRatio (double position) noexcept
{
    return std::pow (2.0, foldPosition (position));
}

double ratioToPosition (double ratio) noexcept
{
    if (ratio <= 0.0)
        return 0.0;

    return foldPosition (std::log2 (ratio));
}

int findDotNear (const ReferenceGroup& group, double position, double tolerance) noexcept
{
    const auto target = foldPosition (position);
    int best = -1;
    auto bestDistance = tolerance;

    for (int i = 0; i < (int) group.dots.size(); ++i)
    {
        // Compare on the circle so that 0.999 and 0.001 count as neighbours.
        auto distance = std::abs (group.dots[(size_t) i].position - target);
        distance = juce::jmin (distance, 1.0 - distance);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

bool addDotAt (ReferenceGroup& group, double position, int colourIndex, double tolerance)
{
    const auto folded = foldPosition (position);

    if (findDotNear (group, folded, tolerance) >= 0)
        return false;

    // Positions 0 and 1 are the frame dots and are never stored.
    if (folded < tolerance || folded > 1.0 - tolerance)
        return false;

    ReferenceDot dot;
    dot.position = folded;
    dot.colourIndex = ((colourIndex % kReferencePaletteSize) + kReferencePaletteSize)
                          % kReferencePaletteSize;

    group.dots.push_back (dot);

    std::sort (group.dots.begin(), group.dots.end(),
               [] (const ReferenceDot& a, const ReferenceDot& b) { return a.position < b.position; });

    return true;
}

bool removeDotAt (ReferenceGroup& group, double position, double tolerance)
{
    const auto index = findDotNear (group, position, tolerance);

    if (index < 0)
        return false;

    group.dots.erase (group.dots.begin() + index);
    return true;
}

std::vector<double> dividePositions (double fromPosition, double toPosition, int parts)
{
    std::vector<double> positions;

    if (parts < 2)
        return positions;

    // The two frame dots sit at 0 and 1, so the selected interval must NOT be
    // folded: folding would collapse the full octave (0 -> 1) into zero length.
    auto low  = juce::jlimit (0.0, 1.0, fromPosition);
    auto high = juce::jlimit (0.0, 1.0, toPosition);

    if (low > high)
        std::swap (low, high);

    const auto span = high - low;

    if (span <= 0.0)
        return positions;

    positions.reserve ((size_t) (parts - 1));

    for (int i = 1; i < parts; ++i)
        positions.push_back (low + span * (double) i / (double) parts);

    return positions;
}

bool isPositionSelected (const std::vector<double>& selected, double position,
                         double tolerance) noexcept
{
    return std::any_of (selected.begin(), selected.end(),
                        [position, tolerance] (double existing)
                        { return std::abs (existing - position) <= tolerance; });
}

void toggleSelectedPosition (std::vector<double>& selected, double position, double tolerance)
{
    const auto found = std::find_if (selected.begin(), selected.end(),
                                     [position, tolerance] (double existing)
                                     { return std::abs (existing - position) <= tolerance; });

    if (found != selected.end())
        selected.erase (found);
    else
        selected.push_back (position);
}

std::vector<double> referenceFrequencies (const std::vector<ReferenceDot>& dots,
                                          double anchorHz,
                                          double anchorPosition)
{
    std::vector<double> frequencies;
    frequencies.reserve (dots.size());

    if (anchorHz <= 0.0)
        return frequencies;

    for (const auto& dot : dots)
        frequencies.push_back (groupFrequencyAt (anchorHz, anchorPosition, dot.position));

    return frequencies;
}

double groupFrequencyAt (double anchorHz, double anchorPosition, double position)
{
    if (anchorHz <= 0.0)
        return 0.0;

    return anchorHz * std::pow (2.0, position - anchorPosition);
}

//==============================================================================
ReferenceLineEntry makeFixedHzEntry (juce::int64 id, double hz, const juce::String& name)
{
    ReferenceLineEntry entry;
    entry.id     = id;
    entry.kind   = ReferenceLineKind::fixedHz;
    entry.baseHz = hz;
    entry.name   = name.isNotEmpty() ? name : formatHz (hz);
    return entry;
}

ReferenceLineEntry makeGroupEntry (juce::int64 id, const ReferenceLineGroup& group)
{
    ReferenceLineEntry entry;
    entry.id             = id;
    entry.kind           = ReferenceLineKind::group;
    entry.name           = group.name;
    entry.anchorHz       = group.anchorHz;
    entry.anchorPosition = group.anchorPosition;
    entry.dots           = group.dots;
    entry.visible        = group.visible;
    return entry;
}

ReferenceLineGroup makeGroupFromNotes (const std::vector<Note>& notes)
{
    ReferenceLineGroup group;

    if (notes.size() < 2)
    {
        // No anchor at all, so callers can tell "no group" from "a group".
        group.anchorHz = 0.0;
        return group;
    }

    auto ordered = notes;

    std::sort (ordered.begin(), ordered.end(),
               [] (const Note& a, const Note& b) { return a.frequencyHz < b.frequencyHz; });

    const auto anchorHz = ordered.front().frequencyHz;

    if (anchorHz <= 0.0)
        return group;

    group.anchorHz       = anchorHz;
    group.anchorPosition = 0.0;
    group.visible        = true;

    // The anchor itself is stored as the position-0 dot, exactly like the "应用"
    // flow stores its frame dot: a group only expands the dots it holds, so
    // without this the base note's own octave family would not be drawn.
    ReferenceDot frame;
    frame.position    = 0.0;
    frame.colourIndex = 0;
    group.dots.push_back (frame);

    // The ladder's own dot rules (fold into one octave, merge positions that are
    // already taken, skip the frame positions) are exactly what the others need,
    // so build into a scratch group and copy the dots over. Colours run up the
    // palette from 1 so every note of the selection gets its own line colour.
    ReferenceGroup scratch;
    auto colour = 1;

    for (const auto& note : ordered)
    {
        if (note.frequencyHz <= 0.0 || note.frequencyHz == anchorHz)
            continue;

        if (addDotAt (scratch, ratioToPosition (note.frequencyHz / anchorHz), colour))
            colour = (colour + 1) % (kReferencePaletteSize - 1);
    }

    group.dots.insert (group.dots.end(), scratch.dots.begin(), scratch.dots.end());
    std::sort (group.dots.begin(), group.dots.end(),
               [] (const ReferenceDot& a, const ReferenceDot& b)
               { return a.position < b.position; });

    return group;
}

ReferenceLineEntry makeNoteRatioEntry (juce::int64 id, const Note& note, double ratio)
{
    ReferenceLineEntry entry;
    entry.id          = id;
    entry.kind        = ReferenceLineKind::noteRatio;
    entry.name        = derivedEntryName (ReferenceLineKind::noteRatio, note, ratio);
    entry.frozenHz    = note.frequencyHz;
    entry.frozenRatio = ratio;
    return entry;
}

ReferenceLineEntry makeMultiNoteEntry (juce::int64 id, const std::vector<Note>& notes)
{
    ReferenceLineEntry entry;
    entry.id   = id;
    entry.kind = ReferenceLineKind::multiNote;
    entry.name = juce::String ((int) notes.size()) + NI_TXT (" 个音符");

    for (const auto& note : notes)
        entry.frozenNotes.push_back (note.frequencyHz);

    return entry;
}

juce::String derivedEntryName (ReferenceLineKind kind, const Note& note, double ratio)
{
    if (kind == ReferenceLineKind::multiNote)
        return juce::String (1) + NI_TXT (" 个音符");

    return formatHz (note.frequencyHz) + NI_TXT (" - ") + formatRatioText (ratio);
}

int clearUnlockedEntries (std::vector<ReferenceLineEntry>& entries, juce::int64 keepId)
{
    const auto before = entries.size();

    entries.erase (std::remove_if (entries.begin(), entries.end(),
                                   [keepId] (const ReferenceLineEntry& entry)
                                   { return ! entry.locked && entry.id != keepId; }),
                   entries.end());

    return (int) (before - entries.size());
}

int moveEntry (std::vector<ReferenceLineEntry>& entries, int from, int to)
{
    if (from < 0 || from >= (int) entries.size() || entries.empty())
        return from;

    const auto target = juce::jlimit (0, (int) entries.size() - 1, to);

    if (target == from)
        return from;

    auto moved = entries[(size_t) from];
    entries.erase (entries.begin() + from);
    entries.insert (entries.begin() + target, std::move (moved));
    return target;
}

std::vector<ReferenceLineEntry> resolveReferenceEntries (const std::vector<ReferenceLineEntry>& entries,
                                                         const std::vector<Note>& selectedNotes,
                                                         double currentRatio,
                                                         juce::int64 soloEntryId)
{
    std::vector<ReferenceLineEntry> resolved;
    resolved.reserve (entries.size());

    for (auto entry : entries)
    {
        if (! entry.visible)
            continue;

        if (soloEntryId != 0 && entry.id != soloEntryId)
            continue;

        switch (entry.kind)
        {
            case ReferenceLineKind::fixedHz:
                if (entry.baseHz <= 0.0)
                    continue;
                break;

            case ReferenceLineKind::group:
                if (entry.dots.empty() || entry.anchorHz <= 0.0)
                    continue;
                break;

            case ReferenceLineKind::noteRatio:
                if (! entry.locked)
                {
                    // Live: exactly one note means this entry follows it.
                    if (selectedNotes.size() == 1)
                    {
                        entry.frozenHz    = selectedNotes.front().frequencyHz;
                        entry.frozenRatio = currentRatio > 0.0 ? currentRatio : 2.0;
                        entry.name        = derivedEntryName (entry.kind, selectedNotes.front(),
                                                              entry.frozenRatio);
                    }
                    else
                    {
                        continue;
                    }
                }

                if (entry.frozenHz <= 0.0 || entry.frozenRatio <= 0.0)
                    continue;
                break;

            case ReferenceLineKind::multiNote:
                if (! entry.locked)
                {
                    if (selectedNotes.size() < 2)
                        continue;

                    entry.frozenNotes.clear();

                    for (const auto& note : selectedNotes)
                        entry.frozenNotes.push_back (note.frequencyHz);

                    entry.name = juce::String ((int) selectedNotes.size()) + NI_TXT (" 个音符");
                }

                if (entry.frozenNotes.empty())
                    continue;
                break;
        }

        resolved.push_back (std::move (entry));
    }

    return resolved;
}

std::vector<ReferenceLine> buildReferenceLinesForEntries (const std::vector<ReferenceLineEntry>& resolved,
                                                          double topHz,
                                                          double bottomHz)
{
    std::vector<ReferenceLine> lines;

    auto append = [&lines, topHz, bottomHz] (const ReferenceLineEntry& entry,
                                             double baseHz,
                                             bool dashed,
                                             bool interactive,
                                             bool isManual,
                                             bool isTarget,
                                             bool isGroup,
                                             int  entryColour,
                                             int  fallbackColour,
                                             double targetRatio = 0.0,
                                             int    targetSide  = 0)
    {
        auto family = buildOctaveFamily (baseHz, topHz, bottomHz, dashed, interactive, false);

        for (auto& line : family)
        {
            line.entryId     = entry.id;
            line.isManual    = isManual;
            line.isTarget    = isTarget;
            line.isGroup     = isGroup;
            line.colourIndex = entryColour >= 0 ? entryColour : fallbackColour;
            line.ratio       = targetRatio;
            line.targetSide  = targetSide;

            const bool duplicate = std::any_of (lines.begin(), lines.end(),
                                                [&line] (const ReferenceLine& existing)
                                                { return std::abs (existing.hz - line.hz) < 1.0e-6; });

            if (! duplicate)
                lines.push_back (line);
        }
    };

    for (const auto& entry : resolved)
    {
        switch (entry.kind)
        {
            case ReferenceLineKind::fixedHz:
                append (entry, entry.baseHz, false, true, true, false, false, entry.colourIndex, -1);
                break;

            case ReferenceLineKind::noteRatio:
            {
                const auto ratio = entry.frozenRatio;

                if (isOctaveRatio (ratio))
                {
                    append (entry, entry.frozenHz, false, true, false, false, false,
                            entry.colourIndex, -1);
                }
                else
                {
                    // Own octaves are dimmed and stop responding; the two target
                    // families stay solid and interactive.
                    append (entry, entry.frozenHz, true, false, false, false, false,
                            entry.colourIndex, -1);
                    append (entry, entry.frozenHz * ratio, false, true, false, true, false,
                            entry.colourIndex, -1, ratio, +1);
                    append (entry, entry.frozenHz / ratio, false, true, false, true, false,
                            entry.colourIndex, -1, ratio, -1);
                }

                break;
            }

            case ReferenceLineKind::group:
            {
                const auto frequencies = referenceFrequencies (entry.dots, entry.anchorHz,
                                                               entry.anchorPosition);

                for (size_t i = 0; i < frequencies.size(); ++i)
                    append (entry, frequencies[i], false, true, false, false, true,
                            entry.colourIndex, entry.dots[i].colourIndex);

                break;
            }

            case ReferenceLineKind::multiNote:
                for (const auto hz : entry.frozenNotes)
                    append (entry, hz, false, true, false, false, false, entry.colourIndex, -1);

                break;
        }
    }

    std::sort (lines.begin(), lines.end(),
               [] (const ReferenceLine& a, const ReferenceLine& b) { return a.hz < b.hz; });

    return lines;
}

int findNearestLineIndex (const std::vector<ReferenceLine>& lines, double hz)
{
    if (lines.empty() || hz <= 0.0)
        return -1;

    const auto target = std::log2 (hz);
    int best = -1;
    auto bestDistance = 1.0e30;

    for (int i = 0; i < (int) lines.size(); ++i)
    {
        if (lines[(size_t) i].hz <= 0.0)
            continue;

        const auto distance = std::abs (std::log2 (lines[(size_t) i].hz) - target);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

NoteTiming resizeNoteStart (juce::int64 originalEndTick, juce::int64 newStartTick,
                            juce::int64 minDurationTicks)
{
    const auto minDuration = juce::jmax ((juce::int64) 1, minDurationTicks);
    const auto end = juce::jmax (minDuration, originalEndTick);

    NoteTiming timing;
    timing.startTick = juce::jlimit ((juce::int64) 0, end - minDuration, newStartTick);
    timing.durationTicks = end - timing.startTick;
    return timing;
}

NoteTiming resizeNoteEnd (juce::int64 originalStartTick, juce::int64 newEndTick,
                          juce::int64 minDurationTicks)
{
    const auto minDuration = juce::jmax ((juce::int64) 1, minDurationTicks);
    const auto start = juce::jmax ((juce::int64) 0, originalStartTick);

    NoteTiming timing;
    timing.startTick = start;
    timing.durationTicks = juce::jmax (minDuration, newEndTick - start);
    return timing;
}

juce::int64 clampTickDeltaForGroup (juce::int64 earliestStartTick, juce::int64 deltaTicks)
{
    if (earliestStartTick + deltaTicks < 0)
        return -earliestStartTick;

    return deltaTicks;
}

double scaledFrequency (double hz, double factor)
{
    return VerticalAxis::clampHz (hz * factor);
}

double parseRatioText (const juce::String& text)
{
    auto t = text.trim();

    if (t.isEmpty())
        return 0.0;

    if (t.containsChar (':') || t.containsChar ('/'))
    {
        auto normalised = t.replaceCharacter (':', '/');
        auto parts = juce::StringArray::fromTokens (normalised, "/", "");
        parts.removeEmptyStrings();

        if (parts.size() != 2)
            return 0.0;

        const auto a = parts[0].getDoubleValue();
        const auto b = parts[1].getDoubleValue();

        if (a <= 0.0 || b <= 0.0)
            return 0.0;

        // "1:3" is the interval between f and 3f, so it means three times the
        // frequency (the 1:2 button is the octave, i.e. a multiplier of 2).
        return b / a;
    }

    const auto v = t.getDoubleValue();
    return v > 0.0 ? v : 0.0;
}

juce::String formatRatioText (double ratio)
{
    if (ratio <= 0.0)
        return "-";

    if (ratio >= 1.0)
    {
        const auto rounded = std::round (ratio);

        if (std::abs (ratio - rounded) < 1.0e-6 && rounded <= 64.0)
            return "1:" + juce::String ((int) rounded);

        return "1:" + juce::String (ratio, 4);
    }

    const auto inverse = 1.0 / ratio;
    const auto rounded = std::round (inverse);

    if (std::abs (inverse - rounded) < 1.0e-6 && rounded <= 64.0)
        return juce::String ((int) rounded) + ":1";

    return "1:" + juce::String (ratio, 4);
}

std::optional<ReferenceLine> findLineNear (const std::vector<ReferenceLine>& lines,
                                           double hz,
                                           double tolerance)
{
    if (hz <= 0.0)
        return std::nullopt;

    for (const auto& line : lines)
        if (std::abs (line.hz - hz) <= tolerance)
            return line;

    return std::nullopt;
}

RatioArrow describeRatioArrow (const ReferenceLine& hoveredLine)
{
    RatioArrow arrow;

    // Only the two target families of a single-note entry carry a ratio; the
    // dimmed own octaves and every other kind of line carry none.
    if (! hoveredLine.isTarget || hoveredLine.targetSide == 0
        || hoveredLine.ratio <= 0.0 || isOctaveRatio (hoveredLine.ratio)
        || hoveredLine.hz <= 0.0)
        return arrow;

    arrow.visible = true;
    arrow.headHz  = hoveredLine.hz;
    arrow.ratio   = hoveredLine.ratio;

    // The f0 * r family came from multiplying the base note, so its source is the
    // base note below it; the f0 / r family came from dividing, so its source is
    // above. Either way the source is the base note's own octave line.
    arrow.tailHz = hoveredLine.targetSide > 0 ? arrow.headHz / arrow.ratio
                                              : arrow.headHz * arrow.ratio;

    arrow.direction = arrow.tailHz < arrow.headHz ? +1 : -1;
    return arrow;
}

juce::String ratioArrowLabel (const RatioArrow& arrow)
{
    if (! arrow.visible || arrow.ratio <= 0.0)
        return {};

    return (arrow.direction > 0 ? NI_TXT ("正") : NI_TXT ("负"))
               + " " + formatRatioText (arrow.ratio);
}

TempoMap::TempoMap()
{
    rebuild ({});
}

TempoMap::TempoMap (const std::vector<TempoMarker>& markers)
{
    rebuild (markers);
}

void TempoMap::rebuild (const std::vector<TempoMarker>& markers)
{
    segments.clear();

    auto sorted = markers;

    for (auto& m : sorted)
    {
        m.numerator   = juce::jlimit (1, 32, m.numerator);
        m.denominator = juce::jlimit (1, 32, m.denominator);
        m.bpm         = juce::jlimit (10.0, 400.0, m.bpm);
        m.tick        = juce::jmax ((juce::int64) 0, m.tick);
    }

    std::sort (sorted.begin(), sorted.end(),
               [] (const TempoMarker& a, const TempoMarker& b) { return a.tick < b.tick; });

    if (sorted.empty() || sorted.front().tick != 0)
        sorted.insert (sorted.begin(), TempoMarker { 0, 4, 4, 120.0 });

    for (size_t i = sorted.size(); i-- > 1;)
        if (sorted[i].tick == sorted[i - 1].tick)
            sorted.erase (sorted.begin() + (long) i - 1);

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        Segment seg;
        seg.startTick    = sorted[i].tick;
        seg.numerator    = sorted[i].numerator;
        seg.denominator  = sorted[i].denominator;
        seg.bpm          = sorted[i].bpm;
        seg.ticksPerBeat = (double) kTicksPerQuarter * 4.0 / (double) seg.denominator;

        if (i == 0)
        {
            seg.startBar     = 1;
            seg.startSeconds = 0.0;
        }
        else
        {
            const auto& prev = segments.back();
            const auto span  = (double) (seg.startTick - prev.startTick);

            seg.startSeconds = prev.startSeconds + span * prev.secondsPerTick();
            seg.startBar     = prev.startBar + (int) std::floor (span / prev.ticksPerBar() + 1.0e-9);
        }

        segments.push_back (seg);
    }
}

int TempoMap::segmentIndexFor (juce::int64 tick) const
{
    int index = 0;

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        if (segments[(size_t) i].startTick <= tick)
            index = i;
        else
            break;
    }

    return index;
}

double TempoMap::bpmAt (juce::int64 tick) const
{
    return segments[(size_t) segmentIndexFor (tick)].bpm;
}

int TempoMap::numeratorAt (juce::int64 tick) const
{
    return segments[(size_t) segmentIndexFor (tick)].numerator;
}

int TempoMap::denominatorAt (juce::int64 tick) const
{
    return segments[(size_t) segmentIndexFor (tick)].denominator;
}

double TempoMap::ticksPerBeatAt (juce::int64 tick) const
{
    return segments[(size_t) segmentIndexFor (tick)].ticksPerBeat;
}

double TempoMap::secondsAtTick (juce::int64 tick) const
{
    const auto& seg = segments[(size_t) segmentIndexFor (tick)];
    return seg.startSeconds + (double) (tick - seg.startTick) * seg.secondsPerTick();
}

juce::int64 TempoMap::tickAtSeconds (double seconds) const
{
    if (seconds <= 0.0)
        return 0;

    int index = 0;

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        if (segments[(size_t) i].startSeconds <= seconds)
            index = i;
        else
            break;
    }

    const auto& seg = segments[(size_t) index];
    return seg.startTick + (juce::int64) std::llround ((seconds - seg.startSeconds) / seg.secondsPerTick());
}

void TempoMap::tickToBarBeat (juce::int64 tick, int& bar, int& beat, int& tickInBeat) const
{
    const auto safeTick = juce::jmax ((juce::int64) 0, tick);
    const auto& seg = segments[(size_t) segmentIndexFor (safeTick)];

    const auto rel          = (double) (safeTick - seg.startTick);
    const auto ticksPerBar  = seg.ticksPerBar();
    const auto ticksPerBeat = seg.ticksPerBeat;

    const auto barIndex   = std::floor (rel / ticksPerBar + 1.0e-9);
    const auto withinBar  = rel - barIndex * ticksPerBar;
    const auto beatIndex  = std::floor (withinBar / ticksPerBeat + 1.0e-9);
    const auto withinBeat = withinBar - beatIndex * ticksPerBeat;

    bar        = seg.startBar + (int) barIndex;
    beat       = (int) beatIndex + 1;
    tickInBeat = (int) std::floor (withinBeat + 1.0e-6);
}

juce::int64 TempoMap::barBeatToTick (int bar, int beat) const
{
    const auto targetBar = juce::jmax (1, bar);

    int index = 0;

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        if (segments[(size_t) i].startBar <= targetBar)
            index = i;
        else
            break;
    }

    const auto& seg = segments[(size_t) index];
    const auto tick = (double) seg.startTick
                    + (double) (targetBar - seg.startBar) * seg.ticksPerBar()
                    + (double) juce::jmax (0, beat - 1) * seg.ticksPerBeat;

    return (juce::int64) std::llround (tick);
}

juce::int64 TempoMap::barStartTick (int bar) const
{
    return barBeatToTick (bar, 1);
}

int TempoMap::barAtTick (juce::int64 tick) const
{
    int bar = 1, beat = 1, t = 0;
    tickToBarBeat (tick, bar, beat, t);
    return bar;
}

juce::int64 TempoMap::nextBeatTick (juce::int64 tick) const
{
    const auto tpb = ticksPerBeatAt (tick);
    const auto n   = std::floor ((double) tick / tpb + 1.0e-9);
    return (juce::int64) std::llround ((n + 1.0) * tpb);
}

namespace
{
    /** Beat 1 of a bar, on the beat itself: the accented metronome click. */
    bool isDownbeatAt (const TempoMap& tempoMap, juce::int64 tick)
    {
        int bar = 1, beat = 1, tickInBeat = 0;
        tempoMap.tickToBarBeat (tick, bar, beat, tickInBeat);
        return beat == 1 && tickInBeat == 0;
    }
}

MetronomeClick firstClickAtOrAfter (const TempoMap& tempoMap, juce::int64 tick)
{
    const auto safeTick = juce::jmax ((juce::int64) 0, tick);

    int bar = 1, beat = 1, tickInBeat = 0;
    tempoMap.tickToBarBeat (safeTick, bar, beat, tickInBeat);

    // Exactly on a beat: that beat is the click (this is what makes playing from
    // tick 0 click straight away instead of a beat late).
    MetronomeClick click;
    click.tick     = tickInBeat == 0 ? safeTick : tempoMap.nextBeatTick (safeTick);
    click.downbeat = isDownbeatAt (tempoMap, click.tick);
    return click;
}

MetronomeClick nextClickAfter (const TempoMap& tempoMap, juce::int64 tick)
{
    MetronomeClick click;
    click.tick     = tempoMap.nextBeatTick (juce::jmax ((juce::int64) 0, tick));
    click.downbeat = isDownbeatAt (tempoMap, click.tick);
    return click;
}

juce::String TempoMap::formatPosition (int bar, int beat, int tickInBeat)
{
    return juce::String (bar).paddedLeft ('0', 3)
         + ":" + juce::String (juce::jmax (1, beat)).paddedLeft ('0', 2)
         + ":" + juce::String (juce::jmax (0, tickInBeat)).paddedLeft ('0', 3);
}

juce::int64 TempoMap::snapToGrid (juce::int64 tick, double gridTicks)
{
    if (gridTicks <= 0.0)
        return tick;

    return (juce::int64) std::llround (std::round ((double) tick / gridTicks) * gridTicks);
}

} // namespace hmdaw::music
