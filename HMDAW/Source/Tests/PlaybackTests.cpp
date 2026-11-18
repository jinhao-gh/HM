#include "TestSupport.h"

namespace
{

/** A hosted instrument that adds a known constant to every sample.

    The mixer's tests use it to see exactly what the track fader, the meter and the
    master fader did with a signal, without depending on what the FM synth happens to
    put out this week.
*/
class TestToneInstrument : public HostedInstrument
{
public:
    void prepare (double, int) override {}
    void reset() override {}
    void noteOn (juce::int64 noteId, float, float) override { sounding.insert (noteId); }
    void noteOff (juce::int64 noteId) override { sounding.erase (noteId); }
    void allNotesOff() override {}
    bool isActive() const noexcept override { return true; }
    juce::MemoryBlock captureState() const override { return {}; }

    /** What the engine sent it: the drawn curves go to an external instrument
        rather than being applied by the engine itself. */
    void sendController (int controllerNumber, int value) override
    {
        controllersSeen.push_back ({ controllerNumber, value });
    }

    std::vector<std::pair<int, int>> controllersSeen;
    /** The notes it is holding right now - the sustain test checks that the engine
        did *not* hold a note-off back for an external instrument. */
    std::set<juce::int64> sounding;

    void renderAdding (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       float gainLeft, float gainRight) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer.addSample (0, startSample + i, level * gainLeft);

            if (buffer.getNumChannels() > 1)
                buffer.addSample (1, startSample + i, level * gainRight);
        }
    }

    /** The sample it plays. Setting it to 0 is how a test makes the track go quiet. */
    float level = 0.9f;
};

/** One insert slot pointing at a (made-up) plugin: having a slot is what makes the
    engine ask the factory for an instance in the first place. */
inline EffectSlot makeEffectSlot (const juce::String& name)
{
    EffectSlot slot;
    slot.kind = 0;
    slot.name = name;
    slot.pluginPath = "C:\\plugins\\" + name + ".vst3";
    return slot;
}

} // namespace

class ScheduleTests : public juce::UnitTest
{
public:
    ScheduleTests() : juce::UnitTest ("Schedule", "hmdaw") {}

    void runTest() override
    {
        beginTest ("notes become sorted note on / note off events");

        auto project = makeProjectWithSynthTrack();
        project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

        Note late;
        late.id = 2;
        late.startTick = 3840;          // bar 2 -> 2.0 s
        late.durationTicks = 960;
        late.frequencyHz = 660.0;

        Note early;
        early.id = 1;
        early.startTick = 0;
        early.durationTicks = 960;      // 0.0 s .. 0.5 s
        early.frequencyHz = 440.0;

        project.tracks.front().clips.front().notes = { late, early };

        const auto schedule = buildSchedule (project);

        expectEquals ((int) schedule.tracks.size(), 1);
        expectEquals ((int) schedule.tracks.front().events.size(), 4);
        expectWithinAbsoluteError (schedule.lengthSeconds, 2.5, 1.0e-9);

        const auto& events = schedule.tracks.front().events;

        expectEquals (events[0].noteId, (juce::int64) 1);
        expect (events[0].isNoteOn);
        expectWithinAbsoluteError (events[0].timeSeconds, 0.0, 1.0e-9);

        expectEquals (events[1].noteId, (juce::int64) 1);
        expect (! events[1].isNoteOn);
        expectWithinAbsoluteError (events[1].timeSeconds, 0.5, 1.0e-9);

        expectEquals (events[2].noteId, (juce::int64) 2);
        expect (events[2].isNoteOn);
        expectWithinAbsoluteError (events[2].timeSeconds, 2.0, 1.0e-9);
        expectWithinAbsoluteError ((double) events[2].frequency, 660.0, 1.0e-6);

        beginTest ("a tied pair has one attack and a continuous pitch stream");

        Note tieFirst;
        tieFirst.id = 10;
        tieFirst.startTick = 0;
        tieFirst.durationTicks = 960;
        tieFirst.frequencyHz = 220.0;
        tieFirst.tailFrequencyHz = 330.0;

        Note tieSecond;
        tieSecond.id = 11;
        tieSecond.startTick = 960;
        tieSecond.durationTicks = 960;
        tieSecond.frequencyHz = 330.0;
        tieSecond.tailFrequencyHz = 440.0;
        tieSecond.tieFromNoteId = 10;

        project.tracks.front().clips.front().notes = { tieFirst, tieSecond };
        const auto tied = buildSchedule (project);
        const auto& tiedEvents = tied.tracks.front().events;
        int noteOns = 0, noteOffs = 0, pitchEvents = 0;

        for (const auto& event : tiedEvents)
        {
            if (event.isPitch)
                ++pitchEvents;
            else if (event.isNoteOn)
                ++noteOns;
            else
                ++noteOffs;
        }

        expectEquals (noteOns, 1);
        expectEquals (noteOffs, 1);
        expectGreaterThan (pitchEvents, 10);
        expectEquals (tiedEvents.front().frequency, 220.0f);
        expectWithinAbsoluteError (tied.lengthSeconds, 1.0, 1.0e-9);

        tieSecond.tieFromNoteId = 0;
        project.tracks.front().clips.front().notes = { tieFirst, tieSecond };
        const auto separate = buildSchedule (project);
        int separateOns = 0;

        for (const auto& event : separate.tracks.front().events)
            if (event.isNoteOn && ! event.isPitch)
                ++separateOns;

        expectEquals (separateOns, 2);

        beginTest ("track volume and pan are folded into the schedule");

        project.tracks.front().volumeDb = -6.0f;
        project.tracks.front().pan = 1.0f;

        const auto schedule2 = buildSchedule (project);
        expectWithinAbsoluteError ((double) schedule2.tracks.front().volumeLinear, 0.5011872336, 1.0e-6);
        expectEquals (schedule2.tracks.front().pan, 1.0f);

        beginTest ("a stem render carries only the tracks it was asked for");

        {
            auto two = makeProjectWithSynthTrack();
            two.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            const auto firstId = two.tracks.front().id;
            const auto secondId = addTrack (two, TrackKind::synth);

            Note low;
            low.id = 1;
            low.durationTicks = 960;
            low.frequencyHz = 220.0;

            Note high;
            high.id = 2;
            high.durationTicks = 960;
            high.frequencyHz = 660.0;

            two.tracks.front().clips.front().notes = { low };
            two.tracks.back().clips.front().notes = { high };

            expectEquals ((int) buildSchedule (two).tracks.size(), 2);

            const auto onlySecond = buildSchedule (two, { secondId });
            expectEquals ((int) onlySecond.tracks.size(), 1);
            expectEquals (onlySecond.tracks.front().trackId, secondId);
            expectEquals ((int) onlySecond.tracks.front().events.size(), 2);   // on + off

            for (const auto& event : onlySecond.tracks.front().events)
                expectWithinAbsoluteError ((double) event.frequency, 660.0, 1.0e-6);

            const auto onlyFirst = buildSchedule (two, { firstId });
            expectEquals ((int) onlyFirst.tracks.size(), 1);
            expectEquals (onlyFirst.tracks.front().trackId, firstId);

            // An empty filter means "everything", not "nothing".
            expectEquals ((int) buildSchedule (two, {}).tracks.size(), 2);
        }

        beginTest ("a muted note makes no events and does not lengthen the render");

        {
            auto muted = makeProjectWithSynthTrack();
            muted.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            Note quiet;
            quiet.id = 1;
            quiet.startTick = 0;
            quiet.durationTicks = 960;
            quiet.frequencyHz = 440.0;
            quiet.muted = true;

            Note loud;
            loud.id = 2;
            loud.startTick = 0;
            loud.durationTicks = 480;
            loud.frequencyHz = 660.0;

            muted.tracks.front().clips.front().notes = { quiet, loud };

            const auto withMute = buildSchedule (muted);
            expectEquals ((int) withMute.tracks.front().events.size(), 2);
            expectWithinAbsoluteError (withMute.lengthSeconds, 0.25, 1.0e-9);

            for (const auto& event : withMute.tracks.front().events)
                expectWithinAbsoluteError ((double) event.frequency, 660.0, 1.0e-6);

            // Unmuting brings the note straight back.
            muted.tracks.front().clips.front().notes.front().muted = false;

            const auto withoutMute = buildSchedule (muted);
            expectEquals ((int) withoutMute.tracks.front().events.size(), 4);
            expectWithinAbsoluteError (withoutMute.lengthSeconds, 0.5, 1.0e-9);
        }

        beginTest (NI_TXT ("track mute and solo decide what is audible"));

        {
            // Two tracks, each with one note.
            auto caseProject = makeProjectWithSynthTrack();
            caseProject.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            const auto secondId = addTrack (caseProject, TrackKind::synth);
            expect (secondId != 0);

            Note low;
            low.id = 1;
            low.durationTicks = 960;
            low.frequencyHz = 220.0;

            Note high;
            high.id = 2;
            high.durationTicks = 960;
            high.frequencyHz = 660.0;

            caseProject.tracks.front().clips.front().notes = { low };
            caseProject.tracks.back().clips.front().notes = { high };

            auto& first = caseProject.tracks.front();
            auto& second = caseProject.tracks.back();

            // Nothing muted, nothing soloed: both sound.
            expect (isTrackAudible (caseProject, first));
            expect (isTrackAudible (caseProject, second));
            expectEquals ((int) buildSchedule (caseProject).tracks.size(), 2);

            // Mute wins, even when the same track is soloed.
            first.muted = true;
            expect (! isTrackAudible (caseProject, first));

            first.soloed = true;
            expect (! isTrackAudible (caseProject, first),
                    NI_TXT ("mute beats solo on the same track"));

            first.soloed = false;
            first.muted = false;

            // Solo is additive: the other track goes quiet, the soloed one stays.
            second.soloed = true;
            expect (! isTrackAudible (caseProject, first));
            expect (isTrackAudible (caseProject, second));

            const auto soloed = buildSchedule (caseProject);
            expectEquals ((int) soloed.tracks.size(), 1);

            if (! soloed.tracks.empty())
            {
                expectEquals (soloed.tracks.front().trackId, second.id);
                expectEquals ((int) soloed.tracks.front().events.size(), 2);
            }

            // A track the loop asks for on its own but mute keeps silent gives an
            // empty schedule rather than a silent lane.
            first.muted = true;
            second.soloed = false;
            expectEquals ((int) buildSchedule (caseProject, { first.id }).tracks.size(), 0);

            first.muted = false;
            expectEquals ((int) buildSchedule (caseProject, { first.id }).tracks.size(), 1);
        }
    }
};

