#include "TestSupport.h"

class VerticalAxisTests : public juce::UnitTest
{
public:
    VerticalAxisTests() : juce::UnitTest ("VerticalAxis", "nidaw") {}

    void runTest() override
    {
        music::VerticalAxis axis;
        axis.height = 600.0;
        axis.centreHz = 440.0;
        axis.octavesVisible = 4.0;

        beginTest ("frequency / y round trip");

        for (const auto hz : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0 })
        {
            const auto y = axis.frequencyToY (hz);
            const auto back = axis.yToFrequency (y);
            expectWithinAbsoluteError (back, hz, 0.01);
        }

        beginTest ("equal frequency ratios are equally spaced");

        const auto y220 = axis.frequencyToY (220.0);
        const auto y440 = axis.frequencyToY (440.0);
        const auto y880 = axis.frequencyToY (880.0);

        expectWithinAbsoluteError (y220 - y440, y440 - y880, 1.0e-9);

        beginTest ("the visible range is centred on centreHz");

        expectWithinAbsoluteError (axis.topHz(), 1760.0, 0.1);
        expectWithinAbsoluteError (axis.bottomHz(), 110.0, 0.1);
    }
};

class ReferenceLineTests : public juce::UnitTest
{
public:
    ReferenceLineTests() : juce::UnitTest ("ReferenceLines", "nidaw") {}

    static Note noteAt (double hz)
    {
        Note n;
        n.id = 1;
        n.frequencyHz = hz;
        return n;
    }

    /** Expands entries the way the editor does, over the full visible range.
        The ratio is the transient lens of the current selection. */
    static std::vector<music::ReferenceLine> linesFor (const std::vector<ReferenceLineEntry>& entries,
                                                       const std::vector<Note>& selected,
                                                       double currentRatio = 2.0)
    {
        const auto resolved = music::resolveReferenceEntries (entries, selected, currentRatio, 0);
        return music::buildReferenceLinesForEntries (resolved, 20000.0, 8.0);
    }

    void runTest() override
    {
        beginTest ("plain octave family is cropped to the visible range");

        const auto family = music::buildOctaveFamily (440.0, 20000.0, 8.0, false, true, false);
        expectEquals ((int) family.size(), 11);   // k = -5 .. 5
        expectEquals (family.front().hz, 13.75);
        expectEquals (family.back().hz, 14080.0);

        beginTest ("visible range is respected exactly");

        const auto narrow = music::buildOctaveFamily (440.0, 880.0, 220.0, false, true, false);
        expectEquals ((int) narrow.size(), 3);

        beginTest ("octave ratio keeps the family solid and interactive");

        const auto note = noteAt (440.0);
        const auto octave = linesFor ({ music::makeNoteRatioEntry (1, note, 2.0) }, { note });
        expectEquals ((int) octave.size(), 11);

        for (const auto& line : octave)
        {
            expect (! line.dashed);
            expect (line.interactive);
            expect (! line.isTarget);
        }

        beginTest ("non octave ratio dims the own family and adds two target families");

        const auto thirdNote = noteAt (440.0);
        const auto third = linesFor ({ music::makeNoteRatioEntry (1, thirdNote, 3.0) },
                                     { thirdNote }, 3.0);

        int dashedCount = 0, targetCount = 0;

        for (const auto& line : third)
        {
            if (line.dashed)
            {
                ++dashedCount;
                expect (! line.interactive);
                expect (! line.isTarget);
            }
            else
            {
                expect (line.interactive);
                expect (line.isTarget);
                ++targetCount;
            }
        }

        expectEquals (dashedCount, 11);
        expect (targetCount > 0);

        // 440 * 3 = 1320 must be present as a target.
        const bool foundUpper = std::any_of (third.begin(), third.end(),
                                             [] (const music::ReferenceLine& l)
                                             { return std::abs (l.hz - 1320.0) < 1.0e-6 && l.isTarget; });
        expect (foundUpper);

        beginTest ("manual input and selected note coexist, duplicates removed");

        const auto both = linesFor ({ music::makeFixedHzEntry (1, 440.0, juce::String()),
                                      music::makeNoteRatioEntry (2, note, 2.0) },
                                    { note });
        expectEquals ((int) both.size(), 11);

        beginTest ("nothing selected and no manual input means no lines");

        expectEquals ((int) linesFor ({}, {}).size(), 0);
        // An unlocked 2nd-kind entry with no note selected resolves to nothing.
        expectEquals ((int) linesFor ({ music::makeNoteRatioEntry (1, note, 2.0) }, {}).size(), 0);
    }
};

class ReferenceLineEntryTests : public juce::UnitTest
{
public:
    ReferenceLineEntryTests() : juce::UnitTest ("ReferenceLineEntries", "nidaw") {}

    static Note noteAt (juce::int64 id, double hz)
    {
        Note n;
        n.id = id;
        n.frequencyHz = hz;
        return n;
    }

