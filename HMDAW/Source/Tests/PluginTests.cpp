#include "TestSupport.h"

class VstInstrumentTests : public juce::UnitTest
{
public:
    VstInstrumentTests() : juce::UnitTest ("VstInstrument", "hmdaw") {}

    void runTest() override
    {
        beginTest ("a frequency becomes a MIDI note plus the cents left over");
        {
            // MIDI note 69 is A4 = 440 Hz, so 660 Hz (a 1:3 above A4) is 76 plus
            // the cents that are left over.
            const auto a4 = vst::midiPitchForHz (440.0);
            expectEquals (a4.note, 69);
            expectWithinAbsoluteError (a4.cents, 0.0, 1.0e-9);

            const auto c4 = vst::midiPitchForHz (440.0 * std::pow (2.0, -9.0 / 12.0));
            expectEquals (c4.note, 60);
            expectWithinAbsoluteError (c4.cents, 0.0, 0.01);

            const auto a3 = vst::midiPitchForHz (220.0);
            expectEquals (a3.note, 57);
            expectWithinAbsoluteError (a3.cents, 0.0, 1.0e-9);

            // 660 Hz - the frequency the user writes after a 1:3 - is not a
            // semitone: it has to come back as MIDI 76 plus its cents.
            const auto third = vst::midiPitchForHz (660.0);
            expectEquals (third.note, 76);
            expectWithinAbsoluteError (third.cents, 1.955, 0.02);
            expectWithinAbsoluteError (vst::hzForMidiPitch (third.note, third.cents), 660.0, 0.01);

            // Outside the MIDI range the note clamps but the offset stays honest.
            const auto low = vst::midiPitchForHz (4.0);
            expectEquals (low.note, 0);
            expect (low.cents < 0.0);
        }

        beginTest ("an equal tempered note is the fallback for a plugin that cannot bend per note");
        {
            // The host's exact-frequency trick needs a plugin that treats a bend as per
            // channel. Instruments that bend the whole patch are played semitones
            // instead (VstParams::equalTemperament), and this is the frequency they get.
            const auto e5 = vst::hzForMidiNote (76);

            expectWithinAbsoluteError (vst::snapToSemitone (660.0), e5, 0.001);
            expectWithinAbsoluteError (vst::snapToSemitone (e5), e5, 0.001);
            expectWithinAbsoluteError (vst::snapToSemitone (440.0), 440.0, 0.001);
            // Halfway between two semitones rounds to the nearer one.
            expectWithinAbsoluteError (vst::snapToSemitone (440.0 * std::pow (2.0, 0.49 / 12.0)),
                                       440.0, 0.001);
            expectWithinAbsoluteError (vst::snapToSemitone (440.0 * std::pow (2.0, 0.51 / 12.0)),
                                       vst::hzForMidiNote (70), 0.001);
        }

        beginTest ("the pitch wheel value spans the range the mapping needs");
        {
            expectEquals (vst::bendValueFor (0.0), 0);
            expectEquals (vst::bendValueFor (50.0), 2048);
            expectEquals (vst::bendValueFor (100.0), 4096);
            expectEquals (vst::bendValueFor (-100.0), -4096);
            expectEquals (vst::bendValueFor (200.0), 8191);     // clamped at full bend
            expectEquals (vst::bendValueFor (-200.0), -8192);
            expectEquals (vst::bendValueFor (2000.0), 8191);
        }

        beginTest ("the pitch bend range is programmed into the plugin");
        {
            const auto messages = vst::pitchBendRangeMessages();

            expectEquals ((int) messages.size(), 6);
            expectEquals (messages[0].controller, 101);   // RPN MSB
            expectEquals (messages[0].value, 0);
            expectEquals (messages[1].controller, 100);   // RPN LSB
            expectEquals (messages[2].controller, 6);     // data entry MSB
            expectEquals (messages[2].value, (int) vst::kBendRangeSemitones);
            expectEquals (messages[3].controller, 38);    // data entry LSB

            // RPN null: without it the channel is left with RPN 0 selected, and a
            // plugin could read something else as bend range data later.
            expectEquals (messages[4].controller, 101);
            expectEquals (messages[4].value, 127);
            expectEquals (messages[5].controller, 100);
            expectEquals (messages[5].value, 127);

            // The whole point of this list: HM never sends a controller that a plugin
            // could read as volume, pan or expression. 7 / 10 / 11 must never appear
            // (the test plugin's own state block is what keeps that honest end to end).
            for (const auto& message : messages)
            {
                expect (message.controller != 7);    // volume
                expect (message.controller != 10);   // pan
                expect (message.controller != 11);   // expression
                expect (message.controller == 6 || message.controller == 38
                            || message.controller == 100 || message.controller == 101);
            }
        }

        beginTest ("each sounding note owns a MIDI channel of its own");
        {
            vst::ChannelAllocator channels;

            const auto first  = channels.allocate (101).channel;
            const auto second = channels.allocate (102).channel;

            expect (first != second);
            expectEquals (channels.activeCount(), 2);
            expectEquals (channels.channelFor (101), first);
            expectEquals (channels.channelFor (102), second);
            expectEquals (channels.channelFor (103), -1);

            // Asking twice for the same note keeps it where it is.
            expectEquals (channels.allocate (101).channel, first);

            channels.release (101);
            expectEquals (channels.channelFor (101), -1);
            expectEquals (channels.activeCount(), 1);

            // The freed channel is *not* the next one handed out: the plugin is very
            // likely still ringing that note out, and its bend may not be taken over
            // by the new note while it does. The channels that were never used come
            // first, so a take spreads its notes over sixteen channels before any of
            // them comes round again.
            const auto third = channels.allocate (103).channel;

            expect (third != first && third != second);
            expectEquals (channels.channelsUsedSinceReset(), 3);

            // Once every channel has been used, the one whose note was released
            // longest ago comes back first.
            vst::ChannelAllocator round;
            std::set<int> used;

            for (int i = 0; i < vst::kMidiChannels; ++i)
                used.insert (round.allocate (i + 1).channel);

            expectEquals ((int) used.size(), vst::kMidiChannels);
            expectEquals (round.activeCount(), vst::kMidiChannels);
            expectEquals (round.channelsUsedSinceReset(), vst::kMidiChannels);

            round.release (1);     // the oldest note lets go first...
            round.release (2);

            const auto afterRelease = round.allocate (500);

            expectEquals (afterRelease.channel, 0);    // ...so channel 0 comes back
            expectEquals (afterRelease.evictedNoteId, (juce::int64) 0);
            expectEquals (round.channelFor (1), -1);
            expectEquals (round.activeCount(), vst::kMidiChannels - 1);

            vst::ChannelAllocator full;

            std::vector<juce::int64> heldIds;

            for (int i = 0; i < vst::kMidiChannels; ++i)
            {
                heldIds.push_back (i + 1);
                full.allocate (i + 1);
            }

            // A seventeenth note has to take a channel from someone: it is the note
            // that has been held the longest, and the host is told which one so it
            // can let it go with a real note-off instead of leaving it sounding under
            // the new note's bend.
            const auto steal = full.allocate (999);

            expect (steal.channel >= 0);
            expectEquals (steal.evictedNoteId, (juce::int64) 1);
            expectEquals (full.channelFor (1), -1);
            expectEquals (full.channelFor (999), steal.channel);
            expectEquals (full.activeCount(), vst::kMidiChannels);

            full.reset();
            expectEquals (full.activeCount(), 0);
            expectEquals (full.channelsUsedSinceReset(), 0);
            expectEquals (full.allocate (7).channel, 0);
        }

        beginTest ("a VST track survives the project round trip");
        {
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);
            auto* track = project.findTrack (id);

            expect (track != nullptr);

            if (track == nullptr)
                return;

            track->vst.name        = NI_TXT ("测试音源");
            track->vst.pluginPath  = "C:\\plugins\\Test.vst3";
            track->vst.pluginUid   = "123456";
            track->vst.stateBase64 = "SGVsbG8=";
            track->vst.loadedOk    = true;

            const auto json = projectToJson (project);
            Project reloaded;
            juce::String error;

            expect (projectFromJson (json, reloaded, error), error);
            expectEquals ((int) reloaded.tracks.size(), 1);
            expect (reloaded.tracks.front().kind == TrackKind::vst);
            expectEquals (reloaded.tracks.front().vst.name, NI_TXT ("测试音源"));
            expectEquals (reloaded.tracks.front().vst.pluginPath,
                          juce::String ("C:\\plugins\\Test.vst3"));
            expectEquals (reloaded.tracks.front().vst.pluginUid, juce::String ("123456"));
            expectEquals (reloaded.tracks.front().vst.stateBase64, juce::String ("SGVsbG8="));
            expect (reloaded.tracks.front().vst.hasPlugin());

            // "The plugin is up" is runtime knowledge: a file never claims it.
            expect (! reloaded.tracks.front().vst.loadedOk);

            // 按半音演奏 travels with the track, and a file written before the field
            // existed means "exact frequencies" (what the host always did).
            project.tracks.front().vst.equalTemperament = true;

            Project tuned;
            expect (projectFromJson (projectToJson (project), tuned, error), error);
            expect (tuned.tracks.front().vst.equalTemperament);

            auto withoutField = juce::JSON::parse (projectToJson (project));

            if (auto* root = withoutField.getDynamicObject())
                if (auto* tracks = root->getProperty ("tracks").getArray())
                    if (auto* first = (*tracks)[0].getDynamicObject())
                        if (auto* vst = first->getProperty ("vst").getDynamicObject())
                            vst->removeProperty ("equalTemperament");

            Project old;
            expect (projectFromJson (juce::JSON::toString (withoutField, false), old, error), error);
            expect (! old.tracks.front().vst.equalTemperament,
                    NI_TXT ("旧工程缺这个字段时仍然是精确频率"));
        }

