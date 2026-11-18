#include "TestSupport.h"

class PitchShapeTests : public juce::UnitTest
{
public:
    PitchShapeTests() : juce::UnitTest ("PitchShape", "hmdaw") {}

    void runTest() override
    {
        beginTest ("old notes are horizontal");
        {
            Note note;
            note.frequencyHz = 440.0;
            expectEquals (note.tailFrequencyHz, 0.0);
            expectWithinAbsoluteError (note.effectiveTailHz(), 440.0, 1.0e-9);
            expectWithinAbsoluteError (music::pitchAt (note, 0.0), 440.0, 1.0e-9);
            expectWithinAbsoluteError (music::pitchAt (note, 0.5), 440.0, 1.0e-9);
            expectWithinAbsoluteError (music::pitchAt (note, 1.0), 440.0, 1.0e-9);
        }

        beginTest ("log interpolation and flat ends");
        {
            Note note;
            note.frequencyHz = 220.0;
            note.tailFrequencyHz = 880.0;
            note.tiltStart = 0.25f;
            note.tiltEnd = 0.75f;

            expectWithinAbsoluteError (music::pitchAt (note, 0.0), 220.0, 1.0e-9);
            expectWithinAbsoluteError (music::pitchAt (note, 0.25), 220.0, 1.0e-9);
            expectWithinAbsoluteError (music::pitchAt (note, 0.5), 440.0, 1.0e-6);
            expectWithinAbsoluteError (music::pitchAt (note, 0.75), 880.0, 1.0e-6);
            expectWithinAbsoluteError (music::pitchAt (note, 1.0), 880.0, 1.0e-9);
        }

        beginTest ("curvature sign changes the path without overshooting");
        {
            Note note;
            note.frequencyHz = 220.0;
            note.tailFrequencyHz = 880.0;
            note.pitchCurve = 0.5f;
            const auto positive = music::pitchAt (note, 0.25);
            note.pitchCurve = -0.5f;
            const auto negative = music::pitchAt (note, 0.25);
            note.pitchCurve = 0.0f;
            const auto straight = music::pitchAt (note, 0.25);

            expectGreaterThan (positive, straight);
            expectLessThan (negative, straight);

            for (int i = 0; i <= 100; ++i)
            {
                const auto hz = music::pitchAt (note, i / 100.0);
                expect (hz >= 220.0 - 1.0e-6 && hz <= 880.0 + 1.0e-6);
            }
        }

        beginTest ("equal endpoints stay flat even with curvature");
        {
            Note note;
            note.frequencyHz = 440.0;
            note.tailFrequencyHz = 440.0;
            note.pitchCurve = 0.8f;

            for (int i = 0; i <= 20; ++i)
                expectWithinAbsoluteError (music::pitchAt (note, i / 20.0), 440.0, 1.0e-9);
        }

        beginTest ("splitting a shape preserves every point");
        {
            Note original;
            original.frequencyHz = 220.0;
            original.tailFrequencyHz = 880.0;
            original.tiltStart = 0.2f;
            original.tiltEnd = 0.8f;
            original.pitchCurve = 0.6f;

            Note left, right;
            music::sliceNotePitchShape (original, 0.1, 0.65, left);
            music::sliceNotePitchShape (original, 0.65, 0.95, right);

            for (int i = 0; i <= 40; ++i)
            {
                const auto local = 0.1 + 0.55 * i / 40.0;
                expectWithinAbsoluteError (music::pitchAt (left, i / 40.0),
                                           music::pitchAt (original, local), 1.0e-4);
            }

            for (int i = 0; i <= 40; ++i)
            {
                const auto local = 0.65 + 0.30 * i / 40.0;
                expectWithinAbsoluteError (music::pitchAt (right, i / 40.0),
                                           music::pitchAt (original, local), 1.0e-4);
            }
        }

        beginTest ("ties require one contiguous predecessor and no cycles");
        {
            Clip clip;
            Note a, b, c;
            a.id = 1; a.startTick = 0; a.durationTicks = 960;
            b.id = 2; b.startTick = 960; b.durationTicks = 960; b.tieFromNoteId = 1;
            c.id = 3; c.startTick = 1920; c.durationTicks = 960; c.tieFromNoteId = 2;
            clip.notes = { a, b, c };
            music::pruneInvalidNoteTies (clip);
            expectEquals (clip.notes[1].tieFromNoteId, (juce::int64) 1);
            expectEquals (clip.notes[2].tieFromNoteId, (juce::int64) 2);

            clip.notes[2].tieFromNoteId = 1;
            music::pruneInvalidNoteTies (clip);
            expectEquals (clip.notes[1].tieFromNoteId, (juce::int64) 0);
            expectEquals (clip.notes[2].tieFromNoteId, (juce::int64) 0);

            clip.notes[1].tieFromNoteId = 2;
            music::pruneInvalidNoteTies (clip);
            expectEquals (clip.notes[1].tieFromNoteId, (juce::int64) 0);
        }

        beginTest ("pitch shape survives serialization and old notes stay flat");
        {
            Note note;
            note.id = 7;
            note.frequencyHz = 261.6255653;
            note.tailFrequencyHz = 392.0;
            note.pitchCurve = -0.35f;
            note.tiltStart = 0.2f;
            note.tiltEnd = 0.8f;
            note.tieFromNoteId = 3;

            const auto restored = noteFromVar (noteToVar (note));
            expectWithinAbsoluteError (restored.frequencyHz, note.frequencyHz, 1.0e-9);
            expectWithinAbsoluteError (restored.tailFrequencyHz, note.tailFrequencyHz, 1.0e-9);
            expectWithinAbsoluteError (restored.pitchCurve, note.pitchCurve, 1.0e-6f);
            expectWithinAbsoluteError (restored.tiltStart, note.tiltStart, 1.0e-6f);
            expectWithinAbsoluteError (restored.tiltEnd, note.tiltEnd, 1.0e-6f);
            expectEquals (restored.tieFromNoteId, note.tieFromNoteId);

            auto* object = new juce::DynamicObject();
            object->setProperty ("id", 9);
            object->setProperty ("frequencyHz", 440.0);
            const auto old = noteFromVar (juce::var (object));
            expectWithinAbsoluteError (old.effectiveTailHz(), 440.0, 1.0e-9);
            expectEquals (old.pitchCurve, 0.0f);
            expectEquals (old.tiltStart, 0.0f);
            expectEquals (old.tiltEnd, 1.0f);
            expectEquals (old.tieFromNoteId, (juce::int64) 0);
        }
    }
};

static PitchShapeTests pitchShapeTests;