    void runTest() override
    {
        beginTest ("an unlocked 2nd-kind entry follows the selected note");

        {
            const auto entry = music::makeNoteRatioEntry (1, noteAt (1, 440.0), 3.0);
            expectEquals (entry.name, NI_TXT ("440.00 Hz - 1:3"));

            // The ratio comes from the selection lens, not from the note.
            const auto one = music::resolveReferenceEntries ({ entry }, { noteAt (2, 220.0) },
                                                             5.0, 0);
            expectEquals ((int) one.size(), 1);
            expectWithinAbsoluteError (one.front().frozenHz, 220.0, 1.0e-9);
            expectWithinAbsoluteError (one.front().frozenRatio, 5.0, 1.0e-9);
            expectEquals (one.front().name, NI_TXT ("220.00 Hz - 1:5"));

            // No note, or several: the single-note entry has nothing to show.
            expectEquals ((int) music::resolveReferenceEntries ({ entry }, {}, 2.0, 0).size(), 0);
            expectEquals ((int) music::resolveReferenceEntries ({ entry },
                                                                { noteAt (1, 440.0), noteAt (2, 660.0) },
                                                                2.0, 0).size(),
                          0);
        }

        beginTest (NI_TXT ("a multi-note selection becomes a reference line group"));

        {
            const std::vector<Note> picked { noteAt (1, 220.0), noteAt (2, 330.0), noteAt (3, 550.0) };
            const auto group = music::makeGroupFromNotes (picked);

            // The lowest note is the frame dot, so the group stores the intervals
            // rather than absolute pitch.
            expectWithinAbsoluteError (group.anchorHz, 220.0, 1.0e-9);
            expectWithinAbsoluteError (group.anchorPosition, 0.0, 1.0e-9);
            expect (group.visible);
            expectEquals ((int) group.dots.size(), 3);

            if (group.dots.size() == 3)
            {
                // The anchor is stored as the position-0 dot, so its own octave
                // family is drawn too; the others fold into one octave.
                expectWithinAbsoluteError (group.dots[0].position, 0.0, 1.0e-9);
                expectWithinAbsoluteError (group.dots[1].position,
                                           music::ratioToPosition (550.0 / 220.0), 1.0e-9);
                expectWithinAbsoluteError (group.dots[2].position,
                                           music::ratioToPosition (330.0 / 220.0), 1.0e-9);

                for (const auto& dot : group.dots)
                    expect (dot.colourIndex >= 0 && dot.colourIndex < kReferencePaletteSize - 1,
                            NI_TXT ("dot colours stay off the frame colour"));
            }

            // What is stored is the octave-folded sibling, so the drawn family still
            // covers the note the user picked.
            const auto lines = music::referenceFrequencies (group.dots, group.anchorHz, 0.0);

            expectEquals ((int) lines.size(), 3);

            if (lines.size() == 3)
            {
                expectWithinAbsoluteError (lines[0], 220.0, 1.0e-9);
                expectWithinAbsoluteError (lines[1] * 2.0, 550.0, 1.0e-9);
                expectWithinAbsoluteError (lines[2], 330.0, 1.0e-9);
            }

            // An octave above the anchor folds onto the frame dot, so it adds no
            // dot of its own; a lonely note cannot make a group at all.
            expectEquals ((int) music::makeGroupFromNotes ({ noteAt (1, 220.0),
                                                             noteAt (2, 880.0) }).dots.size(), 1);
            expectEquals ((int) music::makeGroupFromNotes ({ noteAt (1, 220.0) }).dots.size(), 0);
            expectEquals (music::makeGroupFromNotes ({}).anchorHz, 0.0);
        }

        beginTest ("an unlocked 4th-kind entry needs at least two notes");

        {
            const auto entry = music::makeMultiNoteEntry (1, { noteAt (1, 220.0), noteAt (2, 330.0) });
            expectEquals (entry.name, NI_TXT ("2 个音符"));

            const auto two = music::resolveReferenceEntries ({ entry },
                                                             { noteAt (1, 220.0), noteAt (2, 440.0), noteAt (3, 660.0) },
                                                             2.0, 0);
            expectEquals ((int) two.size(), 1);
            expectEquals ((int) two.front().frozenNotes.size(), 3);
            expectWithinAbsoluteError (two.front().frozenNotes[2], 660.0, 1.0e-9);
            expectEquals (two.front().name, NI_TXT ("3 个音符"));

            expectEquals ((int) music::resolveReferenceEntries ({ entry }, { noteAt (1, 220.0) },
                                                                2.0, 0).size(),
                          0);
        }

        beginTest ("a sealed entry ignores the selection");

        {
            auto entry = music::makeNoteRatioEntry (1, noteAt (1, 440.0), 3.0);
            entry.locked = true;

            // A sealed entry keeps its frozen ratio whatever the lens says now.
            const auto resolved = music::resolveReferenceEntries ({ entry },
                                                                  { noteAt (2, 220.0) }, 5.0, 0);
            expectEquals ((int) resolved.size(), 1);
            expectWithinAbsoluteError (resolved.front().frozenHz, 440.0, 1.0e-9);
            expectWithinAbsoluteError (resolved.front().frozenRatio, 3.0, 1.0e-9);

            auto sealed = music::makeMultiNoteEntry (2, {});
            sealed.locked = true;
            sealed.frozenNotes = { 220.0, 330.0 };
            expectEquals ((int) music::resolveReferenceEntries ({ sealed }, {}, 2.0, 0).size(), 1);
        }

        beginTest ("hidden entries drop out and solo keeps only one");

        {
            auto a = music::makeFixedHzEntry (1, 440.0, {});
            auto b = music::makeFixedHzEntry (2, 660.0, {});

            auto hidden = b;
            hidden.visible = false;

            expectEquals ((int) music::resolveReferenceEntries ({ a, hidden }, {}, 2.0, 0).size(), 1);

            auto solo = music::resolveReferenceEntries ({ a, b }, {}, 2.0, 2);
            expectEquals ((int) solo.size(), 1);
            expectEquals (solo.front().id, (juce::int64) 2);
        }

        beginTest ("creating a new reference line clears the unlocked entries");

        {
            auto sealed = music::makeFixedHzEntry (1, 440.0, {});
            sealed.locked = true;
            auto scratchA = music::makeFixedHzEntry (2, 660.0, {});
            auto scratchB = music::makeFixedHzEntry (3, 880.0, {});
            std::vector<ReferenceLineEntry> entries { sealed, scratchA, scratchB };

            expectEquals (music::clearUnlockedEntries (entries, 3), 1);
            expectEquals ((int) entries.size(), 2);
            expectEquals (entries[0].id, (juce::int64) 1);
            expectEquals (entries[1].id, (juce::int64) 3);
        }

        beginTest ("entries can be moved up and down the list");

        {
            std::vector<ReferenceLineEntry> entries { music::makeFixedHzEntry (1, 100.0, {}),
                                                      music::makeFixedHzEntry (2, 200.0, {}),
                                                      music::makeFixedHzEntry (3, 300.0, {}) };

            expectEquals (music::moveEntry (entries, 0, 2), 2);
            expectEquals (entries[0].id, (juce::int64) 2);
            expectEquals (entries[2].id, (juce::int64) 1);

            expectEquals (music::moveEntry (entries, 2, 0), 0);
            expectEquals (entries[0].id, (juce::int64) 1);

            // Out-of-range requests are clamped rather than corrupting the list.
            expectEquals (music::moveEntry (entries, 0, 99), 2);
            expectEquals ((int) entries.size(), 3);
        }

        beginTest ("each kind expands into its own lines");

        {
            const auto fixed = music::makeFixedHzEntry (1, 440.0, {});
            const auto fixedLines = music::buildReferenceLinesForEntries ({ fixed }, 20000.0, 8.0);
            expectEquals ((int) fixedLines.size(), 11);

            for (const auto& line : fixedLines)
            {
                expect (line.isManual);
                expect (line.interactive);
                expectEquals (line.entryId, (juce::int64) 1);
            }

            // A non-octave ratio dims its own octaves and adds both target families.
            const auto ratio = music::makeNoteRatioEntry (2, noteAt (1, 440.0), 3.0);
            const auto ratioLines = music::buildReferenceLinesForEntries ({ ratio }, 20000.0, 8.0);
            const auto dashed = std::count_if (ratioLines.begin(), ratioLines.end(),
                                               [] (const music::ReferenceLine& l) { return l.dashed; });
            expectEquals ((int) dashed, 11);
            expect (std::any_of (ratioLines.begin(), ratioLines.end(),
                                 [] (const music::ReferenceLine& l)
                                 { return l.isTarget && std::abs (l.hz - 1320.0) < 1.0e-6; }));

            // Multi-note: one octave family per selected note, no targets.
            auto multi = music::makeMultiNoteEntry (3, { noteAt (1, 440.0), noteAt (2, 220.0) });
            multi.visible = true;
            const auto multiLines = music::buildReferenceLinesForEntries ({ multi }, 20000.0, 8.0);

            expect (! multiLines.empty());

            for (const auto& line : multiLines)
            {
                expect (! line.isTarget);
                expect (! line.dashed);
                expectEquals (line.entryId, (juce::int64) 3);
            }
        }

        beginTest ("group entries keep their dot colours until the entry is recoloured");

        {
            ReferenceLineGroup group;
            group.anchorHz = 220.0;
            group.anchorPosition = 0.0;

            ReferenceDot dot;
            dot.position = music::ratioToPosition (3.0);
            dot.colourIndex = 5;
            group.dots = { dot };

            auto entry = music::makeGroupEntry (1, group);
            auto lines = music::buildReferenceLinesForEntries ({ entry }, 20000.0, 8.0);
            expect (! lines.empty());

            for (const auto& line : lines)
            {
                expect (line.isGroup);
                expectEquals (line.colourIndex, 5);
            }

            auto recoloured = entry;
            recoloured.colourIndex = 2;
            lines = music::buildReferenceLinesForEntries ({ recoloured }, 20000.0, 8.0);
            expect (! lines.empty());

            for (const auto& line : lines)
                expectEquals (line.colourIndex, 2);
        }

        beginTest ("lines stay deduplicated across entries");

        {
            const auto a = music::makeFixedHzEntry (1, 440.0, {});
            const auto b = music::makeFixedHzEntry (2, 440.0, {});
            const auto lines = music::buildReferenceLinesForEntries ({ a, b }, 20000.0, 8.0);

            expectEquals ((int) lines.size(), 11);
            expectEquals (lines.front().entryId, (juce::int64) 1);   // first one wins
        }

        beginTest ("the two target families say which side of the ratio they are on");

        {
            Note note;
            note.id = 1;
            note.durationTicks = 960;
            note.frequencyHz = 220.0;

            const auto entry = music::makeNoteRatioEntry (1, note, 3.0);
            const auto lines = music::buildReferenceLinesForEntries ({ entry }, 20000.0, 8.0);

            // A fractional part, so family members below the base note (negative
            // logarithms) compare the same way as the ones above it.
            const auto fraction = [] (double x) { return x - std::floor (x); };

            auto upLines = 0, downLines = 0, ownLines = 0;

            for (const auto& line : lines)
            {
                if (line.targetSide > 0)
                {
                    ++upLines;
                    expectWithinAbsoluteError (line.ratio, 3.0, 1.0e-9);
                    expectWithinAbsoluteError (fraction (std::log2 (line.hz / 220.0)),
                                               fraction (std::log2 (3.0)), 1.0e-6);
                }
                else if (line.targetSide < 0)
                {
                    ++downLines;
                    expectWithinAbsoluteError (line.ratio, 3.0, 1.0e-9);
                }
                else
                {
                    ++ownLines;
                    expect (line.dashed);
                    expectWithinAbsoluteError (line.ratio, 0.0, 1.0e-12);
                }
            }

            expectGreaterThan (upLines, 0);
            expectGreaterThan (downLines, 0);
            expectGreaterThan (ownLines, 0);

            // With the octave there is only one family, and it is not a ratio target.
            const auto octaveEntry = music::makeNoteRatioEntry (2, note, 2.0);
            const auto octaveLines = music::buildReferenceLinesForEntries ({ octaveEntry },
                                                                          20000.0, 8.0);

            for (const auto& line : octaveLines)
            {
                expectEquals (line.targetSide, 0);
                expectWithinAbsoluteError (line.ratio, 0.0, 1.0e-12);
            }
        }
    }
};