class PlaybackTests : public juce::UnitTest
{
public:
    PlaybackTests() : juce::UnitTest ("Playback", "hmdaw") {}

    static Project makeProjectWithLongNote()
    {
        auto project = makeProjectWithSynthTrack();
        project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

        Note longNote;
        longNote.id = 1;
        longNote.startTick = 0;
        longNote.durationTicks = kTicksPerQuarter * 8;   // four seconds at 120 bpm
        longNote.frequencyHz = 440.0;

        project.tracks.front().clips.front().notes = { longNote };
        project.tracks.front().synth.patch.ops[0].rate[3] = 99;   // fast release
        return project;
    }

    void runTest() override
    {
        constexpr int blockSize = 512;

        beginTest ("seeking releases whatever was sounding");

        {
            const auto project = makeProjectWithLongNote();

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            for (int i = 0; i < 20; ++i)
                engine.renderNextBlock (blockSize);

            expectGreaterThan (engine.getOutputBuffer().getRMSLevel (0, 0, blockSize), 0.01f);
            expectGreaterThan (engine.getActiveVoiceCount(), 0);

            // Jump past the end of the note. Without the release-on-seek handling
            // the old voice would hang and keep sounding here.
            engine.setPositionSeconds (6.0);

            auto latePeak = 0.0f;

            for (int i = 0; i < 60; ++i)
            {
                engine.renderNextBlock (blockSize);

                if (i >= 40)
                    latePeak = juce::jmax (latePeak,
                                           engine.getOutputBuffer().getMagnitude (0, 0, blockSize));
            }

            expectLessThan (latePeak, 0.001f);
            expectEquals (engine.getActiveVoiceCount(), 0);
            expectWithinAbsoluteError (engine.getPositionSeconds(),
                                       6.0 + 60.0 * blockSize / 48000.0, 0.01);
        }

        beginTest (NI_TXT ("the transport wraps at the loop end and plays it again"));

        {
            // A one-beat note at the top, and a two-beat loop (1.0 s at 120 bpm).
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            Note note;
            note.id = 1;
            note.startTick = 0;
            note.durationTicks = kTicksPerQuarter;    // half a second
            note.frequencyHz = 440.0;
            project.tracks.front().clips.front().notes = { note };

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setLoopRegion (0.0, 1.0, true);
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            // Two and a half seconds: the loop has to come round at least twice.
            const auto blocks = (int) std::ceil (2.5 * 48000.0 / blockSize);
            auto bursts = 0;
            auto wasLoud = false;
            auto overshoot = 0.0;

            for (int i = 0; i < blocks; ++i)
            {
                engine.renderNextBlock (blockSize);

                const auto rms = engine.getOutputBuffer().getRMSLevel (0, 0, blockSize);
                const auto loud = rms > 0.01f;

                if (loud && ! wasLoud)
                    ++bursts;

                wasLoud = loud;
                overshoot = juce::jmax (overshoot, engine.getPositionSeconds());
            }

            expectGreaterThan (bursts, 1, NI_TXT ("the note sounds once per pass"));
            expect (overshoot <= 1.0 + 1.0e-6,
                    NI_TXT ("playback never runs past the loop end (got ")
                        + juce::String (overshoot, 4) + ")");
            expectLessThan (engine.getPositionSeconds(), 1.0 + 1.0e-6);
        }

        beginTest (NI_TXT ("the metronome clicks on the beat and accents the downbeat"));

        {
            // Nothing to play: every sample that comes out is the metronome.
            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule>());
            engine.setTempoMap (std::make_shared<const music::TempoMap> (
                std::vector<TempoMarker> { { 0, 4, 4, 120.0 } }));
            engine.setMetronomeEnabled (true);
            engine.setPlaying (true);

            // One beat at 120 bpm is 24000 samples: gather the peak of the first
            // 50 ms of each beat, plus a quiet window between them.
            constexpr int beatSamples = 24000;
            constexpr int windowSamples = 2400;

            std::vector<float> peaks;
            std::vector<float> between;

            for (int i = 0; i < 3 * beatSamples; i += blockSize)
            {
                engine.renderNextBlock (blockSize);

                const auto* samples = engine.getOutputBuffer().getReadPointer (0);
                const auto peakOf = [samples] (int, int count)
                {
                    auto peak = 0.0f;

                    for (int j = 0; j < count; ++j)
                        peak = juce::jmax (peak, std::abs (samples[j]));

                    return peak;
                };

                const auto offset = i % beatSamples;

                if (offset < windowSamples)
                    peaks.push_back (peakOf (0, juce::jmin (windowSamples - offset, blockSize)));
                else if (offset >= beatSamples / 2 && offset < beatSamples / 2 + windowSamples)
                    between.push_back (peakOf (0, blockSize));
            }

            expect (! peaks.empty(), NI_TXT ("the first beats were measured"));
            expectGreaterThan ((double) *std::max_element (peaks.begin(), peaks.end()), 0.05,
                               NI_TXT ("a click is audible"));
            expectLessThan ((double) *std::max_element (between.begin(), between.end()), 0.001,
                            NI_TXT ("and it is silent between beats"));

            // Turning it off leaves the transport silent.
            engine.setMetronomeEnabled (false);

            // Let the last click ring out first (~35 ms decay).
            for (int i = 0; i < 40; ++i)
                engine.renderNextBlock (blockSize);

            expectLessThan ((double) engine.getOutputBuffer().getMagnitude (0, 0, blockSize), 0.001);
        }

        beginTest ("the audition voice sounds even while the transport is stopped");