        beginTest ("the older track kinds keep the numbers they always had");
        {
            auto project = makeDefaultProject();
            addTrack (project, TrackKind::drums);
            addTrack (project, TrackKind::vst);
            addTrack (project, TrackKind::subtractive);

            const auto json = projectToJson (project);
            Project reloaded;
            juce::String error;

            expect (projectFromJson (json, reloaded, error), error);
            expectEquals ((int) reloaded.tracks.size(), 3);
            expect (reloaded.tracks[0].kind == TrackKind::drums);
            expect (reloaded.tracks[1].kind == TrackKind::vst);
            expect (reloaded.tracks[2].kind == TrackKind::subtractive);

            // A kind written by an older build still loads the way it did.
            expectEquals ((int) TrackKind::vst, 6);
            expectEquals ((int) TrackKind::drums, 5);
        }

        beginTest ("a VST track with no plugin is silent and does not crash");
        {
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);

            if (auto* track = project.findTrack (id))
            {
                Note note;
                note.id = 1;
                note.frequencyHz = 660.0;
                note.durationTicks = kTicksPerQuarter;
                track->clips.front().notes.push_back (note);
            }

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::vst);
            expect (schedule.tracks.front().instrument == nullptr);
            expectEquals ((int) schedule.tracks.front().events.size(), 2);

            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (true);
            engine.renderNextBlock (512);

            const auto& output = engine.getOutputBuffer();
            expectEquals (output.getRMSLevel (0, 0, 512), 0.0f);
        }

        beginTest ("the schedule asks the factory for a private instance");
        {
            auto project = makeDefaultProject();
            const auto withPlugin = addTrack (project, TrackKind::vst);
            const auto withoutPlugin = addTrack (project, TrackKind::vst);
            auto* silent = project.findTrack (withoutPlugin);

            expect (silent != nullptr);
            juce::ignoreUnused (silent);   // no plugin path: the factory is not asked

            if (auto* track = project.findTrack (withPlugin))
                track->vst.pluginPath = "C:\\plugins\\Test.vst3";

            int calls = 0;
            auto instrument = std::make_shared<CountingInstrument>();

            const auto factory = [&calls, &instrument] (juce::int64, const VstParams&)
                -> std::shared_ptr<HostedInstrument>
            {
                ++calls;
                return instrument;
            };

            auto schedule = buildSchedule (project, {}, factory);

            // One track names a plugin, the other does not.
            expectEquals (calls, 1);
            expectEquals ((int) schedule.tracks.size(), 2);
            expect (schedule.tracks[0].instrument == instrument);
            expect (schedule.tracks[1].instrument == nullptr);
        }

        beginTest ("a hosted instrument is driven like a built-in one");
        {
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);

            if (auto* track = project.findTrack (id))
            {
                track->vst.pluginPath = "C:\\plugins\\Test.vst3";
                track->volumeDb = 0.0f;

                Note note;
                note.id = 1;
                note.startTick = 0;
                note.durationTicks = 480;
                note.frequencyHz = 440.0;
                note.velocity = 100;
                track->clips.front().notes.push_back (note);
            }

            auto instrument = std::make_shared<CountingInstrument>();

            const auto factory = [&instrument] (juce::int64, const VstParams&)
                -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            auto schedule = buildSchedule (project, {}, factory);

            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (true);
            engine.renderNextBlock (512);

            expect (instrument->noteOnCalls >= 1);
            expect (instrument->renderCalls >= 1);

            // Preparing is the job of whoever created the instance (the plugin host
            // live, or the export's factory), not of the render loop.
            expect (! instrument->prepared);

            // And the track's own gain reaches the plugin's output.
            expect (engine.getOutputBuffer().getRMSLevel (0, 0, 512) > 0.0f);

            engine.allNotesOff();
            engine.renderNextBlock (512);

            expect (instrument->allNotesOffCalls >= 1);
        }

        beginTest ("an idle external instrument is still processed");
        {
            // The plugin's own on-screen keyboard (Kontakt, Serum) makes sound from
            // the plugin's side: the host never sees a note for it. The engine used
            // to skip a hosted instrument unless HM's own channel bookkeeping said
            // something was ringing, which is exactly what made that keyboard silent.
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);

            if (auto* track = project.findTrack (id))
                track->vst.pluginPath = "C:\\plugins\\Test.vst3";

            auto instrument = std::make_shared<CountingInstrument>();
            instrument->active = false;   // nothing ringing as far as HM is concerned

            const auto factory = [&instrument] (juce::int64, const VstParams&)
                -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, factory)));
            engine.setPlaying (true);
            engine.renderNextBlock (512);

            expect (instrument->renderCalls >= 1,
                    NI_TXT ("an instrument with no host notes is still asked to render"));
            expect (engine.getOutputBuffer().getRMSLevel (0, 0, 512) > 0.0f,
                    NI_TXT ("and what it makes on its own reaches the output"));
        }

        beginTest (NI_TXT ("a plugin can be auditioned while the transport is stopped"));
        {
            // Pressing play first must not be a precondition for hearing the plugin's
            // own keyboard: the graph keeps running while stopped.
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::vst);

            if (auto* track = project.findTrack (id))
                track->vst.pluginPath = "C:\\plugins\\Test.vst3";

            auto instrument = std::make_shared<CountingInstrument>();

            const auto factory = [&instrument] (juce::int64, const VstParams&)
                -> std::shared_ptr<HostedInstrument>
            {
                return instrument;
            };

            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (
                buildSchedule (project, {}, factory)));
            engine.setPlaying (false);

            // The very first block: the transport has never run.
            engine.renderNextBlock (512);

            expect (instrument->renderCalls >= 1,
                    NI_TXT ("the plugin is rendered without pressing play first"));
            expect (engine.getOutputBuffer().getRMSLevel (0, 0, 512) > 0.0f,
                    NI_TXT ("and its audition reaches the output"));
        }
    }
};