class NearestLineTests : public juce::UnitTest
{
public:
    NearestLineTests() : juce::UnitTest ("NearestReferenceLine", "nidaw") {}

    void runTest() override
    {
        beginTest ("an empty set has no nearest line");

        expectEquals (music::findNearestLineIndex ({}, 440.0), -1);

        beginTest ("an exact match is chosen");

        const auto family = music::buildOctaveFamily (440.0, 20000.0, 8.0, false, true, false);
        const auto index = music::findNearestLineIndex (family, 440.0);
        expect (index >= 0);

        if (index >= 0)
            expectWithinAbsoluteError (family[(size_t) index].hz, 440.0, 1.0e-6);

        beginTest ("the boundary between two lines is their geometric mean");

        const std::vector<music::ReferenceLine> pair { { 440.0, false, true, false, false },
                                                       { 880.0, false, true, false, false } };

        // Geometric mean of 440 and 880 is 622.25; a linear comparison would put
        // the boundary at 660 instead.
        const auto below = music::findNearestLineIndex (pair, 620.0);
        const auto above = music::findNearestLineIndex (pair, 625.0);

        expectEquals (below, 0);
        expectEquals (above, 1);

        beginTest ("dashed lines are still valid targets");

        const std::vector<music::ReferenceLine> dashed { { 300.0, true, false, false, false },
                                                         { 600.0, true, false, false, false } };

        expectEquals (music::findNearestLineIndex (dashed, 590.0), 1);
    }
};

