#include "TestSupport.h"

#include "../Audio/HybridFxChain.h"
#include "../Audio/HybridSynth.h"
#include "../Audio/SynthEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "../Core/HybridPresets.h"
#include "../Core/WaveformPresets.h"

#include <map>


class HybridTests : public juce::UnitTest
{
public:
    HybridTests() : juce::UnitTest ("Hybrid", "nidaw") {}

    void runTest() override
    {
        beginTest (NI_TXT ("混合器的三个层各自保留参数并相等比较"));
        {
            HybridParams a;
            a.layers[0].mode = HybridLayerMode::wavetable;
            a.layers[1].mode = HybridLayerMode::sample;
            a.layers[2].enabled = false;
            a.layers[0].level = 0.7f;
            a.layers[1].pan = -0.4f;

            HybridParams b = a;
            expect (a == b);
            b.layers[1].pan = 0.4f;
            expect (a != b);
        }

        beginTest (NI_TXT ("波表相位跨过一周期后仍读同一帧"));
        {
            wavetable::ensureBuilt();
            const auto& table = wavetable::getTable (0);
            const auto a = WavetableSynth::sampleTable (table, 0, 0.25, 0.3);
            const auto b = WavetableSynth::sampleTable (table, 0, 1.25, 0.3);
            const auto c = WavetableSynth::sampleTable (table, 0, 2.0, 0.3);
            const auto d = WavetableSynth::sampleTable (table, 0, 0.0, 0.3);
            expectWithinAbsoluteError (a, b, 1.0e-9);
            expectWithinAbsoluteError (c, d, 1.0e-9);
        }

        beginTest (NI_TXT ("每次 note-on 同时驱动两个选中的层，并按音符计声部"));
        {
            HybridSynth synth;
            synth.prepare (48000.0);

            HybridParams p;
            p.gain = 1.0f;
            p.layers[0].mode = HybridLayerMode::wavetable;
            p.layers[0].level = 0.7f;
            p.layers[0].wavetable.ampAttack = 0.001f;
            p.layers[1].mode = HybridLayerMode::wavetable;
            p.layers[1].level = 0.4f;
            p.layers[1].wavetable.ampAttack = 0.001f;
            p.layers[2].enabled = false;
            p.sub.enabled = true;
            p.sub.level = 0.25f;
            p.noise.enabled = true;
            p.noise.level = 0.15f;
            synth.setParams (p);

            synth.noteOn (1, 440.0f, 100.0f);
            expect (synth.isActive());
            expectEquals (synth.activeVoiceCount(), 1);

            juce::AudioBuffer<float> buffer (2, 1024);
            buffer.clear();
            synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
            expectGreaterThan (buffer.getMagnitude (0, 0, buffer.getNumSamples()), 0.001f);

            synth.noteOff (1);
            synth.allNotesOff (true);
            expect (! synth.isActive());
        }

        beginTest (NI_TXT ("混合器参数随工程往返，调度保留混合轨"));
        {
            Project project;
            const auto id = addTrack (project, TrackKind::hybrid);
            auto* track = project.findTrack (id);
            expect (track != nullptr);

            track->hybrid.layers[0].mode = HybridLayerMode::wavetable;
            track->hybrid.layers[0].pan = 0.0f;
            track->hybrid.layers[1].mode = HybridLayerMode::granular;
            track->hybrid.layers[2].pan = 0.25f;
            for (auto& l : track->hybrid.layers)
                l.wavetable.oscs[1].enabled = false;
            track->hybrid.fxSlots[0].enabled = true;
            track->hybrid.fxSlots[0].type = HybridFxType::chorus;
            track->hybrid.fxSlots[0].mix = 0.35f;
            track->hybrid.fxSlots[0].param1 = 0.2f;
            track->hybrid.fxSlots[0].param2 = 0.8f;
            track->hybrid.fxSlots[2].enabled = true;
            track->hybrid.fxSlots[2].type = HybridFxType::delay;
            track->hybrid.fxSlots[2].bypass = true;
            track->hybrid.fxSlots[2].mix = 0.6f;
            track->hybrid.macroNames[0] = NI_TXT ("亮度");
            track->hybrid.layers[0].wavetable.oscs[0].unisonVoices = 5;
            track->hybrid.layers[0].wavetable.oscs[0].unisonDetuneCents = 33.0f;
            track->hybrid.layers[0].wavetable.oscs[0].positionDiscrete = true;
            track->hybrid.envelopes[0].timeMode = HybridTimeMode::beats;
            track->hybrid.envelopes[0].attackDivision = HybridTimeDivision::d1_16;
            track->hybrid.lfos[5].timeMode = HybridTimeMode::beats;
            track->hybrid.lfos[5].syncDivision = HybridTimeDivision::d1_2;
            track->hybrid.lfos[5].rateMs = 777.0f;
            track->hybrid.lfos[5].sync = true;
            track->hybrid.lfos[5].depth = 0.4f;
            track->hybrid.lfos[5].fadeMs = 250.0f;

            juce::String error;
            Project loaded;
            expect (projectFromJson (projectToJson (project), loaded, error));
            expect (error.isEmpty());
            expectEquals ((int) loaded.tracks.front().kind, (int) TrackKind::hybrid);
            expect (loaded.tracks.front().hybrid == track->hybrid);
            expectEquals (loaded.tracks.front().hybrid.macroNames[0], NI_TXT ("亮度"));
            expectEquals (loaded.tracks.front().hybrid.layers[0].wavetable.oscs[0].unisonVoices,
                          5);
            expectWithinAbsoluteError (
                loaded.tracks.front().hybrid.layers[0].wavetable.oscs[0].unisonDetuneCents,
                33.0f, 1.0e-4f);
            expect (loaded.tracks.front().hybrid.layers[0].wavetable.oscs[0].positionDiscrete);
            expectEquals ((int) loaded.tracks.front().hybrid.envelopes[0].timeMode,
                          (int) HybridTimeMode::beats);
            expectEquals ((int) loaded.tracks.front().hybrid.lfos[5].timeMode,
                          (int) HybridTimeMode::beats);
            expectWithinAbsoluteError (loaded.tracks.front().hybrid.lfos[5].rateMs,
                                       777.0f, 1.0e-4f);
            expectWithinAbsoluteError (loaded.tracks.front().hybrid.lfos[5].depth,
                                       0.4f, 1.0e-4f);
            expectWithinAbsoluteError (loaded.tracks.front().hybrid.lfos[5].fadeMs,
                                       250.0f, 1.0e-4f);
            expectEquals ((int) loaded.tracks.front().hybrid.fxSlots[0].type,
                          (int) HybridFxType::chorus);
            expect (loaded.tracks.front().hybrid.fxSlots[2].bypass);

            const auto schedule = buildSchedule (loaded);
            expectEquals ((int) schedule.tracks.front().kind, (int) TrackKind::hybrid);
            expect (schedule.tracks.front().hybrid == loaded.tracks.front().hybrid);
        }

        beginTest (NI_TXT ("混合音色预设可保存、改开、重命名和删除"));
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("hm-hybrid-presets", ".json");
            PresetStore store (file);
            store.load();

            HybridParams preset;
            preset.name = "Layered";
            preset.layers[0].mode = HybridLayerMode::wavetable;
            preset.layers[1].mode = HybridLayerMode::sample;
            preset.layers[1].pan = -0.25f;
            for (auto& l : preset.layers)
                l.wavetable.oscs[1].enabled = false;
            preset.fxSlots[1].enabled = true;
            preset.fxSlots[1].type = HybridFxType::reverb;
            preset.fxSlots[1].mix = 0.45f;
            store.addHybridPreset (preset);

            expectEquals (store.findHybridPresetByName ("Layered"), 0);
            expect (store.getHybridPresets().front() == preset);

            PresetStore reloaded (file);
            reloaded.load();
            expect (reloaded.getHybridPresets().front() == preset);

            auto changed = preset;
            changed.gain = 0.4f;
            reloaded.replaceHybridPreset (0, changed);
            reloaded.renameHybridPreset (0, "Wide Layers");
            expectEquals (reloaded.findHybridPresetByName ("Wide Layers"), 0);
            reloaded.removeHybridPreset (0);
            expect (reloaded.getHybridPresets().empty());
            file.deleteFile();
        }