class EffectChainTests : public juce::UnitTest
{
public:
    EffectChainTests() : juce::UnitTest ("EffectChain", "hmdaw") {}

    static EffectSlot makeSlot (const juce::String& name)
    {
        EffectSlot slot;
        slot.kind = 0;
        slot.name = name;
        slot.pluginPath = "C:\\plugins\\" + name + ".vst3";
        return slot;
    }

    /** Puts one slot on a track, which is what makes the engine ask the factory for
        an instance in the first place. */
    static void addSlot (Project& project, int trackIndex, const juce::String& name)
    {
        if (trackIndex >= 0 && trackIndex < (int) project.tracks.size())
            project.tracks[(size_t) trackIndex].effects.push_back (makeSlot (name));
    }

    /** One synth track playing a steady note, ready to render. */
    static Project makeProject (int noteCount = 1)
    {
        auto project = makeDefaultProject();
        const auto id = addTrack (project, TrackKind::synth);
        auto* track = project.findTrack (id);
        track->clips.front().notes.clear();

        for (int i = 0; i < noteCount; ++i)
        {
            Note note;
            note.id = project.nextNoteId++;
            note.startTick = (juce::int64) i * kTicksPerQuarter;
            note.durationTicks = kTicksPerQuarter * 4;
            note.frequencyHz = 440.0;
            note.velocity = 100;
            track->clips.front().notes.push_back (note);
        }

        return project;
    }

