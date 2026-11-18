#include "TestSupport.h"

class ClipTests : public juce::UnitTest
{
public:
    ClipTests() : juce::UnitTest ("Clips", "hmdaw") {}

    static Note makeNote (juce::int64 id, juce::int64 start, juce::int64 duration = 960)
    {
        Note note;
        note.id = id;
        note.startTick = start;
        note.durationTicks = duration;
        note.frequencyHz = 440.0;
        return note;
    }

    static Clip makeClip (juce::int64 id, juce::int64 start, juce::int64 length)
    {
        Clip clip;
        clip.id = id;
        clip.startTick = start;
        clip.lengthTicks = length;
        clip.loopLengthTicks = length;
        return clip;
    }

    void runTest() override
    {
        beginTest ("only the notes inside the window sound");

        {
            auto clip = makeClip (1, 1920, 1920);
            clip.notes = { makeNote (1, 0), makeNote (2, 1920), makeNote (3, 960) };

            const auto placements = music::expandClip (clip);
            expectEquals ((int) placements.size(), 2);
            expectEquals (placements[0].startTick, (juce::int64) 1920);
            expectEquals (placements[1].startTick, (juce::int64) 2880);
        }

        beginTest (NI_TXT ("removeNotesByIds 一次删掉一整组音符"));

        {
            auto clip = makeClip (1, 0, 1920);
            clip.notes = { makeNote (1, 0), makeNote (2, 480), makeNote (3, 960), makeNote (4, 1440) };

            // Several ids at once - what "select several notes, right click one of
            // them, delete" hands over.
            expectEquals (music::removeNotesByIds (clip, { 2, 4 }), 2);
            expectEquals ((int) clip.notes.size(), 2);
            expectEquals (clip.notes[0].id, (juce::int64) 1);
            expectEquals (clip.notes[1].id, (juce::int64) 3);

            // One id, and the ids that are not there.
            expectEquals (music::removeNotesByIds (clip, { 1 }), 1);
            expectEquals (music::removeNotesByIds (clip, { 1, 2, 4 }), 0,
                          NI_TXT ("已经删掉的 id 再来一次什么也不做"));
            expectEquals ((int) clip.notes.size(), 1);
            expectEquals (clip.notes[0].id, (juce::int64) 3);

            // An empty set leaves everything alone.
            expectEquals (music::removeNotesByIds (clip, {}), 0);
            expectEquals ((int) clip.notes.size(), 1);
        }

        beginTest ("a note crossing the window end is cropped");

        {
            auto clip = makeClip (1, 0, 960);
            clip.notes = { makeNote (1, 0, 5000) };

            const auto placements = music::expandClip (clip);
            expectEquals ((int) placements.size(), 1);
            expectEquals (placements.front().endTick, (juce::int64) 960);
        }

        beginTest ("looping tiles the content");

        {
            auto clip = makeClip (1, 0, 3840);
            clip.loopLengthTicks = 960;
            clip.looped = true;
            clip.notes = { makeNote (1, 0), makeNote (2, 480) };

            expectEquals (music::clipRepeatCount (clip), 4);

            const auto placements = music::expandClip (clip);
            expectEquals ((int) placements.size(), 8);
            expectEquals (placements.front().startTick, (juce::int64) 0);
            expectEquals (placements.back().startTick, (juce::int64) 3360);
        }

        beginTest ("looping ignores notes outside one period");

        {
            auto clip = makeClip (1, 0, 3840);
            clip.loopLengthTicks = 960;
            clip.looped = true;
            clip.notes = { makeNote (1, 0), makeNote (2, 1920) };

            const auto placements = music::expandClip (clip);
            expectEquals ((int) placements.size(), 4);
        }

        beginTest ("moving a clip moves its notes with it");

        {
            auto clip = makeClip (1, 0, 1920);
            clip.notes = { makeNote (1, 0), makeNote (2, 960) };

            const auto before = music::expandClip (clip);
            clip.startTick = 4800;
            const auto after = music::expandClip (clip);

            expectEquals ((int) after.size(), (int) before.size());
            expectEquals (after[0].startTick - before[0].startTick, (juce::int64) 4800);
            expectEquals (after[1].startTick - before[1].startTick, (juce::int64) 4800);
        }

        beginTest ("two overlapping clips both sound");

        {
            // Overlaps are allowed now, so nothing is trimmed away and both
            // clips feed the schedule at the same time.
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.clips.clear();

            auto first = makeClip (1, 0, 1920);
            first.notes = { makeNote (10, 0), makeNote (11, 960) };

            auto second = makeClip (2, 960, 1920);   // starts halfway into the first
            second.notes = { makeNote (12, 0) };

            track.clips = { first, second };

            const auto schedule = buildSchedule (project);
            expectEquals ((int) schedule.tracks.size(), 1);
            expectEquals ((int) schedule.tracks.front().events.size(), 6);   // 3 notes, on + off each

            // Both clips survive untouched - the second one no longer eats into
            // the first, so the first keeps its full length and both notes.
            expectEquals ((int) track.clips.size(), 2);
            expectEquals (track.findClip (1)->lengthTicks, (juce::int64) 1920);
            expectEquals ((int) track.findClip (1)->notes.size(), 2);
            expectEquals ((int) track.findClip (2)->notes.size(), 1);
        }

        beginTest ("pasting looks for the first free slot");

        {
            InstrumentTrack track;
            track.clips = { makeClip (1, 0, 1920), makeClip (2, 2400, 1920) };

            expectEquals (music::findFreeSlot (track, 0, 960), (juce::int64) 4320);
            expectEquals (music::findFreeSlot (track, 5000, 960), (juce::int64) 5000);
            expectEquals (music::findFreeSlot (track, 2000, 960), (juce::int64) 4320);
        }

        beginTest ("repeating a clip lays the copies out back to back");

        {
            InstrumentTrack track;
            auto source = makeClip (1, 480, 1920);
            source.notes = { makeNote (1, 0), makeNote (2, 960) };
            track.clips = { source };

            juce::int64 nextClipId = 2;
            const auto created = music::repeatClip (track, 1, 3, nextClipId);

            expectEquals ((int) created.size(), 3);
            expectEquals ((int) track.clips.size(), 4);
            expectEquals (nextClipId, (juce::int64) 5);
            expectEquals (created[0], (juce::int64) 2);
            expectEquals (created[2], (juce::int64) 4);

            // 480 + 1 * 1920, then + 2 * 1920, then + 3 * 1920.
            expectEquals (track.findClip (2)->startTick, (juce::int64) 2400);
            expectEquals (track.findClip (3)->startTick, (juce::int64) 4320);
            expectEquals (track.findClip (4)->startTick, (juce::int64) 6240);

            // The content travels with the copy, still clip-relative.
            expectEquals ((int) track.findClip (4)->notes.size(), 2);
            expectEquals (track.findClip (4)->notes[1].startTick, (juce::int64) 960);
            expectEquals (track.findClip (4)->lengthTicks, (juce::int64) 1920);
        }

        beginTest ("a repeat copies the loop settings");

        {
            InstrumentTrack track;
            auto source = makeClip (1, 0, 3840);
            source.loopLengthTicks = 960;
            source.looped = true;
            track.clips = { source };

            juce::int64 nextClipId = 2;
            music::repeatClip (track, 1, 1, nextClipId);

            const auto* copy = track.findClip (2);
            expect (copy != nullptr);

            if (copy != nullptr)
            {
                expect (copy->looped);
                expectEquals (copy->loopLengthTicks, (juce::int64) 960);
                expectEquals (music::clipRepeatCount (*copy), 4);
            }
        }

        beginTest ("a repeat skips occupied space and leaves no overlap");

        {
            InstrumentTrack track;
            track.clips = { makeClip (1, 0, 1920), makeClip (2, 2000, 400) };

            juce::int64 nextClipId = 3;
            music::repeatClip (track, 1, 1, nextClipId);

            // The copy wants [1920, 3840), which the neighbour occupies, so it
            // lands in the first free slot after it instead and nothing is lost.
            expectEquals ((int) track.clips.size(), 3);
            expect (track.findClip (2) != nullptr);
            expectEquals (track.findClip (2)->startTick, (juce::int64) 2000);
            expectEquals (track.findClip (3)->startTick, (juce::int64) 2400);

            bool overlap = false;

            for (int i = 0; i < (int) track.clips.size(); ++i)
                for (int j = i + 1; j < (int) track.clips.size(); ++j)
                    if (track.clips[(size_t) i].startTick < track.clips[(size_t) j].endTick()
                        && track.clips[(size_t) j].startTick < track.clips[(size_t) i].endTick())
                        overlap = true;

            expect (! overlap);
        }

        beginTest ("a repeat needs a real clip and a positive count");

        {
            InstrumentTrack track;
            track.clips = { makeClip (1, 0, 960) };

            juce::int64 nextClipId = 2;

            expect (music::repeatClip (track, 999, 3, nextClipId).empty());
            expect (music::repeatClip (track, 1, 0, nextClipId).empty());
            expect (music::repeatClip (track, 1, -2, nextClipId).empty());
            expectEquals ((int) track.clips.size(), 1);
            expectEquals (nextClipId, (juce::int64) 2);
        }

        beginTest ("repeating again extends the run instead of landing on it");

        {
            InstrumentTrack track;
            track.clips = { makeClip (1, 0, 960) };

            juce::int64 nextClipId = 2;
            music::repeatClip (track, 1, 1, nextClipId);
            music::repeatClip (track, 1, 1, nextClipId);

            expectEquals ((int) track.clips.size(), 3);
            expectEquals (track.findClip (2)->startTick, (juce::int64) 960);
            expectEquals (track.findClip (3)->startTick, (juce::int64) 1920);
        }

        beginTest ("splitting a clip cuts the notes that straddle the cut");

        {
            InstrumentTrack track;
            auto clip = makeClip (1, 0, 1920);
            clip.name = NI_TXT ("片段 1");
            clip.notes = { makeNote (10, 0, 480),      // wholly on the left
                           makeNote (11, 1440, 480),   // wholly on the right
                           makeNote (12, 480, 960) };  // straddles a cut at 960

            track.clips = { clip };

            const music::TempoMap tempoMap ({ TempoMarker { 0, 4, 4, 120.0 } });
            juce::int64 nextNoteId = 100;
            juce::int64 nextClipId = 2;
            Clip right;

            expect (music::splitClipAt (track.clips.front(), 960, tempoMap, 1,
                                        nextNoteId, nextClipId, right));

            const auto& left = track.clips.front();

            expectEquals (left.lengthTicks, (juce::int64) 960);
            expectEquals (right.startTick, (juce::int64) 960);
            expectEquals (right.lengthTicks, (juce::int64) 960);
            expectEquals (right.id, (juce::int64) 2);
            expectEquals (right.name, left.name);   // both halves keep the part's name

            // The straddling note became two: head on the left, tail on the right.
            expectEquals ((int) left.notes.size(), 2);
            expectEquals ((int) right.notes.size(), 2);
            expectEquals (left.notes[1].startTick, (juce::int64) 480);
            expectEquals (left.notes[1].durationTicks, (juce::int64) 480);
            expectEquals (right.notes[0].startTick, (juce::int64) 0);
            expectEquals (right.notes[0].durationTicks, (juce::int64) 480);
            expectEquals (right.notes[0].id, (juce::int64) 100);   // a fresh id
            expectEquals (right.notes[1].startTick, (juce::int64) 480);   // rebased by 960

            // Either end is too little to cut.
            InstrumentTrack other;
            other.clips = { makeClip (5, 0, 1920) };
            Clip unused;
            expect (! music::splitClipAt (other.clips.front(), 0, tempoMap, 1,
                                          nextNoteId, nextClipId, unused));
            expect (! music::splitClipAt (other.clips.front(), 1920, tempoMap, 1,
                                          nextNoteId, nextClipId, unused));
            expect (! music::splitClipAt (other.clips.front(), 2400, tempoMap, 1,
                                          nextNoteId, nextClipId, unused));
        }

        beginTest ("splitting keeps a looped clip's loop settings");

        {
            InstrumentTrack track;
            auto clip = makeClip (1, 0, 3840);
            clip.looped = true;
            clip.loopLengthTicks = 960;
            track.clips = { clip };

            const music::TempoMap tempoMap ({ TempoMarker { 0, 4, 4, 120.0 } });
            juce::int64 nextNoteId = 100, nextClipId = 2;
            Clip right;

            expect (music::splitClipAt (track.clips.front(), 960, tempoMap, 1,
                                        nextNoteId, nextClipId, right));
            expect (right.looped);
            expectEquals (right.loopLengthTicks, (juce::int64) 960);
            expect (track.clips.front().looped);
        }

        beginTest ("splitting an audio clip advances the right half's file offset");

        {
            InstrumentTrack track;
            auto clip = makeClip (1, 0, 1920);
            clip.audioFilePath = "C:\\samples\\loop.wav";
            clip.audioOffsetSeconds = 1.0;
            track.clips = { clip };

            // 120 bpm 4:4 -> one quarter note (960 ticks) is half a second.
            const music::TempoMap tempoMap ({ TempoMarker { 0, 4, 4, 120.0 } });
            juce::int64 nextNoteId = 100, nextClipId = 2;
            Clip right;

            expect (music::splitClipAt (track.clips.front(), 960, tempoMap, 1,
                                        nextNoteId, nextClipId, right));
            expectWithinAbsoluteError (right.audioOffsetSeconds, 1.5, 1.0e-6);
            expectEquals (right.audioFilePath, track.clips.front().audioFilePath);
        }

        beginTest ("gluing needs a plain note clip that starts exactly at the end");

        {
            auto first = makeClip (1, 0, 960);
            first.notes = { makeNote (10, 0, 480) };

            auto second = makeClip (2, 960, 960);
            second.notes = { makeNote (11, 0, 480) };

            expect (music::canGlueClips (first, second));

            juce::int64 nextNoteId = 100;

            expect (music::glueClips (first, second, nextNoteId));
            expectEquals (first.lengthTicks, (juce::int64) 1920);
            expectEquals ((int) first.notes.size(), 2);
            expectEquals (first.notes[1].startTick, (juce::int64) 960);   // rebased
            expectEquals (first.notes[1].id, (juce::int64) 100);          // fresh id

            // Not adjacent, looped, and audio clips are all refused.
            // (Not called "far": windows.h still defines that as a macro.)
            auto distant = makeClip (3, 3840, 960);
            expect (! music::canGlueClips (first, distant));

            auto looped = second;
            looped.looped = true;
            expect (! music::canGlueClips (makeClip (4, 0, 960), looped));

            auto audioClip = makeClip (5, 960, 960);
            audioClip.audioFilePath = "C:\\samples\\a.wav";
            expect (! music::canGlueClips (makeClip (6, 0, 960), audioClip));
        }
    }
};