class DragMathTests : public juce::UnitTest
{
public:
    DragMathTests() : juce::UnitTest ("DragMath", "nidaw") {}

    void runTest() override
    {
        beginTest ("dragging the start edge keeps the end fixed");

        auto timing = music::resizeNoteStart (1920, 1000, 240);
        expectEquals (timing.startTick, (juce::int64) 1000);
        expectEquals (timing.durationTicks, (juce::int64) 920);
        expectEquals (timing.endTick(), (juce::int64) 1920);

        beginTest ("the start edge cannot cross the end");

        timing = music::resizeNoteStart (1920, 1900, 240);
        expectEquals (timing.startTick, (juce::int64) 1680);
        expectEquals (timing.durationTicks, (juce::int64) 240);
        expectEquals (timing.endTick(), (juce::int64) 1920);

        beginTest ("the start edge cannot go before zero");

        timing = music::resizeNoteStart (1920, -500, 240);
        expectEquals (timing.startTick, (juce::int64) 0);
        expectEquals (timing.durationTicks, (juce::int64) 1920);

        beginTest ("dragging the end edge keeps the start fixed");

        timing = music::resizeNoteEnd (960, 1000, 240);
        expectEquals (timing.startTick, (juce::int64) 960);
        expectEquals (timing.durationTicks, (juce::int64) 240);

        timing = music::resizeNoteEnd (960, 2400, 240);
        expectEquals (timing.durationTicks, (juce::int64) 1440);

        beginTest ("a group can never be dragged before tick zero");

        expectEquals (music::clampTickDeltaForGroup (480, -1000), (juce::int64) -480);
        expectEquals (music::clampTickDeltaForGroup (480, 100), (juce::int64) 100);
        expectEquals (music::clampTickDeltaForGroup (0, -1000), (juce::int64) 0);

        beginTest ("group transposition keeps the internal intervals");

        const auto lowest = music::scaledFrequency (220.0, 3.0);
        const auto highest = music::scaledFrequency (660.0, 3.0);
        expectWithinAbsoluteError (highest / lowest, 3.0, 1.0e-9);

        beginTest ("frequencies are clamped to the visible range");

        expectWithinAbsoluteError (music::scaledFrequency (440.0, 2.0), 880.0, 1.0e-9);
        expectEquals (music::scaledFrequency (19000.0, 2.0), kMaxVisibleHz);
        expectEquals (music::scaledFrequency (10.0, 0.25), kMinVisibleHz);
    }
};

class ReferenceGroupMathTests : public juce::UnitTest
{
public:
    ReferenceGroupMathTests() : juce::UnitTest ("ReferenceGroupMath", "nidaw") {}