        {
            const auto project = makeProjectWithLongNote();

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setPlaying (false);

            // A quick, sustaining patch so the audition voice is easy to measure.
            SynthParams params;
            params.patch.ops[0].outputLevel = 99;
            params.patch.ops[0].rate[0] = 99;
            params.patch.ops[0].rate[1] = 99;
            params.patch.ops[0].rate[2] = 99;
            params.patch.ops[0].rate[3] = 99;
            params.patch.ops[0].level[0] = 99;
            params.patch.ops[0].level[1] = 99;
            params.patch.ops[0].level[2] = 99;
            params.patch.ops[0].level[3] = 0;

            engine.triggerPreviewNote (params, 440.0, 127.0f);

            auto peak = 0.0f;

            for (int i = 0; i < 10; ++i)
            {
                engine.renderNextBlock (blockSize);
                peak = juce::jmax (peak, engine.getOutputBuffer().getMagnitude (0, 0, blockSize));
            }

            expectGreaterThan (peak, 0.01f);
            expectEquals (engine.getActiveVoiceCount(), 1);

            engine.releasePreviewNote();

            for (int i = 0; i < 40; ++i)
                engine.renderNextBlock (blockSize);

            expectEquals (engine.getActiveVoiceCount(), 0);
        }

        beginTest (NI_TXT ("实时音符：走带停着也出声，而且走的是这条轨的乐器与推子"));