    static float renderRms (const Project& project, const HostedEffectFactory& factory,
                            int blockSize = 512, int blocks = 40)
    {
        PlaybackEngine engine;
        engine.prepare (48000.0, blockSize);
        engine.setSchedule (std::make_shared<const RenderSchedule> (
            buildSchedule (project, {}, {}, factory)));
        engine.setPlaying (true);

        float peak = 0.0f;

        for (int i = 0; i < blocks; ++i)
        {
            engine.renderNextBlock (blockSize);
            // Past the attack, so the envelope has settled.
            if (i > 10)
                peak = juce::jmax (peak, engine.getOutputBuffer().getRMSLevel (0, 0, blockSize));
        }

        return peak;
    }

    static HostedEffectFactory factoryFor (std::shared_ptr<FakeEffect> effect)
    {
        return [effect] (juce::int64, int, const EffectSlot&) -> std::shared_ptr<HostedEffect>
        {
            return effect;
        };
    }

    void runTest() override
    {
        beginTest ("the chain scales what the track renders");
        {
            auto plainProject = makeProject();
            auto processedProject = makeProject();
            addSlot (processedProject, 0, "Half");

            const auto plain = renderRms (plainProject, {});
            auto effect = std::make_shared<FakeEffect> (1, 0.5f);
            const auto halved = renderRms (processedProject, factoryFor (effect));

            expect (plain > 0.01f, "the track makes sound on its own");
            expect (effect->processCount > 0, "the chain ran");
            expectWithinAbsoluteError (halved * 2.0f, plain, plain * 0.02f);
        }

        beginTest ("two inserts run in the order the list shows them");
        {
            // Halving and adding a constant do not commute, so walking the chain
            // backwards would land on a different level: (x / 2) + 0.05 is louder
            // than (x + 0.05) / 2. The slots stay put; only which fake sits in slot
            // 0 changes.
            auto project = makeProject();
            addSlot (project, 0, "First");
            addSlot (project, 0, "Second");

            const auto makeFactory = [] (float firstGain, float firstOffset,
                                         float secondGain, float secondOffset)
            {
                return [firstGain, firstOffset, secondGain, secondOffset]
                       (juce::int64, int slotIndex, const EffectSlot&)
                    -> std::shared_ptr<HostedEffect>
                {
                    return std::make_shared<FakeEffect> (
                        0, slotIndex == 0 ? firstGain : secondGain, 0,
                        slotIndex == 0 ? firstOffset : secondOffset);
                };
            };

            const auto gainThenOffset = renderRms (project, makeFactory (0.5f, 0.0f, 1.0f, 0.05f));
            const auto offsetThenGain = renderRms (project, makeFactory (1.0f, 0.05f, 0.5f, 0.0f));

            expect (gainThenOffset > 0.0f && offsetThenGain > 0.0f, "both orders make sound");
            expect (gainThenOffset > offsetThenGain + 0.002f,
                    "the first slot is processed first, so the two orders differ");
        }

        beginTest ("a bypassed effect is not asked to process anything");
        {
            auto project = makeProject();
            addSlot (project, 0, "Bypassed");

            auto effect = std::make_shared<FakeEffect> (1, 0.5f);
            effect->bypass = true;

            const auto plain = renderRms (makeProject(), {});
            const auto bypassed = renderRms (project, factoryFor (effect));

            expectEquals (effect->processCount, 0);
            expectWithinAbsoluteError (bypassed, plain, plain * 0.001f);
        }

        beginTest ("the track fader is applied after the chain");
        {
            // The chain gets the same signal whatever the fader is set to - that is
            // what "inserts are pre-fader" means. A gain-only effect cannot tell the
            // order apart, so this watches what the effect is handed instead.
            auto quiet = makeProject();
            quiet.tracks.front().volumeDb = -12.0f;
            addSlot (quiet, 0, "Peek");

            auto loud = makeProject();
            loud.tracks.front().volumeDb = 0.0f;
            addSlot (loud, 0, "Peek");

            auto quietEffect = std::make_shared<FakeEffect> (1, 1.0f);
            auto loudEffect = std::make_shared<FakeEffect> (1, 1.0f);

            const auto quietOut = renderRms (quiet, factoryFor (quietEffect));
            const auto loudOut = renderRms (loud, factoryFor (loudEffect));

            expect (quietEffect->peakSeen > 0.0f && loudEffect->peakSeen > 0.0f,
                    "both chains saw a signal");
            expectWithinAbsoluteError (quietEffect->peakSeen, loudEffect->peakSeen, 0.001f);
            expect (quietOut < loudOut * 0.4f, "while the fader still turned the output down");
        }

        beginTest ("an insert only touches its own track");
        {
            auto project = makeProject();
            const auto secondId = addTrack (project, TrackKind::synth);

            if (auto* second = project.findTrack (secondId))
            {
                Note note;
                note.id = project.nextNoteId++;
                note.startTick = 0;
                note.durationTicks = kTicksPerQuarter * 4;
                note.frequencyHz = 660.0;
                note.velocity = 100;
                second->clips.front().notes.push_back (note);
            }

            const auto firstTrackId = project.tracks.front().id;
            addSlot (project, 0, "Half");
            auto effect = std::make_shared<FakeEffect> (firstTrackId, 0.5f);

            const auto factory = [effect, firstTrackId] (juce::int64 trackId, int,
                                                         const EffectSlot&)
                -> std::shared_ptr<HostedEffect>
            {
                // The engine asks for a chain per track; only the first track has
                // one, and the fake records which track it was made for.
                return trackId == firstTrackId ? effect : nullptr;
            };

            renderRms (project, factory);

            expect (effect->trackId == firstTrackId);
            expect (effect->processCount > 0, "the first track's chain ran");
            expect (effect->samplesSeen % 512 == 0 || effect->samplesSeen > 0);
        }

        beginTest ("a reported latency is shown but not compensated");
        {
            const auto plain = renderRms (makeProject(), {});
            
            auto project = makeProject();
            addSlot (project, 0, "Latent");
            auto effect = std::make_shared<FakeEffect> (1, 1.0f, 256);

            const auto latency = renderRms (project, factoryFor (effect));

            expect (effect->processCount > 0, "the chain ran");
            expectEquals (effect->latencySamples, 256);
            expectWithinAbsoluteError (latency, plain, plain * 0.001f);
        }

        beginTest ("an audio track's clips go through its chain as well");
        {
            // A one second ramp file, placed as an audio clip.
            const auto file = writeTempWav ("selftest-effect-audio.wav", 1.0);

            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::audio);
            auto* track = project.findTrack (id);

            Clip clip = makeDefaultClip (project);
            clip.startTick = 0;
            clip.lengthTicks = kTicksPerQuarter * 4;
            clip.loopLengthTicks = clip.lengthTicks;
            clip.audioFilePath = file.getFullPathName();
            clip.audioFileName = file.getFileName();
            track->clips.push_back (clip);

            const auto plain = renderRms (project, {});
            addSlot (project, 0, "Half");
            auto effect = std::make_shared<FakeEffect> (id, 0.5f);
            const auto processed = renderRms (project, factoryFor (effect));

            expect (plain > 0.0f, "the audio clip makes sound on its own");
            expect (effect->processCount > 0, "and its track's chain ran");

            if (plain > 0.001f)
                expectWithinAbsoluteError (processed * 2.0f, plain, plain * 0.05f);
        }

