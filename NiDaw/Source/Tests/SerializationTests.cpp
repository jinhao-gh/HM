#include "TestSupport.h"

class SerializationTests : public juce::UnitTest
{
public:
    SerializationTests() : juce::UnitTest ("Serialization", "nidaw") {}

    void runTest() override
    {
        beginTest ("project round trip");

        auto project = makeProjectWithSynthTrack();
        project.sampleRate = 44100.0;
        project.bufferSize = 256;
        project.tempoMarkers.push_back ({ 7680, 3, 4, 90.0 });

        auto& track = project.tracks.front();
        track.kind = TrackKind::sampler;   // reserved kinds have to round trip too
        track.name = NI_TXT ("测试乐器");
        track.volumeDb = -3.5f;
        track.pan = 0.25f;
        track.synth.patch.name = NI_TXT ("测试音色");
        track.synth.patch.algorithm = 17;
        track.synth.patch.feedback = 5;
        track.synth.patch.lfoPitchDepth = 12;
        track.synth.patch.transpose = -5;
        track.synth.patch.ops[2].ratio = 3.5f;
        track.synth.patch.ops[2].outputLevel = 64;
        track.synth.patch.ops[2].velSens = 3;
        track.synth.patch.ops[2].rate[1] = 42;
        track.synth.patch.ops[2].level[1] = 55;
        track.synth.filterCutoffHz = 3200.0f;
        track.synth.filterResonance = 4.5f;

        Note note;
        note.id = project.nextNoteId++;
        note.startTick = 1920;
        note.durationTicks = 480;
        note.frequencyHz = 261.6255653;
        note.velocity = 77;
        note.muted = true;
        note.comment = NI_TXT ("备注");
        track.clips.front().notes.push_back (note);

        project.view.trackSnap.denominator = 4;
        project.view.editorSnap.denominator = 3;
        project.view.controllerSnap.denominator = 8;

        ReferenceDot groupDot;
        groupDot.position = 0.5849625;
        groupDot.colourIndex = 6;

        project.referenceGroup.name = NI_TXT ("测试参考组");
        project.referenceGroup.dots.push_back (groupDot);

        ReferenceLineGroup lineGroup;
        lineGroup.name = NI_TXT ("测试线组");
        lineGroup.anchorHz = 196.0;
        lineGroup.anchorPosition = 0.0;
        lineGroup.dots.push_back (groupDot);
        lineGroup.visible = false;

        auto sealedGroup = music::makeGroupEntry (project.nextReferenceLineId++, lineGroup);
        sealedGroup.locked = true;
        project.referenceLines.push_back (sealedGroup);

        auto sealedHz = music::makeFixedHzEntry (project.nextReferenceLineId++, 123.456,
                                                 NI_TXT ("测试赫兹"));
        sealedHz.locked = true;
        project.referenceLines.push_back (sealedHz);

        // An unlocked entry is scratch: it must NOT survive a save.
        project.referenceLines.push_back (music::makeFixedHzEntry (project.nextReferenceLineId++,
                                                                   880.0, NI_TXT ("临时")));

        const auto json = projectToJson (project);
        Project reloaded;
        juce::String error;

        expect (projectFromJson (json, reloaded, error), error);
        expectEquals (reloaded.sampleRate, 44100.0);
        expectEquals (reloaded.bufferSize, 256);
        expectEquals ((int) reloaded.tempoMarkers.size(), 2);
        expectEquals ((int) reloaded.tracks.size(), 1);

        const auto& rtrack = reloaded.tracks.front();
        expect (rtrack.kind == TrackKind::sampler);
        expectEquals (rtrack.name, track.name);
        expectEquals (rtrack.volumeDb, -3.5f);
        expectEquals (rtrack.pan, 0.25f);
        expectEquals (rtrack.synth.patch.name, NI_TXT ("测试音色"));
        expectEquals (rtrack.synth.patch.algorithm, 17);
        expectEquals (rtrack.synth.patch.feedback, 5);
        expectEquals (rtrack.synth.patch.lfoPitchDepth, 12);
        expectEquals (rtrack.synth.patch.transpose, -5);
        expectWithinAbsoluteError ((double) rtrack.synth.patch.ops[2].ratio, 3.5, 1.0e-6);
        expectEquals (rtrack.synth.patch.ops[2].outputLevel, 64);
        expectEquals (rtrack.synth.patch.ops[2].velSens, 3);
        expectEquals (rtrack.synth.patch.ops[2].rate[1], 42);
        expectEquals (rtrack.synth.patch.ops[2].level[1], 55);
        expectEquals (rtrack.synth.filterCutoffHz, 3200.0f);
        expectEquals (rtrack.synth.filterResonance, 4.5f);
        expectEquals ((int) rtrack.clips.size(), 1);
        expectEquals ((int) rtrack.clips.front().notes.size(), 1);

        const auto& rnote = rtrack.clips.front().notes.front();
        expectEquals (rnote.startTick, (juce::int64) 1920);
        expectEquals (rnote.durationTicks, (juce::int64) 480);
        expectWithinAbsoluteError (rnote.frequencyHz, 261.6255653, 1.0e-6);
        expectEquals (rnote.velocity, 77);
        expect (rnote.muted);
        expectEquals (rnote.comment, note.comment);
        expectEquals (reloaded.view.trackSnap.denominator, 4);
        expectEquals (reloaded.view.editorSnap.denominator, 3);
        expectEquals (reloaded.view.controllerSnap.denominator, 8);

        beginTest ("the reference group round trips");

        expectEquals (reloaded.referenceGroup.dots.size(), (size_t) 1);
        expectEquals (reloaded.referenceGroup.name, NI_TXT ("测试参考组"));
        expectWithinAbsoluteError (reloaded.referenceGroup.dots.front().position, 0.5849625, 1.0e-9);
        expectEquals (reloaded.referenceGroup.dots.front().colourIndex, 6);

        expectEquals ((int) reloaded.referenceLines.size(), 2);
        expect (reloaded.referenceLines.front().kind == ReferenceLineKind::group);
        expectEquals (reloaded.referenceLines.front().name, NI_TXT ("测试线组"));
        expectWithinAbsoluteError (reloaded.referenceLines.front().anchorHz, 196.0, 1.0e-9);
        expect (! reloaded.referenceLines.front().visible);
        expect (reloaded.referenceLines.front().locked);

        expect (reloaded.referenceLines.back().kind == ReferenceLineKind::fixedHz);
        expectWithinAbsoluteError (reloaded.referenceLines.back().baseHz, 123.456, 1.0e-9);
        expectEquals (reloaded.referenceLines.back().name, NI_TXT ("测试赫兹"));

        beginTest ("garbage input is rejected");

        Project ignored;
        expect (! projectFromJson ("this is not json", ignored, error));

        beginTest ("a project with no tracks stays empty, and old files default to synth");

        {
            // A brand new project has no tracks at all - that is a legal project, and
            // it must not gain a track on the way through the file format.
            auto empty = makeDefaultProject();
            expectEquals ((int) empty.tracks.size(), 0);

            Project reloadedEmpty;
            expect (projectFromJson (projectToJson (empty), reloadedEmpty, error), error);
            expectEquals ((int) reloadedEmpty.tracks.size(), 0);
            expectEquals ((int) reloadedEmpty.tempoMarkers.size(), 1);

            // A first track added by the + menu carries its kind and one empty clip.
            auto withTrack = makeDefaultProject();
            const auto id = addTrack (withTrack, TrackKind::synth);

            expect (id != 0);
            expectEquals ((int) withTrack.tracks.size(), 1);
            expect (withTrack.tracks.front().kind == TrackKind::synth);
            expectEquals ((int) withTrack.tracks.front().clips.size(), 1);
            expectEquals ((int) withTrack.tracks.front().clips.front().notes.size(), 0);

            // Files written before the kind existed come back as synths.
            const juce::String noKind = R"({
                "format": "nidaw-project", "version": 1, "sampleRate": 48000,
                "tracks": [ { "id": 3, "name": "old", "volumeDb": -6, "pan": 0,
                              "colourIndex": 0, "clips": [] } ]
            })";