        beginTest (NI_TXT ("混合器 FX：九种类型都真的改变声音"));
        {
            auto render = [] (HybridFxType type, float frequency, bool bypass, float mix,
                              juce::AudioBuffer<float>& output)
            {
                HybridFxChain chain;
                chain.prepare (48000.0, 4096);
                std::array<HybridFxSlot, 4> slots {};
                slots[0].enabled = true;
                slots[0].type = type;
                slots[0].bypass = bypass;
                slots[0].mix = mix;
                slots[0].param1 = type == HybridFxType::compressor ? 0.2f : 0.65f;
                slots[0].param2 = 0.55f;
                chain.setParams (slots);

                output.setSize (2, 512);
                juce::AudioBuffer<float> dry (2, 512);

                for (int block = 0; block < 8; ++block)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* data = output.getWritePointer (ch);

                        for (int i = 0; i < 512; ++i)
                        {
                            const auto n = block * 512 + i;
                            data[i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi
                                                       * frequency * (float) n / 48000.0f);
                        }
                    }

                    if (block == 0)
                        dry.makeCopyOf (output);

                    chain.process (output, 512);
                }

                return output;
            };

            const HybridFxType types[] = {
                HybridFxType::distortion, HybridFxType::bitcrush, HybridFxType::chorus,
                HybridFxType::phaser, HybridFxType::delay, HybridFxType::reverb,
                HybridFxType::eq, HybridFxType::compressor
            };

            for (auto type : types)
            {
                juce::AudioBuffer<float> wet;
                const auto frequency = type == HybridFxType::eq ? 1100.0f : 440.0f;
                render (type, frequency, false, 0.75f, wet);

                juce::AudioBuffer<float> dry (2, 512);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        dry.setSample (ch, i, 0.2f * std::sin (
                            juce::MathConstants<float>::twoPi * frequency
                            * (float) (7 * 512 + i) / 48000.0f));

                double difference = 0.0;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto delta = (double) wet.getSample (ch, i)
                                           - (double) dry.getSample (ch, i);
                        difference += delta * delta;
                    }

                expectGreaterThan (difference, 1.0e-5,
                                   hybridFxTypeName (type) + NI_TXT (" 没有改变声音"));
            }
        }

        beginTest (NI_TXT ("混合器 FX：旁通与干湿为零时逐样本等于干声"));
        {
            juce::AudioBuffer<float> reference (2, 512);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    reference.setSample (ch, i,
                                         0.15f * std::sin (juce::MathConstants<float>::twoPi
                                                           * 330.0f * (float) i / 48000.0f));

            auto renderExact = [] (bool bypass, float mix)
            {
                HybridFxChain chain;
                chain.prepare (48000.0, 4096);
                std::array<HybridFxSlot, 4> slots {};
                slots[0].enabled = true;
                slots[0].type = HybridFxType::distortion;
                slots[0].bypass = bypass;
                slots[0].mix = mix;
                slots[0].param1 = 0.9f;
                chain.setParams (slots);
                juce::AudioBuffer<float> buffer (2, 512);
                buffer.clear();

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i,
                                          0.15f * std::sin (juce::MathConstants<float>::twoPi
                                                            * 330.0f * (float) i / 48000.0f));

                chain.process (buffer, 512);
                return buffer;
            };

            for (auto buffer : { renderExact (true, 1.0f), renderExact (false, 0.0f) })
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        expectWithinAbsoluteError (buffer.getSample (ch, i),
                                                   reference.getSample (ch, i), 1.0e-8f);
        }

        beginTest (NI_TXT ("混合器 FX：延迟尾巴让轨道保持活跃，放完后自动停"));
        {
            HybridFxChain chain;
            chain.prepare (48000.0, 512);
            std::array<HybridFxSlot, 4> slots {};
            slots[0].enabled = true;
            slots[0].type = HybridFxType::delay;
            slots[0].mix = 0.8f;
            slots[0].param1 = 0.02f;
            slots[0].param2 = 0.15f;
            chain.setParams (slots);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();

            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, 0, 0.5f);

            chain.process (buffer, 512);
            expect (chain.isTailActive());

            auto peak = buffer.getMagnitude (0, 0, 512);

            for (int block = 0; block < 8; ++block)
            {
                buffer.clear();
                chain.process (buffer, 512);
                peak = juce::jmax (peak, buffer.getMagnitude (0, 0, 512));
            }

            expectGreaterThan (peak, 1.0e-6f);

            for (int block = 0; block < 220; ++block)
            {
                buffer.clear();
                chain.process (buffer, 512);
            }

            expect (! chain.isTailActive());
        }

        beginTest (NI_TXT ("混合器 FX：参数名随类型变化"));
        {
            expectEquals (hybridFxTypeName (HybridFxType::reverb), NI_TXT ("混响"));
            expectEquals (hybridFxParam1Name (HybridFxType::delay), NI_TXT ("时间"));
            expectEquals (hybridFxParam2Name (HybridFxType::compressor), NI_TXT ("压缩比"));
            expectEquals (hybridFxTypeCount(), 9);
        }

        beginTest (NI_TXT ("混合器 FX：调制矩阵可以推高干湿"));
        {
            auto render = [] (bool withModulation)
            {
                HybridSynth synth;
                synth.prepare (48000.0, 16384);

                HybridParams p;
                p.gain = 1.0f;
                p.layers[0].mode = HybridLayerMode::wavetable;
                p.layers[0].level = 0.7f;
                p.layers[0].wavetable.ampAttack = 0.001f;
                p.layers[1].enabled = false;
                p.layers[2].enabled = false;
                p.sub.enabled = false;
                p.noise.enabled = false;
                p.fxSlots[0].enabled = true;
                p.fxSlots[0].type = HybridFxType::distortion;
                p.fxSlots[0].mix = 0.0f;
                p.fxSlots[0].param1 = 0.9f;

                if (withModulation)
                {
                    p.modSlots[0].enabled = true;
                    p.modSlots[0].source = HybridModSource::velocity;
                    p.modSlots[0].destination = HybridModDestination::fx1Mix;
                    p.modSlots[0].amount = 1.0f;
                }

                synth.setParams (p);
                synth.noteOn (1, 220.0f, 127.0f);
                juce::AudioBuffer<float> buffer (2, 2048);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                synth.allNotesOff (true);
                return buffer;
            };

            const auto dry = render (false);
            const auto wet = render (true);
            double difference = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 2048; ++i)
                {
                    const auto delta = (double) wet.getSample (ch, i)
                                       - (double) dry.getSample (ch, i);
                    difference += delta * delta;
                }

            expectGreaterThan (difference, 1.0e-4);
        }

        beginTest (NI_TXT ("混合器：两个音各自拥有独立的双滤波状态"));
        {
            auto render = [] (bool playA, bool playB)
            {
                HybridSynth synth;
                synth.prepare (48000.0, 4096);

                HybridParams p;
                p.gain = 1.0f;
                p.layers[0].mode = HybridLayerMode::wavetable;
                p.layers[0].level = 0.5f;
                p.layers[0].wavetable.filterCutoffHz = 20000.0f;
                p.layers[0].wavetable.filterResonance = 0.707f;
                p.layers[0].wavetable.ampAttack = 0.001f;
                p.layers[0].wavetable.ampSustain = 1.0f;
                p.layers[1].enabled = false;
                p.layers[2].enabled = false;
                p.sub.enabled = false;
                p.noise.enabled = false;
                p.filters[0].mode = FilterMode::lowPass;
                p.filters[0].slopeDb = 24;
                p.filters[0].cutoffHz = 600.0f;
                p.filters[0].resonance = 3.0f;
                p.filters[0].envAmount = 0.0f;
                p.filterRouting = HybridFilterRouting::filter1Only;
                p.envelopes[0].attack = 0.001f;
                p.envelopes[0].sustain = 1.0f;
                synth.setParams (p);

                if (playA)
                    synth.noteOn (1, 220.0f, 100.0f);
                if (playB)
                    synth.noteOn (2, 880.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 4096);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                synth.allNotesOff (true);
                return buffer;
            };

            const auto a = render (true, false);
            const auto b = render (false, true);
            const auto ab = render (true, true);
            float largestError = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4096; ++i)
                {
                    const auto expected = a.getSample (ch, i) + b.getSample (ch, i);
                    largestError = juce::jmax (largestError,
                                               std::abs (ab.getSample (ch, i) - expected));
                }

            expectLessThan (largestError, 1.0e-4f);
        }

        beginTest (NI_TXT ("混合器滤波器：陷波、梳状与滤波包络量都进入声音"));
        {
            auto render = [] (FilterMode mode, float frequency, float envAmount,
                              float position, float resonance, bool subOnly)
            {
                HybridSynth synth;
                synth.prepare (48000.0, 16384);

                HybridParams p;
                p.gain = 1.0f;
                p.layers[0].mode = HybridLayerMode::wavetable;
                p.layers[0].enabled = ! subOnly;
                p.layers[0].level = 0.6f;
                p.layers[0].wavetable.filterCutoffHz = 20000.0f;
                p.layers[0].wavetable.filterResonance = 0.707f;
                p.layers[0].wavetable.ampAttack = 0.001f;
                p.layers[0].wavetable.ampSustain = 1.0f;
                p.layers[0].wavetable.oscs[0].position = position;
                p.layers[0].wavetable.posEnvAmount = 0.0f;
                p.layers[1].enabled = false;
                p.layers[2].enabled = false;
                p.sub.enabled = subOnly;
                p.sub.waveform = 0;
                p.sub.octave = -1;
                p.sub.level = 0.7f;
                p.noise.enabled = false;
                p.filters[0].mode = mode;
                p.filters[0].slopeDb = 24;
                p.filters[0].cutoffHz = 1000.0f;
                p.filters[0].resonance = resonance;
                p.filters[0].envAmount = envAmount;
                p.filterRouting = HybridFilterRouting::filter1Only;
                p.envelopes[0].attack = 0.001f;
                p.envelopes[0].sustain = 1.0f;
                p.envelopes[1].attack = 0.001f;
                p.envelopes[1].decay = 0.01f;
                p.envelopes[1].sustain = 1.0f;
                synth.setParams (p);

                synth.noteOn (1, frequency, 100.0f);
                juce::AudioBuffer<float> buffer (2, 16384);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                synth.allNotesOff (true);
                return buffer;
            };

            auto rms = [] (const juce::AudioBuffer<float>& buffer)
            {
                double sum = 0.0;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 12000; i < buffer.getNumSamples(); ++i)
                    {
                        const auto v = (double) buffer.getSample (ch, i);
                        sum += v * v;
                    }

                return std::sqrt (sum / (2.0 * 4384.0));
            };

            const auto notchAtCutoff = render (FilterMode::notch, 2000.0f, 0.0f, 0.0f, 4.0f, true);
            const auto notchBelow = render (FilterMode::notch, 500.0f, 0.0f, 0.0f, 4.0f, true);
            expectGreaterThan (rms (notchBelow), rms (notchAtCutoff) * 1.05,
                               NI_TXT ("陷波应当压掉截止频率上的基音"));

            const auto opened = render (FilterMode::lowPass, 220.0f, 1.0f, 1.0f, 0.707f, false);
            const auto closed = render (FilterMode::lowPass, 220.0f, 0.0f, 1.0f, 0.707f, false);
            double envelopeDifference = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 12000; i < 16384; ++i)
                {
                    const auto delta = (double) opened.getSample (ch, i)
                                       - (double) closed.getSample (ch, i);
                    envelopeDifference += delta * delta;
                }

            expectGreaterThan (envelopeDifference, 1.0e-3,
                               NI_TXT ("滤波包络量应当改变滤波结果"));

            const auto comb = render (FilterMode::comb, 220.0f, 0.0f, 1.0f, 4.0f, false);
            expectGreaterThan (rms (comb), 1.0e-5, NI_TXT ("梳状滤波应当有输出"));
        }

        beginTest (NI_TXT ("混合器最多 16 个音，超出时偷最老的音"));
        {
            HybridSynth synth;
            synth.prepare (48000.0);

            HybridParams p;
            p.layers[0].mode = HybridLayerMode::wavetable;
            p.layers[0].level = 0.3f;
            p.layers[1].enabled = false;
            p.layers[2].enabled = false;
            p.sub.enabled = false;
            p.noise.enabled = false;
            synth.setParams (p);

            for (int note = 0; note < 17; ++note)
                synth.noteOn (1000 + note, 220.0f + (float) note * 10.0f, 100.0f);

            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
            synth.noteOff (1000);
            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
            synth.noteOff (1001);
            juce::AudioBuffer<float> buffer (2, 2048);

            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
            }

            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack - 1);
            synth.allNotesOff (true);
            expect (! synth.isActive());
        }

        beginTest (NI_TXT ("工程文件夹保存会把音频源收进 samples"));
        {
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("hm-project-folder", "");
            const auto sourceFile = testsupport::writeTempWav ("hm-folder-source", 0.1);
            ProjectDocument document;
            document.newProject();
            juce::int64 trackId = 0;
            document.applyEdit (NI_TXT ("准备工程"), [&] (Project& project)
            {
                trackId = addTrack (project, TrackKind::sampler);
                auto* track = project.findTrack (trackId);
                track->sampler.filePath = sourceFile.getFullPathName();
                track->sampler.fileName = sourceFile.getFileName();
            });

            juce::String error;
            expect (document.saveToProjectFolder (root.getChildFile ("My.nprj"), error),
                    error);
            const auto projectFile = root.getChildFile ("My").getChildFile ("project.nprj");
            expect (projectFile.existsAsFile());
            expect (root.getChildFile ("My").getChildFile ("samples")
                        .getChildFile (sourceFile.getFileName()).existsAsFile());
            expect (document.getProject().findTrack (trackId)->sampler.filePath
                        .contains ("samples"));
            expectEquals (document.getProjectName(), juce::String ("My"));
            root.deleteRecursively();
        }

        beginTest (NI_TXT ("波表齐奏与离散位置真的进入声音"));
        {
            auto render = [] (int unison, float detune, bool discrete)
            {
                WavetableSynth synth;
                synth.prepare (48000.0);
                WavetableParams p;
                p.oscs[0].unisonVoices = unison;
                p.oscs[0].unisonDetuneCents = detune;
                p.oscs[0].position = 0.3f;
                p.oscs[0].positionDiscrete = discrete;
                p.ampAttack = 0.001f;
                p.ampSustain = 1.0f;
                p.filterCutoffHz = 20000.0f;
                synth.setParams (p);
                synth.noteOn (1, 440.0f, 100.0f);
                juce::AudioBuffer<float> buffer (2, 4096);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                synth.allNotesOff (true);
                return buffer;
            };

            const auto single = render (1, 0.0f, false);
            const auto four = render (4, 50.0f, false);
            double unisonDifference = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4096; ++i)
                {
                    const auto delta = (double) single.getSample (ch, i)
                                       - (double) four.getSample (ch, i);
                    unisonDifference += delta * delta;
                }

            expectGreaterThan (unisonDifference, 1.0e-4,
                               NI_TXT ("齐奏应当改变声音"));

            wavetable::ensureBuilt();
            const auto& table = wavetable::getTable (0);
            const auto smooth = WavetableSynth::sampleTable (table, 0, 0.1235, 2.0 / 7.0, false);
            const auto discrete = WavetableSynth::sampleTable (table, 0, 0.1235, 0.3, true);
            expectWithinAbsoluteError (smooth, discrete, 1.0e-6);
        }

        beginTest (NI_TXT ("合唱与相位器的左右声道不再相同"));
        {
            for (auto type : { HybridFxType::chorus, HybridFxType::phaser })
            {
                HybridFxChain chain;
                chain.prepare (48000.0, 4096);
                std::array<HybridFxSlot, 4> slots {};
                slots[0].enabled = true;
                slots[0].type = type;
                slots[0].mix = 0.5f;
                slots[0].param1 = 0.3f;
                slots[0].param2 = 0.8f;
                chain.setParams (slots);
                juce::AudioBuffer<float> buffer (2, 4096);

                for (int i = 0; i < 4096; ++i)
                {
                    const auto v = 0.2f * std::sin (juce::MathConstants<float>::twoPi
                                                    * 440.0f * (float) i / 48000.0f);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                chain.process (buffer, 4096);
                double difference = 0.0;

                for (int i = 0; i < 4096; ++i)
                {
                    const auto delta = (double) buffer.getSample (0, i)
                                       - (double) buffer.getSample (1, i);
                    difference += delta * delta;
                }

                expectGreaterThan (difference, 1.0e-5,
                                   hybridFxTypeName (type) + NI_TXT (" 应当有立体声差异"));
            }
        }

        beginTest (NI_TXT ("调制源与目标名称没有乱码"));
        {
            for (int i = 0; i < hybridModSourceCount(); ++i)
            {
                const auto name = hybridModSourceName ((HybridModSource) i);
                expect (name.isNotEmpty());
                expect (! name.containsChar ((juce::juce_wchar) 0xfffd));
            }

            for (int i = 0; i < hybridModDestinationCount(); ++i)
            {
                const auto name = hybridModDestinationName ((HybridModDestination) i);
                expect (name.isNotEmpty());
                expect (! name.containsChar ((juce::juce_wchar) 0xfffd));
            }
        }

        beginTest (NI_TXT ("比特压碎的降采样相位跨块连续"));
        {
            HybridFxChain chain;
            chain.prepare (48000.0, 8);
            std::array<HybridFxSlot, 4> slots {};
            slots[0].enabled = true;
            slots[0].type = HybridFxType::bitcrush;
            slots[0].mix = 1.0f;
            slots[0].param1 = 0.8f;
            slots[0].param2 = 3.0f / 63.0f;   // downsample = 4
            chain.setParams (slots);

            juce::AudioBuffer<float> first (2, 3);
            first.clear();

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 3; ++i)
                    first.setSample (ch, i, 0.25f);

            chain.process (first, 3);

            juce::AudioBuffer<float> second (2, 3);
            second.clear();

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 3; ++i)
                    second.setSample (ch, i, 0.9f);

            chain.process (second, 3);
            expectWithinAbsoluteError (second.getSample (0, 0), 0.25f, 0.01f);
            expectWithinAbsoluteError (second.getSample (0, 1), 0.9f, 0.01f);
        }

        beginTest (NI_TXT ("BPM 模式按拍分数改变 LFO 周期"));
        {
            auto phaseAfter = [] (double bpm)
            {
                HybridSynth synth;
                synth.prepare (48000.0, 16384);
                HybridParams p;
                p.layers[0].mode = HybridLayerMode::wavetable;
                p.layers[1].enabled = false;
                p.layers[2].enabled = false;
                p.sub.enabled = false;
                p.noise.enabled = false;
                p.lfos[0].timeMode = HybridTimeMode::beats;
                p.lfos[0].syncDivision = HybridTimeDivision::d1;
                synth.setTempo (bpm);
                synth.setParams (p);
                synth.noteOn (1, 220.0f, 100.0f);
                juce::AudioBuffer<float> buffer (2, 12000);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                const auto phase = synth.getLfoPhase (0);
                synth.allNotesOff (true);
                return phase;
            };

            expectWithinAbsoluteError (phaseAfter (120.0), 0.5f, 0.03f);
            expectWithinAbsoluteError (phaseAfter (60.0), 0.25f, 0.03f);
        }

        beginTest (NI_TXT ("包络与 LFO 把实时位置上报给界面"));
        {
            HybridSynth synth;
            synth.prepare (48000.0, 16384);
            HybridParams p;
            p.layers[0].mode = HybridLayerMode::wavetable;
            p.layers[1].enabled = false;
            p.layers[2].enabled = false;
            p.sub.enabled = false;
            p.noise.enabled = false;
            p.envelopes[0].attack = 0.2f;
            p.envelopes[0].sustain = 1.0f;
            p.lfos[5].rateMs = 100.0f;
            synth.setParams (p);
            synth.noteOn (1, 220.0f, 100.0f);
            juce::AudioBuffer<float> buffer (2, 4096);
            buffer.clear();
            synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
            expectGreaterThan (synth.getEnvelopePosition (0), 0.0f);
            expectGreaterThan (synth.getEnvelopeValue (0), 0.0f);
            expectGreaterThan (synth.getLfoPhase (5), 0.0f);
            synth.allNotesOff (true);
        }

        beginTest (NI_TXT ("LFO 深度与淡入真的缩放输出"));
        {
            auto valueAfter = [] (float depth, float fadeMs, int samples)
            {
                HybridSynth synth;
                synth.prepare (48000.0, 16384);
                HybridParams p;
                p.layers[0].mode = HybridLayerMode::wavetable;
                p.layers[1].enabled = false;
                p.layers[2].enabled = false;
                p.sub.enabled = false;
                p.noise.enabled = false;
                p.lfos[0].waveform = 2;
                p.lfos[0].rateMs = 1000.0f;
                p.lfos[0].depth = depth;
                p.lfos[0].fadeMs = fadeMs;
                synth.setParams (p);
                synth.noteOn (1, 220.0f, 100.0f);
                juce::AudioBuffer<float> buffer (2, samples);
                buffer.clear();
                synth.renderAdding (buffer, 0, samples, 1.0f, 1.0f);
                const auto value = synth.getLfoValue (0);
                synth.allNotesOff (true);
                return value;
            };

            expectLessThan (std::abs (valueAfter (0.0f, 0.0f, 2048)), 1.0e-5f);
            expectGreaterThan (std::abs (valueAfter (1.0f, 0.0f, 2048)), 0.5f);
            const auto faded = std::abs (valueAfter (1.0f, 1000.0f, 12000));
            expectWithinAbsoluteError (faded, 0.25f, 0.08f);
        }

        beginTest ("factory presets are a catalogue of eight categories");
        {
            expectEquals (hybridpresets::factoryPresetCount(), 48);

            juce::StringArray categories;
            std::map<juce::String, int> perCategory;
            juce::StringArray names;

            for (int i = 0; i < hybridpresets::factoryPresetCount(); ++i)
            {
                const auto category = hybridpresets::category (i);
                const auto name = hybridpresets::name (i);

                expect (category.isNotEmpty());
                expect (name.isNotEmpty());
                expect (! names.contains (name));
                names.add (name);

                if (! categories.contains (category))
                    categories.add (category);

                ++perCategory[category];
            }

            expectEquals (categories.size(), 8);

            for (const auto& entry : perCategory)
                expectEquals (entry.second, 6);
        }

        beginTest ("factory presets use the whole hybrid engine");
        {
            int withFx = 0;
            int withLfoMod = 0;
            int withSubOrNoise = 0;
            int withSecondOsc = 0;
            int withModMatrix = 0;

            for (int i = 0; i < hybridpresets::factoryPresetCount(); ++i)
            {
                HybridParams p;
                hybridpresets::apply (p, i);

                expectEquals (p.name, hybridpresets::name (i));

                int enabledSources = 0;

                for (const auto& layer : p.layers)
                    if (layer.enabled)
                        ++enabledSources;

                if (p.sub.enabled)
                    ++enabledSources;

                if (p.noise.enabled)
                    ++enabledSources;

                expectGreaterThan (enabledSources, 0);

                bool fxOn = false;
                bool lfoModOn = false;
                bool secondOscOn = false;
                bool modMatrixOn = false;

                for (const auto& slot : p.fxSlots)
                    if (slot.enabled)
                        fxOn = true;

                for (const auto& slot : p.modSlots)
                    if (slot.enabled && slot.source != HybridModSource::none)
                    {
                        modMatrixOn = true;

                        if ((int) slot.source >= (int) HybridModSource::lfo1
                            && (int) slot.source <= (int) HybridModSource::lfo6)
                            lfoModOn = true;
                    }

                for (const auto& layer : p.layers)
                    if (layer.enabled && layer.wavetable.oscs[1].enabled)
                        secondOscOn = true;

                if (fxOn)
                    ++withFx;

                if (lfoModOn)
                    ++withLfoMod;

                if (p.sub.enabled || p.noise.enabled)
                    ++withSubOrNoise;

                if (secondOscOn)
                    ++withSecondOsc;

                if (modMatrixOn)
                    ++withModMatrix;
            }

            expect (withFx >= 40, "factory presets with FX: " + juce::String (withFx));
            expect (withLfoMod >= 16, "factory presets with LFO modulation: "
                                         + juce::String (withLfoMod));
            expect (withSubOrNoise >= 8, "factory presets with Sub/Noise: "
                                            + juce::String (withSubOrNoise));
            expect (withSecondOsc >= 12, "factory presets with a second oscillator: "
                                            + juce::String (withSecondOsc));
            expect (withModMatrix >= 10, "factory presets using the mod matrix: "
                                            + juce::String (withModMatrix));
        }

        beginTest ("every factory preset makes sound");
        {
            for (int i = 0; i < hybridpresets::factoryPresetCount(); ++i)
            {
                HybridParams p;
                hybridpresets::apply (p, i);

                HybridSynth synth;
                synth.prepare (48000.0, 16384);
                synth.setParams (p);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 16384);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);

                double energy = 0.0;
                float peak = 0.0f;
                bool finite = true;

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    for (int s = 0; s < buffer.getNumSamples(); ++s)
                    {
                        const auto value = (double) buffer.getSample (channel, s);
                        energy += value * value;
                        peak = juce::jmax (peak, (float) std::abs (value));
                        finite = finite && std::isfinite ((float) value);
                    }

                expect (energy > 1.0e-4,
                        "factory preset " + hybridpresets::name (i)
                            + " produced no audio (energy " + juce::String (energy, 6) + ")");
                expect (finite, "factory preset " + hybridpresets::name (i)
                                    + " produced a non-finite sample");
                expect (peak < 4.0f, "factory preset " + hybridpresets::name (i)
                                         + " peaks at " + juce::String (peak, 3));
                synth.allNotesOff (true);
            }
        }

        beginTest ("waveform presets build one-cycle and multi-frame tables");
        {
            expectEquals (waveformpresets::count(), 16);

            int single = 0;
            int multi = 0;
            juce::StringArray names;

            for (int i = 0; i < waveformpresets::count(); ++i)
            {
                const auto name = waveformpresets::name (i);
                expect (name.isNotEmpty());
                expect (! names.contains (name));
                names.add (name);

                if (waveformpresets::isMultiFrame (i))
                    ++multi;
                else
                    ++single;

                const auto table = waveformpresets::build (i, 1000 + i);
                const auto expectedFrames = waveformpresets::isMultiFrame (i) ? 8 : 1;

                expectEquals (table.frameCount, expectedFrames);
                expectEquals ((int) table.samples.size(),
                              expectedFrames * kCustomWavetableFrameSize);

                float peak = 0.0f;

                for (const auto value : table.samples)
                    peak = juce::jmax (peak, std::abs ((float) value) / 32767.0f);

                expect (peak > 0.8f, "waveform " + name + " peak "
                                         + juce::String (peak, 3));
                expect (peak <= 1.001f, "waveform " + name + " peak too high "
                                            + juce::String (peak, 3));

                if (expectedFrames > 1)
                {
                    const auto firstFrame = (size_t) 0;
                    const auto lastFrame = (size_t) (expectedFrames - 1)
                                               * (size_t) kCustomWavetableFrameSize;
                    int differing = 0;

                    for (int s = 0; s < kCustomWavetableFrameSize; ++s)
                        if (table.samples[firstFrame + (size_t) s]
                            != table.samples[lastFrame + (size_t) s])
                            ++differing;

                    expect (differing > kCustomWavetableFrameSize / 10,
                            "waveform " + name + " morphs between frames ("
                                + juce::String (differing) + " samples differ)");
                }
            }

            expectEquals (single, 10);
            expectEquals (multi, 6);
        }

        beginTest ("24 dB band-pass is a finite two-stage cascade");
        {
            auto energyFor = [] (int slope)
            {
                HybridParams p;
                hybridpresets::apply (p, 14);
                p.filters[0].mode = FilterMode::bandPass;
                p.filters[0].slopeDb = slope;

                HybridSynth synth;
                synth.prepare (48000.0, 16384);
                synth.setParams (p);
                synth.noteOn (1, 220.0f, 100.0f);
                juce::AudioBuffer<float> buffer (2, 16384);
                buffer.clear();
                synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
                double energy = 0.0;

                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < buffer.getNumSamples(); ++s)
                    {
                        const auto value = (double) buffer.getSample (ch, s);
                        energy += value * value;
                    }

                synth.allNotesOff (true);
                return energy;
            };

            const auto twelve = energyFor (12);
            const auto twentyFour = energyFor (24);

            expect (std::isfinite (twelve) && twelve > 1.0e-4,
                    "12 dB band-pass energy " + juce::String (twelve, 6));
            expect (std::isfinite (twentyFour) && twentyFour > 1.0e-4,
                    "24 dB band-pass energy " + juce::String (twentyFour, 6));
        }

        beginTest ("each hybrid layer publishes its own wavetable position");
        {
            HybridSynth synth;
            synth.prepare (48000.0, 16384);

            HybridParams p;
            p.layers[0].mode = HybridLayerMode::wavetable;
            p.layers[1].mode = HybridLayerMode::wavetable;
            p.layers[2].enabled = false;

            for (int i = 0; i < 2; ++i)
            {
                p.layers[(size_t) i].wavetable.oscs[0].position = i == 0 ? 0.2f : 0.8f;
                p.layers[(size_t) i].wavetable.posEnvAmount = 0.0f;
            }

            synth.setParams (p);
            synth.noteOn (1, 220.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, 4096);
            buffer.clear();
            synth.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);

            expectWithinAbsoluteError (synth.getWavetablePosition (0), 0.2f, 0.01f);
            expectWithinAbsoluteError (synth.getWavetablePosition (1), 0.8f, 0.01f);
            synth.allNotesOff (true);
        }

    }
};

static HybridTests hybridTests;