        beginTest ("the chain itself: add, move, bypass, remove");
        {
            std::vector<EffectSlot> chain;

            expect (effects::add (chain, makeSlot ("A")), "add A");
            expect (effects::add (chain, makeSlot ("B")), "add B");
            expect (effects::add (chain, makeSlot ("C")), "add C");
            expectEquals ((int) chain.size(), 3);

            // A slot without a plugin is refused.
            expect (! effects::add (chain, EffectSlot{}), NI_TXT ("一个没有插件的槽加不进去"));
            expectEquals ((int) chain.size(), 3);

            expect (effects::moveUp (chain, 2), "C moves up");
            expectEquals (chain[1].name, juce::String ("C"));
            expectEquals (chain[2].name, juce::String ("B"));

            expect (effects::moveDown (chain, 0), "A moves down");
            expectEquals (chain[1].name, juce::String ("A"));

            expect (! effects::moveUp (chain, 0), "the first slot cannot move up");
            expect (! effects::moveDown (chain, 2), "the last slot cannot move down");
            expect (! effects::move (chain, 9, 0), "an index outside the chain is refused");

            expect (effects::toggleBypass (chain, 1), "bypass toggles");
            expect (chain[1].bypass);
            expect (! effects::remove (chain, 7), "removing an index outside the chain is refused");
            expect (effects::remove (chain, 1), "remove the middle one");
            expectEquals ((int) chain.size(), 2);

            while ((int) chain.size() < effects::kMaxPerTrack)
                effects::add (chain, makeSlot ("fill"));

            expectEquals ((int) chain.size(), effects::kMaxPerTrack);
            expect (! effects::add (chain, makeSlot ("over")), "the chain has a ceiling");
        }