            Project legacyKind;
            expect (projectFromJson (noKind, legacyKind, error), error);
            expectEquals ((int) legacyKind.tracks.size(), 1);
            expect (legacyKind.tracks.front().kind == TrackKind::synth);
        }

        beginTest ("a project saved before the filter existed still loads");

        const juce::String legacy = R"({
            "format": "nidaw-project", "version": 1, "sampleRate": 48000,
            "tempoMarkers": [ { "tick": 0, "numerator": 4, "denominator": 4, "bpm": 120 } ],
            "tracks": [ {
                "id": 1, "name": "legacy", "volumeDb": -6, "pan": 0, "colourIndex": 0,
                "synth": { "waveform": 2, "attack": 0.01, "decay": 0.1, "sustain": 0.7,
                           "release": 0.2, "fmRatio": 1, "fmDepth": 0, "gain": 1 },
                "notes": [ { "id": 7, "startTick": 1920, "durationTicks": 960,
                             "frequencyHz": 440, "velocity": 100, "ratio": 2, "comment": "" } ]
            } ]
        })";

        Project legacyProject;
        expect (projectFromJson (legacy, legacyProject, error), error);
        expectEquals ((int) legacyProject.tracks.size(), 1);

        if (! legacyProject.tracks.empty())
        {
            const auto& legacySynth = legacyProject.tracks.front().synth;

            // The old single-oscillator synth is mapped onto operators 1 and 2 of
            // algorithm 1, so an old project keeps its shape.
            expectEquals (legacySynth.patch.algorithm, 1);
            expectEquals (legacySynth.patch.ops[0].outputLevel, 99);
            expectEquals (legacySynth.patch.ops[1].outputLevel, 0);   // that file had fmDepth 0
            expectGreaterThan (legacySynth.patch.ops[0].rate[0], 50); // 0.01 s attack
            expectGreaterThan (legacySynth.patch.ops[0].level[1], 50); // sustain 0.7
            expectEquals (legacySynth.filterCutoffHz, 20000.0f);
            expectEquals (legacySynth.filterResonance, 0.707f);
        }

        beginTest ("a project saved before reference groups existed has none");

        expectEquals ((int) legacyProject.referenceGroup.dots.size(), 0);
        expectEquals ((int) legacyProject.referenceLines.size(), 0);

        // A file written before the 指示线 switch existed keeps the arrow on.
        expect (legacyProject.view.ratioArrowVisible);

        // Same for 不吸附: a file written before it existed keeps snapping on.
        expect (legacyProject.view.trackSnap.enabled);
        expect (legacyProject.view.editorSnap.enabled);
        expect (legacyProject.view.controllerSnap.enabled);

        // ...and the metronome stays off, with no loop region at all.
        expect (! legacyProject.view.metronomeEnabled);
        expect (! legacyProject.loop.enabled);
        expect (! legacyProject.loop.isValid());

        beginTest (NI_TXT ("the loop region survives the round trip"));

        {
            auto caseProject = makeDefaultProject();
            caseProject.loop.startTick = 3840;
            caseProject.loop.endTick   = 11520;
            caseProject.loop.enabled   = true;
            caseProject.view.metronomeEnabled = true;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expectEquals (caseReloaded.loop.startTick, (juce::int64) 3840);
            expectEquals (caseReloaded.loop.endTick, (juce::int64) 11520);
            expect (caseReloaded.loop.enabled && caseReloaded.loop.isValid());
            expect (caseReloaded.view.metronomeEnabled);

            // A region with no length counts as "not set", so the loop button
            // cannot wrap playback into a zero-length cycle.
            auto empty = caseProject;
            empty.loop = {};
            expect (projectFromJson (projectToJson (empty), caseReloaded, error), error);
            expect (! caseReloaded.loop.isValid());
            expect (! caseReloaded.loop.enabled);
        }

        beginTest (NI_TXT ("the 指示线 switch survives the round trip"));

        {
            auto caseProject = makeDefaultProject();
            caseProject.view.ratioArrowVisible = false;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expect (! caseReloaded.view.ratioArrowVisible);

            caseProject.view.ratioArrowVisible = true;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expect (caseReloaded.view.ratioArrowVisible);
        }

        beginTest (NI_TXT ("the 不吸附 option survives the round trip"));

        {
            auto caseProject = makeDefaultProject();
            caseProject.view.trackSnap.enabled = false;
            caseProject.view.trackSnap.denominator = 4;
            caseProject.view.editorSnap.enabled = true;
            caseProject.view.editorSnap.denominator = 3;
            caseProject.view.controllerSnap.enabled = false;
            caseProject.view.controllerSnap.denominator = 16;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expect (! caseReloaded.view.trackSnap.enabled);
            expect (caseReloaded.view.editorSnap.enabled);
            expect (! caseReloaded.view.controllerSnap.enabled);

            // The subdivision is kept even while snapping is off: it is still what
            // sizes the default and minimum note lengths.
            expectEquals (caseReloaded.view.trackSnap.denominator, 4);
            expectEquals (caseReloaded.view.editorSnap.denominator, 3);
            expectEquals (caseReloaded.view.controllerSnap.denominator, 16);
        }

        beginTest (NI_TXT ("each pane's own horizontal view survives the round trip"));

        {
            auto caseProject = makeDefaultProject();
            caseProject.view.pixelsPerQuarter = 220.0;
            caseProject.view.horizontalOffsetTicks = 480.0;
            caseProject.view.arrangementPixelsPerQuarter = 90.0;
            caseProject.view.arrangementHorizontalOffsetTicks = 1920.0;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);

            // The editor's view and the arrangement's are separate numbers, and both
            // come back: zooming the editor no longer drags the lanes along.
            expectWithinAbsoluteError (caseReloaded.view.pixelsPerQuarter, 220.0, 1.0e-9);
            expectWithinAbsoluteError (caseReloaded.view.horizontalOffsetTicks, 480.0, 1.0e-9);
            expectWithinAbsoluteError (caseReloaded.view.arrangementPixelsPerQuarter, 90.0, 1.0e-9);
            expectWithinAbsoluteError (caseReloaded.view.arrangementHorizontalOffsetTicks,
                                       1920.0, 1.0e-9);

            // And the accessors really pick the one that was asked for.
            expectWithinAbsoluteError (pixelsPerQuarterFor (caseReloaded.view, TimelineView::editor),
                                       220.0, 1.0e-9);
            expectWithinAbsoluteError (
                pixelsPerQuarterFor (caseReloaded.view, TimelineView::arrangement), 90.0, 1.0e-9);
            expectWithinAbsoluteError (
                horizontalOffsetFor (caseReloaded.view, TimelineView::arrangement), 1920.0, 1.0e-9);
        }

        beginTest (NI_TXT ("a project written before the panes were split keeps one view"));

        {
            // The view object of an older file: no arrangement fields at all. The
            // (empty) track list is what a real file has; a file without one at all
            // is treated as a fresh project, view included.
            const juce::String caseJson (R"({"format":"nidaw-project","version":1,)"
                                     R"("tracks":[],)"
                                     R"("view":{"pixelsPerQuarter":240.0,)"
                                     R"("horizontalOffsetTicks":960.0}})");

            Project caseReloaded;
            expect (projectFromJson (caseJson, caseReloaded, error), error);
            expectWithinAbsoluteError (caseReloaded.view.pixelsPerQuarter, 240.0, 1.0e-9);

            // The arrangement is seeded from the editor, so such a file opens
            // looking exactly as it was left.
            expectWithinAbsoluteError (caseReloaded.view.arrangementPixelsPerQuarter, 240.0, 1.0e-9);
            expectWithinAbsoluteError (caseReloaded.view.arrangementHorizontalOffsetTicks,
                                       960.0, 1.0e-9);
        }

        beginTest (NI_TXT ("track mute and solo survive the round trip"));

        {
            auto caseProject = makeProjectWithSynthTrack();
            const auto secondId = addTrack (caseProject, TrackKind::synth);
            expect (secondId != 0);

            caseProject.tracks.front().muted  = true;
            caseProject.tracks.back().soloed  = true;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expectEquals ((int) caseReloaded.tracks.size(), 2);

            if (caseReloaded.tracks.size() == 2)
            {
                expect (caseReloaded.tracks.front().muted);
                expect (! caseReloaded.tracks.front().soloed);
                expect (caseReloaded.tracks.back().soloed);
                expect (! caseReloaded.tracks.back().muted);
            }

            // A caseProject written before track mute / solo existed reads as neither.
            auto older = caseProject;
            older.tracks.front().muted = false;
            older.tracks.back().soloed = false;

            auto parsed = juce::JSON::parse (projectToJson (older));

            if (auto* root = parsed.getDynamicObject())
                if (auto* tracks = root->getProperty ("tracks").getArray())
                    for (auto& entry : *tracks)
                        if (auto* trackObject = entry.getDynamicObject())
                        {
                            trackObject->removeProperty ("muted");
                            trackObject->removeProperty ("soloed");
                        }

            Project withoutFlags;
            expect (projectFromJson (juce::JSON::toString (parsed), withoutFlags, error), error);

            for (const auto& caseTrack : withoutFlags.tracks)
            {
                expect (! caseTrack.muted, NI_TXT ("a missing mute reads as false"));
                expect (! caseTrack.soloed, NI_TXT ("a missing solo reads as false"));
            }
        }

        beginTest (NI_TXT ("a subtractive sound survives the project round trip"));

        {
            auto caseProject = makeProjectWithSynthTrack();
            auto& caseTrack = caseProject.tracks.front();
            caseTrack.kind = TrackKind::subtractive;
            caseTrack.subtractive = SubtractiveParams();
            caseTrack.subtractive.name = NI_TXT ("铺底");
            caseTrack.subtractive.oscs[1] = subtractive::osc (OscWave::triangle, 0.5f, 1, -2, 22.0f, 0.35f);
            caseTrack.subtractive.oscs[1].enabled = false;
            caseTrack.subtractive.filterMode = FilterMode::bandPass;
            caseTrack.subtractive.filterSlopeDb = 12;
            caseTrack.subtractive.filterCutoffHz = 850.0f;
            caseTrack.subtractive.filterEnvAmount = -0.6f;
            caseTrack.subtractive.filterKeyTrack = 0.25f;
            caseTrack.subtractive.noiseLevel = 0.15f;
            caseTrack.subtractive.ampSustain = 0.35f;
            caseTrack.subtractive.filterDecay = 0.75f;
            caseTrack.subtractive.lfoWaveform = 3;
            caseTrack.subtractive.lfoAmpDepth = 0.4f;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expectEquals ((int) caseReloaded.tracks.size(), 1);

            if (! caseReloaded.tracks.empty())
            {
                const auto& back = caseReloaded.tracks.front();
                expect (back.kind == TrackKind::subtractive);
                expectEquals (back.subtractive.name, NI_TXT ("铺底"));
                expect (back.subtractive == caseTrack.subtractive,
                        NI_TXT ("every subtractive parameter survives"));
            }

            // A caseTrack written before the subtractive instrument existed loads with
            // a usable default sound rather than an empty one.
            auto older = caseProject;
            older.tracks.front().kind = TrackKind::synth;
            older.tracks.front().subtractive = SubtractiveParams();

            auto parsed = juce::JSON::parse (projectToJson (older));

            if (auto* root = parsed.getDynamicObject())
                if (auto* tracks = root->getProperty ("tracks").getArray())
                    if (! tracks->isEmpty())
                        if (auto* trackObject = (*tracks)[0].getDynamicObject())
                            trackObject->removeProperty ("subtractive");

            Project withoutSection;
            expect (projectFromJson (juce::JSON::toString (parsed), withoutSection, error), error);
            expectEquals ((int) withoutSection.tracks.size(), 1);

            if (! withoutSection.tracks.empty())
            {
                const auto& back = withoutSection.tracks.front();
                expect (back.subtractive == SubtractiveParams(),
                        NI_TXT ("a missing section reads as the default sound"));
                expectGreaterThan ((double) back.subtractive.oscs[0].level, 0.0);
                expectGreaterThan ((double) back.subtractive.filterCutoffHz, 0.0);
            }
        }

        beginTest (NI_TXT ("a wavetable sound survives the project round trip"));

        {
            auto caseProject = makeProjectWithSynthTrack();
            auto& caseTrack = caseProject.tracks.front();
            caseTrack.kind = TrackKind::wavetable;
            caseTrack.wavetable = WavetableParams();
            caseTrack.wavetable.name = NI_TXT ("铺底");
            caseTrack.wavetable.oscs[0].tableIndex = 12;
            caseTrack.wavetable.oscs[0].position = 0.37f;
            caseTrack.wavetable.oscs[1].tableIndex = 5;
            caseTrack.wavetable.oscs[1].position = 0.88f;
            caseTrack.wavetable.oscs[1].enabled = false;
            caseTrack.wavetable.oscs[1].detuneCents = -17.0f;
            caseTrack.wavetable.noiseLevel = 0.12f;
            caseTrack.wavetable.filterMode = FilterMode::highPass;
            caseTrack.wavetable.filterSlopeDb = 12;
            caseTrack.wavetable.filterCutoffHz = 640.0f;
            caseTrack.wavetable.filterKeyTrack = 0.3f;
            caseTrack.wavetable.posEnvAmount = -0.45f;
            caseTrack.wavetable.posSustain = 0.25f;
            caseTrack.wavetable.lfoWaveform = 3;
            caseTrack.wavetable.lfoPositionDepth = 0.6f;
            caseTrack.wavetable.gain = 0.65f;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expectEquals ((int) caseReloaded.tracks.size(), 1);

            if (! caseReloaded.tracks.empty())
            {
                const auto& back = caseReloaded.tracks.front();
                expect (back.kind == TrackKind::wavetable);
                expectEquals (back.wavetable.name, NI_TXT ("铺底"));
                expect (back.wavetable == caseTrack.wavetable,
                        NI_TXT ("every wavetable parameter survives"));
            }

            // The older kinds keep their serialised values.
            auto older = caseProject;
            older.tracks.front().kind = TrackKind::subtractive;
            expect (projectFromJson (projectToJson (older), caseReloaded, error), error);
            expect (caseReloaded.tracks.front().kind == TrackKind::subtractive);

            older.tracks.front().kind = TrackKind::audio;
            expect (projectFromJson (projectToJson (older), caseReloaded, error), error);
            expect (caseReloaded.tracks.front().kind == TrackKind::audio);

            older.tracks.front().kind = TrackKind::sampler;
            expect (projectFromJson (projectToJson (older), caseReloaded, error), error);
            expect (caseReloaded.tracks.front().kind == TrackKind::sampler);

            older.tracks.front().kind = TrackKind::synth;
            expect (projectFromJson (projectToJson (older), caseReloaded, error), error);
            expect (caseReloaded.tracks.front().kind == TrackKind::synth);

            // A caseTrack written before the wavetable instrument existed loads with
            // the default sound.
            auto without = caseProject;
            without.tracks.front().kind = TrackKind::synth;
            without.tracks.front().wavetable = WavetableParams();

            auto parsed = juce::JSON::parse (projectToJson (without));

            if (auto* root = parsed.getDynamicObject())
                if (auto* tracks = root->getProperty ("tracks").getArray())
                    if (! tracks->isEmpty())
                        if (auto* trackObject = (*tracks)[0].getDynamicObject())
                            trackObject->removeProperty ("wavetable");

            Project missing;
            expect (projectFromJson (juce::JSON::toString (parsed), missing, error), error);
            expectEquals ((int) missing.tracks.size(), 1);

            if (! missing.tracks.empty())
                expect (missing.tracks.front().wavetable == WavetableParams(),
                        NI_TXT ("a missing section reads as the default sound"));
        }

        beginTest (NI_TXT ("a sealed reference line group survives the round trip"));

        {
            auto caseProject = makeDefaultProject();

            ReferenceLineGroup group;
            group.name           = NI_TXT ("参考线组 1");
            group.anchorHz       = 220.0;
            group.anchorPosition = 0.0;
            group.visible        = true;

            ReferenceDot dot;
            dot.position    = 0.5849625007211562;   // log2 (330 / 220)
            dot.colourIndex = 3;
            group.dots.push_back (dot);

            auto entry = music::makeGroupEntry (1, group);
            entry.locked = true;
            caseProject.referenceLines.push_back (entry);
            caseProject.nextReferenceLineId = 2;

            Project caseReloaded;
            expect (projectFromJson (projectToJson (caseProject), caseReloaded, error), error);
            expectEquals ((int) caseReloaded.referenceLines.size(), 1);

            if (! caseReloaded.referenceLines.empty())
            {
                const auto& back = caseReloaded.referenceLines.front();
                expectEquals ((int) back.kind, (int) ReferenceLineKind::group);
                expect (back.locked, NI_TXT ("the seal is what makes it survive"));
                expectEquals (back.name, NI_TXT ("参考线组 1"));
                expectWithinAbsoluteError (back.anchorHz, 220.0, 1.0e-9);
                expectEquals ((int) back.dots.size(), 1);

                if (! back.dots.empty())
                {
                    expectWithinAbsoluteError (back.dots[0].position, 0.5849625007211562, 1.0e-9);
                    expectEquals (back.dots[0].colourIndex, 3);
                }
            }
        }

        beginTest ("old reference groups and manual input migrate to sealed entries");

        // The UTF-8 decode matters here: juce::String(const char*) is ASCII-only,
        // so a raw string with Chinese in it has to go through fromUTF8. (Not
        // NI_TXT - the macro would split on the commas inside the JSON.)
        const juce::String olderWithLines = juce::String::fromUTF8 (R"({
            "format": "nidaw-project", "version": 1, "sampleRate": 48000,
            "tempoMarkers": [ { "tick": 0, "numerator": 4, "denominator": 4, "bpm": 120 } ],
            "tracks": [ { "id": 1, "name": "old", "volumeDb": -6, "pan": 0, "colourIndex": 0,
                "synth": { "waveform": 0, "attack": 0.01, "decay": 0.1, "sustain": 0.7,
                           "release": 0.2, "fmRatio": 1, "fmDepth": 0, "gain": 1 },
                "notes": [] } ],
            "view": { "manualInputHz": 330.5, "referenceLock": true, "lockedBaseHz": 660,
                      "lockedRatio": 3, "currentRatio": 5 },
            "referenceLineGroups": [ { "name": "旧线组", "anchorHz": 196, "anchorPosition": 0,
                                       "visible": true,
                                       "dots": [ { "position": 0.5, "colourIndex": 2 } ] } ]
        })");

        Project migratedProject;
        expect (projectFromJson (olderWithLines, migratedProject, error), error);
        expectEquals ((int) migratedProject.referenceLines.size(), 2);

        if (migratedProject.referenceLines.size() == 2)
        {
            // Migrated state used to be persistent, so it comes back sealed.
            expect (migratedProject.referenceLines[0].kind == ReferenceLineKind::group);
            expectEquals (migratedProject.referenceLines[0].name, NI_TXT ("旧线组"));
            expect (migratedProject.referenceLines[0].locked);
            expectWithinAbsoluteError (migratedProject.referenceLines[0].anchorHz, 196.0, 1.0e-9);

            expect (migratedProject.referenceLines[1].kind == ReferenceLineKind::fixedHz);
            expectWithinAbsoluteError (migratedProject.referenceLines[1].baseHz, 330.5, 1.0e-9);
            expect (migratedProject.referenceLines[1].locked);
        }

        // The old "lock the reference lines" switch and the old per-note ratio are
        // simply ignored now; the legacy note carries no mute flag either.
        expect (migratedProject.tracks.front().clips.front().notes.empty()
                    || ! migratedProject.tracks.front().clips.front().notes.front().muted);
        expect (migratedProject.nextReferenceLineId > migratedProject.referenceLines.back().id);

        beginTest ("a project saved before clips existed migrates to one clip");

        expectEquals ((int) legacyProject.tracks.size(), 1);

        if (! legacyProject.tracks.empty())
        {
            const auto& migrated = legacyProject.tracks.front().clips;

            expectEquals ((int) migrated.size(), 1);

            if (! migrated.empty())
            {
                expectEquals (migrated.front().startTick, (juce::int64) 1920);
                expectEquals ((int) migrated.front().notes.size(), 1);
                expectEquals (migrated.front().notes.front().startTick, (juce::int64) 0);
                expect (migrated.front().lengthTicks >= (juce::int64) 960);
            }
        }

        // ----------------------------------------------------------------------
        beginTest ("the collapsible columns survive the project round trip");
        {
            auto columnsProject = makeProjectWithSynthTrack();
            columnsProject.view.referenceGroupPanel.size = 263;
            columnsProject.view.referenceGroupPanel.collapsed = false;
            columnsProject.view.presetPanel.size = 305;
            columnsProject.view.presetPanel.collapsed = true;
            columnsProject.view.referenceLinesPanel.size = 371;
            columnsProject.view.referenceLinesPanel.collapsed = true;

            Project columnsReloaded;
            juce::String columnsError;
            expect (projectFromJson (projectToJson (columnsProject), columnsReloaded, columnsError));

            expectEquals (columnsReloaded.view.referenceGroupPanel.size, 263);
            expect (! columnsReloaded.view.referenceGroupPanel.collapsed);
            expectEquals (columnsReloaded.view.presetPanel.size, 305);
            expect (columnsReloaded.view.presetPanel.collapsed);
            expectEquals (columnsReloaded.view.referenceLinesPanel.size, 371);
            expect (columnsReloaded.view.referenceLinesPanel.collapsed);

            // A project written before the columns could be resized carries none of
            // those fields: it opens at the defaults, unfolded.
            auto asVar = juce::JSON::parse (projectToJson (columnsProject));

            if (auto* root = asVar.getDynamicObject())
                if (auto* view = root->getProperty ("view").getDynamicObject())
                    for (const auto* key : { "panelReferenceGroupSize", "panelReferenceGroupCollapsed",
                                             "panelPresetSize", "panelPresetCollapsed",
                                             "panelReferenceLinesSize",
                                             "panelReferenceLinesCollapsed" })
                        view->removeProperty (key);

            Project columnsOld;
            expect (projectFromJson (juce::JSON::toString (asVar, false), columnsOld, columnsError));

            expectEquals (columnsOld.view.referenceGroupPanel.size, 0);
            expect (! columnsOld.view.referenceGroupPanel.collapsed);
            expectEquals (columnsOld.view.presetPanel.size, 0);
            expect (! columnsOld.view.presetPanel.collapsed);
            expect (! columnsOld.view.referenceLinesPanel.collapsed);
        }

        // ----------------------------------------------------------------------
        beginTest (NI_TXT ("侧视（其他轨道当背景板）开关随工程保存"));
        {
            auto sideProject = makeDefaultProject();
            sideProject.view.sideView = true;

            Project sideReloaded;
            juce::String sideError;
            expect (projectFromJson (projectToJson (sideProject), sideReloaded, sideError), sideError);
            expect (sideReloaded.view.sideView, NI_TXT ("打开侧视保存下来还是打开的"));

            sideProject.view.sideView = false;
            expect (projectFromJson (projectToJson (sideProject), sideReloaded, sideError), sideError);
            expect (! sideReloaded.view.sideView, NI_TXT ("关掉之后保存下来还是关的"));

            // A file written before 侧视 existed has never had it on.
            auto asVar = juce::JSON::parse (projectToJson (sideProject));

            if (auto* root = asVar.getDynamicObject())
                if (auto* view = root->getProperty ("view").getDynamicObject())
                    view->removeProperty ("viewSideView");

            Project sideOld;
            expect (projectFromJson (juce::JSON::toString (asVar, false), sideOld, sideError), sideError);
            expect (! sideOld.view.sideView, NI_TXT ("旧工程缺这个字段就是关的"));

            // The picker's un-ticked tracks travel with the project too, and an
            // older file has none of them (which means "show every other track").
            sideProject.view.sideView = true;
            sideProject.view.sideViewHiddenTracks = { 3, 7 };
            expect (projectFromJson (projectToJson (sideProject), sideReloaded, sideError), sideError);
            expectEquals ((int) sideReloaded.view.sideViewHiddenTracks.size(), 2);
            expect (sideReloaded.view.sideViewHiddenTracks.count (3) == 1
                        && sideReloaded.view.sideViewHiddenTracks.count (7) == 1,
                    NI_TXT ("勾掉过的轨道存下来还在"));

            auto asVar2 = juce::JSON::parse (projectToJson (sideProject));

            if (auto* root = asVar2.getDynamicObject())
                if (auto* view = root->getProperty ("view").getDynamicObject())
                    view->removeProperty ("viewSideViewHidden");

            Project sideOld2;
            expect (projectFromJson (juce::JSON::toString (asVar2, false), sideOld2, sideError),
                    sideError);
            expectEquals ((int) sideOld2.view.sideViewHiddenTracks.size(), 0);
        }

        // ----------------------------------------------------------------------
        beginTest (NI_TXT ("MIDI 输入的映射随工程保存"));
        {
            auto midiProject = makeDefaultProject();
            midiProject.midiInput.map.mode = midimap::Map::Mode::looping;
            midiProject.midiInput.map.anchorNote = 55;
            midimap::setLoopKeys (midiProject.midiInput.map, 6);
            midiProject.midiInput.map.setFrequency (55, 110.0);
            midiProject.midiInput.map.setFrequency (58, 165.0);
            midiProject.midiInput.groupSource.kind = MidiGroupSource::Kind::preset;
            midiProject.midiInput.groupSource.presetIndex = 3;
            midiProject.midiInput.group.name = NI_TXT ("三倍频");
            midiProject.midiInput.group.anchorHz = 220.0;
            midiProject.midiInput.group.dots = { { 0.585, 2 } };
            midiProject.midiInput.groupColourIndex = 5;   // the entry's own colour
            midiProject.midiInput.presetName = NI_TXT ("我的映射");

            Project midiReloaded;
            juce::String midiError;
            expect (projectFromJson (projectToJson (midiProject), midiReloaded, midiError), midiError);

            expect (midiReloaded.midiInput.map.mode == midimap::Map::Mode::looping);
            expectEquals (midiReloaded.midiInput.map.loopKeys, 6);
            expectEquals (midiReloaded.midiInput.map.anchorNote, 55);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (55), 110.0, 1.0e-9);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (58), 165.0, 1.0e-9);
            // Six semitones above the anchor with a six-semitone tile: the same step
            // again, an octave up (the tile rides with the keyboard).
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (61), 220.0, 1.0e-9);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (49), 55.0, 1.0e-9);
            expect (midiReloaded.midiInput.groupSource.kind == MidiGroupSource::Kind::preset);
            expectEquals (midiReloaded.midiInput.groupSource.presetIndex, 3);
            expectEquals (midiReloaded.midiInput.group.name, NI_TXT ("三倍频"));
            expectWithinAbsoluteError (midiReloaded.midiInput.group.anchorHz, 220.0, 1.0e-9);
            expectEquals ((int) midiReloaded.midiInput.group.dots.size(), 1);
            expectEquals (midiReloaded.midiInput.group.dots.front().colourIndex, 2);
            expectEquals (midiReloaded.midiInput.groupColourIndex, 5);
            expectEquals (midiReloaded.midiInput.presetName, NI_TXT ("我的映射"));

            // A MIDI 输入 block written before that field existed must read as -1
            // ("every dot keeps its own colour"), not as palette entry 0.
            auto withoutColour = juce::JSON::parse (projectToJson (midiProject));

            if (auto* root = withoutColour.getDynamicObject())
                if (auto* midi = root->getProperty ("midiInput").getDynamicObject())
                    midi->removeProperty ("groupColourIndex");

            Project midiNoColour;
            expect (projectFromJson (juce::JSON::toString (withoutColour, false),
                                     midiNoColour, midiError), midiError);
            expectEquals (midiNoColour.midiInput.groupColourIndex, -1);

            // A free-mode mapping travels the same way.
            midiProject.midiInput.map.mode = midimap::Map::Mode::free;
            midiProject.midiInput.map.setFrequency (36, 65.406);
            expect (projectFromJson (projectToJson (midiProject), midiReloaded, midiError), midiError);
            expect (midiReloaded.midiInput.map.mode == midimap::Map::Mode::free);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (36), 65.406, 1.0e-3);
            expectEquals (midiReloaded.midiInput.map.frequencyFor (55), 0.0);

            // A file written before MIDI 输入 existed: an empty mapping, no group.
            auto asVar = juce::JSON::parse (projectToJson (midiProject));

            if (auto* root = asVar.getDynamicObject())
                root->removeProperty ("midiInput");

            Project midiOld;
            expect (projectFromJson (juce::JSON::toString (asVar, false), midiOld, midiError), midiError);
            expect (midiOld.midiInput.map.mode == midimap::Map::Mode::looping);
            expectEquals (midiOld.midiInput.map.boundKeyCount(), 0);
            expect (midiOld.midiInput.groupSource.kind == MidiGroupSource::Kind::none);
            expect (midiOld.midiInput.group.dots.empty());
            expect (! midiOld.midiInput.arp.enabled,
                    NI_TXT ("旧工程没有 arp 段：琶音是关的（第四十七批）"));

            // 琶音器 rides in the same 段, so a project (and a mapping preset, which
            // shares this serializer) carries it.
            midiProject.midiInput.arp.enabled = true;
            midiProject.midiInput.arp.rate = midiarp::Rate::eighthTriplet;
            midiProject.midiInput.arp.direction = midiarp::Direction::downUp;
            midiProject.midiInput.arp.octaves = 2;
            midiProject.midiInput.arp.gatePercent = 30;
            midiProject.midiInput.arp.swingPercent = 45;
            midiProject.midiInput.arp.velocityMode = midiarp::VelocityMode::decrescendo;
            midiProject.midiInput.arp.hold = true;

            expect (projectFromJson (projectToJson (midiProject), midiReloaded, midiError), midiError);
            expect (midiReloaded.midiInput.arp.enabled);
            expectEquals ((int) midiReloaded.midiInput.arp.rate,
                          (int) midiarp::Rate::eighthTriplet);
            expectEquals ((int) midiReloaded.midiInput.arp.direction,
                          (int) midiarp::Direction::downUp);
            expectEquals (midiReloaded.midiInput.arp.octaves, 2);
            expectEquals (midiReloaded.midiInput.arp.gatePercent, 30);
            expectEquals (midiReloaded.midiInput.arp.swingPercent, 45);
            expectEquals ((int) midiReloaded.midiInput.arp.velocityMode,
                          (int) midiarp::VelocityMode::decrescendo);
            expect (midiReloaded.midiInput.arp.hold);

            // An older *file* that has the midiInput 段 but no arp in it: off.
            auto midiNoArpVar = juce::JSON::parse (projectToJson (midiProject));

            if (auto* root = midiNoArpVar.getDynamicObject())
                if (auto* midi = root->getProperty ("midiInput").getDynamicObject())
                    midi->removeProperty ("arp");

            Project midiNoArp;
            expect (projectFromJson (juce::JSON::toString (midiNoArpVar, false), midiNoArp, midiError),
                    midiError);
            expect (! midiNoArp.midiInput.arp.enabled);

            // A key tied to several dots travels as a chord: numbers where there is
            // one cable, an array where the key is a stack.
            midiProject.midiInput.map.mode = midimap::Map::Mode::looping;
            midiProject.midiInput.map.setFrequency (55, 110.0);
            midiProject.midiInput.map.addFrequency (55, 165.0);
            midiProject.midiInput.map.addFrequency (55, 220.0);

            expect (projectFromJson (projectToJson (midiProject), midiReloaded, midiError), midiError);
            expectEquals (midiReloaded.midiInput.map.bindingCount (55), 3);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequenciesFor (55)[0], 110.0, 1.0e-9);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequenciesFor (55)[1], 165.0, 1.0e-9);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequenciesFor (55)[2], 220.0, 1.0e-9);
            // Three cables on 55, plus the single one 58 kept from earlier: four.
            expectEquals (midiReloaded.midiInput.map.cableCount(), 4);
            expectEquals (midiReloaded.midiInput.map.bindingCount (58), 1);

            // 非循环 mode keeps its stacks too.
            midiProject.midiInput.map.mode = midimap::Map::Mode::free;
            midiProject.midiInput.map.setFrequency (40, 80.0);
            midiProject.midiInput.map.addFrequency (40, 120.0);

            expect (projectFromJson (projectToJson (midiProject), midiReloaded, midiError), midiError);
            expectEquals (midiReloaded.midiInput.map.bindingCount (40), 2);
            expectWithinAbsoluteError (midiReloaded.midiInput.map.frequencyFor (40), 80.0, 1.0e-9);

            // A block written before keys could be stacks: plain numbers, and a 0
            // still means "nothing here" rather than a cable at 0 Hz.
            auto singleNumbers = juce::JSON::parse (projectToJson (midiProject));

            if (auto* root = singleNumbers.getDynamicObject())
                if (auto* midi = root->getProperty ("midiInput").getDynamicObject())
                {
                    juce::Array<juce::var> tile;
                    tile.add (0.0);      // 55: a hole, not a cable at 0 Hz
                    tile.add (62.5);     // 56
                    midi->setProperty ("tile", tile);
                    midi->setProperty ("mode", 0);   // looping: the tile is the table
                }

            Project midiPlainNumbers;
            expect (projectFromJson (juce::JSON::toString (singleNumbers, false),
                                     midiPlainNumbers, midiError), midiError);
            expectEquals (midiPlainNumbers.midiInput.map.bindingCount (55), 0);
            expectWithinAbsoluteError (midiPlainNumbers.midiInput.map.frequencyFor (56), 62.5, 1.0e-9);

            // ...and the same shape for 非循环's per-note numbers.
            if (auto* root = singleNumbers.getDynamicObject())
                if (auto* midi = root->getProperty ("midiInput").getDynamicObject())
                {
                    juce::Array<juce::var> freeNotes;
                    auto* hole = new juce::DynamicObject();
                    hole->setProperty ("note", 40);
                    hole->setProperty ("hz", 0.0);
                    freeNotes.add (juce::var (hole));
                    auto* two = new juce::DynamicObject();
                    two->setProperty ("note", 41);
                    two->setProperty ("hz", 96.0);
                    freeNotes.add (juce::var (two));
                    midi->setProperty ("free", freeNotes);
                    midi->setProperty ("mode", 1);   // 非循环: the per-note table
                }

            Project midiPlainFree;
            expect (projectFromJson (juce::JSON::toString (singleNumbers, false),
                                     midiPlainFree, midiError), midiError);
            expectEquals (midiPlainFree.midiInput.map.bindingCount (40), 0);
            expectWithinAbsoluteError (midiPlainFree.midiInput.map.frequencyFor (41), 96.0, 1.0e-9);
        }

        // ----------------------------------------------------------------------
        beginTest (NI_TXT ("the mixer's master fader rides in the project file"));
        {
            auto mixerProject = makeProjectWithSynthTrack();
            mixerProject.masterVolumeDb = -7.5f;

            Project mixerReloaded;
            juce::String mixerError;
            expect (projectFromJson (projectToJson (mixerProject), mixerReloaded, mixerError),
                    mixerError);
            expectWithinAbsoluteError (mixerReloaded.masterVolumeDb, -7.5f, 1.0e-4f);

            // A project written before the mixer existed has no such field: 0 dB,
            // which is what it has always sounded like.
            auto asVar = juce::JSON::parse (projectToJson (mixerProject));

            if (auto* root = asVar.getDynamicObject())
                root->removeProperty ("masterVolumeDb");

            Project mixerOld;
            expect (projectFromJson (juce::JSON::toString (asVar, false), mixerOld, mixerError),
                    mixerError);
            expectWithinAbsoluteError (mixerOld.masterVolumeDb, 0.0f, 1.0e-6f);

            // Out of range values are pulled back to what the fader can reach.
            mixerOld.masterVolumeDb = 40.0f;
            Project mixerClamped;
            expect (projectFromJson (projectToJson (mixerOld), mixerClamped, mixerError),
                    mixerError);
            expectWithinAbsoluteError (mixerClamped.masterVolumeDb, 6.0f, 1.0e-4f);

            mixerOld.masterVolumeDb = -400.0f;
            expect (projectFromJson (projectToJson (mixerOld), mixerClamped, mixerError),
                    mixerError);
            expectWithinAbsoluteError (mixerClamped.masterVolumeDb, -60.0f, 1.0e-4f);
        }

        // ----------------------------------------------------------------------
        beginTest ("a column's width is clamped, and the canvas keeps its room");
        {
            EditorViewState::PanelSize column;
            expectEquals (panelWidthFor (column, 210), 210);          // never resized
            column.size = 5;
            expectEquals (panelWidthFor (column, 210), kMinPanelWidth);   // dragged too far
            column.size = 5000;
            expectEquals (panelWidthFor (column, 210), 5000);        // the row does the clamping
            column.collapsed = true;
            expectEquals (panelWidthFor (column, 210), kCollapsedPanelWidth);

            // Three wide columns in a narrow row: the widest gives way first, and the
            // canvas ends up with what the columns left (never below the minimum a
            // column may be).
            auto group = 400, lines = 400, presets = 400;
            std::vector<int*> widths { &group, &lines, &presets };
            fitPanelsToCanvas (1000, 18, widths);

            const auto laidOut = juce::String (group) + "/" + juce::String (lines) + "/"
                                     + juce::String (presets);

            expect (group + lines + presets + 18 <= 1000, "the row fits: " + laidOut);
            expect (group >= kMinPanelWidth && lines >= kMinPanelWidth && presets >= kMinPanelWidth,
                   "nothing below the minimum: " + laidOut);
            // The cut goes to whichever column is widest at that moment, so three
            // equal columns end up sharing the squeeze instead of one of them being
            // crushed while the others keep their width.
            expect (std::abs (group - lines) <= 1 && std::abs (group - presets) <= 1,
                   "the columns shared the squeeze: " + laidOut);
        }
    }
};

namespace
{
    SerializationTests  serializationTests;
}