        {
            auto project = makeProjectWithLongNote();
            project.tracks.front().clips.front().notes.clear();   // nothing scheduled

            // A quick, sustaining patch so the live note is easy to measure, and a
            // fast release so "it stops" is measurable too.
            auto& params = project.tracks.front().synth;
            params.patch.ops[0].outputLevel = 99;

            for (int stage = 0; stage < 4; ++stage)
            {
                params.patch.ops[0].rate[stage] = 99;
                params.patch.ops[0].level[stage] = stage < 3 ? 99 : 0;
            }

            const auto trackId = project.tracks.front().id;

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, nullptr, nullptr)));
            engine.setPlaying (false);

            engine.noteOnTrack (trackId, 4242, 440.0, 127.0f);

            auto peak = 0.0f;

            for (int i = 0; i < 10; ++i)
            {
                engine.renderNextBlock (blockSize);
                peak = juce::jmax (peak, engine.getOutputBuffer().getMagnitude (0, 0, blockSize));
            }

            expectGreaterThan (peak, 0.01f);

            // The track's own fader is obeyed: this is the difference between a live
            // note and the instrument windows' preview voice.
            auto quiet = project;
            quiet.tracks.front().volumeDb = -24.0f;

            PlaybackEngine quietEngine;
            quietEngine.prepare (48000.0, blockSize);
            quietEngine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (quiet, {}, nullptr, nullptr)));
            quietEngine.setPlaying (false);
            quietEngine.noteOnTrack (trackId, 4242, 440.0, 127.0f);

            auto quietPeak = 0.0f;

            for (int i = 0; i < 10; ++i)
            {
                quietEngine.renderNextBlock (blockSize);
                quietPeak = juce::jmax (quietPeak,
                                        quietEngine.getOutputBuffer().getMagnitude (0, 0, blockSize));
            }

            expectLessThan (quietPeak, peak * 0.5f);

            // Note-off lets the voice go.
            engine.noteOffTrack (trackId, 4242);

            for (int i = 0; i < 60; ++i)
                engine.renderNextBlock (blockSize);

            expectEquals (engine.getActiveVoiceCount(), 0);
            expectEquals (engine.getOutputBuffer().getMagnitude (0, 0, blockSize), 0.0f);

            // A track that is not there is simply ignored (no crash, no sound).
            engine.noteOnTrack (9999, 1, 440.0, 100.0f);
            engine.renderNextBlock (blockSize);
            expectEquals (engine.getOutputBuffer().getMagnitude (0, 0, blockSize), 0.0f);
            expectEquals (engine.getLiveQueueOverflowCountForTest(), 0);
        }

        beginTest (NI_TXT ("实时音符与工程音符各用一套编号：两个音不会挤进一个通道"));
        {
            const auto block = 512;

            // A VST track whose first note is playing (the schedule numbers its
            // events 1, 2, 3…) plus a live note the caller *also* calls 1: that is
            // exactly the shape that used to end up as one note - one MIDI channel,
            // one pitch bend - and pull the project's note onto the live one's pitch.
            auto project = makeDefaultProject();
            const auto trackId = addTrack (project, TrackKind::vst);
            auto* track = project.findTrack (trackId);
            track->vst.pluginPath = "C:\\plugins\\Test.vst3";
            track->clips.front().notes.clear();

            Note note;
            note.id = project.nextNoteId++;      // 1, like the schedule's first event
            note.startTick = 0;
            note.durationTicks = kTicksPerQuarter * 8;
            note.frequencyHz = 660.0;
            note.velocity = 100;
            track->clips.front().notes.push_back (note);

            auto instrument = std::make_shared<testsupport::CountingInstrument>();
            const auto factory = [instrument] (juce::int64, const VstParams&)
                                     -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 4; ++i)
                engine.renderNextBlock (block);

            expectEquals ((int) instrument->noteOnIds.size(), 1,
                          NI_TXT ("工程里的那个音先响起来"));
            expectEquals (instrument->noteOnIds.front(), (juce::int64) 1,
                          NI_TXT ("调度给它的编号就是 1"));

            // The live note the MIDI keyboard's own counter would also call 1.
            engine.noteOnTrack (trackId, 1, 880.0, 100.0f);
            engine.renderNextBlock (block);

            expectEquals ((int) instrument->noteOnIds.size(), 2,
                          NI_TXT ("实时音也送到了乐器上"));
            expect (instrument->noteOnIds[0] != instrument->noteOnIds[1],
                    NI_TXT ("两个音拿到的是两个不同的编号（改之前会是同一个：同一个通道、同一条弯音）"));
            expectEquals (instrument->noteOnIds[1], liveNoteIdFor (1),
                          NI_TXT ("实时音用的是它自己的私有编号"));
            expect (instrument->noteOnIds[1] < 0,
                    NI_TXT ("实时编号一律是负数，撞不到工程音符的 1..N"));

            // ...and the three live sources cannot collide with each other either
            // (a MIDI keyboard counts from 1, the two auditions from -1000 / -100000).
            expect (liveNoteIdFor (1) != liveNoteIdFor (-1000)
                        && liveNoteIdFor (1) != liveNoteIdFor (-100000)
                        && liveNoteIdFor (-1000) != liveNoteIdFor (-100000),
                    NI_TXT ("三个实时来源翻译之后仍然互不相同"));
        }

        beginTest (NI_TXT ("实时控制器：表情走进轨道的增益、踏板挂住实时音符（第四十七批）"));
        {
            const auto block = 512;

            auto project = makeProjectWithLongNote();
            project.tracks.front().clips.front().notes.clear();   // nothing scheduled

            // A quick, sustaining patch, so a live note is easy to measure.
            auto& params = project.tracks.front().synth;
            params.patch.ops[0].outputLevel = 99;

            for (int stage = 0; stage < 4; ++stage)
            {
                params.patch.ops[0].rate[stage] = 99;
                params.patch.ops[0].level[stage] = stage < 3 ? 99 : 0;
            }

            const auto trackId = project.tracks.front().id;
            const auto makeEngine = [&project] (int blockSize)
            {
                auto engine = std::make_unique<PlaybackEngine>();
                engine->prepare (48000.0, blockSize);
                engine->setSchedule (std::make_shared<const RenderSchedule> (
                    buildSchedule (project, {}, nullptr, nullptr)));
                engine->setPlaying (false);
                return engine;
            };

            const auto renderPeak = [] (PlaybackEngine& engine, int blocks)
            {
                auto peak = 0.0f;

                for (int i = 0; i < blocks; ++i)
                {
                    engine.renderNextBlock (block);
                    peak = juce::jmax (peak, engine.getOutputBuffer().getMagnitude (0, 0, 512));
                }

                return peak;
            };

            // 表情 (CC11) played from the keyboard: it multiplies this track's
            // output exactly as a drawn expression curve does.
            {
                auto engine = makeEngine (block);
                engine->noteOnTrack (trackId, 1, 440.0, 127.0f);

                const auto loud = renderPeak (*engine, 8);

                engine->sendControllerToTrack (trackId, 11, 0);

                const auto quiet = renderPeak (*engine, 8);

                expectGreaterThan (loud, 0.01f, NI_TXT ("活着的时候有声音"));
                expectLessThan (quiet, loud * 0.1f,
                                NI_TXT ("实时 CC11 把这一轨拉下去了（与画出来的曲线同一条路）"));
            }

            // 踏板 (CC64) played from the keyboard: the note-off of a live note is
            // held back until the pedal comes up - the same rule the drawn curve
            // follows, and the only thing that makes a pedal audible on a built-in
            // instrument.
            {
                auto engine = makeEngine (block);
                engine->noteOnTrack (trackId, 2, 440.0, 127.0f);
                engine->sendControllerToTrack (trackId, 64, 127);
                engine->noteOffTrack (trackId, 2);

                const auto held = renderPeak (*engine, 30);

                expectGreaterThan (held, 0.01f, NI_TXT ("踏板踩着时，实时音符的 note-off 被挂住"));

                engine->sendControllerToTrack (trackId, 64, 0);
                renderPeak (*engine, 20);        // let the release finish

                const auto after = renderPeak (*engine, 5);

                expectLessThan (after, held * 0.1f, NI_TXT ("抬起踏板以后那个音被放掉了"));
            }
        }

        beginTest (NI_TXT ("VST 轨的实时控制器交给插件，引擎不重复处理"));
        {
            const auto block = 512;
            auto instrument = std::make_shared<TestToneInstrument>();

            RenderSchedule schedule;
            TrackRenderInfo info;
            info.trackId = 7;
            info.kind = TrackKind::vst;
            info.instrument = instrument;
            info.volumeLinear = 1.0f;
            schedule.tracks.push_back (info);

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (false);

            engine.noteOnTrack (7, 1, 440.0, 100.0f);
            engine.sendControllerToTrack (7, 64, 127);   // 延音踏板
            engine.sendControllerToTrack (7, 1, 64);     // 调制轮
            engine.sendControllerToTrack (7, 11, 90);    // 表情
            engine.noteOffTrack (7, 1);
            engine.renderNextBlock (block);

            expectEquals ((int) instrument->controllersSeen.size(), 3,
                          NI_TXT ("三个实时控制器都转给了插件"));

            auto sawPedal = false, sawMod = false, sawExpression = false;

            for (const auto& [controller, value] : instrument->controllersSeen)
            {
                juce::ignoreUnused (value);

                if (controller == 64) sawPedal = true;
                if (controller == 1)  sawMod = true;
                if (controller == 11) sawExpression = true;
            }

            expect (sawPedal && sawMod && sawExpression,
                    NI_TXT ("踏板 / 调制轮 / 表情都到了插件上"));

            // ...and the engine did *not* keep the note alive for it: the pedal is
            // the plugin's own business on an external instrument.
            expect (! instrument->sounding.count (liveNoteIdFor (1)),
                    NI_TXT ("note-off 真的到了插件（引擎没有替它挂住这个音）"));

            // An unknown track id is ignored rather than crashing or leaking.
            engine.sendControllerToTrack (999, 64, 127);
            engine.renderNextBlock (block);
            expectEquals ((int) instrument->controllersSeen.size(), 3,
                          NI_TXT ("未知轨道 id 的控制器被忽略"));
        }

        beginTest (NI_TXT ("VST 轨可以改成按半音演奏：整机弯音的插件也有退路"));
        {
            const auto block = 512;

            auto project = makeDefaultProject();
            const auto trackId = addTrack (project, TrackKind::vst);
            auto* track = project.findTrack (trackId);
            track->vst.pluginPath = "C:\\plugins\\Test.vst3";
            track->clips.front().notes.clear();

            // 660 Hz is a 1:3 - 1.9 cents above E5 - so it only comes out at 660 Hz if
            // the instrument is given a per-note bend.
            Note note;
            note.id = project.nextNoteId++;
            note.startTick = 0;
            note.durationTicks = kTicksPerQuarter * 4;
            note.frequencyHz = 660.0;
            note.velocity = 100;
            track->clips.front().notes.push_back (note);

            auto instrument = std::make_shared<testsupport::CountingInstrument>();
            const auto factory = [instrument] (juce::int64, const VstParams&)
                                     -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, factory)));
            engine.setPlaying (true);
            engine.renderNextBlock (block);

            expectEquals ((int) instrument->noteOnFrequencies.size(), 1);
            expectWithinAbsoluteError ((double) instrument->noteOnFrequencies.front(), 660.0, 0.01,
                                       NI_TXT ("默认模式下乐器拿到的是精确频率"));

            // The same note on a track set to 按半音演奏: the instrument is handed the
            // nearest semitone instead, so nothing needs a bend at all.
            auto semitoneProject = project;
            semitoneProject.tracks.front().vst.equalTemperament = true;

            auto semitoneInstrument = std::make_shared<testsupport::CountingInstrument>();
            const auto semitoneFactory = [semitoneInstrument] (juce::int64, const VstParams&)
                                             -> std::shared_ptr<HostedInstrument>
            {
                return semitoneInstrument;
            };

            PlaybackEngine semitoneEngine;
            semitoneEngine.prepare (48000.0, block);
            semitoneEngine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (semitoneProject, {}, semitoneFactory)));
            semitoneEngine.setPlaying (true);
            semitoneEngine.renderNextBlock (block);

            expectEquals ((int) semitoneInstrument->noteOnFrequencies.size(), 1);
            expectWithinAbsoluteError ((double) semitoneInstrument->noteOnFrequencies.front(),
                                       vst::hzForMidiNote (76), 0.01,
                                       NI_TXT ("按半音演奏时拿到的是最近的半音（E5 = 659.26 Hz）"));
            expectEquals (vst::bendValueFor (0.0), 0,
                          NI_TXT ("那个音不需要弯音：弯音值正好在中间，整机弯音的插件也不会被拉动"));
        }

        beginTest (NI_TXT ("总输出推子进了 RenderSchedule"));

        {
            auto project = makeProjectWithLongNote();

            project.masterVolumeDb = 0.0f;
            expectWithinAbsoluteError ((double) buildSchedule (project).masterGain,
                                       1.0, 1.0e-5);

            // -6 dB is half the amplitude; the fader's bottom is -60 dBFS, which is
            // silence by any measure but not a hard zero (the same convention a
            // track's own volume has always used).
            project.masterVolumeDb = -6.0f;
            expectWithinAbsoluteError ((double) buildSchedule (project).masterGain,
                                       0.5011872, 2.0e-4);

            project.masterVolumeDb = -60.0f;
            expectLessThan (buildSchedule (project).masterGain, 0.002f);

            // Whatever the project says, the fader can only be at -60..+6.
            project.masterVolumeDb = 40.0f;
            expectWithinAbsoluteError ((double) buildSchedule (project).masterGain,
                                       1.9952623, 2.0e-3);
        }

        beginTest (NI_TXT ("总输出推子把整条混音乘一遍（导出走的是同一段代码）"));

        {
            auto project = makeProjectWithLongNote();

            auto loudestBlockRms = [&project, blockSize] (float masterDb)
            {
                auto withMaster = project;
                withMaster.masterVolumeDb = masterDb;

                PlaybackEngine engine;
                engine.prepare (48000.0, blockSize);
                engine.setSchedule (
                    std::make_shared<const RenderSchedule> (buildSchedule (withMaster)));
                engine.setPositionSeconds (0.0);
                engine.setPlaying (true);

                auto peak = 0.0f;

                for (int i = 0; i < 20; ++i)
                {
                    engine.renderNextBlock (blockSize);
                    peak = juce::jmax (peak,
                                       engine.getOutputBuffer().getRMSLevel (0, 0, blockSize));
                }

                return peak;
            };

            const auto atZeroDb = loudestBlockRms (0.0f);
            const auto atMinusSix = loudestBlockRms (-6.0f);

            expectGreaterThan (atZeroDb, 0.001f);
            expectWithinAbsoluteError ((double) atMinusSix, (double) atZeroDb * 0.5011872,
                                       (double) atZeroDb * 0.02);
        }

        beginTest (NI_TXT ("每条轨道的电平表报自己的峰值，削顶读一次就清"));

        {
            auto instrument = std::make_shared<TestToneInstrument>();

            RenderSchedule schedule;
            TrackRenderInfo info;
            info.trackId = 42;
            info.kind = TrackKind::vst;
            info.instrument = instrument;
            info.volumeLinear = 2.0f;   // +6 dB, enough to run past full scale
            schedule.tracks.push_back (info);

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (false);

            engine.renderNextBlock (blockSize);

            // 0.9 through a +6 dB fader at pan centre: 0.9 * 2 * 0.7071, on both sides.
            const auto loud = engine.readTrackMeter (42);

            expectWithinAbsoluteError ((double) loud.peakLeft, 1.2728, 0.01);
            expectWithinAbsoluteError ((double) loud.peakRight, 1.2728, 0.01);
            expect (loud.clipLeft && loud.clipRight);

            // Reading it cleared it.
            const auto again = engine.readTrackMeter (42);

            expect (! again.clipLeft && ! again.clipRight);
            expectWithinAbsoluteError ((double) again.peakLeft, 0.0, 1.0e-6);

            const auto loudMaster = engine.readMasterMeter();

            expect (loudMaster.clipLeft && loudMaster.clipRight);
            expectGreaterThan (loudMaster.peakLeft, 1.0f);
            expect (! engine.readMasterMeter().clipLeft);

            // Nothing new came in, so the next reading is nothing at all: that is
            // what lets the UI tell "still loud" from "went quiet".
            instrument->level = 0.0f;
            engine.renderNextBlock (blockSize);

            const auto quiet = engine.readTrackMeter (42);

            expectWithinAbsoluteError ((double) quiet.peakLeft, 0.0, 1.0e-6);
            expectWithinAbsoluteError ((double) quiet.peakRight, 0.0, 1.0e-6);
            expect (! quiet.clipLeft && ! quiet.clipRight);
        }

        beginTest (NI_TXT ("声像一边倒时电平表只有一边有读数"));

        {
            auto instrument = std::make_shared<TestToneInstrument>();

            RenderSchedule schedule;
            TrackRenderInfo info;
            info.trackId = 7;
            info.kind = TrackKind::vst;
            info.instrument = instrument;
            info.pan = -1.0f;            // hard left
            info.volumeLinear = 1.0f;
            schedule.tracks.push_back (info);

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (false);
            engine.renderNextBlock (blockSize);

            const auto panned = engine.readTrackMeter (7);

            expectGreaterThan (panned.peakLeft, 0.5f);
            expectLessThan (panned.peakRight, 0.001f);

            // And the whole mix is just as one-sided while that is all there is.
            const auto master = engine.readMasterMeter();

            expectGreaterThan (master.peakLeft, 0.5f);
            expectLessThan (master.peakRight, 0.001f);
        }

        beginTest (NI_TXT ("画出来的 CC11 表情让内置乐器这一轨变轻"));

        {
            // A track with a note right at the start, and a drawn expression curve
            // that pulls the output down as it plays.
            auto project = makeProjectWithLongNote();

            auto& clip = project.tracks.front().clips.front();
            auto& curve = controllers::laneFor (clip, 11);
            controllers::upsertControllerPoint (curve, 0, 127);
            controllers::upsertControllerPoint (curve, kTicksPerQuarter * 2, 0);

            // The level at the end of the ramp: the curve is interpolated, so by one
            // second the drawn expression has pulled the track right down.
            auto rmsLate = [&project, blockSize] (bool withCurve)
            {
                auto copy = project;

                if (! withCurve)
                    copy.tracks.front().clips.front().controllerCurves.clear();

                PlaybackEngine engine;
                engine.prepare (48000.0, blockSize);
                engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (copy)));
                engine.setPositionSeconds (0.0);
                engine.setPlaying (true);

                for (int i = 0; i < 90; ++i)      // 0.96 s: the ramp is nearly at 0
                    engine.renderNextBlock (blockSize);

                return engine.getOutputBuffer().getRMSLevel (0, 0, blockSize);
            };

            const auto plain = rmsLate (false);
            const auto expressed = rmsLate (true);

            expectGreaterThan (plain, 0.005f);
            expectLessThan (expressed, plain * 0.2f,
                            "the drawn expression really takes the level down");
        }

        beginTest (NI_TXT ("CC64 延音踏板把音符的结尾挂住，抬起才放"));

        {
            auto project = makeProjectWithLongNote();

            auto& clip = project.tracks.front().clips.front();
            clip.notes.front().durationTicks = kTicksPerQuarter;      // half a second
            clip.notes.front().muted = false;
            project.tracks.front().synth.patch.ops[0].rate[3] = 40;   // a slow release

            auto& pedal = controllers::laneFor (clip, 64);

            // Down at the start and up at two quarters. The curve is interpolated
            // between its knees, so the value just before the lift is what makes this
            // a pedal press rather than a slow slide off it (a ramp through 64 would
            // let go of the note the moment it crossed).
            controllers::upsertControllerPoint (pedal, 0, 127);                    // down
            controllers::upsertControllerPoint (pedal, kTicksPerQuarter * 2 - 1, 127);
            controllers::upsertControllerPoint (pedal, kTicksPerQuarter * 2, 0);   // up

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            // Render past the note's own end (0.5 s) but before the pedal comes up
            // (1.0 s): the note has to still be sounding.
            for (int i = 0; i < 70; ++i)      // 70 * 512 / 48000 = 0.75 s
                engine.renderNextBlock (blockSize);

            auto heldPeak = 0.0f;

            for (int i = 0; i < 6; ++i)
            {
                engine.renderNextBlock (blockSize);
                heldPeak = juce::jmax (heldPeak, engine.getOutputBuffer().getMagnitude (0, 0, blockSize));
            }

            expectGreaterThan (engine.getActiveVoiceCount(), 0,
                               "the pedal is holding a note that has already ended");
            expectGreaterThan (engine.getOutputBuffer().getMagnitude (0, 0, blockSize), 0.001f,
                               "and it is still making a sound");

            // Now run past the pedal coming up and let the release die away.
            for (int i = 0; i < 80; ++i)
                engine.renderNextBlock (blockSize);

            expectEquals (engine.getActiveVoiceCount(), 0,
                          "lifting the pedal ends the note");

            juce::ignoreUnused (heldPeak);
        }

        beginTest (NI_TXT ("跨越循环右边界的音符在回卷时被放掉，不会一直响下去"));

        {
            // A note that starts before the loop ends and would have stopped after
            // it: its note-off lies outside the loop, so the wrap is the only thing
            // that can let it go (the event stream never reaches that note-off
            // again). The subtractive instrument is deliberately the one used here:
            // its voices were the ones the wrap forgot to release.
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };
            project.tracks.front().kind = TrackKind::subtractive;

            Note note;
            note.id = project.nextNoteId++;
            note.startTick = kTicksPerQuarter * 3 / 2;        // 0.75 s
            note.durationTicks = kTicksPerQuarter * 3 / 2;    // ends at 1.5 s: past the loop end
            note.frequencyHz = 220.0;
            note.velocity = 100;
            project.tracks.front().clips.front().notes = { note };

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setLoopRegion (0.0, 1.0, true);
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            const auto blocksPerPass = (int) std::ceil (48000.0 / (double) blockSize);

            // The note is still sounding as the loop turns over, and the wrap is what
            // lets it go: just after the wrap the level has to be falling away. Left
            // to itself (the bug) the voice is never told to stop, so the level stays
            // exactly where it was - it is "always being played".
            auto beforeWrap = 0.0f;
            auto afterWrap = 0.0f;

            for (int i = 0; i < blocksPerPass * 4 / 3; ++i)
            {
                engine.renderNextBlock (blockSize);

                const auto peak = engine.getOutputBuffer().getMagnitude (0, 0, blockSize);
                const auto endTime = (double) (i + 1) * (double) blockSize / 48000.0;

                if (endTime > 0.9 && endTime <= 1.0)
                    beforeWrap = juce::jmax (beforeWrap, peak);

                // Well into the release, and before the next pass's note starts.
                if (endTime > 1.15 && endTime <= 1.35)
                    afterWrap = juce::jmax (afterWrap, peak);
            }

            expectGreaterThan (beforeWrap, 0.01f,
                               "the note is sounding as the loop turns over");
            expectLessThan (afterWrap, beforeWrap * 0.7f,
                            "and the wrap let it go instead of leaving it running");

            juce::ignoreUnused (blocksPerPass);
        }

        beginTest (NI_TXT ("循环回到开头时，表情从这一遍重新算"));

        {
            // A one second loop with two notes in it, and an expression curve whose
            // only knee is *inside* that loop (at half a second, value 0): the curve
            // says nothing about the top of the loop, so the first note of every pass
            // has to play at full level. (Before the wrap reset the state, the second
            // pass started at the value the first one ended on - silence.)
            auto project = makeProjectWithLongNote();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& clip = project.tracks.front().clips.front();
            clip.notes.front().startTick = 0;
            clip.notes.front().durationTicks = kTicksPerQuarter / 2;
            clip.notes.front().muted = false;

            Note second = clip.notes.front();
            second.id = 2;
            second.startTick = kTicksPerQuarter;
            clip.notes.push_back (second);

            auto& curve = controllers::laneFor (clip, 11);
            controllers::upsertControllerPoint (curve, kTicksPerQuarter, 0);   // nothing from half a second on

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setLoopRegion (0.0, 1.0, true);
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            const auto blocksPerPass = (int) std::ceil (48000.0 / (double) blockSize);   // 1.0 s
            const auto windowBlocks = 20;
            const auto windowStart = 4;
            auto firstPass = 0.0f;
            auto secondPass = 0.0f;

            for (int i = 0; i < blocksPerPass + windowStart + windowBlocks + 2; ++i)
            {
                engine.renderNextBlock (blockSize);

                const auto peak = engine.getOutputBuffer().getMagnitude (0, 0, blockSize);

                if (i >= windowStart && i < windowStart + windowBlocks)
                    firstPass = juce::jmax (firstPass, peak);

                if (i >= blocksPerPass + windowStart && i < blocksPerPass + windowStart + windowBlocks)
                    secondPass = juce::jmax (secondPass, peak);
            }

            expectGreaterThan (firstPass, 0.005f);
            expectWithinAbsoluteError ((double) secondPass, (double) firstPass,
                                       (double) firstPass * 0.15 + 1.0e-5);
        }

        beginTest (NI_TXT ("VST 轨的曲线交给插件，引擎不重复处理"));

        {
            auto instrument = std::make_shared<TestToneInstrument>();

            RenderSchedule schedule;
            TrackRenderInfo info;
            info.trackId = 3;
            info.kind = TrackKind::vst;
            info.instrument = instrument;
            info.volumeLinear = 1.0f;

            // One note from 0.0 to 0.2 s, a pedal pressed at 0 and lifted at 0.5 s,
            // and an expression curve - all sent to the plugin.
            SynthEvent on;
            on.timeSeconds = 0.0;
            on.noteId = 1;
            on.isNoteOn = true;
            on.frequency = 440.0f;
            on.velocity = 100.0f;
            info.events.push_back (on);

            SynthEvent off = on;
            off.timeSeconds = 0.2;
            off.isNoteOn = false;
            info.events.push_back (off);

            info.controllers.push_back ({ 0.0, 64, 127 });
            info.controllers.push_back ({ 0.0, 11, 64 });
            info.controllers.push_back ({ 0.5, 64, 0 });
            schedule.tracks.push_back (info);

            PlaybackEngine engine;
            engine.prepare (48000.0, blockSize);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            for (int i = 0; i < 20; ++i)      // 0.21 s
                engine.renderNextBlock (blockSize);

            // The plugin got the controllers...
            expectGreaterThan ((int) instrument->controllersSeen.size(), 0,
                               "the plugin is told about the drawn curves");

            auto sawPedal = false;
            auto sawExpression = false;

            for (const auto& [controller, value] : instrument->controllersSeen)
            {
                juce::ignoreUnused (value);

                if (controller == 64)
                    sawPedal = true;

                if (controller == 11)
                    sawExpression = true;
            }

            expect (sawPedal, "the pedal went to the plugin");
            expect (sawExpression, "and so did the expression");

            // ...and the engine did *not* keep the note alive for it: the pedal is
            // the plugin's business on a VST track, and a host that also held the
            // note-off would fight the plugin's own sustain.
            expectEquals ((int) instrument->sounding.size(), 0,
                          "the note-off reached the plugin even though the pedal is down");

            // The engine's own expression state stays out of it too: the track's
            // level is what the plugin makes of the CC.
            expectGreaterThan (engine.getOutputBuffer().getMagnitude (0, 0, blockSize), 0.5f);
        }

        //==========================================================================
        // 第四十三批: a track is not finished when its instrument stops singing.
        //
        // The engine used to skip a built-in track the moment its last voice went
        // quiet, which cut the insert chain (a reverb or a delay) mid-tail; and
        // 停止 was treated as a jump, which reset every chain and sent the plugins a
        // hard all-notes-off. These four checks pin the new behaviour down.

        beginTest (NI_TXT ("插入链跟着尾巴走：乐器静了，混响还在响"));
        {
            const auto block = 512;

            // A short note (a 20 ms blip), so the instrument's own release is over
            // long before the chain has finished. The fake effect adds a constant
            // offset - i.e. it keeps making sound even from an empty buffer, which is
            // exactly what a reverb tail does.
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& track = project.tracks.front();
            track.clips.front().notes.clear();

            Note blip;
            blip.id = project.nextNoteId++;
            blip.startTick = 0;
            blip.durationTicks = 10;      // ~10 ms at 120 bpm
            blip.frequencyHz = 440.0;
            blip.velocity = 100;
            track.clips.front().notes.push_back (blip);
            track.effects.push_back (makeEffectSlot (NI_TXT ("尾巴")));

            auto effect = std::make_shared<testsupport::FakeEffect> (track.id, 1.0f, 0, 0.02f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setPlaying (true);

            // Half a second: the note and the synth's release (300 ms) are finished
            // well inside this, so what is left in the output can only be the chain.
            for (int i = 0; i < 47; ++i)
                engine.renderNextBlock (block);

            const auto callsBefore = effect->processCount;
            const auto tailLevel = engine.getOutputBuffer().getMagnitude (0, 0, block);

            expectGreaterThan (tailLevel, 0.001f,
                               NI_TXT ("音符与释音都结束之后，链的尾巴还在输出里（读到 ")
                                   + juce::String (tailLevel, 4) + NI_TXT ("）"));

            // ...and it is still being rendered, not just left over in a buffer.
            engine.renderNextBlock (block);

            expectGreaterThan (effect->processCount, callsBefore,
                               NI_TXT ("乐器静下来之后效果器仍然被调用（")
                                   + juce::String (callsBefore) + NI_TXT (" → ")
                                   + juce::String (effect->processCount) + NI_TXT ("）"));
        }

        beginTest (NI_TXT ("真的静下来之后，这条轨的链会自动停（不再空转）"));
        {
            const auto block = 512;

            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& track = project.tracks.front();
            track.clips.front().notes.clear();

            Note blip;
            blip.id = project.nextNoteId++;
            blip.durationTicks = 10;
            blip.frequencyHz = 440.0;
            blip.velocity = 100;
            track.clips.front().notes.push_back (blip);
            track.effects.push_back (makeEffectSlot (NI_TXT ("静音效果")));

            // This one adds nothing, so once the instrument is quiet the chain's own
            // output is silence - the engine is allowed to stop rendering it.
            auto effect = std::make_shared<testsupport::FakeEffect> (track.id, 1.0f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 60; ++i)      // 0.64 s: note, release, then silence
                engine.renderNextBlock (block);

            const auto callsAfterSilence = effect->processCount;
            expectLessThan (engine.getOutputBuffer().getMagnitude (0, 0, block), 1.0e-5f,
                            NI_TXT ("这条轨已经彻底静了"));

            for (int i = 0; i < 10; ++i)
                engine.renderNextBlock (block);

            expectEquals (effect->processCount, callsAfterSilence,
                          NI_TXT ("静下来之后不再空转，效果器不再被调用"));
        }

        beginTest (NI_TXT ("没有插入链的轨：乐器静了就跳过（和以前一样）"));
        {
            const auto block = 512;

            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };
            project.tracks.front().clips.front().notes.clear();

            Note blip;
            blip.id = project.nextNoteId++;
            blip.durationTicks = 10;
            blip.frequencyHz = 440.0;
            blip.velocity = 100;
            project.tracks.front().clips.front().notes.push_back (blip);

            auto effect = std::make_shared<testsupport::FakeEffect> (project.tracks.front().id, 1.0f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 60; ++i)
                engine.renderNextBlock (block);

            expectEquals (effect->processCount, 0,
                          NI_TXT ("这条轨没有插入槽，工厂都不该被问到"));
        }

        beginTest (NI_TXT ("停止不 reset 插入链，跳转才 reset"));
        {
            const auto block = 512;

            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& track = project.tracks.front();
            track.clips.front().notes.clear();

            Note note;
            note.id = project.nextNoteId++;
            note.durationTicks = kTicksPerQuarter * 4;
            note.frequencyHz = 440.0;
            note.velocity = 100;
            track.clips.front().notes.push_back (note);
            track.effects.push_back (makeEffectSlot (NI_TXT ("混响")));

            auto effect = std::make_shared<testsupport::FakeEffect> (track.id, 1.0f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 20; ++i)
                engine.renderNextBlock (block);

            const auto resetsBeforeStop = effect->resetCount;

            // 停止 does its own rewind: the notes are released, the cursors rewind,
            // but the chain is left alone so its tail rings out.
            engine.setPlaying (false);
            engine.setStopRewindSeconds (0.0);

            for (int i = 0; i < 4; ++i)
                engine.renderNextBlock (block);

            expectEquals (effect->resetCount, resetsBeforeStop,
                          NI_TXT ("停止之后插入链没有被 reset（")
                              + juce::String (effect->resetCount) + NI_TXT ("）"));
            expect (engine.getPositionSeconds() < 0.001,
                    NI_TXT ("位置仍然回到了起点"));

            // A real jump is the opposite: the delay line must not replay what came
            // before the pointer.
            engine.setPositionSeconds (4.0);
            engine.renderNextBlock (block);

            expectGreaterThan (effect->resetCount, resetsBeforeStop,
                               NI_TXT ("拖播放头的跳转会 reset 插入链（")
                                   + juce::String (effect->resetCount) + NI_TXT ("）"));
        }

        beginTest (NI_TXT ("停止 / 循环回卷给 VST 发的是 note-off，不是 all-notes-off"));
        {
            const auto block = 512;

            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);
            auto* track = project.findTrack (id);
            track->vst.pluginPath = "C:\\plugins\\Test.vst3";
            track->clips.front().notes.clear();

            Note note;
            note.id = project.nextNoteId++;
            note.durationTicks = kTicksPerQuarter * 8;
            note.frequencyHz = 440.0;
            note.velocity = 100;
            track->clips.front().notes.push_back (note);

            auto instrument = std::make_shared<testsupport::CountingInstrument>();
            const auto factory = [instrument] (juce::int64, const VstParams&)
                                     -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 8; ++i)
                engine.renderNextBlock (block);

            // 停止: the plugin is told the player lifted their hands, not "be quiet".
            engine.setPlaying (false);
            engine.setStopRewindSeconds (0.0);

            for (int i = 0; i < 2; ++i)
                engine.renderNextBlock (block);

            expectGreaterThan (instrument->releaseHeldNotesCalls, 0,
                               NI_TXT ("停止时给插件发了真正的 note-off（releaseHeldNotes）"));
            expectEquals (instrument->allNotesOffCalls, 0,
                          NI_TXT ("停止时没有发 all-notes-off（那会把释音和混响一起掐掉）"));

            // 再按播放 is the exception: that one really does mean "be quiet now", so
            // the plugin gets the hard all-notes-off and its release is over.
            engine.cutSoundingTails();
            engine.renderNextBlock (block);

            expectGreaterThan (instrument->allNotesOffCalls, 0,
                               NI_TXT ("再按播放给插件发的是 all-notes-off（尾巴掐断）"));
        }

        beginTest (NI_TXT ("循环回卷不 reset 插入链（尾巴连着下一遍响）"));
        {
            const auto block = 512;

            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };
            project.loop = { 0, kTicksPerQuarter * 4, true };   // one bar at 120 bpm = 2 s

            auto& track = project.tracks.front();
            track.clips.front().notes.clear();

            Note note;
            note.id = project.nextNoteId++;
            note.startTick = 0;
            note.durationTicks = kTicksPerQuarter * 4;   // the whole bar: it crosses the loop end
            note.frequencyHz = 440.0;
            note.velocity = 100;
            track.clips.front().notes.push_back (note);
            track.effects.push_back (makeEffectSlot (NI_TXT ("延迟")));

            auto effect = std::make_shared<testsupport::FakeEffect> (track.id, 1.0f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setLoopRegion (0.0, 2.0, true);
            engine.setPlaying (true);

            // Play past the loop end (2 s = 188 blocks) so it wraps at least once.
            for (int i = 0; i < 200; ++i)
                engine.renderNextBlock (block);

            expectEquals (effect->resetCount, 0,
                          NI_TXT ("循环回卷没有 reset 插入链（")
                              + juce::String (effect->resetCount) + NI_TXT ("）"));
            expect (engine.getPositionSeconds() < 2.0,
                    NI_TXT ("而且确实回卷过了（现在 ")
                        + juce::String (engine.getPositionSeconds(), 2) + NI_TXT (" s）"));
        }

        beginTest (NI_TXT ("再按播放：上一次停止留下的尾巴被掐断"));
        {
            const auto block = 512;

            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& track = project.tracks.front();
            track.clips.front().notes.clear();

            Note blip;
            blip.id = project.nextNoteId++;
            blip.startTick = 0;
            blip.durationTicks = 10;      // ~10 ms: long over before the tail ends
            blip.frequencyHz = 440.0;
            blip.velocity = 100;
            track.clips.front().notes.push_back (blip);
            track.effects.push_back (makeEffectSlot (NI_TXT ("混响")));

            // A chain that keeps making sound from an empty buffer is exactly what a
            // reverb tail looks like from here.
            auto effect = std::make_shared<testsupport::FakeEffect> (track.id, 1.0f, 0, 0.02f);
            const auto factory = [effect] (juce::int64, int, const EffectSlot&)
                                     -> std::shared_ptr<HostedEffect>
            {
                return effect;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, block);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, {}, factory)));
            engine.setPlaying (true);

            for (int i = 0; i < 47; ++i)
                engine.renderNextBlock (block);

            // 停止: the tail is deliberately left ringing (that is the batch above).
            const auto resetsBeforeStop = effect->resetCount;
            engine.setPlaying (false);
            engine.setStopRewindSeconds (0.0);

            for (int i = 0; i < 4; ++i)
                engine.renderNextBlock (block);

            expectEquals (effect->resetCount, resetsBeforeStop,
                          NI_TXT ("停止之后插入链还是没被 reset"));
            expectGreaterThan (engine.getOutputBuffer().getMagnitude (0, 0, block), 0.001f,
                               NI_TXT ("停止之后尾巴确实还听得到（读到 ")
                                   + juce::String (engine.getOutputBuffer()
                                                       .getMagnitude (0, 0, block), 4)
                                   + NI_TXT ("）"));

            // ...but starting again means that sound is over.
            engine.cutSoundingTails();
            engine.renderNextBlock (block);

            expectEquals (engine.getTailCutCountForTest(), 1,
                          NI_TXT ("音频线程应用了一次掐断"));
            expectGreaterThan (effect->resetCount, resetsBeforeStop,
                               NI_TXT ("再按播放把插入链冲掉了（reset ")
                                   + juce::String (resetsBeforeStop) + NI_TXT (" → ")
                                   + juce::String (effect->resetCount) + NI_TXT ("）"));
            expectLessThan (engine.getOutputBuffer().getMagnitude (0, 0, block), 0.001f,
                            NI_TXT ("重开之后这一块是安静的（读到 ")
                                + juce::String (engine.getOutputBuffer()
                                                    .getMagnitude (0, 0, block), 4)
                                + NI_TXT ("）"));
        }
    }
};