        beginTest ("the chain survives the project file");
        {
            auto project = makeProject();
            auto& chain = project.tracks.front().effects;

            effects::add (chain, makeSlot ("Reverb"));
            effects::add (chain, makeSlot ("Delay"));
            chain[1].stateBase64 = "SGVsbG8=";
            chain[1].bypass = true;

            Project reloaded;
            juce::String error;

            expect (projectFromJson (projectToJson (project), reloaded, error), error);
            expectEquals ((int) reloaded.tracks.front().effects.size(), 2);
            expectEquals (reloaded.tracks.front().effects[0].name, juce::String ("Reverb"));
            expectEquals (reloaded.tracks.front().effects[1].name, juce::String ("Delay"));
            expectEquals (reloaded.tracks.front().effects[1].stateBase64, juce::String ("SGVsbG8="));
            expect (reloaded.tracks.front().effects[1].bypass);
            expect (! reloaded.tracks.front().effects[0].bypass);

            // A project written before insert chains existed has none.
            const juce::String old (R"({"format":"hmdaw-project","version":1,)"
                                    R"("tracks":[{"id":1,"kind":0,"name":"T"}]})");
            Project ancient;

            expect (projectFromJson (old, ancient, error), error);
            expect (ancient.tracks.front().effects.empty());
        }
    }
};

class PluginGroupingTests : public juce::UnitTest
{
public:
    PluginGroupingTests() : juce::UnitTest ("PluginGrouping", "hmdaw") {}

    void runTest() override
    {
        // pluginName / pluginCategory, not name / category: the short spellings
        // shadow members of the JUCE types used right next to them.
        const auto makePlugin = [] (const juce::String& pluginName, const juce::String& vendor,
                                    const juce::String& pluginCategory)
        {
            juce::PluginDescription plugin;
            plugin.name = pluginName;
            plugin.manufacturerName = vendor;
            plugin.category = pluginCategory;
            plugin.isInstrument = pluginCategory.containsIgnoreCase ("Instrument");
            return plugin;
        };

        const std::vector<juce::PluginDescription> plugins
        {
            makePlugin ("Zeta", "Vendor B", "Instrument|Synth"),
            makePlugin ("Alpha", "Vendor A", "Fx|Reverb"),
            makePlugin ("Beta", "Vendor A", "Fx|Dynamics|Compressor"),
            makePlugin ("Gamma", "", "Instrument|Sampler"),
            makePlugin ("Delta", "Vendor C", ""),
            makePlugin ("Epsilon", "Vendor C", "Fx")
        };

        beginTest ("the type of a plugin comes from its VST3 subcategories");
        {
            expectEquals (pluginTypeOf (plugins[0]), NI_TXT ("合成器"));
            expectEquals (pluginTypeOf (plugins[1]), NI_TXT ("混响"));
            // A third, more specific token is not used: the first specific one is.
            expectEquals (pluginTypeOf (plugins[2]), NI_TXT ("动态"));
            expectEquals (pluginTypeOf (plugins[3]), NI_TXT ("采样器"));
            // Nothing but the generic "Fx" says no more than "effect".
            expectEquals (pluginTypeOf (plugins[4]), NI_TXT ("其他"));
            expectEquals (pluginTypeOf (plugins[5]), NI_TXT ("其他"));

            // A subcategory the table does not know is shown as the plugin wrote it.
            expectEquals (pluginTypeOf (makePlugin ("X", "V", "Fx|Phase Distortion")),
                          juce::String ("Phase Distortion"));
        }

        beginTest ("the vendor of a plugin falls back to 未知厂家");
        {
            expectEquals (pluginVendorOf (plugins[0]), juce::String ("Vendor B"));
            expectEquals (pluginVendorOf (plugins[3]), NI_TXT ("未知厂家"));
        }

        beginTest ("grouping by vendor sorts headings and plugins");
        {
            const auto groups = groupPlugins (plugins, PluginGroupAxis::vendor);

            expectEquals ((int) groups.size(), 4);
            expectEquals (groups[0].name, juce::String ("Vendor A"));
            expectEquals (groups[1].name, juce::String ("Vendor B"));
            expectEquals (groups[2].name, juce::String ("Vendor C"));
            // 未知厂家 is a catch-all, so it comes last even though 未 sorts early.
            expectEquals (groups[3].name, NI_TXT ("未知厂家"));

            expectEquals (groups[2].plugins[0].name, juce::String ("Delta"));
            expectEquals (groups[2].plugins[1].name, juce::String ("Epsilon"));
            expectEquals ((int) groups[0].plugins.size(), 2);
        }

        beginTest ("grouping by type files the same plugins under Chinese headings");
        {
            const auto groups = groupPlugins (plugins, PluginGroupAxis::type);
            juce::StringArray names;

            for (const auto& group : groups)
                names.add (group.name);

            expect (names.contains (NI_TXT ("合成器")));
            expect (names.contains (NI_TXT ("采样器")));
            expect (names.contains (NI_TXT ("混响")));
            expect (names.contains (NI_TXT ("动态")));
            // 其他 is last.
            expectEquals (names[names.size() - 1], NI_TXT ("其他"));
            expectEquals ((int) groups[groups.size() - 1].plugins.size(), 2);
        }

        beginTest ("不分组 is one heading holding everything, sorted by name");
        {
            const auto groups = groupPlugins (plugins, PluginGroupAxis::none);

            expectEquals ((int) groups.size(), 1);
            expect (groups[0].name.isEmpty());
            expectEquals ((int) groups[0].plugins.size(), 6);
            expectEquals (groups[0].plugins.front().name, juce::String ("Alpha"));
            expectEquals (groups[0].plugins.back().name, juce::String ("Zeta"));
        }

        beginTest ("the grouping axis survives the settings text round trip");
        {
            for (auto axis : { PluginGroupAxis::vendor, PluginGroupAxis::type, PluginGroupAxis::none })
                expect (pluginGroupAxisFromText (pluginGroupAxisToText (axis)) == axis);

            expect (pluginGroupAxisFromText ("") == PluginGroupAxis::vendor);
            expect (pluginGroupAxisFromText ("something else") == PluginGroupAxis::vendor);
            expect (pluginGroupAxisFromText ("TYPE") == PluginGroupAxis::type);
        }
    }
};