    void runTest() override
    {
        beginTest ("ratio and position are inverses");

        expectWithinAbsoluteError (music::ratioToPosition (2.0), 0.0, 1.0e-9);
        expectWithinAbsoluteError (music::ratioToPosition (3.0), 0.5849625007, 1.0e-9);
        expectWithinAbsoluteError (music::ratioToPosition (1.0 / 3.0), 0.4150374992, 1.0e-9);
        expectWithinAbsoluteError (music::positionToRatio (0.5), std::sqrt (2.0), 1.0e-9);

        beginTest ("multiplying and dividing by a ratio mirror each other");

        expectWithinAbsoluteError (music::ratioToPosition (5.0) + music::ratioToPosition (1.0 / 5.0),
                                   1.0, 1.0e-9);

        beginTest ("positions fold into [0, 1)");

        expectWithinAbsoluteError (music::foldPosition (1.25), 0.25, 1.0e-9);
        expectWithinAbsoluteError (music::foldPosition (-0.25), 0.75, 1.0e-9);
        expectWithinAbsoluteError (music::foldPosition (3.0), 0.0, 1.0e-9);

        beginTest ("adding dots folds, dedupes and refuses the frame positions");

        {
            ReferenceGroup group;

            expect (music::addDotAt (group, music::ratioToPosition (3.0), 1));
            expect (music::addDotAt (group, music::ratioToPosition (1.0 / 3.0), 2));
            expect (! music::addDotAt (group, music::ratioToPosition (3.0000001), 0));
            expect (! music::addDotAt (group, 0.0, 0));
            expect (! music::addDotAt (group, 1.0, 0));
            expectEquals ((int) group.dots.size(), 2);
            expect (group.dots[0].position < group.dots[1].position);
            expectEquals (group.dots[1].colourIndex, 1);
        }

        beginTest ("removing a dot");

        {
            ReferenceGroup group;
            music::addDotAt (group, 0.25, 0);
            music::addDotAt (group, 0.5, 0);

            expect (music::removeDotAt (group, 0.2501, 0.01));
            expectEquals ((int) group.dots.size(), 1);
            expect (! music::removeDotAt (group, 0.9, 0.01));
        }

        beginTest ("dividing a segment gives evenly spaced interior positions");

        {
            const auto thirds = music::dividePositions (0.0, 1.0, 3);
            expectEquals ((int) thirds.size(), 2);
            expectWithinAbsoluteError (thirds[0], 1.0 / 3.0, 1.0e-9);
            expectWithinAbsoluteError (thirds[1], 2.0 / 3.0, 1.0e-9);

            const auto reversed = music::dividePositions (0.5, 0.0, 2);
            expectEquals ((int) reversed.size(), 1);
            expectWithinAbsoluteError (reversed[0], 0.25, 1.0e-9);

            expectEquals ((int) music::dividePositions (0.0, 1.0, 1).size(), 0);
        }

        beginTest ("the two frame dots are independent selection targets");

        {
            std::vector<double> selected;

            music::toggleSelectedPosition (selected, 0.0);
            expectEquals ((int) selected.size(), 1);
            expect (music::isPositionSelected (selected, 0.0));
            expect (! music::isPositionSelected (selected, 1.0));

            // Selecting the far end must ADD to the selection, not toggle the near
            // end off - folding the two endpoints together was the original bug.
            music::toggleSelectedPosition (selected, 1.0);
            expectEquals ((int) selected.size(), 2);
            expect (music::isPositionSelected (selected, 0.0));
            expect (music::isPositionSelected (selected, 1.0));

            music::toggleSelectedPosition (selected, 0.0);
            expectEquals ((int) selected.size(), 1);
            expect (! music::isPositionSelected (selected, 0.0));
            expect (music::isPositionSelected (selected, 1.0));

            // An interior dot is independent of both ends.
            music::toggleSelectedPosition (selected, 0.25);
            expectEquals ((int) selected.size(), 2);
            expect (music::isPositionSelected (selected, 0.25));
            expect (! music::isPositionSelected (selected, 0.2500001));
        }

        beginTest ("dividing the whole octave between the two frame dots");

        {
            const auto positions = music::dividePositions (0.0, 1.0, 12);
            expectEquals ((int) positions.size(), 11);
            expectWithinAbsoluteError (positions.front(), 1.0 / 12.0, 1.0e-9);
            expectWithinAbsoluteError (positions.back(), 11.0 / 12.0, 1.0e-9);

            for (const auto position : positions)
                expect (position > 0.0 && position < 1.0);
        }

        beginTest ("pinning a group to an anchor note");

        {
            ReferenceGroup group;
            music::addDotAt (group, music::ratioToPosition (3.0), 0);
            music::addDotAt (group, music::ratioToPosition (1.0 / 3.0), 1);

            const auto frequencies = music::referenceFrequencies (group.dots, 220.0, 0.0);
            expectEquals ((int) frequencies.size(), 2);
            // Dots are sorted by position, so the divided one (220 x 2^0.415) comes first
            // and the multiplied one (220 x 2^0.585 == 330) second.
            expectWithinAbsoluteError (frequencies[0], 293.3333333, 0.01);
            expectWithinAbsoluteError (frequencies[1], 330.0, 0.01);
        }

        beginTest (NI_TXT ("groupFrequencyAt 是画布画线用的那条规则"));

        {
            // The anchor itself, at its own position.
            expectWithinAbsoluteError (music::groupFrequencyAt (220.0, 0.0, 0.0), 220.0, 1.0e-9);
            expectWithinAbsoluteError (music::groupFrequencyAt (440.0, 0.5, 0.5), 440.0, 1.0e-9);

            // One octave up is twice the anchor, whichever end of the ladder it is on.
            expectWithinAbsoluteError (music::groupFrequencyAt (220.0, 0.0, 1.0), 440.0, 1.0e-9);
            expectWithinAbsoluteError (music::groupFrequencyAt (220.0, 0.5, 1.5), 440.0, 1.0e-9);

            // A position below the anchor folds back down (half the frequency per
            // octave), which is what the negative side of the ladder is.
            expectWithinAbsoluteError (music::groupFrequencyAt (440.0, 0.5, -0.5), 220.0, 1.0e-9);
            expectWithinAbsoluteError (music::groupFrequencyAt (440.0, 0.0, -1.0), 220.0, 1.0e-9);

            // And a fifth above the anchor: 440 x 2^(log2 1.5) is the 1:3 the ratio
            // keys produce.
            expectWithinAbsoluteError (music::groupFrequencyAt (440.0, 0.0, std::log2 (3.0)),
                                       1320.0, 1.0e-6);

            // It is the same rule referenceFrequencies uses for a pinned group.
            ReferenceGroup group;
            music::addDotAt (group, 0.25, 0);
            music::addDotAt (group, 0.5, 1);

            const auto frequencies = music::referenceFrequencies (group.dots, 440.0, 0.5);
            expectEquals ((int) frequencies.size(), 2);
            expectWithinAbsoluteError (frequencies[0],
                                       music::groupFrequencyAt (440.0, 0.5, 0.25), 1.0e-9);
            expectWithinAbsoluteError (frequencies[1],
                                       music::groupFrequencyAt (440.0, 0.5, 0.5), 1.0e-9);
        }

        beginTest ("group reference lines are cropped, coloured and toggleable");

        {
            ReferenceLineGroup group;
            group.anchorHz = 220.0;
            group.anchorPosition = 0.0;

            ReferenceDot upper;
            upper.position = music::ratioToPosition (3.0);
            upper.colourIndex = 3;

            ReferenceDot lower;
            lower.position = music::ratioToPosition (1.0 / 3.0);
            lower.colourIndex = 5;

            group.dots = { lower, upper };

            const auto groupEntry = music::makeGroupEntry (1, group);
            const auto lines = ReferenceLineTests::linesFor ({ groupEntry }, {});
            expect (lines.size() > 4);

            const auto hasFrequency = [&lines] (double hz)
            {
                return std::any_of (lines.begin(), lines.end(),
                                    [hz] (const music::ReferenceLine& line)
                                    { return std::abs (line.hz - hz) < 0.01; });
            };

            expect (hasFrequency (660.0));       // 3 x 220, one octave up from the folded 330
            expect (hasFrequency (146.6667));    // 220 / 1.5

            for (const auto& line : lines)
            {
                expect (line.isGroup);
                expect (line.interactive);
                expect (! line.dashed);
                expect (! line.isTarget);
                expect (line.colourIndex == 3 || line.colourIndex == 5);
            }

            auto hidden = group;
            hidden.visible = false;
            expectEquals ((int) ReferenceLineTests::linesFor ({ music::makeGroupEntry (1, hidden) },
                                                              {}).size(),
                          0);
        }

        beginTest ("the combined builder keeps the note's own families as well");

        {
            ReferenceLineGroup group;
            group.anchorHz = 220.0;
            group.anchorPosition = 0.0;

            ReferenceDot dot;
            dot.position = music::ratioToPosition (3.0);
            dot.colourIndex = 2;
            group.dots = { dot };

            const auto note = ReferenceLineTests::noteAt (220.0);
            const auto lines = ReferenceLineTests::linesFor ({ music::makeGroupEntry (1, group),
                                                               music::makeNoteRatioEntry (2, note, 2.0) },
                                                             { note });

            const auto countMatching = [&lines] (bool wantGroup)
            {
                return (int) std::count_if (lines.begin(), lines.end(),
                                            [wantGroup] (const music::ReferenceLine& line)
                                            { return line.isGroup == wantGroup; });
            };

            expect (countMatching (false) > 0);
            expect (countMatching (true) > 0);
        }
    }
};