class ExportTests : public juce::UnitTest
{
public:
    ExportTests() : juce::UnitTest ("Export", "hmdaw") {}

    static juce::File makeTempFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("hmdaw-export-test");
        folder.createDirectory();
        return folder;
    }

    static std::unique_ptr<juce::AudioFormatReader> readBack (const juce::File& file)
    {
        juce::AudioFormatManager manager;
        manager.registerBasicFormats();
        return std::unique_ptr<juce::AudioFormatReader> (manager.createReaderFor (file));
    }

    /** Two synth tracks, each with one note, so a stem batch has something to say. */
    static Project twoTrackProject()
    {
        auto project = makeProjectWithSynthTrack();
        project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

        const auto secondId = addTrack (project, TrackKind::synth);
        project.tracks.back().name = NI_TXT ("高音");

        Note low;
        low.id = 1;
        low.durationTicks = 960;
        low.frequencyHz = 220.0;

        Note high;
        high.id = 2;
        high.durationTicks = 960;
        high.frequencyHz = 660.0;

        project.tracks.front().clips.front().notes = { low };
        project.tracks.back().clips.front().notes = { high };

        juce::ignoreUnused (secondId);
        return project;
    }

    void runTest() override
    {
        const auto folder = makeTempFolder();

        beginTest ("every lossless format comes out at the right depth");

        {
            const auto project = twoTrackProject();

            for (auto format : { ExportSettings::Format::wav, ExportSettings::Format::aiff,
                                 ExportSettings::Format::flac })
            {
                for (auto depth : ExportSettings::bitDepthsFor (format))
                {
                    ExportSettings settings;
                    settings.format = format;
                    settings.bitsPerSample = depth;

                    const auto file = folder.getChildFile ("depth")
                                          .withFileExtension (ExportSettings::extensionFor (format));
                    juce::String error;

                    const auto what = juce::String (ExportSettings::extensionFor (format))
                                          + " " + juce::String (depth) + " bit";

                    // Rendered first, so the message can quote the real error:
                    // argument evaluation order is not guaranteed.
                    const auto rendered = Exporter::renderToFile (project, file, settings,
                                                                  {}, {}, {}, error);
                    expect (rendered, "render " + what + ": " + error);

                    auto reader = readBack (file);
                    expect (reader != nullptr, "readable " + what + ": " + file.getFullPathName());

                    if (reader != nullptr)
                    {
                        expectEquals ((int) reader->numChannels, 2);
                        expectWithinAbsoluteError (reader->sampleRate, 48000.0, 1.0);
                        expectEquals ((int) reader->bitsPerSample, depth);

                        if (depth > 24)
                            expect (reader->usesFloatingPointData, "32 means 32-bit float");
                    }

                    file.deleteFile();
                }
            }
        }

        beginTest ("a different export rate is really resampled");

        {
            const auto project = twoTrackProject();

            ExportSettings settings;
            settings.format = ExportSettings::Format::wav;
            settings.bitsPerSample = 16;
            settings.sampleRate = 44100.0;

            const auto file = folder.getChildFile ("rate44100.wav");
            juce::String error;

            expect (Exporter::renderToFile (project, file, settings, {}, {}, {}, error),
                    "render: " + error);

            auto reader = readBack (file);
            expect (reader != nullptr);

            if (reader != nullptr)
            {
                expectWithinAbsoluteError (reader->sampleRate, 44100.0, 1.0);

                // The note is one beat at 120 bpm = 0.5 s, plus the release tail:
                // the same duration as at 48 kHz, just carried by more samples.
                const auto seconds = reader->lengthInSamples / reader->sampleRate;
                expectGreaterThan (seconds, 0.5);
                expectLessThan (seconds, 1.5);
            }

            file.deleteFile();
        }

        beginTest (NI_TXT ("a silent track is skipped when exporting stems"));

        {
            auto project = twoTrackProject();
            project.tracks.front().muted = true;

            ExportSettings settings;
            settings.format = ExportSettings::Format::wav;
            settings.bitsPerSample = 24;
            settings.master = true;
            settings.trackIds = { project.tracks.front().id, project.tracks.back().id };

            const auto caseFolder = makeTempFolder().getChildFile ("mute-solo");
            std::vector<juce::File> written;
            std::vector<juce::String> skipped;
            juce::String error;

            expect (Exporter::renderSelection (project, caseFolder, "song", settings,
                                               {}, {}, error, written, &skipped),
                    "render: " + error);

            // The master mix plus the one audible track; the muted stem is not
            // written at all, and it is named in the skipped list.
            expectEquals ((int) written.size(), 2);
            expectEquals ((int) skipped.size(), 1);
            expectEquals (skipped.front(), project.tracks.front().name);

            for (const auto& file : written)
                file.deleteFile();

            // With every track silent the master render says so.
            project.tracks.back().muted = true;

            const auto masterFile = caseFolder.getChildFile ("all-muted.wav");
            settings.master = true;
            settings.trackIds.clear();

            expect (! Exporter::renderToFile (project, masterFile, settings, {},
                                              {}, {}, error));
            expect (error.contains (NI_TXT ("静音")), "the message names the reason: " + error);

            masterFile.deleteFile();
        }

        beginTest (NI_TXT ("the loop and the metronome never reach an exported file"));

        {
            // A two-beat cycle sitting on top of the music, with the click switched
            // on: an offline render has to stay the whole arrangement, dry.
            auto looped = twoTrackProject();
            looped.loop.startTick = 0;
            looped.loop.endTick   = 2 * kTicksPerQuarter;
            looped.loop.enabled   = true;
            looped.view.metronomeEnabled = true;

            const auto dry = twoTrackProject();

            ExportSettings settings;
            settings.format = ExportSettings::Format::wav;
            settings.bitsPerSample = 24;

            const auto loopedFile = folder.getChildFile ("loop-ignored.wav");
            const auto dryFile    = folder.getChildFile ("loop-ignored-dry.wav");
            juce::String error;

            expect (Exporter::renderToFile (looped, loopedFile, settings, {}, {}, {}, error),
                    "render: " + error);
            expect (Exporter::renderToFile (dry, dryFile, settings, {}, {}, {}, error),
                    "render: " + error);

            auto withLoop = readBack (loopedFile);
            auto without  = readBack (dryFile);

            expect (withLoop != nullptr && without != nullptr);

            if (withLoop != nullptr && without != nullptr)
            {
                expectEquals (withLoop->lengthInSamples, without->lengthInSamples,
                              NI_TXT ("the render length ignores the loop"));

                const auto count = (int) juce::jmin (withLoop->lengthInSamples,
                                                     without->lengthInSamples);
                juce::AudioBuffer<float> a (2, count), b (2, count);
                withLoop->read (&a, 0, count, 0, true, true);
                without->read (&b, 0, count, 0, true, true);

                auto worst = 0.0f;

                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < count; ++i)
                        worst = juce::jmax (worst, std::abs (a.getSample (channel, i)
                                                                 - b.getSample (channel, i)));

                expectLessThan ((double) worst, 1.0e-6,
                                NI_TXT ("and no click was mixed into it"));
            }

            loopedFile.deleteFile();
            dryFile.deleteFile();
        }

        beginTest ("a stem batch writes the master mix plus one file per track");

        {
            auto project = twoTrackProject();

            ExportSettings settings;
            settings.format = ExportSettings::Format::wav;
            settings.bitsPerSample = 24;
            settings.master = true;

            for (const auto& track : project.tracks)
                settings.trackIds.insert (track.id);

            const auto batchFolder = folder.getChildFile ("stems");
            batchFolder.createDirectory();

            juce::String error;
            std::vector<juce::File> written;

            expect (Exporter::renderSelection (project, batchFolder, "stems", settings,
                                               {}, {}, error, written),
                    "render: " + error);

            expectEquals ((int) written.size(), 3);   // master + two tracks

            for (const auto& file : written)
            {
                expect (file.existsAsFile(), "written: " + file.getFullPathName());

                auto reader = readBack (file);
                expect (reader != nullptr, "readable: " + file.getFileName());

                if (reader != nullptr)
                    expectWithinAbsoluteError (reader->sampleRate, 48000.0, 1.0);
            }

            expect (written.front().getFileNameWithoutExtension() == "stems_总混音",
                    "the master mix is named after the batch (got '"
                        + written.front().getFileNameWithoutExtension() + "')");

            // Nothing ticked is an error rather than an empty run.
            ExportSettings nothing;
            nothing.master = false;
            std::vector<juce::File> none;
            juce::String emptyError;

            expect (! Exporter::renderSelection (project, batchFolder, "none", nothing,
                                                 {}, {}, emptyError, none));
            expect (emptyError.isNotEmpty());
        }

        beginTest ("an illegal character in a track name does not escape the folder");

        {
            // NI_TXT, because juce::String(const char*) is ASCII-only: a raw
            // literal with Chinese in it arrives as mojibake.
            expectEquals (Exporter::stemFileName ("proj", NI_TXT ("鼓/打击")),
                          juce::String ("proj_") + NI_TXT ("鼓_打击"));
            expectEquals (Exporter::stemFileName ("proj", "  "), juce::String ("proj_")
                                                                   + NI_TXT ("未命名"));
        }
    }
};

namespace
{
    ScheduleTests       scheduleTests;
    PlaybackTests       playbackTests;
    ExportTests         exportTests;
}