class WindowPlacementTests : public juce::UnitTest
{
public:
    WindowPlacementTests() : juce::UnitTest ("WindowPlacement", "hmdaw") {}

    void runTest() override
    {
        const juce::Rectangle<int> fullHd { 0, 0, 1920, 1040 };

        beginTest ("a window that fits keeps its size and lands centred");
        {
            const auto bounds = placement::placeWindow ({ 800, 600 }, fullHd);

            expectEquals (bounds.getWidth(), 800);
            expectEquals (bounds.getHeight(), 600);
            expect (fullHd.contains (bounds), "and sits inside the display");
            expectWithinAbsoluteError ((double) bounds.getCentreX(), 960.0, 2.0);
            expectWithinAbsoluteError ((double) bounds.getCentreY(), 520.0, 2.0);
        }

        beginTest ("a window bigger than the screen is shrunk to the work area");
        {
            // The main window's design size on a 1366x768 laptop.
            const juce::Rectangle<int> laptop { 0, 0, 1366, 728 };
            const auto bounds = placement::placeWindow ({ 1460, 950 }, laptop);

            expect (laptop.contains (bounds), "nothing hangs off the screen");
            expectEquals (bounds.getWidth(), 1366 - placement::kMargin * 2);
            expectEquals (bounds.getHeight(), 728 - placement::kMargin * 2);
            expectEquals (bounds.getX(), placement::kMargin);
            expectEquals (bounds.getY(), placement::kMargin);
        }

        beginTest ("a window lands on the display it belongs to");
        {
            // A second monitor to the left of the main one has a negative origin;
            // every HM window asks for the area of the screen it is opening on.
            const juce::Rectangle<int> second { -1280, 0, 1280, 1024 };
            const auto bounds = placement::placeWindow ({ 600, 400 }, second);

            expect (second.contains (bounds), "it stays on that monitor");
            expect (bounds.getX() >= second.getX() && bounds.getRight() <= second.getRight(),
                    "and horizontally inside it");
        }

        beginTest ("a work area smaller than the margins still gives a window");
        {
            const juce::Rectangle<int> tiny { 0, 0, 6, 4 };
            const auto bounds = placement::placeWindow ({ 800, 600 }, tiny);

            expect (bounds.getWidth() >= 1 && bounds.getHeight() >= 1,
                    "never an inside-out rectangle");
            expect (bounds.getWidth() <= tiny.getWidth() && bounds.getHeight() <= tiny.getHeight(),
                    "and never wider than what there is");

            const auto area = placement::usableArea ({ 10, 20, 4, 4 });

            expect (area.getWidth() >= 1 && area.getHeight() >= 1);
            expectEquals (area.getX(), 10 + placement::kMargin);
            expectEquals (area.getY(), 20 + placement::kMargin);
        }

        beginTest ("the resize minimum gives way to a small screen");
        {
            // 1120x720 is what the layout needs; a 150%-scaled 1920x1080 screen only
            // offers the app 1280x672 of work area.
            const juce::Rectangle<int> scaledScreen { 0, 0, 1280, 672 };
            const auto minimum = placement::fitMinimum ({ 1120, 720 }, scaledScreen);

            expectEquals (minimum.x, 1120);
            expectEquals (minimum.y, 672 - placement::kMargin * 2);
            expect (minimum.y <= scaledScreen.getHeight(), "the minimum fits the screen");

            // On a screen with room for it the asked-for minimum survives untouched.
            const auto roomy = placement::fitMinimum ({ 1120, 720 }, fullHd);

            expectEquals (roomy.x, 1120);
            expectEquals (roomy.y, 720);

            // And nonsense input still comes back as a window, not as a 0 x 0 one.
            const auto floored = placement::fitMinimum ({ -40, 0 }, fullHd);

            expectEquals (floored.x, 1);
            expectEquals (floored.y, 1);
        }
    }
};