class RatioArrowTests : public juce::UnitTest
{
public:
    RatioArrowTests() : juce::UnitTest ("RatioArrow", "nidaw") {}

    static music::ReferenceLine targetLine (double hz, double ratio, int side)
    {
        music::ReferenceLine line;
        line.hz         = hz;
        line.isTarget   = true;
        line.interactive = true;
        line.ratio      = ratio;
        line.targetSide = side;
        return line;
    }

    void runTest() override
    {
        beginTest ("a multiplied target line points up to the note it came from");

        {
            // 660 Hz is 220 Hz times three, so the arrow runs from 220 up to 660.
            const auto arrow = music::describeRatioArrow (targetLine (660.0, 3.0, +1));

            expect (arrow.visible);
            expectWithinAbsoluteError (arrow.headHz, 660.0, 1.0e-9);
            expectWithinAbsoluteError (arrow.tailHz, 220.0, 1.0e-9);
            expectWithinAbsoluteError (arrow.ratio, 3.0, 1.0e-9);
            expectEquals (arrow.direction, 1);
            expectEquals (music::ratioArrowLabel (arrow), NI_TXT ("正 1:3"));
        }

        beginTest ("a divided target line points down to the note it came from");

        {
            const auto arrow = music::describeRatioArrow (targetLine (220.0 / 3.0, 3.0, -1));

            expect (arrow.visible);
            expectWithinAbsoluteError (arrow.headHz, 220.0 / 3.0, 1.0e-9);
            expectWithinAbsoluteError (arrow.tailHz, 220.0, 1.0e-9);
            expectEquals (arrow.direction, -1);
            expectEquals (music::ratioArrowLabel (arrow), NI_TXT ("负 1:3"));
        }

        beginTest ("only ratio target lines get an arrow");

        {
            // The base note's own octaves are dashed and carry no ratio.
            music::ReferenceLine own;
            own.hz = 220.0;
            own.dashed = true;
            own.interactive = false;
            expect (! music::describeRatioArrow (own).visible);

            // A hand-typed hertz line.
            music::ReferenceLine manual;
            manual.hz = 440.0;
            manual.isManual = true;
            expect (! music::describeRatioArrow (manual).visible);

            // A reference group dot.
            music::ReferenceLine group;
            group.hz = 660.0;
            group.isGroup = true;
            expect (! music::describeRatioArrow (group).visible);

            // The octave ratio has no target families at all, and a target line
            // that somehow carries it must not produce a zero length arrow.
            expect (! music::describeRatioArrow (targetLine (440.0, 2.0, +1)).visible);
            expect (! music::describeRatioArrow (targetLine (440.0, 0.0, +1)).visible);
            expect (! music::describeRatioArrow (targetLine (440.0, 3.0, 0)).visible);
        }

        beginTest ("a custom ratio works too, and follows the arrow direction");

        {
            const auto up = music::describeRatioArrow (targetLine (330.0, 1.5, +1));

            expect (up.visible);
            expectWithinAbsoluteError (up.tailHz, 220.0, 1.0e-9);
            expectEquals (up.direction, 1);
            expectEquals (music::ratioArrowLabel (up),
                          NI_TXT ("正 ") + music::formatRatioText (1.5));

            // A ratio below one swaps which side sits higher, and the caption
            // follows the arrow, not the multiplication.
            const auto flipped = music::describeRatioArrow (testLineForRatio (0.5));

            expect (flipped.visible);
            expectEquals (flipped.direction, -1);
            expect (music::ratioArrowLabel (flipped).startsWith (NI_TXT ("负")));
        }

        beginTest ("finding a line by frequency copies it out");

        {
            const auto lines = std::vector<music::ReferenceLine> { targetLine (220.0, 3.0, +1),
                                                                  targetLine (660.0, 3.0, +1) };

            const auto found = music::findLineNear (lines, 660.0);
            expect (found.has_value());
            expect (found.has_value() && std::abs (found->hz - 660.0) < 1.0e-9);

            expect (! music::findLineNear (lines, 500.0).has_value());
        }
    }

private:
    /** The line the multiplied family of a ratio below one would produce. */
    static music::ReferenceLine testLineForRatio (double ratio)
    {
        return targetLine (440.0 * ratio, ratio, +1);
    }
};

