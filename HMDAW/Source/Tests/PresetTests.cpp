#include "TestSupport.h"

class PresetStoreTests : public juce::UnitTest
{
public:
    PresetStoreTests() : juce::UnitTest ("PresetStore", "hmdaw") {}

    static juce::File makeTempFile()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("hmdaw-preset-test.json");
    }

    void runTest() override
    {
        beginTest ("a missing file loads as an empty library");

        {
            auto file = makeTempFile();
            file.deleteFile();

            PresetStore store (file);
            expect (store.load());
            expectEquals ((int) store.getReferenceGroups().size(), 0);
            expectEquals ((int) store.getReferenceLineGroups().size(), 0);
        }

        beginTest ("saving and reloading a library");

        {
            auto file = makeTempFile();
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                ReferenceDot dot;
                dot.position = 0.585;
                dot.colourIndex = 3;

            ReferenceGroup group;
            group.name = NI_TXT ("三倍频");
            group.dots.push_back (dot);
            group.frameColourIndex = 5;
            store.addReferenceGroup (group);

                ReferenceLineGroup lineGroup;
                lineGroup.name = NI_TXT ("线组一");
                lineGroup.anchorHz = 220.0;
                lineGroup.anchorPosition = 0.0;
                lineGroup.dots.push_back (dot);
                store.addReferenceLineGroup (lineGroup);

                expect (file.existsAsFile());
            }

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getReferenceGroups().size(), 1);
            expectEquals (reloaded.getReferenceGroups().front().name, NI_TXT ("三倍频"));
            expectEquals ((int) reloaded.getReferenceGroups().front().dots.size(), 1);
            expectWithinAbsoluteError (reloaded.getReferenceGroups().front().dots.front().position,
                                       0.585, 1.0e-9);
            expectEquals (reloaded.getReferenceGroups().front().dots.front().colourIndex, 3);
            expectEquals (reloaded.getReferenceGroups().front().frameColourIndex, 5);

            expectEquals ((int) reloaded.getReferenceLineGroups().size(), 1);
            expectWithinAbsoluteError (reloaded.getReferenceLineGroups().front().anchorHz, 220.0, 1.0e-9);
            expect (reloaded.getReferenceLineGroups().front().visible);

            beginTest (NI_TXT ("编辑栏的「保存」覆盖原处：replaceReferenceGroup / replaceReferenceLineGroup"));

            {
                ReferenceGroup edited;
                edited.dots.push_back ({ 0.25, 6 });

                reloaded.replaceReferenceGroup (0, edited);
                expectEquals (reloaded.getReferenceGroups().front().name, NI_TXT ("三倍频"),
                              NI_TXT ("空名字沿用原来那个名字"));
                expectEquals ((int) reloaded.getReferenceGroups().front().dots.size(), 1);
                expectWithinAbsoluteError (reloaded.getReferenceGroups().front().dots.front().position,
                                           0.25, 1.0e-9);
                expectEquals (reloaded.getReferenceGroups().front().dots.front().colourIndex, 6);

                ReferenceLineGroup editedLine;
                editedLine.name = NI_TXT ("线组改过");
                editedLine.anchorHz = 660.0;
                editedLine.anchorPosition = 0.5;
                editedLine.dots.push_back ({ 0.75, 2 });

                reloaded.replaceReferenceLineGroup (0, editedLine);
                expectEquals (reloaded.getReferenceLineGroups().front().name, NI_TXT ("线组改过"));
                expectWithinAbsoluteError (reloaded.getReferenceLineGroups().front().anchorHz, 660.0, 1.0e-9);
                expectWithinAbsoluteError (reloaded.getReferenceLineGroups().front().anchorPosition,
                                           0.5, 1.0e-9);
                expectEquals ((int) reloaded.getReferenceLineGroups().front().dots.size(), 1);
                expect (reloaded.getReferenceLineGroups().front().visible,
                        NI_TXT ("覆盖进去的参考线组一定可见"));

                // An index that is out of range is ignored, not a crash: the library
                // can change under an editor that is still open.
                const auto groupsBefore = (int) reloaded.getReferenceGroups().size();
                const auto linesBefore  = (int) reloaded.getReferenceLineGroups().size();

                reloaded.replaceReferenceGroup (-1, edited);
                reloaded.replaceReferenceGroup (9, edited);
                reloaded.replaceReferenceLineGroup (-1, editedLine);
                reloaded.replaceReferenceLineGroup (9, editedLine);

                expectEquals ((int) reloaded.getReferenceGroups().size(), groupsBefore);
                expectEquals ((int) reloaded.getReferenceLineGroups().size(), linesBefore);

                // And the overwrite really did go to disk.
                PresetStore fromDisk (file);
                expect (fromDisk.load());
                expectEquals ((int) fromDisk.getReferenceGroups().size(), 1);
                expectEquals (fromDisk.getReferenceGroups().front().name, NI_TXT ("三倍频"));
                expectWithinAbsoluteError (fromDisk.getReferenceGroups().front().dots.front().position,
                                           0.25, 1.0e-9);
                expectEquals (fromDisk.getReferenceLineGroups().front().name, NI_TXT ("线组改过"));
                expectWithinAbsoluteError (fromDisk.getReferenceLineGroups().front().anchorHz,
                                           660.0, 1.0e-9);
            }

            beginTest (NI_TXT ("MIDI 映射预设：存、取、改名、删"));

            {
                MidiInputState state;
                state.presetName = NI_TXT ("我的映射");
                state.map.mode = midimap::Map::Mode::looping;
                state.map.anchorNote = 57;
                midimap::setLoopKeys (state.map, 5);
                state.map.setFrequency (57, 220.0);
                state.map.setFrequency (59, 275.0);
                state.groupSource.kind = MidiGroupSource::Kind::entry;
                state.groupSource.entryId = 12;
                state.group.name = NI_TXT ("工程的参考线组");
                state.group.anchorHz = 440.0;
                state.group.dots = { { 0.25, 1 } };
                // 琶音器 is part of the mapping preset (第四十七批): the two are one
                // thing to the player.
                state.arp.enabled = true;
                state.arp.rate = midiarp::Rate::eighth;
                state.arp.direction = midiarp::Direction::upDown;
                state.arp.octaves = 3;
                state.arp.swingPercent = 55;
                state.arp.hold = true;

                reloaded.addMidiMap (state);
                expectEquals ((int) reloaded.getMidiMaps().size(), 1);
                expectEquals (reloaded.getMidiMaps().front().presetName, NI_TXT ("我的映射"));
                expectWithinAbsoluteError (reloaded.getMidiMaps().front().map.frequencyFor (57),
                                           220.0, 1.0e-9);
                expect (reloaded.getMidiMaps().front().groupSource.kind == MidiGroupSource::Kind::entry);
                expectEquals (reloaded.getMidiMaps().front().groupSource.entryId, (juce::int64) 12);

                PresetStore mapsOnDisk (file);
                expect (mapsOnDisk.load());
                expectEquals ((int) mapsOnDisk.getMidiMaps().size(), 1);
                expectEquals (mapsOnDisk.getMidiMaps().front().presetName, NI_TXT ("我的映射"));
                expectWithinAbsoluteError (mapsOnDisk.getMidiMaps().front().map.frequencyFor (59),
                                           275.0, 1.0e-9);
                expectEquals ((int) mapsOnDisk.getMidiMaps().front().group.dots.size(), 1);
                expect (mapsOnDisk.getMidiMaps().front().arp.enabled,
                        NI_TXT ("预设把琶音设置一起带上了"));
                expectEquals ((int) mapsOnDisk.getMidiMaps().front().arp.rate,
                              (int) midiarp::Rate::eighth);
                expectEquals ((int) mapsOnDisk.getMidiMaps().front().arp.direction,
                              (int) midiarp::Direction::upDown);
                expectEquals (mapsOnDisk.getMidiMaps().front().arp.octaves, 3);
                expectEquals (mapsOnDisk.getMidiMaps().front().arp.swingPercent, 55);
                expect (mapsOnDisk.getMidiMaps().front().arp.hold);

                // Overwrite in place, and the out-of-range index is ignored.
                auto edited = mapsOnDisk.getMidiMaps().front();
                edited.map.setFrequency (61, 330.0);
                mapsOnDisk.replaceMidiMap (0, edited);
                expectWithinAbsoluteError (mapsOnDisk.getMidiMaps().front().map.frequencyFor (61),
                                           330.0, 1.0e-9);

                mapsOnDisk.replaceMidiMap (4, edited);
                expectEquals ((int) mapsOnDisk.getMidiMaps().size(), 1);

                mapsOnDisk.renameMidiMap (0, NI_TXT ("改名了"));
                expectEquals (mapsOnDisk.getMidiMaps().front().presetName, NI_TXT ("改名了"));

                mapsOnDisk.removeMidiMap (0);
                expectEquals ((int) mapsOnDisk.getMidiMaps().size(), 0);

                PresetStore emptyAgain (file);
                expect (emptyAgain.load());
                expectEquals ((int) emptyAgain.getMidiMaps().size(), 0);
            }

            beginTest ("renaming and deleting are persisted");

            reloaded.renameReferenceGroup (0, NI_TXT ("改名了"));
            expectEquals (reloaded.getReferenceGroups().front().name, NI_TXT ("改名了"));

            reloaded.removeReferenceGroup (0);
            reloaded.removeReferenceLineGroup (0);
            expectEquals ((int) reloaded.getReferenceGroups().size(), 0);
            expectEquals ((int) reloaded.getReferenceLineGroups().size(), 0);

            PresetStore again (file);
            expect (again.load());
            expectEquals ((int) again.getReferenceGroups().size(), 0);
            expectEquals ((int) again.getReferenceLineGroups().size(), 0);

            file.deleteFile();
        }

        beginTest ("a corrupt file is reported but the library still opens");

        {
            auto file = makeTempFile();
            file.replaceWithText ("this is not json");

            PresetStore store (file);
            expect (! store.load());
            expect (store.getLastError().isNotEmpty());
            expectEquals ((int) store.getReferenceGroups().size(), 0);

            file.deleteFile();
        }
    }
};

