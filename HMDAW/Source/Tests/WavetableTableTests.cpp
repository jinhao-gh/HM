#include "TestSupport.h"

#include "Audio/WavetableSynth.h"
#include "Core/WavetableTables.h"

class WavetableTableTests : public juce::UnitTest
{
public:
    WavetableTableTests() : juce::UnitTest ("WavetableTables", "hmdaw") {}

    static std::vector<float> makeSine (double hz, double sampleRate, double seconds)
    {
        const auto count = (int) (sampleRate * seconds);
        std::vector<float> samples ((size_t) juce::jmax (0, count), 0.0f);

        for (int i = 0; i < count; ++i)
            samples[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz
                                                    * (double) i / sampleRate);

        return samples;
    }

    /** Zero crossings of one frame, which is how a "does it still hold this
        frequency" check is done without a second FFT. */
    static int zeroCrossings (const CustomWavetable& table, int frame)
    {
        auto previous = wavetabletables::sampleAt (table, frame, kCustomWavetableFrameSize - 1);
        auto count = 0;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
        {
            const auto value = wavetabletables::sampleAt (table, frame, i);

            if ((previous < 0.0f && value >= 0.0f) || (previous > 0.0f && value <= 0.0f))
                ++count;

            previous = value;
        }

        return count;
    }

    static float frameMean (const CustomWavetable& table, int frame)
    {
        auto sum = 0.0f;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            sum += wavetabletables::sampleAt (table, frame, i);

        return sum / (float) kCustomWavetableFrameSize;
    }

    static float framePeak (const CustomWavetable& table, int frame)
    {
        auto peak = 0.0f;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            peak = juce::jmax (peak, std::abs (wavetabletables::sampleAt (table, frame, i)));

        return peak;
    }

    /** The magnitude of harmonic `n` in one frame, 0..~1. */
    static float harmonicLevel (const CustomWavetable& table, int frame, int n)
    {
        juce::dsp::FFT fft (11);
        std::vector<float> spectrum ((size_t) kCustomWavetableFrameSize * 2, 0.0f);

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            spectrum[(size_t) i] = wavetabletables::sampleAt (table, frame, i);

        fft.performRealOnlyForwardTransform (spectrum.data());

        const auto real = spectrum[(size_t) (2 * n)];
        const auto imaginary = spectrum[(size_t) (2 * n + 1)];

        return std::sqrt (real * real + imaginary * imaginary) * 2.0f / (float) kCustomWavetableFrameSize;
    }