class RatioTextTests : public juce::UnitTest
{
public:
    RatioTextTests() : juce::UnitTest ("RatioText", "nidaw") {}

    void runTest() override
    {
        beginTest ("parsing");

        expectWithinAbsoluteError (music::parseRatioText ("1:3"), 3.0, 1.0e-9);
        expectWithinAbsoluteError (music::parseRatioText ("3"), 3.0, 1.0e-9);
        expectWithinAbsoluteError (music::parseRatioText ("1:2"), 2.0, 1.0e-9);
        expectWithinAbsoluteError (music::parseRatioText ("1/3"), 3.0, 1.0e-9);
        expectWithinAbsoluteError (music::parseRatioText ("3:1"), 1.0 / 3.0, 1.0e-9);
        expectEquals (music::parseRatioText (juce::String()), 0.0);
        expectEquals (music::parseRatioText ("abc"), 0.0);

        beginTest ("formatting");

        expectEquals (music::formatRatioText (3.0), juce::String ("1:3"));
        expectEquals (music::formatRatioText (2.0), juce::String ("1:2"));
        expect (music::formatRatioText (0.3333333333).startsWith ("3:1"));
    }
};

class TempoMapTests : public juce::UnitTest
{
public:
    TempoMapTests() : juce::UnitTest ("TempoMap", "nidaw") {}

    void runTest() override
    {
        beginTest ("4:4 at 120 bpm");

        const music::TempoMap map ({ TempoMarker { 0, 4, 4, 120.0 } });

        expectEquals (map.ticksPerBeatAt (0), 960.0);
        expectWithinAbsoluteError (map.secondsAtTick (960), 0.5, 1.0e-9);
        expectWithinAbsoluteError (map.secondsAtTick (3840), 2.0, 1.0e-9);
        expectEquals (map.barStartTick (2), (juce::int64) 3840);
        expectEquals (map.barStartTick (3), (juce::int64) 7680);
        expectEquals (map.tickAtSeconds (2.0), (juce::int64) 3840);

        int bar = 0, beat = 0, tick = 0;

        map.tickToBarBeat (0, bar, beat, tick);
        expectEquals (bar, 1);
        expectEquals (beat, 1);
        expectEquals (tick, 0);

        map.tickToBarBeat (960, bar, beat, tick);
        expectEquals (bar, 1);
        expectEquals (beat, 2);

        map.tickToBarBeat (3840, bar, beat, tick);
        expectEquals (bar, 2);
        expectEquals (beat, 1);

        expectEquals (music::TempoMap::formatPosition (2, 1, 0), juce::String ("002:01:000"));

        beginTest ("tempo change on bar 2");

        const music::TempoMap m2 ({ TempoMarker { 0, 4, 4, 120.0 },
                                    TempoMarker { 3840, 4, 4, 60.0 } });

        expectWithinAbsoluteError (m2.secondsAtTick (3840), 2.0, 1.0e-9);
        expectWithinAbsoluteError (m2.secondsAtTick (3840 + 960), 3.0, 1.0e-9);
        expectEquals (m2.barAtTick (3840), 2);

        beginTest ("6:8 puts the beat on an eighth note");

        const music::TempoMap m3 ({ TempoMarker { 0, 6, 8, 120.0 } });
        expectEquals (m3.ticksPerBeatAt (0), 480.0);
        expectEquals (m3.barStartTick (2), (juce::int64) 2880);   // 6 * 480

        beginTest ("snapping");

        expectEquals (music::TempoMap::snapToGrid (1000, 960.0), (juce::int64) 960);
        expectEquals (music::TempoMap::snapToGrid (1500, 960.0), (juce::int64) 1920);
        expectEquals (music::TempoMap::snapToGrid (1000, 480.0), (juce::int64) 960);

        beginTest (NI_TXT ("the metronome grid clicks every beat and accents bar starts"));

        {
            const music::TempoMap fourFour ({ TempoMarker { 0, 4, 4, 120.0 } });

            // Starting exactly on a beat clicks that beat; starting between beats
            // waits for the next one.
            auto click = music::firstClickAtOrAfter (fourFour, 0);
            expectEquals (click.tick, (juce::int64) 0);
            expect (click.downbeat, "tick 0 is the downbeat");

            click = music::firstClickAtOrAfter (fourFour, 1);
            expectEquals (click.tick, (juce::int64) 960);
            expect (! click.downbeat);

            click = music::firstClickAtOrAfter (fourFour, 960);
            expectEquals (click.tick, (juce::int64) 960);
            expect (! click.downbeat);

            // Walking beat by beat: accents land on 0, 3840, 7680 ...
            auto accents = 0;
            auto caseTick = (juce::int64) 0;

            for (int i = 0; i < 8; ++i)
            {
                const auto walked = music::nextClickAfter (fourFour, caseTick);
                caseTick = walked.tick;

                if (i == 7)                      // the 8th beat is bar 3 beat 1
                {
                    expectEquals (caseTick, (juce::int64) 7680);
                    expect (walked.downbeat, "bar 3 beat 1 is accented");
                }

                if (walked.downbeat)
                    ++accents;
            }

            expectEquals (accents, 2, NI_TXT ("two accents in eight beats of 4:4"));

            // A 3/4 bar accents every third beat.
            const music::TempoMap threeFour ({ TempoMarker { 0, 3, 4, 120.0 } });

            // 3/4: bar 2 starts at 3 beats = 2880 ticks.
            expect (music::firstClickAtOrAfter (threeFour, 2880).downbeat,
                    NI_TXT ("beat 4 of 3:4 is the next bar's downbeat"));
            expect (! music::firstClickAtOrAfter (threeFour, 1920).downbeat,
                    NI_TXT ("the third beat of a 3:4 bar is not accented"));
        }
    }
};