/** The handshake between HM and the copy of itself that reads one plugin file.

    The process split is the only reason a bad plugin cannot take the editor down, so
    the part of it that can be tested without a plugin - the command line and the
    result file - is tested here. The half that really loads a plugin is covered by
    the offscreen self test.
*/
class PluginScanProtocolTests : public juce::UnitTest
{
public:
    PluginScanProtocolTests() : juce::UnitTest ("PluginScanProtocol", "hmdaw") {}

    void runTest() override
    {
        beginTest ("a scan child reads its job off the command line");
        {
            const juce::StringArray tokens { "--scan-plugin", "C:\\plugins\\A.vst3",
                                             "--scan-out", "C:\\temp\\out.xml" };

            expect (scanproto::isChildInvocation (tokens));

            scanproto::Request request;

            expect (scanproto::parseRequest (tokens, request));
            expectEquals (request.pluginFile.getFullPathName(),
                          juce::File ("C:\\plugins\\A.vst3").getFullPathName());
            expectEquals (request.resultFile.getFullPathName(),
                          juce::File ("C:\\temp\\out.xml").getFullPathName());
            expect (! request.failForTest);
            expect (! request.hangForTest);

            // What the parent builds has to parse back.
            const auto rebuilt = scanproto::makeChildArguments (request);
            scanproto::Request again;

            expect (scanproto::parseRequest (rebuilt, again));
            expectEquals (again.pluginFile.getFullPathName(), request.pluginFile.getFullPathName());
            expectEquals (again.resultFile.getFullPathName(), request.resultFile.getFullPathName());

            // The self test switches ride along with the rest.
            auto withSwitch = request;
            withSwitch.failForTest = true;

            scanproto::Request third;

            expect (scanproto::parseRequest (scanproto::makeChildArguments (withSwitch), third));
            expect (third.failForTest);
            expect (! third.hangForTest);
        }

        beginTest ("a half-written request is refused");
        {
            scanproto::Request request;

            expect (! scanproto::isChildInvocation ({ "--selftest", "C:\\out" }));
            expect (! scanproto::parseRequest ({}, request));
            expect (! scanproto::parseRequest ({ "--scan-plugin", "A.vst3" }, request));
            // Two switches in a row: the first one has no value at all.
            expect (! scanproto::parseRequest ({ "--scan-plugin", "--scan-out", "x.xml" }, request));
        }

        beginTest ("a found plugin survives the trip through the result file");
        {
            juce::PluginDescription description;
            description.name = "Test Instrument";
            description.manufacturerName = "HM";
            description.category = "Instrument|Synth";
            description.fileOrIdentifier = "C:\\plugins\\A.vst3";
            description.uniqueId = 4242;
            description.isInstrument = true;

            scanproto::Result result;
            result.plugins.add (description);
            expect (result.ok());

            scanproto::Result back;

            expect (scanproto::parse (scanproto::serialize (result), back));
            expect (! scanproto::hasFailed (back));
            expectEquals (back.plugins.size(), 1);
            expectEquals (back.plugins[0].name, description.name);
            expectEquals (back.plugins[0].manufacturerName, description.manufacturerName);
            expectEquals (back.plugins[0].category, description.category);
            expectEquals (back.plugins[0].uniqueId, description.uniqueId);
        }

        beginTest ("junk in the result file is a failure, not a plugin");
        {
            scanproto::Result result;

            expect (! scanproto::parse ({}, result));
            expect (! scanproto::parse ("not xml at all", result));
            expect (! scanproto::parse ("<SomethingElse/>", result));

            // A file the child never got round to writing is a failure.
            expect (scanproto::hasFailed (result));

            // So is a result that came back with an explanation and nothing else.
            scanproto::Result explained;
            explained.error = "could not read";
            expect (! explained.ok());
            expect (scanproto::hasFailed (explained));

            // And a truncated one: whatever the parser makes of it, it must never
            // count as a plugin that was found.
            scanproto::Result good;
            juce::PluginDescription description;
            description.name = "Test Instrument";
            description.fileOrIdentifier = "C:\\plugins\\A.vst3";
            good.plugins.add (description);

            const auto text = scanproto::serialize (good);
            const auto truncated = text.substring (0, juce::jmax (0, text.length() / 2));

            scanproto::Result partial;
            const auto parsed = scanproto::parse (truncated, partial);

            expect (! (parsed && ! scanproto::hasFailed (partial)));
        }
    }
};

namespace
{
    VstInstrumentTests  vstInstrumentTests;
    EffectChainTests    effectChainTests;
    PluginGroupingTests pluginGroupingTests;
    WindowPlacementTests windowPlacementTests;
    PluginScanProtocolTests pluginScanProtocolTests;
}