class InstrumentPresetTests : public juce::UnitTest
{
public:
    InstrumentPresetTests() : juce::UnitTest ("InstrumentPresets", "hmdaw") {}

    static juce::File makeTempFile()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("hmdaw-instrument-preset-test.json");
    }

    void runTest() override
    {
        beginTest ("an operator switch survives the round trip");

        {
            SynthParams params;
            params.patch.algorithm = 7;
            params.patch.ops[2].enabled = false;
            params.gain = 0.5f;
            params.filterCutoffHz = 1234.0f;

            const auto restored = synthParamsFromVar (synthParamsToVar (params));

            expect (! restored.patch.ops[2].enabled);
            expect (restored.patch.ops[0].enabled);
            expect (restored.patch.ops[1].enabled);
            expect (restored.patch.ops[3].enabled);
            expectEquals (restored.patch.algorithm, 7);
            expectWithinAbsoluteError ((double) restored.gain, 0.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterCutoffHz, 1234.0, 1.0e-3);
        }

        beginTest ("the DX7 engine fields survive the round trip");
        {
            SynthParams params;
            params.patch.classicEngine = true;
            params.patch.algorithm = 40;
            params.patch.lfoPitchModSens = 5;
            params.patch.lfoSync = true;
            params.patch.pitchEg.rate[1] = 12;
            params.patch.pitchEg.level[2] = 70;

            auto& op = params.patch.ops[3];
            op.mode = FmOscMode::fixed;
            op.fixedHz = 1234.0f;
            op.breakPoint = 55;
            op.leftCurve = FmScaleCurve::posExp;
            op.rightCurve = FmScaleCurve::negExp;
            op.leftDepth = 21;
            op.rightDepth = 42;
            op.amSens = 2;

            const auto restored = synthParamsFromVar (synthParamsToVar (params));
            expect (restored.patch == params.patch);

            // A file from before this batch has no `classic` key: it loads as the
            // legacy engine and keeps the algorithm number it was saved with.
            auto* object = new juce::DynamicObject();
            object->setProperty ("algorithm", 7);
            object->setProperty ("operators", juce::Array<juce::var>());
            const auto old = synthParamsFromVar (juce::var (object));

            expect (! old.patch.classicEngine);
            expectEquals (old.patch.algorithm, 7);
        }

        beginTest ("a project written before the switches existed loads with them all on");
        {
            Project project = makeDefaultProject();
            const auto trackId = addTrack (project, TrackKind::synth);
            expect (trackId != 0);

            project.tracks.front().synth.patch.ops[0].enabled = false;

            auto parsed = juce::JSON::parse (projectToJson (project));
            auto* root = parsed.getDynamicObject();
            expect (root != nullptr);

            if (root != nullptr)
            {
                auto* tracks = root->getProperty ("tracks").getArray();
                expect (tracks != nullptr && ! tracks->isEmpty());

                if (tracks != nullptr && ! tracks->isEmpty())
                {
                    auto* trackObject = (*tracks)[0].getDynamicObject();
                    auto* synth = trackObject != nullptr
                                      ? trackObject->getProperty ("synth").getDynamicObject()
                                      : nullptr;
                    auto* operators = synth != nullptr
                                          ? synth->getProperty ("operators").getArray()
                                          : nullptr;
                    expect (operators != nullptr);

                    if (operators != nullptr)
                        for (auto& op : *operators)
                            if (auto* object = op.getDynamicObject())
                                object->removeProperty ("enabled");
                }
            }

            Project loaded;
            juce::String error;
            expect (projectFromJson (juce::JSON::toString (parsed), loaded, error));
            expectEquals ((int) loaded.tracks.size(), 1);

            if (! loaded.tracks.empty())
                for (const auto& op : loaded.tracks.front().synth.patch.ops)
                    expect (op.enabled);
        }

        beginTest ("the FM preset bank saves, renames and deletes");

        {
            const auto file = makeTempFile();
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                SynthParams preset;
                preset.patch.name = NI_TXT ("我的音色");
                preset.patch.algorithm = 12;
                preset.patch.ops[4].enabled = false;
                preset.patch.ops[4].ratio = 5.0f;
                preset.gain = 0.75f;

                store.addFmPreset (preset);

                expectEquals ((int) store.getFmPresets().size(), 1);
                expectEquals (store.findFmPresetByName (NI_TXT ("我的音色")), 0);
            }

            PresetStore reloaded (file);
            expect (reloaded.load());

            expectEquals ((int) reloaded.getFmPresets().size(), 1);
            expectEquals (reloaded.getFmPresets().front().patch.name, NI_TXT ("我的音色"));
            expectEquals (reloaded.getFmPresets().front().patch.algorithm, 12);
            expect (! reloaded.getFmPresets().front().patch.ops[4].enabled);
            expectWithinAbsoluteError ((double) reloaded.getFmPresets().front().gain, 0.75, 1.0e-6);

            reloaded.renameFmPreset (0, NI_TXT ("改名"));
            expectEquals (reloaded.getFmPresets().front().patch.name, NI_TXT ("改名"));
            expectEquals (reloaded.findFmPresetByName (NI_TXT ("我的音色")), -1);

            {
                SynthParams replacement;
                replacement.patch.name = NI_TXT ("改名");
                replacement.patch.algorithm = 30;
                reloaded.replaceFmPreset (0, replacement);
            }

            expectEquals ((int) reloaded.getFmPresets().size(), 1);
            expectEquals (reloaded.getFmPresets().front().patch.algorithm, 30);

            reloaded.removeFmPreset (0);
            expectEquals ((int) reloaded.getFmPresets().size(), 0);

            PresetStore again (file);
            expect (again.load());
            expectEquals ((int) again.getFmPresets().size(), 0);

            file.deleteFile();
        }

        beginTest ("a sampler preset keeps the file and every setting");

        {
            const auto file = makeTempFile();
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                SamplerParams preset;
                preset.name = NI_TXT ("我的采样");
                preset.filePath = "C:\\samples\\bell.wav";
                preset.fileName = "bell.wav";
                preset.rootHz = 261.63;
                preset.looped = true;
                preset.loopStart = 0.2;
                preset.loopEnd = 0.8;
                preset.attack = 0.03f;
                preset.sustain = 0.42f;
                preset.lfoPitchCents = 33.0f;
                preset.filterCutoffHz = 4321.0f;
                preset.gain = 0.6f;

                store.addSamplerPreset (preset);
            }

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getSamplerPresets().size(), 1);

            const auto& restored = reloaded.getSamplerPresets().front();
            expectEquals (restored.name, NI_TXT ("我的采样"));
            expectEquals (restored.filePath, juce::String ("C:\\samples\\bell.wav"));
            expectWithinAbsoluteError (restored.rootHz, 261.63, 1.0e-9);
            expect (restored.looped);
            expectWithinAbsoluteError (restored.loopStart, 0.2, 1.0e-9);
            expectWithinAbsoluteError (restored.loopEnd, 0.8, 1.0e-9);
            expectWithinAbsoluteError ((double) restored.attack, 0.03, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.sustain, 0.42, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.lfoPitchCents, 33.0, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterCutoffHz, 4321.0, 1.0e-3);
            expectWithinAbsoluteError ((double) restored.gain, 0.6, 1.0e-6);

            expectEquals (reloaded.findSamplerPresetByName (NI_TXT ("我的采样")), 0);
            expectEquals (reloaded.findSamplerPresetByName (NI_TXT ("没有这个")), -1);

            reloaded.renameSamplerPreset (0, NI_TXT ("改名的采样"));
            expectEquals (reloaded.getSamplerPresets().front().name, NI_TXT ("改名的采样"));
            reloaded.removeSamplerPreset (0);
            expectEquals ((int) reloaded.getSamplerPresets().size(), 0);

            file.deleteFile();
        }

        beginTest (NI_TXT ("a subtractive preset keeps every knob"));

        {
            const auto file = makeTempFile();
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                SubtractiveParams preset;
                preset.name = NI_TXT ("我的减法");
                preset.oscs[0] = subtractive::osc (OscWave::square, 0.8f, -1, 5, -12.0f, 0.3f);
                preset.oscs[1] = subtractive::osc (OscWave::pulse, 0.4f, 1, -3, 7.0f, 0.6f);
                preset.oscs[1].enabled = false;
                preset.noiseLevel = 0.2f;
                preset.filterMode = FilterMode::highPass;
                preset.filterSlopeDb = 12;
                preset.filterCutoffHz = 777.0f;
                preset.filterResonance = 4.5f;
                preset.filterEnvAmount = -0.4f;
                preset.filterKeyTrack = 0.6f;
                preset.ampAttack = 0.02f;
                preset.ampRelease = 1.5f;
                preset.filterSustain = 0.3f;
                preset.lfoRateHz = 7.5f;
                preset.lfoWaveform = 2;
                preset.lfoPitchCents = 42.0f;
                preset.lfoCutoffDepth = 0.5f;
                preset.gain = 0.7f;

                store.addSubtractivePreset (preset);
            }

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getSubtractivePresets().size(), 1);

            const auto& restored = reloaded.getSubtractivePresets().front();
            expectEquals (restored.name, NI_TXT ("我的减法"));
            expect (restored.oscs[0].wave == OscWave::square);
            expectEquals (restored.oscs[0].octave, -1);
            expectEquals (restored.oscs[0].semitone, 5);
            expectWithinAbsoluteError ((double) restored.oscs[0].detuneCents, -12.0, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.oscs[0].pulseWidth, 0.3, 1.0e-6);
            expect (! restored.oscs[1].enabled);
            expect (restored.oscs[1].wave == OscWave::pulse);
            expectWithinAbsoluteError ((double) restored.oscs[1].pulseWidth, 0.6, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.noiseLevel, 0.2, 1.0e-6);
            expect (restored.filterMode == FilterMode::highPass);
            expectEquals (restored.filterSlopeDb, 12);
            expectWithinAbsoluteError ((double) restored.filterCutoffHz, 777.0, 1.0e-3);
            expectWithinAbsoluteError ((double) restored.filterResonance, 4.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterEnvAmount, -0.4, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterKeyTrack, 0.6, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.ampAttack, 0.02, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.ampRelease, 1.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterSustain, 0.3, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.lfoRateHz, 7.5, 1.0e-6);
            expectEquals (restored.lfoWaveform, 2);
            expectWithinAbsoluteError ((double) restored.lfoPitchCents, 42.0, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.lfoCutoffDepth, 0.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.gain, 0.7, 1.0e-6);

            expectEquals (reloaded.findSubtractivePresetByName (NI_TXT ("我的减法")), 0);
            expectEquals (reloaded.findSubtractivePresetByName (NI_TXT ("没有这个")), -1);

            reloaded.renameSubtractivePreset (0, NI_TXT ("改名的减法"));
            expectEquals (reloaded.getSubtractivePresets().front().name, NI_TXT ("改名的减法"));

            auto replacement = reloaded.getSubtractivePresets().front();
            replacement.filterCutoffHz = 99.0f;
            reloaded.replaceSubtractivePreset (0, replacement);
            expectWithinAbsoluteError ((double) reloaded.getSubtractivePresets().front().filterCutoffHz,
                                       99.0, 1.0e-3);

            reloaded.removeSubtractivePreset (0);
            expectEquals ((int) reloaded.getSubtractivePresets().size(), 0);

            file.deleteFile();
        }

        beginTest ("a library written before the instrument sections existed still loads");

        {
            const auto file = makeTempFile();
            file.replaceWithText (R"({"format":"hmdaw-presets","version":1,"referenceGroups":[]})");

            PresetStore store (file);
            expect (store.load());
            expectEquals ((int) store.getFmPresets().size(), 0);
            expectEquals ((int) store.getSamplerPresets().size(), 0);
            expectEquals ((int) store.getSubtractivePresets().size(), 0);
            expectEquals ((int) store.getWavetablePatches().size(), 0);

            file.deleteFile();
        }

        beginTest (NI_TXT ("a wavetable preset keeps every knob"));

        {
            const auto file = makeTempFile();
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                WavetableParams preset;
                preset.name = NI_TXT ("我的波表");
                preset.oscs[0] = wavetable::osc (11, 0.65f, 0.8f, -1, 4, -12.0f);
                preset.oscs[1] = wavetable::osc (2, 0.15f, 0.4f, 1, -3, 9.0f);
                preset.oscs[1].enabled = false;
                preset.noiseLevel = 0.3f;
                preset.filterMode = FilterMode::bandPass;
                preset.filterSlopeDb = 12;
                preset.filterCutoffHz = 1234.0f;
                preset.filterResonance = 3.5f;
                preset.filterKeyTrack = 0.75f;
                preset.ampAttack = 0.02f;
                preset.ampRelease = 1.25f;
                preset.posAttack = 1.5f;
                preset.posSustain = 0.3f;
                preset.posEnvAmount = -0.6f;
                preset.lfoRateHz = 7.5f;
                preset.lfoWaveform = 2;
                preset.lfoPositionDepth = 0.45f;
                preset.lfoPitchCents = 33.0f;
                preset.gain = 0.7f;

                WavetablePatch patch;
                patch.params = preset;
                store.addWavetablePreset (std::move (patch));
            }

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getWavetablePatches().size(), 1);

            const auto& restored = reloaded.getWavetablePatches().front().params;
            expectEquals (restored.name, NI_TXT ("我的波表"));
            expectEquals (restored.oscs[0].tableIndex, 11);
            expectWithinAbsoluteError ((double) restored.oscs[0].position, 0.65, 1.0e-6);
            expectEquals (restored.oscs[0].octave, -1);
            expectEquals (restored.oscs[0].semitone, 4);
            expectWithinAbsoluteError ((double) restored.oscs[0].detuneCents, -12.0, 1.0e-6);
            expect (! restored.oscs[1].enabled);
            expectEquals (restored.oscs[1].tableIndex, 2);
            expectWithinAbsoluteError ((double) restored.noiseLevel, 0.3, 1.0e-6);
            expect (restored.filterMode == FilterMode::bandPass);
            expectEquals (restored.filterSlopeDb, 12);
            expectWithinAbsoluteError ((double) restored.filterCutoffHz, 1234.0, 1.0e-3);
            expectWithinAbsoluteError ((double) restored.filterResonance, 3.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.filterKeyTrack, 0.75, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.ampAttack, 0.02, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.ampRelease, 1.25, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.posAttack, 1.5, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.posSustain, 0.3, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.posEnvAmount, -0.6, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.lfoRateHz, 7.5, 1.0e-6);
            expectEquals (restored.lfoWaveform, 2);
            expectWithinAbsoluteError ((double) restored.lfoPositionDepth, 0.45, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.lfoPitchCents, 33.0, 1.0e-6);
            expectWithinAbsoluteError ((double) restored.gain, 0.7, 1.0e-6);

            expectEquals (reloaded.findWavetablePresetByName (NI_TXT ("我的波表")), 0);
            expectEquals (reloaded.findWavetablePresetByName (NI_TXT ("没有这个")), -1);

            reloaded.renameWavetablePreset (0, NI_TXT ("改名的波表"));
            expectEquals (reloaded.getWavetablePatches().front().params.name, NI_TXT ("改名的波表"));

            auto replacement = reloaded.getWavetablePatches().front();
            replacement.params.oscs[0].tableIndex = 4;
            reloaded.replaceWavetablePreset (0, replacement);
            expectEquals (reloaded.getWavetablePatches().front().params.oscs[0].tableIndex, 4);

            reloaded.removeWavetablePreset (0);
            expectEquals ((int) reloaded.getWavetablePatches().size(), 0);

            file.deleteFile();
        }

        beginTest ("a corrupt library is reported but adding to it still works");

        {
            const auto file = makeTempFile();
            file.replaceWithText ("{ this is not json");

            PresetStore store (file);
            expect (! store.load());
            expect (store.getLastError().isNotEmpty());
            expectEquals ((int) store.getFmPresets().size(), 0);
            expectEquals ((int) store.getSamplerPresets().size(), 0);

            SynthParams preset;
            preset.patch.name = NI_TXT ("重来");
            store.addFmPreset (preset);

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getFmPresets().size(), 1);
            expectEquals (reloaded.getFmPresets().front().patch.name, NI_TXT ("重来"));

            file.deleteFile();
        }
    }
};

namespace
{
    PresetStoreTests    presetStoreTests;
    InstrumentPresetTests instrumentPresetTests;
}