class AudioTrackTests : public juce::UnitTest
{
public:
    AudioTrackTests() : juce::UnitTest ("AudioTrack", "hmdaw") {}

    static std::shared_ptr<const LoadedSample> makeRamp (double seconds)
    {
        auto loaded = std::make_shared<LoadedSample>();
        loaded->sourceSampleRate = 48000.0;
        loaded->filePath = "（内置测试样本）";

        const auto length = (int) (48000.0 * seconds);
        loaded->audio.setSize (1, length);
        auto* data = loaded->audio.getWritePointer (0);

        // A ramp makes it obvious which part of the file ended up where.
        for (int i = 0; i < length; ++i)
            data[i] = (float) i / (float) length;

        return loaded;
    }

    static AudioPlacement placement (std::shared_ptr<const LoadedSample> sample,
                                     double start, double length, double sourceOffset = 0.0)
    {
        AudioPlacement p;
        p.startSeconds = start;
        p.lengthSeconds = length;
        p.sourceOffsetSeconds = sourceOffset;
        p.gain = 1.0f;
        p.sample = std::move (sample);
        return p;
    }

    /** Writes a short ramp to a temporary WAV so the loader has something real to
        read - the schedule only places clips whose file actually loads. */
    void runTest() override
    {
        beginTest ("an audio clip only sounds inside its window");

        {
            const std::vector<AudioPlacement> placements { placement (makeRamp (1.0), 0.5, 0.25) };

            // Before the clip: silence.
            juce::AudioBuffer<float> before (2, 4800);
            before.clear();
            renderAudioClips (placements, before, 0, 4800, 0.0, 48000.0, 1.0f, 1.0f);
            expectLessThan (before.getRMSLevel (0, 0, 4800), 1.0e-6f);

            // Inside the clip: sound.
            juce::AudioBuffer<float> inside (2, 4800);
            inside.clear();
            renderAudioClips (placements, inside, 0, 4800, 0.5, 48000.0, 1.0f, 1.0f);
            expectGreaterThan (inside.getRMSLevel (0, 0, 4800), 0.01f);

            // After the clip: silence again.
            juce::AudioBuffer<float> after (2, 4800);
            after.clear();
            renderAudioClips (placements, after, 0, 4800, 1.0, 48000.0, 1.0f, 1.0f);
            expectLessThan (after.getRMSLevel (0, 0, 4800), 1.0e-6f);
        }

        beginTest ("the clip's source offset reads from the right place in the file");


        {
            auto sample = makeRamp (1.0);
            const auto* data = sample->audio.getReadPointer (0);

            const std::vector<AudioPlacement> placements { placement (sample, 0.0, 0.25, 0.5) };

            juce::AudioBuffer<float> buffer (2, 480);
            buffer.clear();
            renderAudioClips (placements, buffer, 0, 480, 0.0, 48000.0, 1.0f, 1.0f);

            // Half a second into a one second ramp starts at 0.5.
            expectWithinAbsoluteError ((double) buffer.getSample (0, 0), (double) data[24000], 1.0e-6);
        }

        beginTest ("an audio track becomes a placed clip in the schedule");

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::audio;
            track.clips.clear();

            const auto file = writeTempWav ("hmdaw-audio-schedule.wav", 2.0);

            Clip clip = makeDefaultClip (project);
            clip.audioFilePath = file.getFullPathName();
            clip.audioFileName = file.getFileName();
            clip.startTick = kTicksPerQuarter * 2;      // bar 1 beat 3 -> 1.0 s at 120 bpm
            clip.lengthTicks = kTicksPerQuarter * 2;    // 1.0 s
            clip.loopLengthTicks = clip.lengthTicks;
            clip.audioOffsetSeconds = 0.25;
            clip.audioGain = 0.5f;
            track.clips.push_back (std::move (clip));

            // No notes at all, but the audio clip still gives the project a length.
            const auto schedule = buildSchedule (project);
            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::audio);
            expectEquals ((int) schedule.tracks.front().audioClips.size(), 1);
            expect (schedule.tracks.front().events.empty());
            expectWithinAbsoluteError (schedule.tracks.front().audioClips.front().startSeconds,
                                       1.0, 1.0e-9);
            expectWithinAbsoluteError (schedule.tracks.front().audioClips.front().lengthSeconds,
                                       1.0, 1.0e-9);
            expectWithinAbsoluteError (schedule.tracks.front().audioClips.front().sourceOffsetSeconds,
                                       0.25, 1.0e-9);
            expectWithinAbsoluteError ((double) schedule.tracks.front().audioClips.front().gain,
                                       0.5, 1.0e-6);
            expectWithinAbsoluteError (schedule.lengthSeconds, 2.0, 1.0e-9);

            file.deleteFile();
        }

        beginTest ("an audio clip round trips through the project file");

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::audio;

            Clip clip = makeDefaultClip (project);
            clip.audioFilePath = "/tmp/some-file.wav";
            clip.audioFileName = "some-file.wav";
            clip.audioOffsetSeconds = 1.25;
            clip.audioGain = 0.75f;
            track.clips.push_back (std::move (clip));

            Project reloaded;
            juce::String error;
            expect (projectFromJson (projectToJson (project), reloaded, error), error);
            expect (reloaded.tracks.front().kind == TrackKind::audio);

            const auto& reloadedClip = reloaded.tracks.front().clips.back();
            expect (reloadedClip.isAudio());
            expectEquals (reloadedClip.audioFilePath, juce::String ("/tmp/some-file.wav"));
            expectEquals (reloadedClip.audioFileName, juce::String ("some-file.wav"));
            expectWithinAbsoluteError (reloadedClip.audioOffsetSeconds, 1.25, 1.0e-9);
            expectWithinAbsoluteError ((double) reloadedClip.audioGain, 0.75, 1.0e-6);
        }
    }
};

namespace
{
    ClipTests           clipTests;
    AudioTrackTests     audioTrackTests;
}