    void runTest() override
    {
        beginTest (NI_TXT ("从音频切帧：等分、去直流、整表归一化"));

        {
            // Two seconds of 440 Hz at 48 kHz: eight frames of a quarter second
            // each, i.e. 110 cycles per frame.
            const auto audio = makeSine (440.0, 48000.0, 2.0);
            const auto table = wavetabletables::makeFromAudio (audio.data(),
                                                              (juce::int64) audio.size(),
                                                              8, 7, NI_TXT ("正弦"));

            expectEquals (table.id, (juce::int64) 7);
            expectEquals (table.name, NI_TXT ("正弦"));
            expectEquals (table.frameCount, 8);
            expectEquals ((int) table.samples.size(), 8 * kCustomWavetableFrameSize);

            auto meansFlat = true;
            auto peaksRight = true;
            auto cyclesRight = true;

            for (int frame = 0; frame < 8; ++frame)
            {
                if (std::abs (frameMean (table, frame)) > 0.01f)
                    meansFlat = false;

                if (std::abs (framePeak (table, frame) - 0.98f) > 0.03f)
                    peaksRight = false;

                // 110 cycles -> about 220 crossings; a box filter of five samples
                // per output point keeps the crossing count.
                const auto crossings = zeroCrossings (table, frame);

                if (crossings < 214 || crossings > 226)
                    cyclesRight = false;
            }

            expect (meansFlat, NI_TXT ("每一帧的直流都被去掉了"));
            expect (peaksRight, NI_TXT ("整张表归一化到 0.98（每一帧都测到峰值）"));
            expect (cyclesRight, NI_TXT ("切出来的每一帧仍然是那 110 个周期的波"));

            // A single-cycle file (2048 samples) becomes a one-frame table.
            const auto single = makeSine (440.0, 48000.0, (double) kCustomWavetableFrameSize / 48000.0);
            const auto oneFrame = wavetabletables::makeFromAudio (single.data(),
                                                                 (juce::int64) single.size(),
                                                                 8, 1, NI_TXT ("单周期"));

            expectEquals (oneFrame.frameCount, 8);
            expect (wavetabletables::storedBytes (oneFrame) > 0);

            // Not an exact multiple: still no crash and still 8 frames.
            auto odd = audio;
            odd.resize (odd.size() - 137);
            const auto oddTable = wavetabletables::makeFromAudio (odd.data(), (juce::int64) odd.size(),
                                                                 8, 1, NI_TXT ("零头"));
            expectEquals (oddTable.frameCount, 8);

            // Empty source: a legal, silent table.
            const auto empty = wavetabletables::makeFromAudio (nullptr, 0, 4, 1, NI_TXT ("空"));
            expectEquals (empty.frameCount, 4);
            expectEquals (framePeak (empty, 0), 0.0f);
        }

        beginTest (NI_TXT ("手绘与谐波：只给第 3 次谐波，分析回来也只有它"));

        {
            auto table = wavetabletables::makeEmpty (1, NI_TXT ("手绘"), 1);
            std::vector<float> amounts ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
            amounts[2] = 1.0f;   // the third harmonic
            wavetabletables::synthesiseFromHarmonics (table, 0, amounts);

            expectWithinAbsoluteError (harmonicLevel (table, 0, 3), 1.0f, 0.02f);

            auto othersQuiet = true;

            for (const auto n : { 1, 2, 4, 5, 7, 9, 16 })
                if (harmonicLevel (table, 0, n) > 1.0e-3f)
                    othersQuiet = false;

            expect (othersQuiet, NI_TXT ("其它谐波都是 0"));

            // Analysing it back gives the same single bar.
            const auto analysed = wavetabletables::analyseHarmonics (table, 0,
                                                                    wavetabletables::kMaxHarmonicBars);
            expectWithinAbsoluteError (analysed[2], 1.0f, 0.02f);
            expectLessThan (analysed[0], 1.0e-3f);
            expectLessThan (analysed[5], 1.0e-3f);

            // A drawn value is clamped, and the sample helpers round trip.
            wavetabletables::setSampleAt (table, 0, 5, 2.0f);
            expectWithinAbsoluteError (wavetabletables::sampleAt (table, 0, 5), 1.0f, 1.0e-4f);
            wavetabletables::setSampleAt (table, 0, 5, -3.0f);
            expectWithinAbsoluteError (wavetabletables::sampleAt (table, 0, 5), -1.0f, 1.0e-4f);
        }

        beginTest (NI_TXT ("泛音相位可分析并重新合成，帧数支持 64"));

        {
            auto table = wavetabletables::makeEmpty (1, "phase", 64);
            expectEquals (table.frameCount, 64);

            std::vector<float> amplitudes ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
            std::vector<float> phases ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
            amplitudes[2] = 0.6f;
            phases[2] = juce::MathConstants<float>::halfPi;
            wavetabletables::synthesiseFromHarmonics (table, 0, amplitudes, phases);

            const auto analysed = wavetabletables::analyseHarmonicsWithPhase (
                table, 0, wavetabletables::kMaxHarmonicBars);
            expectWithinAbsoluteError (analysed.amplitudes[2], 0.6f, 0.01f);
            expectWithinAbsoluteError (analysed.phases[2],
                                       juce::MathConstants<float>::halfPi, 0.02f);
        }

        beginTest (NI_TXT ("平滑 / 清空 / 归一化"));

        {
            auto table = wavetabletables::makeEmpty (1, NI_TXT ("工具"), 1);

            for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                wavetabletables::setSampleAt (table, 0, i, (i % 2 == 0) ? 1.0f : -1.0f);

            const auto before = std::abs (wavetabletables::sampleAt (table, 0, 0));
            wavetabletables::smoothFrame (table, 0);
            expectLessThan (std::abs (wavetabletables::sampleAt (table, 0, 0)), before);

            wavetabletables::clearFrame (table, 0);
            expectEquals (framePeak (table, 0), 0.0f);

            wavetabletables::setSampleAt (table, 0, 3, 0.25f);
            wavetabletables::normalise (table);
            expectWithinAbsoluteError (framePeak (table, 0), 0.98f, 0.01f);
        }

        beginTest (NI_TXT ("拷贝内置表，逐点一致"));

        {
            const auto copy = wavetabletables::copyBuiltIn (3, 8, 1, NI_TXT ("拷贝"));
            const auto& source = wavetable::getTable (3);

            expectEquals (copy.frameCount, juce::jmin (8, source.frameCount));

            auto identical = true;

            for (int frame = 0; frame < copy.frameCount && identical; ++frame)
            {
                const auto* data = source.frameData (0, frame);

                for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                    if (std::abs (wavetabletables::sampleAt (copy, frame, i) - data[i]) > 1.0e-4f)
                        identical = false;
            }

            expect (identical, NI_TXT ("拷贝出来的帧与内置表 level 0 逐点相同"));
        }

        beginTest (NI_TXT ("播放用的表：按级截断谐波"));

        {
            // A square wave has every odd harmonic, so the mipmaps have something
            // to cut.
            auto table = wavetabletables::makeEmpty (1, NI_TXT ("方波"), 1);

            for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                wavetabletables::setSampleAt (table, 0, i,
                                              std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * (double) i / (double) kCustomWavetableFrameSize) >= 0.0
                                                  ? 0.9f : -0.9f);

            const auto playable = wavetabletables::buildCustomTable (table);

            expectEquals (playable.frameCount, 1);
            expectEquals (playable.levelCount, wavetable::kLevelCount);

            // One level's frame, read back as a CustomWavetable so the FFT helper can
            // look at it.
            auto levelOf = [&playable] (int level)
            {
                auto copy = wavetabletables::makeEmpty (1, NI_TXT ("读回"), 1);
                const auto* data = playable.frameData (level, 0);

                for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                    wavetabletables::setSampleAt (copy, 0, i, data[i]);

                return copy;
            };

            // Level 0 keeps up to 1024 harmonics; level 5 only 32. (201 is odd,
            // which is where a square wave has its energy: 4 / (pi * 201) = 0.0063.)
            expectGreaterThan (harmonicLevel (levelOf (0), 0, 201), 0.004f);
            expectLessThan (harmonicLevel (levelOf (5), 0, 201), 1.0e-3f);
            expectGreaterThan (harmonicLevel (levelOf (5), 0, 3), 0.05f);
        }