class EditorModeTests : public juce::UnitTest
{
public:
    EditorModeTests() : juce::UnitTest ("EditorMode", "nidaw") {}

    void runTest() override
    {
        beginTest ("the semitone grid is 12 tone equal temperament at A4 = 440");
        {
            expectWithinAbsoluteError (music::piano::frequencyOf (69), 440.0, 1.0e-9);
            expectWithinAbsoluteError (music::piano::frequencyOf (60), 261.6255653005986, 1.0e-9);
            expectWithinAbsoluteError (music::piano::frequencyOf (81), 880.0, 1.0e-9);

            expectEquals (music::piano::noteForFrequency (440.0), 69);
            expectEquals (music::piano::noteForFrequency (261.6255653005986), 60);
            expectEquals (music::piano::noteForFrequency (220.0), 57);

            // 660 Hz is the 1:3 a user writes by hand: it is not a semitone, and
            // the piano window pulls it to the nearest one.
            expectEquals (music::piano::noteForFrequency (660.0), 76);
            expectWithinAbsoluteError (music::piano::quantize (660.0),
                                       music::piano::frequencyOf (76), 1.0e-9);
            expect (music::piano::quantize (660.0) < 660.0);

            // Something already on a semitone stays exactly where it is.
            expectWithinAbsoluteError (music::piano::quantize (440.0), 440.0, 1.0e-9);

            // Frequencies outside the MIDI range clamp to its ends.
            expectEquals (music::piano::noteForFrequency (4.0), 0);
            expectEquals (music::piano::noteForFrequency (90000.0), 127);
        }

        beginTest ("the keyboard knows its black keys and its octave names");
        {
            expectEquals (music::piano::pitchClassOf (60), 0);     // C
            expectEquals (music::piano::pitchClassOf (61), 1);     // C#
            expectEquals (music::piano::pitchClassOf (72), 0);     // C one octave up

            expect (! music::piano::isBlackKey (60));
            expect (music::piano::isBlackKey (61));
            expect (! music::piano::isBlackKey (64));
            expect (music::piano::isBlackKey (66));
            expect (! music::piano::isBlackKey (65));
            expect (music::piano::isBlackKey (68));
            expect (music::piano::isBlackKey (70));

            expectEquals (music::piano::noteName (60), juce::String ("C4"));
            expectEquals (music::piano::noteName (69), juce::String ("A4"));
            expectEquals (music::piano::noteName (61), juce::String ("C#4"));
            expectEquals (music::piano::noteName (48), juce::String ("C3"));
        }

        beginTest ("the editor mode survives the project round trip");
        {
            auto project = makeProjectWithSynthTrack();
            project.view.editorMode = EditorMode::piano;
            project.view.showReferenceLinesInPianoMode = false;

            Project reloaded;
            juce::String error;

            expect (projectFromJson (projectToJson (project), reloaded, error), error);
            expect (reloaded.view.editorMode == EditorMode::piano);
            expect (! reloaded.view.showReferenceLinesInPianoMode);

            project.view.editorMode = EditorMode::reference;
            project.view.showReferenceLinesInPianoMode = true;

            expect (projectFromJson (projectToJson (project), reloaded, error), error);
            expect (reloaded.view.editorMode == EditorMode::reference);
            expect (reloaded.view.showReferenceLinesInPianoMode);
        }

        beginTest ("a project written before the piano window opens in the reference view");
        {
            // The view object of an older file, with none of the new fields.
            const juce::String json (R"({"format":"nidaw-project","version":1,)"
                                     R"("view":{"centreHz":440.0,"octavesVisible":4.0}})");

            Project reloaded;
            juce::String error;

            expect (projectFromJson (json, reloaded, error), error);
            expect (reloaded.view.editorMode == EditorMode::reference);
            expect (reloaded.view.showReferenceLinesInPianoMode);
        }
    }
};

namespace
{
    VerticalAxisTests   verticalAxisTests;
    ReferenceLineTests  referenceLineTests;
    ReferenceLineEntryTests referenceLineEntryTests;
    NearestLineTests    nearestLineTests;
    DragMathTests       dragMathTests;
    ReferenceGroupMathTests referenceGroupMathTests;
    RatioArrowTests     ratioArrowTests;
    RatioTextTests      ratioTextTests;
    TempoMapTests       tempoMapTests;
    EditorModeTests     editorModeTests;
}
