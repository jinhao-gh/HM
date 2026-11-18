#include "TestSupport.h"

class MidiInputMapTests : public juce::UnitTest
{
public:
    MidiInputMapTests() : juce::UnitTest ("MidiInputMap", "hmdaw") {}

    void runTest() override
    {
        beginTest (NI_TXT ("循环模式：一格在键盘上上下重复，音高跟着升降八度"));

        {
            midimap::Map map;
            map.mode = midimap::Map::Mode::looping;
            map.anchorNote = 60;                     // C4
            midimap::setLoopKeys (map, 5);
            map.setFrequency (60, 220.0);
            map.setFrequency (62, 330.0);
            map.setFrequency (64, 440.0);

            expectWithinAbsoluteError (map.frequencyFor (60), 220.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (62), 330.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (64), 440.0, 1.0e-9);

            // One tile up: the same pattern, an octave higher (×2) - playing up the
            // keyboard plays the same scale higher, not the same notes again.
            expectWithinAbsoluteError (map.frequencyFor (65), 440.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (67), 660.0, 1.0e-9);

            // ...and one tile down: ÷2, with everything below the anchor folding
            // down rather than towards zero (floor, not truncation).
            expectWithinAbsoluteError (map.frequencyFor (55), 110.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (57), 165.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (54), 110.0, 1.0e-9);   // 440 ÷ 4

            // Two tiles up / three down, on a five-semitone tile.
            expectWithinAbsoluteError (map.frequencyFor (70), 880.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (49), 55.0, 1.0e-9);

            // A step of the tile nobody bound is silent.
            expectEquals (map.frequencyFor (61), 0.0);
            // ...in every octave: the holes stay holes.
            expectEquals (map.frequencyFor (61 + 5), 0.0);
            expectEquals (map.frequencyFor (61 - 5), 0.0);
            expect (! map.isBound (61));
            expectEquals (map.boundKeyCount(), 3);

            // The editor only offers the tile's own keys while looping.
            const auto visible = map.visibleNotes();
            expectEquals ((int) visible.size(), 5);
            expectEquals (visible.front(), 60);
            expectEquals (visible.back(), 64);
        }

        beginTest (NI_TXT ("一格 12 个半音：白键连圆点、黑键留空、整组跟着键盘升降八度"));

        {
            // The user's case: a C major reference group's seven dots are dragged
            // onto the seven white keys, so the tile has values on the white-key
            // steps and holes on the black ones.
            midimap::Map map;
            map.mode = midimap::Map::Mode::looping;
            map.anchorNote = 60;                       // C4
            midimap::setLoopKeys (map, 12);

            const double cMajor[7] = { 261.6255653, 293.6647679, 329.6275569, 349.2282314,
                                       391.9954360, 440.0000000, 493.8833013 };
            const int whiteKeys[7] = { 0, 2, 4, 5, 7, 9, 11 };

            for (int i = 0; i < 7; ++i)
                map.setFrequency (60 + whiteKeys[i], cMajor[i]);

            // Left to right across one octave: the major scale, rising.
            for (int i = 0; i < 7; ++i)
                expectWithinAbsoluteError (map.frequencyFor (60 + whiteKeys[i]), cMajor[i], 1.0e-6);

            // The five black keys are holes, so they stay silent...
            expectEquals (map.frequencyFor (61), 0.0);
            expectEquals (map.frequencyFor (63), 0.0);
            expectEquals (map.frequencyFor (66), 0.0);
            expectEquals (map.frequencyFor (68), 0.0);
            expectEquals (map.frequencyFor (70), 0.0);
            expectEquals (map.boundKeyCount(), 7);

            // ...in every octave (an empty step is empty wherever it lands)...
            expectEquals (map.frequencyFor (61 + 12), 0.0);
            expectEquals (map.frequencyFor (61 - 12), 0.0);

            // ...while the scale itself rides up and down with the keyboard.
            expectWithinAbsoluteError (map.frequencyFor (72), 523.2511306, 1.0e-6);   // C5
            expectWithinAbsoluteError (map.frequencyFor (71 + 12), 987.7666026, 1.0e-6);  // B5
            expectWithinAbsoluteError (map.frequencyFor (59), 246.9416506, 1.0e-6);   // B3
            expectWithinAbsoluteError (map.frequencyFor (48), 130.8127827, 1.0e-6);   // C3
            expectWithinAbsoluteError (map.frequencyFor (47), 123.4708253, 1.0e-6);   // B2

            // The names the editor shows are the plain MIDI ones.
            expectEquals (midimap::noteName (60), juce::String ("C4"));
            expectEquals (midimap::noteName (71), juce::String ("B4"));
            expectEquals (midimap::noteName (49), juce::String ("C#3"));
        }

        beginTest (NI_TXT ("非循环模式：每个音符号自己一条"));

        {
            midimap::Map map;
            map.mode = midimap::Map::Mode::free;
            map.setFrequency (0, 55.0);
            map.setFrequency (127, 7040.0);
            map.setFrequency (60, 261.625565);

            expectWithinAbsoluteError (map.frequencyFor (0), 55.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (127), 7040.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (60), 261.625565, 1.0e-6);
            expectEquals (map.frequencyFor (61), 0.0);
            expectEquals ((int) map.visibleNotes().size(), 128);

            // Unbinding is just as easy as binding.
            map.clearFrequency (60);
            expect (! map.isBound (60));
            expectEquals (map.boundKeyCount(), 2);

            // Out of range notes are simply not there.
            map.setFrequency (-1, 100.0);
            map.setFrequency (200, 100.0);
            expectEquals (map.frequencyFor (-1), 0.0);
            expectEquals (map.frequencyFor (200), 0.0);
        }

        beginTest (NI_TXT ("改「循环键数」时保住还能装下的那些键"));

        {
            midimap::Map map;
            map.anchorNote = 60;
            midimap::setLoopKeys (map, 12);

            for (int i = 0; i < 12; ++i)
                map.setFrequency (60 + i, 100.0 + i);

            midimap::setLoopKeys (map, 4);
            expectEquals (map.loopKeys, 4);
            expectEquals ((int) map.tile.size(), 4);
            expectWithinAbsoluteError (map.frequencyFor (60), 100.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (63), 103.0, 1.0e-9);
            // The tile is the pattern that repeats, so one key past its end is the
            // first step again - an octave higher, not a hole.
            expectWithinAbsoluteError (map.frequencyFor (64), 200.0, 1.0e-9);

            // Growing pads with empty steps instead of inventing notes: the steps
            // that were cropped away when it shrank do not come back.
            midimap::setLoopKeys (map, 8);
            expectEquals ((int) map.tile.size(), 8);
            expectEquals (map.frequencyFor (67), 0.0);
            expectWithinAbsoluteError (map.frequencyFor (68), 200.0, 1.0e-9);

            // The bounds are respected whatever is asked for.
            midimap::setLoopKeys (map, 999);
            expectEquals (map.loopKeys, midimap::Map::kMaxLoopKeys);
            midimap::setLoopKeys (map, 0);
            expectEquals (map.loopKeys, midimap::Map::kMinLoopKeys);
        }

        beginTest (NI_TXT ("「这一格里的第几个」与音名"));

        {
            midimap::Map map;
            map.anchorNote = 60;
            midimap::setLoopKeys (map, 7);

            expectEquals (map.tileOffsetOf (60), 0);
            expectEquals (map.tileOffsetOf (63), 3);
            expectEquals (map.tileOffsetOf (67), 0);   // one tile up
            expectEquals (map.tileOffsetOf (59), 6);   // one step below the anchor
            expectEquals (map.tileOffsetOf (53), 0);   // a whole tile below

            expectEquals (midimap::noteName (60), juce::String ("C4"));
            expectEquals (midimap::noteName (61), juce::String ("C#4"));
            expectEquals (midimap::noteName (21), juce::String ("A0"));
        }

        beginTest (NI_TXT ("两张映射是不是同一张"));

        {
            midimap::Map a, b;
            expect (midimap::isSame (a, b));

            a.setFrequency (60, 440.0);
            expect (! midimap::isSame (a, b));

            b.setFrequency (60, 440.0);
            expect (midimap::isSame (a, b));

            b.anchorNote = 62;
            expect (! midimap::isSame (a, b));

            midimap::Map freeA, freeB;
            freeA.mode = freeB.mode = midimap::Map::Mode::free;
            freeA.setFrequency (60, 440.0);
            freeB.setFrequency (60, 440.0);
            expect (midimap::isSame (freeA, freeB));

            freeB.setFrequency (60, 441.0);
            expect (! midimap::isSame (freeA, freeB));
        }

        beginTest (NI_TXT ("一个键可以连多个圆点：加一条、去掉一条、整键断开"));

        {
            midimap::Map map;
            map.anchorNote = 60;
            midimap::setLoopKeys (map, 12);

            // Two dots on one key: both sound, low to high, and the single-value read
            // (what a caption wants) is the lower of them.
            map.addFrequency (60, 440.0);
            map.addFrequency (60, 660.0);

            expectEquals (map.bindingCount (60), 2);
            expectEquals ((int) map.frequenciesFor (60).size(), 2);
            expectWithinAbsoluteError (map.frequenciesFor (60)[0], 440.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequenciesFor (60)[1], 660.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequencyFor (60), 440.0, 1.0e-9);

            // Adding the same one again does not stack it twice...
            map.addFrequency (60, 660.0);
            expectEquals (map.bindingCount (60), 2);

            // ...and it is entered low-to-high whatever order it arrives in.
            map.addFrequency (62, 990.0);
            map.addFrequency (62, 110.0);
            expectWithinAbsoluteError (map.frequenciesFor (62)[0], 110.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequenciesFor (62)[1], 990.0, 1.0e-9);

            // The tile rides up and down with the keyboard, so every cable of the key
            // moves together (one octave up = both of them doubled).
            expectWithinAbsoluteError (map.frequenciesFor (72)[0], 880.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequenciesFor (72)[1], 1320.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequenciesFor (48)[0], 220.0, 1.0e-9);
            expectWithinAbsoluteError (map.frequenciesFor (48)[1], 330.0, 1.0e-9);

            // One cable at a time (the right-click on a line), then the whole key.
            map.removeFrequency (60, 660.0);
            expectEquals (map.bindingCount (60), 1);
            expectWithinAbsoluteError (map.frequencyFor (60), 440.0, 1.0e-9);

            map.clearFrequency (60);
            expectEquals (map.bindingCount (60), 0);
            expectEquals (map.frequencyFor (60), 0.0);

            // setFrequency is the "replace" form: it drops the rest of the stack.
            map.addFrequency (64, 500.0);
            map.addFrequency (64, 700.0);
            map.setFrequency (64, 900.0);
            expectEquals (map.bindingCount (64), 1);
            expectWithinAbsoluteError (map.frequencyFor (64), 900.0, 1.0e-9);

            // The counts the status line reads: two keys (62 and 64) but three
            // cables, because 62 is a stack.
            expectEquals (map.boundKeyCount(), 2);
            expectEquals (map.cableCount(), 3);

            // 非循环模式 has the same shapes per note.
            midimap::Map freeMap;
            freeMap.mode = midimap::Map::Mode::free;
            freeMap.addFrequency (30, 55.0);
            freeMap.addFrequency (30, 82.5);
            expectEquals (freeMap.bindingCount (30), 2);
            expectWithinAbsoluteError (freeMap.frequencyFor (30), 55.0, 1.0e-9);
            expectEquals (freeMap.bindingCount (42), 0);

            freeMap.removeFrequency (30, 55.0);
            expectEquals (freeMap.bindingCount (30), 1);
            expectWithinAbsoluteError (freeMap.frequencyFor (30), 82.5, 1.0e-9);

            freeMap.clearFrequency (30);
            expectEquals (freeMap.bindingCount (30), 0);

            // A shrunk tile keeps the chords of the steps that still fit.
            midimap::Map shrunk;
            shrunk.anchorNote = 60;
            midimap::setLoopKeys (shrunk, 12);
            shrunk.addFrequency (60, 100.0);
            shrunk.addFrequency (60, 150.0);
            midimap::setLoopKeys (shrunk, 5);
            expectEquals (shrunk.bindingCount (60), 2);

            // And two maps are only "the same" when the stacks match.
            midimap::Map chordA, chordB;
            chordA.addFrequency (60, 100.0);
            chordA.addFrequency (60, 200.0);
            chordB.addFrequency (60, 100.0);
            expect (! midimap::isSame (chordA, chordB));
            chordB.addFrequency (60, 200.0);
            expect (midimap::isSame (chordA, chordB));
            chordB.addFrequency (60, 300.0);
            expect (! midimap::isSame (chordA, chordB));
        }
    }
};

static MidiInputMapTests midiInputMapTests;