        beginTest (NI_TXT ("工程往返：自定义表 + 引用；旧工程缺字段读成空"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::wavetable;

            auto table = wavetabletables::makeEmpty (1, NI_TXT ("我的表"), 4);

            for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                wavetabletables::setSampleAt (table, 0, i, (float) std::sin ((double) i * 0.01));

            project.nextCustomWavetableId = 2;
            project.customWavetables.push_back (table);
            track.wavetable.oscs[0].customTableId = 1;
            track.wavetable.oscs[0].tableIndex = 5;

            const auto json = projectToJson (project);

            expect (json.contains ("customWavetables"), NI_TXT ("表写进了工程文件"));

            Project reloaded;
            juce::String error;
            expect (projectFromJson (json, reloaded, error), error);
            expectEquals ((int) reloaded.customWavetables.size(), 1);
            expectEquals (reloaded.customWavetables.front().id, (juce::int64) 1);
            expectEquals (reloaded.customWavetables.front().name, NI_TXT ("我的表"));
            expectEquals (reloaded.customWavetables.front().frameCount, 4);
            expect (reloaded.customWavetables.front().samples == table.samples,
                    NI_TXT ("16 位数据逐点相同"));
            expectEquals (reloaded.tracks.front().wavetable.oscs[0].customTableId, (juce::int64) 1);
            expectEquals (reloaded.tracks.front().wavetable.oscs[0].tableIndex, 5);
            expectEquals (reloaded.nextCustomWavetableId, (juce::int64) 2);

            // A project written before custom tables existed: no tables at all, and
            // every oscillator falls back to its built-in index.
            auto stripped = juce::JSON::parse (json);

            if (auto* root = stripped.getDynamicObject())
            {
                root->removeProperty ("customWavetables");
                root->removeProperty ("nextCustomWavetableId");

                if (auto* tracks = root->getProperty ("tracks").getArray())
                    if (auto* firstTrack = (*tracks)[0].getDynamicObject())
                        if (auto* wavetable = firstTrack->getProperty ("wavetable").getDynamicObject())
                            if (auto* oscs = wavetable->getProperty ("oscs").getArray())
                                if (auto* firstOsc = (*oscs)[0].getDynamicObject())
                                    firstOsc->removeProperty ("customTableId");
            }

            Project old;
            expect (projectFromJson (juce::JSON::toString (stripped, false), old, error), error);
            expectEquals ((int) old.customWavetables.size(), 0);
            expectEquals (old.tracks.front().wavetable.oscs[0].customTableId, (juce::int64) 0);
            expectEquals (old.tracks.front().wavetable.oscs[0].tableIndex, 5);
        }

        beginTest (NI_TXT ("调度解析自定义表：非空、同表复用、改了会重建"));

        {
            auto project = makeProjectWithSynthTrack();
            // Take the ids, not references: adding the second track reallocates the
            // vector and a reference into it would dangle.
            const auto firstId = project.tracks.front().id;
            project.tracks.front().kind = TrackKind::wavetable;
            const auto secondId = addTrack (project, TrackKind::wavetable);
            auto* second = project.findTrack (secondId);

            auto table = wavetabletables::makeEmpty (1, NI_TXT ("调度的表"), 2);
            wavetabletables::setSampleAt (table, 0, 10, 0.5f);
            project.customWavetables.push_back (table);
            project.nextCustomWavetableId = 2;

            project.findTrack (firstId)->wavetable.oscs[0].customTableId = 1;
            second->wavetable.oscs[0].customTableId = 1;
            second->wavetable.oscs[1].customTableId = 0;

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 2);
            expect (schedule.tracks[0].wavetables[0] != nullptr,
                    NI_TXT ("第一轨的振荡器 1 拿到了一张表"));
            expect (schedule.tracks[1].wavetables[0] != nullptr,
                    NI_TXT ("第二轨也拿到了"));
            expect (schedule.tracks[1].wavetables[1] == nullptr,
                    NI_TXT ("用内置表的那一个振荡器是空指针（回退内置）"));
            expect (schedule.tracks[0].wavetables[0] == schedule.tracks[1].wavetables[0],
                    NI_TXT ("两条轨用同一个 id 时共用同一份表（缓存生效）"));

            // Redraw a point: the schedule has to build a new table.
            auto edited = project;
            edited.customWavetables[0].samples[10] = 12345;
            const auto afterEdit = buildSchedule (edited);

            expect (afterEdit.tracks[0].wavetables[0] != schedule.tracks[0].wavetables[0],
                    NI_TXT ("改了数据之后按新内容重建"));
            expectGreaterThan (afterEdit.tracks[0].wavetables[0]->levels[0][10], 0.3f);

            // An id the project does not have: no table, and no crash.
            auto missing = project;
            missing.tracks.front().wavetable.oscs[0].customTableId = 999;
            const auto missingSchedule = buildSchedule (missing);
            expect (missingSchedule.tracks[0].wavetables[0] == nullptr,
                    NI_TXT ("指向一张不存在的表时回退内置，而不是崩"));
        }

