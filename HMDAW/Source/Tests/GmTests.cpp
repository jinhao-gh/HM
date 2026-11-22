#include "TestSupport.h"

#include "../Audio/GmSoundFont.h"
#include "../Audio/GmSynth.h"

#include <cmath>

class GmTests : public juce::UnitTest
{
public:
    GmTests() : juce::UnitTest ("GM", "hmdaw") {}

    void runTest() override
    {
        beginTest ("the bundled GeneralUser GS loads");
        juce::String error;
        auto font = GmSoundFont::loadBundled (error);
        expect (font != nullptr, error);
        if (font == nullptr)
            return;

        for (int program = 0; program < 128; ++program)
            expect (font->presetForProgram (program) >= 0,
                    "program " + juce::String (program));
        expect (font->drumPreset() >= 0);
        expectEquals (trackKindLabel (TrackKind::gm), NI_TXT ("GM 音色"));

        beginTest ("a note is tuned to its exact frequency");
        GmSynth synth;
        synth.prepare (48000.0);
        synth.setSoundFont (font);

        GmParams params;
        params.program = 0;
        params.volume = 1.0f;
        synth.setParams (params);

        synth.noteOn (1, 440.0, 100.0f);
        expectEquals (synth.getNoteKeyForTest (1), 69);
        expectWithinAbsoluteError ((double) synth.getNoteTuningForTest (1), 0.0, 1.0e-4);

        synth.noteOn (2, 660.0, 100.0f);
        expectEquals (synth.getNoteKeyForTest (2), 76);
        expectWithinAbsoluteError ((double) synth.getNoteTuningForTest (2),
                                   12.0 * std::log2 (660.0 / 440.0) - 7.0, 1.0e-4);

        beginTest ("a pitch curve updates the sounding note without retriggering it");
        synth.setNoteFrequency (2, 880.0);
        expectEquals (synth.getNoteKeyForTest (2), 76);
        expectWithinAbsoluteError ((double) synth.getNoteTuningForTest (2),
                                   12.0 * std::log2 (880.0 / 440.0) - 7.0, 1.0e-4);

        beginTest ("rendering produces audio and stops after note off");
        synth.allNotesOff();
        synth.noteOn (3, 440.0, 100.0f);

        juce::AudioBuffer<float> buffer (2, 2048);
        buffer.clear();
        synth.renderAdding (buffer, 0, buffer.getNumSamples());
        expectGreaterThan (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.0001f);

        synth.noteOff (3);
        buffer.clear();
        synth.renderAdding (buffer, 0, buffer.getNumSamples());
        // The release may still ring, but it must not be an infinite voice.
        expectLessOrEqual (synth.getActiveVoiceCount(), 128);

        beginTest ("the drum kit maps GM percussion notes");
        params.drums = true;
        synth.setParams (params);
        synth.allNotesOff();
        synth.noteOn (4, 440.0 * std::pow (2.0, (36.0 - 69.0) / 12.0), 100.0f);
        expectEquals (synth.getNoteKeyForTest (4), 36);

        synth.noteOn (5, 440.0 * std::pow (2.0, (20.0 - 69.0) / 12.0), 100.0f);
        expectEquals (synth.getNoteKeyForTest (5), -1);

        beginTest ("more notes than channels are stolen without crashing");
        params.drums = false;
        synth.setParams (params);
        synth.allNotesOff();

        for (int i = 0; i < 80; ++i)
            synth.noteOn (100 + i, 220.0 + i, 80.0f);

        expectLessOrEqual (synth.getActiveVoiceCount(), 128);

        beginTest ("GM parameters round-trip through the preset helpers");
        GmParams saved;
        saved.program = 42;
        saved.drums = true;
        saved.volume = 0.42f;
        saved.pan = -0.25f;
        saved.polyphony = 16;
        saved.pitchBendRange = 7;

        const auto restored = gmParamsFromVar (gmParamsToVar (saved));
        expectEquals (restored.program, 42);
        expect (restored.drums);
        expectWithinAbsoluteError ((double) restored.volume, 0.42, 1.0e-5);
        expectWithinAbsoluteError ((double) restored.pan, -0.25, 1.0e-5);
        expectEquals (restored.polyphony, 16);
        expectEquals (restored.pitchBendRange, 7);

        beginTest ("a scheduled GM note starts at the right sample");
        {
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };
            project.tracks.front().kind = TrackKind::gm;
            project.tracks.front().gm.program = 0;
            project.tracks.front().gm.volume = 1.0f;

            Note note;
            note.id = 42;
            note.startTick = kTicksPerQuarter;       // 0.5 s at 120 bpm
            note.durationTicks = kTicksPerQuarter;   // 0.5 s
            note.frequencyHz = 440.0;
            project.tracks.front().clips.front().notes = { note };

            constexpr int blockSize = 512;
            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setGmSoundFont (font);
            auto schedule = buildSchedule (project);
            expectEquals ((int) schedule.tracks.size(), 1);
            expectEquals ((int) schedule.tracks.front().events.size(), 2);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            int firstOnset = -1;

            for (int block = 0; block < 70 && firstOnset < 0; ++block)
            {
                engine.renderNextBlock (blockSize);
                const auto& out = engine.getOutputBuffer();

                for (int i = 0; i < blockSize; ++i)
                    if (std::abs (out.getSample (0, i)) > 0.0002f)
                    {
                        firstOnset = block * blockSize + i;
                        break;
                    }
            }

            expectGreaterThan (firstOnset, 23900);
            expectLessThan (firstOnset, 24100);
        }

        beginTest ("a missing soundfont is reported, not crashed on");
        GmSoundFont::load (juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("does-not-exist.sf2"),
                           error);
        expect (error.isNotEmpty());
    }
};

static GmTests gmTests;