        beginTest (NI_TXT ("预设连表一起存"));

        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("hmdaw-wavetable-patch-test.json");
            file.deleteFile();

            auto table = wavetabletables::makeEmpty (4, NI_TXT ("预设里的表"), 2);
            wavetabletables::setSampleAt (table, 0, 7, 0.75f);

            WavetablePatch patch;
            patch.params.name = NI_TXT ("带表的音色");
            patch.params.oscs[0].customTableId = 4;
            patch.tables.push_back (table);

            PresetStore store (file);
            store.addWavetablePreset (patch);

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getWavetablePatches().size(), 1);
            expectEquals (reloaded.getWavetablePatches().front().params.name, NI_TXT ("带表的音色"));
            expectEquals ((int) reloaded.getWavetablePatches().front().tables.size(), 1);
            expect (reloaded.getWavetablePatches().front().tables.front().samples == table.samples,
                    NI_TXT ("预设里的表逐点相同"));

            // A patch written before tables travelled with presets: parameters only.
            auto parsed = juce::JSON::parse (file.loadFileAsString());

            if (auto* root = parsed.getDynamicObject())
                if (auto* patches = root->getProperty ("wavetablePatches").getArray())
                    if (auto* firstPatch = (*patches)[0].getDynamicObject())
                        firstPatch->removeProperty ("tables");

            file.replaceWithText (juce::JSON::toString (parsed, false));

            PresetStore old (file);
            expect (old.load());
            expectEquals ((int) old.getWavetablePatches().size(), 1);
            expectEquals ((int) old.getWavetablePatches().front().tables.size(), 0);

            file.deleteFile();
        }

        beginTest (NI_TXT ("自定义表真的改变了渲染出来的波形"));

        {
            auto table = wavetabletables::makeEmpty (1, NI_TXT ("自定义"), 1);

            // A triangle-ish wave, which the built-in sine table is not.
            for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            {
                const auto phase = (double) i / (double) kCustomWavetableFrameSize;
                const auto value = 2.0 * std::abs (2.0 * (phase - std::floor (phase + 0.5))) - 1.0;
                wavetabletables::setSampleAt (table, 0, i, (float) value * 0.8f);
            }

            const auto custom = wavetabletables::playableTable (table);

            WavetableParams params;
            params.oscs[0].position = 0.0f;
            params.oscs[1].enabled  = false;
            params.ampAttack = 0.0f;
            params.ampDecay  = 0.0f;
            params.ampSustain = 1.0f;
            params.ampRelease = 0.0f;
            params.posEnvAmount = 0.0f;
            params.filterCutoffHz = 20000.0f;
            params.filterKeyTrack = 0.0f;

            auto render = [&params] (std::shared_ptr<const wavetable::Wavetable> tableToUse)
            {
                WavetableSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.setTables (tableToUse, {});
                synth.noteOn (1, 440.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 4096);
                buffer.clear();
                synth.renderAdding (buffer, 0, 4096, 1.0f, 1.0f);
                return buffer;
            };

            const auto customBuffer = render (custom);
            const auto builtInBuffer = render ({});

            auto different = false;

            for (int i = 0; i < 4096; ++i)
                if (std::abs (customBuffer.getSample (0, i) - builtInBuffer.getSample (0, i)) > 1.0e-3f)
                    different = true;

            expect (different, NI_TXT ("换成自定义表之后渲染出来的波形不同"));
            expectGreaterThan (customBuffer.getRMSLevel (0, 0, 4096), 0.001f);

            // ...and it is still a 440 Hz note when the table is a plain sine.
            auto sineTable = wavetabletables::makeEmpty (2, NI_TXT ("正弦"), 1);

            for (int i = 0; i < kCustomWavetableFrameSize; ++i)
                wavetabletables::setSampleAt (sineTable, 0, i,
                                              (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                * (double) i
                                                                / (double) kCustomWavetableFrameSize));

            const auto sineBuffer = render (wavetabletables::playableTable (sineTable));
            auto crossings = 0;
            auto previous = sineBuffer.getSample (0, 0);

            for (int i = 1; i < 4096; ++i)
            {
                const auto value = sineBuffer.getSample (0, i);

                if ((previous < 0.0f && value >= 0.0f) || (previous > 0.0f && value <= 0.0f))
                    ++crossings;

                previous = value;
            }

            // 440 Hz over 4096 samples at 48 kHz is 37.5 cycles, i.e. 75 crossings.
            expect (crossings >= 70 && crossings <= 80,
                    NI_TXT ("一张正弦表渲染出来仍然是 440 Hz（实测过零 ") + juce::String (crossings)
                        + NI_TXT (" 次）"));
        }
    }
};

namespace
{
    WavetableTableTests wavetableTableTests;
}
