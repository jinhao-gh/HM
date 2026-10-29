#include "TestSupport.h"
#include "../Core/FmPresets.h"

class SynthTests : public juce::UnitTest
{
public:
    SynthTests() : juce::UnitTest ("Synth", "nidaw") {}

    static double measureFrequency (const juce::AudioBuffer<float>& buffer, int startSample, int endSample)
    {
        const auto* data = buffer.getReadPointer (0);
        double firstCrossing = -1.0, lastCrossing = -1.0;
        int crossings = 0;

        for (int i = startSample + 1; i < endSample; ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                // Linear interpolation gives a sub-sample accurate crossing time.
                const auto previous = (double) data[i - 1];
                const auto current = (double) data[i];
                const auto fraction = previous == current ? 0.0 : (-previous) / (current - previous);
                const auto time = ((double) (i - 1) + fraction) / 48000.0;

                if (firstCrossing < 0.0)
                    firstCrossing = time;

                lastCrossing = time;
                ++crossings;
            }
        }

        if (crossings < 2 || lastCrossing <= firstCrossing)
            return 0.0;

        return (double) (crossings - 1) / (lastCrossing - firstCrossing);
    }

    /** A plain two-operator patch in algorithm 1: operator 1 is the carrier and
        operator 2 is its modulator. Modulator level 0 gives a pure sine. */
    static SynthParams testParams (int modulatorLevel = 0, double modulatorRatio = 1.0)
    {
        SynthParams p;
        p.gain = 1.0f;
        p.patch.algorithm = 1;

        for (auto& op : p.patch.ops)
        {
            op.rate[0] = 99; op.rate[1] = 99; op.rate[2] = 99; op.rate[3] = 90;
            op.level[0] = 99; op.level[1] = 99; op.level[2] = 99; op.level[3] = 0;
        }

        p.patch.ops[0].ratio = 1.0f;
        p.patch.ops[0].outputLevel = 99;

        p.patch.ops[1].ratio = (float) modulatorRatio;
        p.patch.ops[1].outputLevel = modulatorLevel;

        return p;
    }

    void runTest() override
    {
        beginTest ("a note is rendered at the requested frequency");

        for (const auto hz : { 220.0, 440.0, 880.0 })
        {
            TrackSynth synth;
            synth.prepare (48000.0);
            synth.setParams (testParams());
            synth.noteOn (1, (float) hz, 127.0f);

            juce::AudioBuffer<float> buffer (2, 48000);
            buffer.clear();
            synth.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);

            const auto measured = measureFrequency (buffer, 480, 48000);
            expectWithinAbsoluteError (measured, hz, 0.5);
        }

        beginTest ("velocity scales the output level");

        {
            TrackSynth loud, quiet;
            loud.prepare (48000.0);
            quiet.prepare (48000.0);
            loud.setParams (testParams());
            quiet.setParams (testParams());
            loud.noteOn (1, 440.0f, 127.0f);
            quiet.noteOn (1, 440.0f, 32.0f);

            juce::AudioBuffer<float> loudBuffer (2, 4800), quietBuffer (2, 4800);
            loudBuffer.clear();
            quietBuffer.clear();
            loud.renderAdding (loudBuffer, 0, 4800, 1.0f, 1.0f);
            quiet.renderAdding (quietBuffer, 0, 4800, 1.0f, 1.0f);

            const auto loudRms = loudBuffer.getRMSLevel (0, 1000, 3000);
            const auto quietRms = quietBuffer.getRMSLevel (0, 1000, 3000);

            expect (loudRms > quietRms * 2.0f);
        }

        beginTest ("all 64 algorithms produce sound and stay bounded");

        for (int algorithm = 1; algorithm <= kFmAlgorithmCount; ++algorithm)
        {
            TrackSynth synth;
            synth.prepare (48000.0);

            // Everything at full level: every algorithm has at least one carrier,
            // so none of them may be silent.
            auto params = testParams (99, 2.0);
            params.patch.algorithm = algorithm;

            for (auto& op : params.patch.ops)
                op.outputLevel = 99;

            synth.setParams (params);
            synth.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 9600);
            buffer.clear();
            synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);

            auto peak = 0.0f;
            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < 9600; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));

            expectGreaterThan (buffer.getRMSLevel (0, 1000, 9000), 0.005f);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0f);
        }

        beginTest (NI_TXT ("the DX7 factory bank is 32 voices in four categories"));
        {
            int count = 0;
            const auto* presets = fm::bank (count);
            expectEquals (count, 32);

            juce::StringArray names;
            std::map<juce::String, int> perCategory;

            for (int i = 0; i < count; ++i)
            {
                const auto presetName = juce::String::fromUTF8 (presets[i].name);
                const auto presetCategory = juce::String::fromUTF8 (presets[i].category);

                expect (presetName.isNotEmpty());
                expect (presetCategory.isNotEmpty());
                expect (! names.contains (presetName));
                names.add (presetName);
                ++perCategory[presetCategory];
            }

            expectEquals ((int) perCategory.size(), 4);

            for (const auto& entry : perCategory)
                expectEquals (entry.second, 8);

            for (int i = 0; i < count; ++i)
            {
                FmPatch patch;
                fm::applyPreset (patch, i);

                // The DX7 bank keeps its own routing: algorithm 32 + N is the
                // original Yamaha algorithm N, and the classic engine is on.
                expectEquals (patch.algorithm, 32 + presets[i].dx7Algorithm,
                              "preset " + juce::String (presets[i].name) + " algorithm");
                expect (patch.classicEngine, "preset " + juce::String (presets[i].name)
                                                + " classic engine");
            }

            for (int i = 0; i < count; ++i)
            {
                FmPatch patch;
                fm::applyPreset (patch, i);
                expectEquals (patch.name, juce::String::fromUTF8 (presets[i].name));

                SynthParams params;
                params.patch = patch;
                params.gain = 1.0f;

                TrackSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 288000);
                buffer.clear();
                synth.renderAdding (buffer, 0, 288000, 1.0f, 1.0f);

                auto peak = 0.0f;

                for (int s = 0; s < 288000; ++s)
                    peak = juce::jmax (peak, std::abs (buffer.getSample (0, s)));

                // Six seconds: the DX7 envelopes (TAKE OFF) fade in over several
                // seconds, while others are percussive and silent again by then,
                // so the peak is what "makes sound" has to mean here.
                expect (std::isfinite (peak), "preset " + patch.name + " is not finite");
                expectGreaterThan (peak, 0.005f, "preset " + patch.name + " is silent");
                expectLessThan (peak, 4.0f, "preset " + patch.name + " peaks at "
                                                + juce::String (peak, 3));
            }
        }

        beginTest ("the DX7 routings and parameter set are honoured");
        {
            // The DX7 bank lives in 33..64: 33 is Yamaha's algorithm 1 (carriers
            // OP1 and OP3, feedback on OP6), 64 its algorithm 32 (six carriers),
            // 36 its algorithm 4 (feedback on OP4 instead of OP6).
            expectEquals ((int) getFmAlgorithm (33).carriers, 0x05);
            expectEquals (getFmAlgorithm (33).feedbackOp, 5);
            expectEquals ((int) getFmAlgorithm (64).carriers, 0x3f);
            expectEquals (getFmAlgorithm (36).feedbackOp, 3);

            auto makeOneCarrier = []
            {
                SynthParams params;
                params.patch.classicEngine = true;
                params.patch.algorithm = 33;

                for (auto& op : params.patch.ops)
                    op.outputLevel = 0;

                auto& op = params.patch.ops[0];
                op.outputLevel = 99;
                op.level[0] = 99;
                op.level[1] = 99;
                op.level[2] = 99;
                op.level[3] = 0;
                op.rate[0] = 99;
                op.rate[1] = 99;
                op.rate[2] = 99;
                op.rate[3] = 99;
                return params;
            };

            auto render = [] (const SynthParams& params, float hz, float velocity)
            {
                TrackSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, hz, velocity);

                juce::AudioBuffer<float> buffer (2, 9600);
                buffer.clear();
                synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);
                return buffer;
            };

            // Keyboard level scaling: -LIN with a depth of 99 must make a note
            // four octaves above the breakpoint clearly quieter.
            {
                auto params = makeOneCarrier();
                auto& op = params.patch.ops[0];
                op.breakPoint = 60;
                op.rightCurve = FmScaleCurve::negLin;
                op.rightDepth = 99;

                const auto low = render (params, 130.81f, 100.0f).getRMSLevel (0, 1000, 8000);
                const auto high = render (params, 2093.0f, 100.0f).getRMSLevel (0, 1000, 8000);

                expectGreaterThan (low, 0.0001f);
                expectLessThan (high, low * 0.5f, "keyboard level scaling");
            }

            // Fixed frequency: the note number stops mattering entirely.
            {
                auto params = makeOneCarrier();
                auto& op = params.patch.ops[0];
                op.mode = FmOscMode::fixed;
                op.fixedHz = 1000.0f;

                const auto a = render (params, 220.0f, 100.0f);
                const auto b = render (params, 880.0f, 100.0f);

                auto maxDifference = 0.0f;

                for (int s = 0; s < 9600; ++s)
                    maxDifference = juce::jmax (maxDifference,
                                                std::abs (a.getSample (0, s) - b.getSample (0, s)));

                expectLessThan (maxDifference, 1.0e-5f, "fixed frequency follows the note");
            }

            // Velocity sensitivity: a soft note is quieter when the operator asks
            // for it (the DX7 curve, not a linear fade).
            {
                auto params = makeOneCarrier();
                params.patch.ops[0].velSens = 7;

                const auto loud = render (params, 440.0f, 127.0f).getRMSLevel (0, 1000, 8000);
                const auto soft = render (params, 440.0f, 30.0f).getRMSLevel (0, 1000, 8000);

                expectGreaterThan (loud, 0.0001f);
                expectLessThan (soft, loud * 0.7f, "velocity sensitivity");
            }

            // Pitch envelope: L2 one octave up, reached almost instantly, makes
            // the first part of the note cross zero more often.
            {
                auto plain = makeOneCarrier();
                plain.patch.pitchEg.level[1] = 50;

                auto bent = makeOneCarrier();
                bent.patch.pitchEg.level[1] = 62;    // (62 - 50) / 50 * 48 = 11.5 semitones
                bent.patch.pitchEg.rate[0] = 99;

                auto crossings = [] (const juce::AudioBuffer<float>& buffer)
                {
                    auto count = 0;
                    const auto* d = buffer.getReadPointer (0);

                    for (int s = 1; s < 4000; ++s)
                        if ((d[s - 1] < 0.0f) != (d[s] < 0.0f))
                            ++count;

                    return count;
                };

                const auto plainCrossings = crossings (render (plain, 440.0f, 100.0f));
                const auto bentCrossings = crossings (render (bent, 440.0f, 100.0f));
                expectGreaterThan (bentCrossings, plainCrossings, "pitch envelope bends up");
            }
        }

        beginTest ("a switched-off modulator stops contributing");

        {
            // Level 0 and "switched off" have to mean exactly the same thing to
            // the engine, so the two renders must match sample for sample, and
            // both must be the clean sine the note asked for.
            TrackSynth silentModulator, disabledModulator;
            silentModulator.prepare (48000.0);
            disabledModulator.prepare (48000.0);

            silentModulator.setParams (testParams (0, 2.0));

            auto disabled = testParams (99, 2.0);
            disabled.patch.ops[1].enabled = false;
            disabledModulator.setParams (disabled);

            silentModulator.noteOn (1, 440.0f, 127.0f);
            disabledModulator.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> reference (2, 9600), switchedOff (2, 9600);
            reference.clear();
            switchedOff.clear();
            silentModulator.renderAdding (reference, 0, 9600, 1.0f, 1.0f);
            disabledModulator.renderAdding (switchedOff, 0, 9600, 1.0f, 1.0f);

            auto worst = 0.0f;

            for (int i = 0; i < 9600; ++i)
                worst = juce::jmax (worst, std::abs (reference.getSample (0, i)
                                                     - switchedOff.getSample (0, i)));

            expectLessThan (worst, 1.0e-6f);
            expectWithinAbsoluteError (measureFrequency (reference, 1000, 9000), 440.0, 0.5);

            // ... and the modulator really is what colours the sound: with it on,
            // the same patch is no longer a plain sine.
            TrackSynth modulated;
            modulated.prepare (48000.0);
            modulated.setParams (testParams (99, 2.0));
            modulated.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> rich (2, 9600);
            rich.clear();
            modulated.renderAdding (rich, 0, 9600, 1.0f, 1.0f);

            auto difference = 0.0f;

            for (int i = 0; i < 9600; ++i)
                difference = juce::jmax (difference, std::abs (rich.getSample (0, i)
                                                               - reference.getSample (0, i)));

            expectGreaterThan (difference, 0.05f);
        }

        beginTest ("a switched-off carrier goes silent and comes back");

        {
            TrackSynth synth;
            synth.prepare (48000.0);

            auto params = testParams (99, 2.0);
            params.patch.ops[0].enabled = false;    // algorithm 1's only carrier
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> silent (2, 4800);
            silent.clear();
            synth.renderAdding (silent, 0, 4800, 1.0f, 1.0f);
            expectLessThan (silent.getRMSLevel (0, 0, 4800), 1.0e-6f);

            // The voice keeps running, so switching it back on restores the sound
            // without a new note-on.
            params.patch.ops[0].enabled = true;
            synth.setParams (params);

            juce::AudioBuffer<float> restored (2, 4800);
            restored.clear();
            synth.renderAdding (restored, 0, 4800, 1.0f, 1.0f);
            expectGreaterThan (restored.getRMSLevel (0, 0, 4800), 0.01f);
        }

        beginTest ("the envelope releases and frees the voice");

        {
            TrackSynth synth;
            synth.prepare (48000.0);
            auto params = testParams();
            params.patch.ops[0].rate[3] = 99;   // fast release
            params.patch.ops[1].rate[3] = 99;
            synth.setParams (params);
            synth.noteOn (7, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 4800);
            buffer.clear();
            synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            expect (synth.isActive());

            synth.noteOff (7);

            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            }

            expect (! synth.isActive());
        }

        beginTest ("the modulator changes the timbre");

        {
            TrackSynth plain, modulated;
            plain.prepare (48000.0);
            modulated.prepare (48000.0);

            plain.setParams (testParams());
            modulated.setParams (testParams (80, 1.0));

            plain.noteOn (1, 440.0f, 127.0f);
            modulated.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> plainBuffer (2, 48000), fmBuffer (2, 48000);
            plainBuffer.clear();
            fmBuffer.clear();
            plain.renderAdding (plainBuffer, 0, 48000, 1.0f, 1.0f);
            modulated.renderAdding (fmBuffer, 0, 48000, 1.0f, 1.0f);

            expectGreaterThan (fmBuffer.getRMSLevel (0, 1000, 40000), 0.05f);

            double difference = 0.0;

            for (int i = 1000; i < 40000; ++i)
                difference += std::abs ((double) plainBuffer.getSample (0, i)
                                      - (double) fmBuffer.getSample (0, i));

            expectGreaterThan ((float) (difference / 39000.0), 0.01f);
        }

        beginTest ("light modulation keeps the carrier at the note's pitch");

        {
            TrackSynth synth;
            synth.prepare (48000.0);

            // A small modulation index keeps the phase monotonic, so counting zero
            // crossings is still a valid way of measuring the carrier frequency.
            auto params = testParams (12, 1.0);
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 48000);
            buffer.clear();
            synth.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);

            expectWithinAbsoluteError (measureFrequency (buffer, 480, 48000), 440.0, 0.5);
        }

        beginTest ("the modulator ratio changes the timbre as well");

        {
            const auto render = [] (double ratio)
            {
                TrackSynth synth;
                synth.prepare (48000.0);
                synth.setParams (testParams (70, ratio));
                synth.noteOn (1, 440.0f, 127.0f);

                juce::AudioBuffer<float> buffer (2, 4800);
                buffer.clear();
                synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
                return buffer.getRMSLevel (0, 1000, 4000);
            };

            expect (std::abs (render (1.0) - render (3.5)) > 0.005f);
        }

        beginTest ("voice stealing keeps the voice count bounded");

        {
            TrackSynth synth;
            synth.prepare (48000.0);
            synth.setParams (testParams());

            for (int i = 0; i < 40; ++i)
                synth.noteOn (100 + i, 220.0f + (float) i, 100.0f);

            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
        }

        beginTest ("the low pass filter attenuates above its cutoff");

        {
            const auto measureRms = [] (double cutoffHz, float resonance)
            {
                TrackSynth synth;
                synth.prepare (48000.0);

                auto params = testParams();
                params.filterCutoffHz = (float) cutoffHz;
                params.filterResonance = resonance;
                synth.setParams (params);
                synth.noteOn (1, 880.0f, 127.0f);

                juce::AudioBuffer<float> buffer (2, 4800);
                buffer.clear();
                synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
                return buffer.getRMSLevel (0, 1000, 3800);
            };

            const auto wideOpen = measureRms (20000.0, 0.707f);
            const auto closed = measureRms (100.0, 0.707f);

            expectGreaterThan (wideOpen, 0.05f);
            expectLessThan (closed, wideOpen * 0.35f);
        }

        beginTest ("a high resonance filter stays stable");

        {
            TrackSynth synth;
            synth.prepare (48000.0);

            auto params = testParams (60, 3.0);   // a bright patch to filter
            params.filterCutoffHz = 1000.0f;
            params.filterResonance = 15.0f;
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 48000);
            buffer.clear();
            synth.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);

            auto peak = 0.0f;
            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < 48000; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));

            expect (std::isfinite (peak));
            expectLessThan (peak, 5.0f);
            expectGreaterThan (peak, 0.001f);
        }
    }
};

class SubtractiveTests : public juce::UnitTest
{
public:
    SubtractiveTests() : juce::UnitTest ("Subtractive", "nidaw") {}

    /** A plain sine with the filter wide open: everything measured below is then
        the oscillator or the filter, never a side effect of something else. */
    static SubtractiveParams sineParams()
    {
        SubtractiveParams p;
        p.oscs[0] = subtractive::osc (OscWave::sine, 1.0f);
        p.oscs[1].enabled = false;
        p.noiseLevel = 0.0f;
        p.filterCutoffHz = 20000.0f;
        p.filterResonance = 0.707f;
        p.filterEnvAmount = 0.0f;
        p.ampAttack = 0.001f; p.ampDecay = 0.0f; p.ampSustain = 1.0f; p.ampRelease = 0.02f;
        p.filterAttack = 0.001f; p.filterDecay = 0.0f; p.filterSustain = 1.0f;
        p.filterRelease = 0.02f;
        p.lfoRateHz = 0.0f;
        p.lfoPitchCents = 0.0f;
        p.lfoCutoffDepth = 0.0f;
        p.lfoAmpDepth = 0.0f;
        p.gain = 1.0f;
        return p;
    }

    /** Upward zero crossings: the frequency of a clean sine. */
    static int countUpwardCrossings (const juce::AudioBuffer<float>& buffer,
                                     int channel, int start, int count)
    {
        const auto* data = buffer.getReadPointer (channel);
        auto crossings = 0;

        for (int i = start + 1; i < start + count; ++i)
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                ++crossings;

        return crossings;
    }

    static double renderRms (SubtractiveSynth& synth, int blocks, int skipBlocks = 0)
    {
        constexpr int blockSize = 512;
        juce::AudioBuffer<float> buffer (2, blockSize);
        auto sum = 0.0;
        auto counted = 0;

        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

            if (i >= skipBlocks)
            {
                sum += (double) buffer.getRMSLevel (0, 0, blockSize);
                ++counted;
            }
        }

        return counted > 0 ? sum / counted : 0.0;
    }

    void runTest() override
    {
        constexpr int blockSize = 512;

        beginTest (NI_TXT ("a note comes out at the frequency it was asked for"));

        {
            SubtractiveSynth synth;
            synth.prepare (48000.0);
            synth.setParams (sineParams());
            synth.noteOn (1, 220.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            auto crossings = 0;

            for (int i = 0; i < 93; ++i)      // just under a second
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);
                crossings += countUpwardCrossings (buffer, 0, 0, blockSize);
            }

            // 220 Hz over ~0.99 s is about 218 crossings; the attack costs a few.
            expectWithinAbsoluteError ((double) crossings, 218.0, 6.0);
            expectEquals (synth.activeVoiceCount(), 1);
            expectWithinAbsoluteError (synth.getVoiceFrequencyForTest (1), 220.0, 1.0e-9);
        }

        beginTest (NI_TXT ("the filter really filters"));

        {
            const auto rmsWithCutoff = [] (double cutoff)
            {
                auto params = sineParams();
                params.filterCutoffHz = (float) cutoff;

                SubtractiveSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 2000.0f, 100.0f);

                return renderRms (synth, 40, 10);
            };

            const auto open = rmsWithCutoff (20000.0);
            const auto closed = rmsWithCutoff (200.0);

            expectGreaterThan (open, 0.05);
            expectLessThan (closed, open * 0.5,
                            NI_TXT ("a 2 kHz tone through a 200 Hz low pass is much quieter"));
        }

        beginTest (NI_TXT ("the filter envelope opens the sound up"));

        {
            const auto earlyRms = [] (float envAmount)
            {
                auto params = sineParams();
                params.oscs[0] = subtractive::osc (OscWave::saw, 1.0f);
                params.filterCutoffHz = 300.0f;
                params.filterEnvAmount = envAmount;
                params.filterAttack = 0.001f;
                params.filterDecay = 2.0f;
                params.filterSustain = 0.0f;

                SubtractiveSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                return renderRms (synth, 4);
            };

            const auto dark = earlyRms (0.0f);
            const auto bright = earlyRms (1.0f);

            expectGreaterThan (dark, 0.0);
            expectGreaterThan (bright, dark * 1.2,
                               NI_TXT ("the envelope makes the first moments brighter"));
        }

        beginTest (NI_TXT ("the LFO changes the sound"));

        {
            const auto render = [] (float pitchCents)
            {
                auto params = sineParams();
                params.lfoRateHz = 5.0f;
                params.lfoPitchCents = pitchCents;

                SubtractiveSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 440.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, blockSize);
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

                std::vector<float> samples (blockSize);

                for (int i = 0; i < blockSize; ++i)
                    samples[(size_t) i] = buffer.getSample (0, i);

                return samples;
            };

            const auto flat = render (0.0f);
            const auto vibrato = render (80.0f);

            auto worst = 0.0f;

            for (size_t i = 0; i < flat.size(); ++i)
                worst = juce::jmax (worst, std::abs (flat[i] - vibrato[i]));

            expectGreaterThan ((double) worst, 0.01, NI_TXT ("the vibrato moves the wave"));
        }

        beginTest (NI_TXT ("the release frees the voice"));

        {
            auto params = sineParams();
            params.ampRelease = 0.05f;

            SubtractiveSynth synth;
            synth.prepare (48000.0);
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int i = 0; i < 10; ++i)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);
            }

            expectEquals (synth.activeVoiceCount(), 1);
            synth.noteOff (1);

            auto tail = 0.0f;

            for (int i = 0; i < 30; ++i)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

                // The release itself is audible; what has to be silent is what
                // comes after it.
                if (i >= 10)
                    tail = juce::jmax (tail, buffer.getMagnitude (0, 0, blockSize));
            }

            expectEquals (synth.activeVoiceCount(), 0);
            expectLessThan ((double) tail, 0.001);
        }


        beginTest (NI_TXT ("voice stealing keeps the count bounded"));

        {
            SubtractiveSynth synth;
            synth.prepare (48000.0);
            synth.setParams (sineParams());

            for (int i = 1; i <= kVoicesPerTrack + 8; ++i)
                synth.noteOn (i, 220.0f + (float) i, 100.0f);

            expect (synth.activeVoiceCount() <= kVoicesPerTrack);
            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
        }

        beginTest (NI_TXT ("the factory bank is twelve distinct sounds"));

        {
            const auto fallback = SubtractiveParams();
            const auto count = subtractive::builtInCount();

            expectEquals (count, 12);

            std::vector<SubtractiveParams> bank;

            for (int i = 0; i < count; ++i)
            {
                auto params = fallback;
                subtractive::applyPreset (params, i);
                bank.push_back (params);

                expect (params.name.isNotEmpty(), NI_TXT ("every factory sound is named"));
                expect (! (params == fallback),
                        NI_TXT ("and none is left at the defaults"));
            }

            auto duplicates = 0;

            for (int i = 0; i < (int) bank.size(); ++i)
                for (int j = i + 1; j < (int) bank.size(); ++j)
                    if (bank[(size_t) i] == bank[(size_t) j])
                        ++duplicates;

            expectEquals (duplicates, 0, NI_TXT ("no two factory sounds are identical"));

            // An out of range index is clamped rather than throwing.
            auto clamped = fallback;
            subtractive::applyPreset (clamped, 999);
            expectEquals (clamped.name, bank.back().name);
        }

        beginTest (NI_TXT ("a subtractive track reaches the schedule"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::subtractive;
            track.subtractive.filterCutoffHz = 1234.0f;

            Note note;
            note.id = 1;
            note.durationTicks = 960;
            note.frequencyHz = 330.0;
            track.clips.front().notes = { note };

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::subtractive);
            expectWithinAbsoluteError ((double) schedule.tracks.front().subtractive.filterCutoffHz,
                                       1234.0, 1.0e-3);
            expectEquals ((int) schedule.tracks.front().events.size(), 2);
        }
    }
};

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest ("Wavetable", "nidaw") {}

    /** A pure sine at frame 0: every measurement below is then the oscillator. */
    static WavetableParams sineParams()
    {
        WavetableParams p;
        p.oscs[0].tableIndex = 0;      // 正弦→锯齿, frame 0 is a sine
        p.oscs[0].position   = 0.0f;
        p.oscs[0].level      = 1.0f;
        p.oscs[1].enabled    = false;
        p.noiseLevel = 0.0f;
        p.filterCutoffHz  = 20000.0f;
        p.filterResonance = 0.707f;
        p.filterKeyTrack  = 0.0f;
        p.ampAttack = 0.001f; p.ampDecay = 0.0f; p.ampSustain = 1.0f; p.ampRelease = 0.02f;
        p.posEnvAmount = 0.0f;
        p.lfoRateHz = 0.0f;
        p.lfoPositionDepth = 0.0f;
        p.lfoPitchCents = 0.0f;
        p.lfoCutoffDepth = 0.0f;
        p.lfoAmpDepth = 0.0f;
        p.gain = 1.0f;
        return p;
    }

    static int countUpwardCrossings (const juce::AudioBuffer<float>& buffer,
                                     int channel, int start, int count)
    {
        const auto* data = buffer.getReadPointer (channel);
        auto crossings = 0;

        for (int i = start + 1; i < start + count; ++i)
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                ++crossings;

        return crossings;
    }

    static double renderRms (WavetableSynth& synth, int blocks, int skipBlocks = 0)
    {
        constexpr int blockSize = 512;
        juce::AudioBuffer<float> buffer (2, blockSize);
        auto sum = 0.0;
        auto counted = 0;

        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

            if (i >= skipBlocks)
            {
                sum += (double) buffer.getRMSLevel (0, 0, blockSize);
                ++counted;
            }
        }

        return counted > 0 ? sum / counted : 0.0;
    }

    void runTest() override
    {
        constexpr int blockSize = 512;

        beginTest (NI_TXT ("波表位置上报给界面：引擎说的是真在用的那个数"));

        {
            const auto block = 512;
            WavetableSynth synth;
            synth.prepare (48000.0);

            auto params = sineParams();
            params.oscs[1].enabled = true;
            params.oscs[1].tableIndex = 3;
            params.oscs[0].position = 0.05f;      // where each oscillator starts
            params.oscs[1].position = 0.50f;
            params.posEnvAmount = 0.35f;          // ...and the envelope sweeps from there
            params.posAttack = 0.02f;
            params.posDecay = 0.01f;
            params.posSustain = 1.0f;
            params.lfoPositionDepth = 0.0f;
            synth.setParams (params);

            juce::AudioBuffer<float> buffer (2, block);

            expectEquals (synth.getPublishedVoices(), 0, NI_TXT ("还没按键时没有声部"));

            synth.noteOn (1, 440.0f, 100.0f);
            expectEquals (synth.getPublishedVoices(), 1, NI_TXT ("按下之后有一个声部"));

            // The first block already walks the position a little, and the envelope
            // keeps climbing: after a while neither oscillator is at its slider value.
            for (int i = 0; i < 40; ++i)
                synth.renderAdding (buffer, 0, block, 1.0f, 1.0f);

            expectGreaterThan (synth.getPublishedPosition (0), 0.05f,
                               NI_TXT ("振荡器 1 的位置跟着包络往上走（")
                                   + juce::String (synth.getPublishedPosition (0), 3) + NI_TXT ("）"));
            expectGreaterThan (synth.getPublishedPosition (1), 0.50f,
                               NI_TXT ("振荡器 2 也一样（")
                                   + juce::String (synth.getPublishedPosition (1), 3) + NI_TXT ("）"));
            expect (synth.getPublishedPosition (0) <= 1.0f
                        && synth.getPublishedPosition (1) <= 1.0f,
                    NI_TXT ("两个位置都在 0…1 之间"));

            // Both oscillators report their own position, not one shared number.
            expect (std::abs (synth.getPublishedPosition (0) - synth.getPublishedPosition (1)) > 0.3f,
                    NI_TXT ("两个振荡器各自报自己的位置"));

            // A position at the very top is clamped, never published as more than 1.
            auto high = params;
            high.oscs[0].position = 0.98f;
            high.posEnvAmount = 1.0f;
            synth.setParams (high);

            for (int i = 0; i < 40; ++i)
                synth.renderAdding (buffer, 0, block, 1.0f, 1.0f);

            expect (synth.getPublishedPosition (0) <= 1.0f
                        && synth.getPublishedPosition (0) > 0.98f,
                    NI_TXT ("读到的是夹在 0…1 里的位置"));

            synth.allNotesOff (true);
            expectEquals (synth.getPublishedVoices(), 0, NI_TXT ("全部放开之后声部归零"));
        }

        beginTest (NI_TXT ("the table bank is normalised and band limited per level"));

        {
            wavetable::ensureBuilt();

            expectEquals (wavetable::builtInTableCount(), 16);
            expectEquals (wavetable::tableName (0), NI_TXT ("正弦→锯齿"));

            for (int index = 0; index < wavetable::builtInTableCount(); ++index)
            {
                const auto& table = wavetable::getTable (index);

                expectEquals (table.frameCount, wavetable::kFrameCount);
                expect (table.levelCount > 1, NI_TXT ("every table has mipmap levels"));
                expectEquals ((int) table.levels.size(), table.levelCount);
                expect (table.name.isNotEmpty(), NI_TXT ("every table is named"));

                // The table gets one gain, taken from its loudest frame, so a
                // frame whose band-limited peak is lower stays lower on purpose.
                auto tablePeak = 0.0f;

                for (int frame = 0; frame < table.frameCount; ++frame)
                {
                    auto peak = 0.0f;
                    auto energy = 0.0f;
                    const auto* data = table.frameData (0, frame);

                    for (int i = 0; i < wavetable::kFrameSize; ++i)
                    {
                        peak = juce::jmax (peak, std::abs (data[i]));
                        energy += data[i] * data[i];
                    }

                    expect (energy > 1.0e-6f, NI_TXT ("no frame is silent"));
                    tablePeak = juce::jmax (tablePeak, peak);
                }

                expect (tablePeak > 0.9f && tablePeak <= 1.001f,
                        NI_TXT ("the loudest frame is normalised (peak ")
                            + juce::String (tablePeak, 4) + ")");

                // The top level keeps a single harmonic: a pure sine, so its
                // spectrum cannot hold anything above the fundamental.
                const auto& last = table.levels[(size_t) (table.levelCount - 1)];
                const auto* frame = table.frameData (table.levelCount - 1, 0);

                juce::dsp::FFT fft (11);
                std::vector<float> spectrum ((size_t) wavetable::kFrameSize * 2, 0.0f);

                for (int i = 0; i < wavetable::kFrameSize; ++i)
                    spectrum[(size_t) i] = frame[i];

                fft.performRealOnlyForwardTransform (spectrum.data());

                auto above = 0.0f;

                for (int n = 2; n <= wavetable::kFrameSize / 2; ++n)
                    above = juce::jmax (above,
                                        std::sqrt (spectrum[(size_t) (2 * n)] * spectrum[(size_t) (2 * n)]
                                                       + spectrum[(size_t) (2 * n + 1)]
                                                             * spectrum[(size_t) (2 * n + 1)]));

                const auto fundamental = std::sqrt (spectrum[2] * spectrum[2]
                                                        + spectrum[3] * spectrum[3]);

                expect (above < fundamental * 0.05f,
                        NI_TXT ("the coarsest level keeps one harmonic (above ")
                            + juce::String (above, 4) + " vs " + juce::String (fundamental, 4) + ")");

                juce::ignoreUnused (last);
            }
        }

        beginTest (NI_TXT ("the level follows the pitch"));

        {
            // A low note may use the full bandwidth; a high one must not.
            // (20 Hz: 1200 harmonics fit under Nyquist, so level 0 is allowed.)
            expectEquals (wavetable::levelForFrequency (20.0, 48000.0), 0);
            expect (wavetable::levelForFrequency (5000.0, 48000.0) > 0,
                    NI_TXT ("a high note drops to a coarser level"));
            expectEquals (wavetable::levelForFrequency (20000.0, 48000.0),
                          wavetable::kLevelCount - 1);
        }

        beginTest (NI_TXT ("a note comes out at the frequency it was asked for"));

        {
            WavetableSynth synth;
            synth.prepare (48000.0);
            synth.setParams (sineParams());
            synth.noteOn (1, 220.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            auto crossings = 0;

            for (int i = 0; i < 93; ++i)      // just under a second
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);
                crossings += countUpwardCrossings (buffer, 0, 0, blockSize);
            }

            expectWithinAbsoluteError ((double) crossings, 218.0, 6.0);
            expectEquals (synth.activeVoiceCount(), 1);
            expectWithinAbsoluteError (synth.getVoiceFrequencyForTest (1), 220.0, 1.0e-9);
        }

        beginTest (NI_TXT ("the position envelope sweeps the table"));

        {
            const auto render = [] (float envAmount)
            {
                auto params = sineParams();
                params.posAttack = 0.001f;
                params.posDecay = 0.300f;
                params.posSustain = 0.0f;
                params.posEnvAmount = envAmount;

                WavetableSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, blockSize);
                std::vector<float> samples;

                for (int i = 0; i < 4; ++i)
                {
                    buffer.clear();
                    synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

                    for (int s = 0; s < blockSize; ++s)
                        samples.push_back (buffer.getSample (0, s));
                }

                return samples;
            };

            const auto still = render (0.0f);
            const auto swept = render (1.0f);

            expectEquals ((int) still.size(), (int) swept.size());

            auto worst = 0.0f;

            for (size_t i = 0; i < still.size() && i < swept.size(); ++i)
                worst = juce::jmax (worst, std::abs (still[i] - swept[i]));

            expectGreaterThan ((double) worst, 0.01,
                               NI_TXT ("the envelope changes the wave over time"));
        }

        beginTest (NI_TXT ("the filter really filters"));

        {
            const auto rmsWithCutoff = [] (double cutoff)
            {
                auto params = sineParams();
                params.filterCutoffHz = (float) cutoff;

                WavetableSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 2000.0f, 100.0f);

                return renderRms (synth, 40, 10);
            };

            const auto open = rmsWithCutoff (20000.0);
            const auto closed = rmsWithCutoff (200.0);

            expectGreaterThan (open, 0.05);
            expectLessThan (closed, open * 0.5,
                            NI_TXT ("a 2 kHz tone through a 200 Hz low pass is much quieter"));
        }

        beginTest (NI_TXT ("the release frees the voice"));

        {
            auto params = sineParams();
            params.ampRelease = 0.05f;

            WavetableSynth synth;
            synth.prepare (48000.0);
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int i = 0; i < 10; ++i)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);
            }

            expectEquals (synth.activeVoiceCount(), 1);
            synth.noteOff (1);

            auto tail = 0.0f;

            for (int i = 0; i < 30; ++i)
            {
                buffer.clear();
                synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

                // The release itself is audible; what has to be silent is what
                // comes after it.
                if (i >= 10)
                    tail = juce::jmax (tail, buffer.getMagnitude (0, 0, blockSize));
            }

            expectEquals (synth.activeVoiceCount(), 0);
            expectLessThan ((double) tail, 0.001);
        }

        beginTest (NI_TXT ("voice stealing keeps the count bounded"));

        {
            WavetableSynth synth;
            synth.prepare (48000.0);
            synth.setParams (sineParams());

            for (int i = 1; i <= kVoicesPerTrack + 8; ++i)
                synth.noteOn (i, 220.0f + (float) i, 100.0f);

            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
        }

        beginTest (NI_TXT ("the factory bank is twelve distinct sounds"));

        {
            const auto fallback = WavetableParams();
            const auto count = wavetable::builtInPresetCount();

            expectEquals (count, 12);

            std::vector<WavetableParams> bank;

            for (int i = 0; i < count; ++i)
            {
                auto params = fallback;
                wavetable::applyPreset (params, i);
                bank.push_back (params);

                expect (params.name.isNotEmpty(), NI_TXT ("every factory sound is named"));
                expect (! (params == fallback),
                        NI_TXT ("and none is left at the defaults"));
            }

            auto duplicates = 0;

            for (int i = 0; i < (int) bank.size(); ++i)
                for (int j = i + 1; j < (int) bank.size(); ++j)
                    if (bank[(size_t) i] == bank[(size_t) j])
                        ++duplicates;

            expectEquals (duplicates, 0, NI_TXT ("no two factory sounds are identical"));

            auto clamped = fallback;
            wavetable::applyPreset (clamped, 999);
            expectEquals (clamped.name, bank.back().name);
        }

        beginTest (NI_TXT ("a wavetable track reaches the schedule"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::wavetable;
            track.wavetable.oscs[0].tableIndex = 3;
            track.wavetable.oscs[0].position = 0.42f;
            track.wavetable.posEnvAmount = -0.3f;

            Note note;
            note.id = 1;
            note.durationTicks = 960;
            note.frequencyHz = 330.0;
            track.clips.front().notes = { note };

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::wavetable);
            expectEquals (schedule.tracks.front().wavetable.oscs[0].tableIndex, 3);
            expectWithinAbsoluteError ((double) schedule.tracks.front().wavetable.oscs[0].position,
                                       0.42, 1.0e-6);
            expectWithinAbsoluteError ((double) schedule.tracks.front().wavetable.posEnvAmount,
                                       -0.3, 1.0e-6);
            expectEquals ((int) schedule.tracks.front().events.size(), 2);
        }

        beginTest ("setNoteFrequency retunes a sounding wavetable voice");
        {
            WavetableSynth synth;
            synth.prepare (48000.0);
            WavetableParams params;
            params.oscs[0].tableIndex = 0;
            params.posEnvAmount = 0.0f;
            synth.setParams (params);
            synth.noteOn (1, 220.0f, 100.0f);
            juce::AudioBuffer<float> first (2, 9600);
            first.clear();
            synth.renderAdding (first, 0, first.getNumSamples(), 1.0f, 1.0f);
            synth.setNoteFrequency (1, 440.0f);
            juce::AudioBuffer<float> second (2, 9600);
            second.clear();
            synth.renderAdding (second, 0, second.getNumSamples(), 1.0f, 1.0f);
            expectWithinAbsoluteError (SynthTests::measureFrequency (first, 480, 9000),
                                       220.0, 1.5);
            expectWithinAbsoluteError (SynthTests::measureFrequency (second, 480, 9000),
                                       440.0, 2.5);
            synth.allNotesOff (true);
        }
    }
};

class DrumTests : public juce::UnitTest
{
public:
    DrumTests() : juce::UnitTest ("Drums", "nidaw") {}

    static std::vector<float> renderPad (const DrumKitParams& kit, int pad,
                                         float velocity, double seconds)
    {
        constexpr int blockSize = 256;

        DrumSynth synth;
        synth.prepare (48000.0);
        synth.setParams (kit);
        synth.noteOn (1, pad, velocity);

        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto total = (int) (seconds * 48000.0);
        std::vector<float> samples;
        samples.reserve ((size_t) total);

        for (int rendered = 0; rendered < total; )
        {
            buffer.clear();
            synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);

            for (int s = 0; s < blockSize && rendered < total; ++s, ++rendered)
                samples.push_back (buffer.getSample (0, s));
        }

        return samples;
    }

    static double rmsOf (const std::vector<float>& samples)
    {
        if (samples.empty())
            return 0.0;

        auto sum = 0.0;

        for (auto sample : samples)
            sum += (double) sample * (double) sample;

        return std::sqrt (sum / (double) samples.size());
    }

    static double peakOf (const std::vector<float>& samples)
    {
        auto peak = 0.0;

        for (auto sample : samples)
            peak = juce::jmax (peak, (double) std::abs (sample));

        return peak;
    }

    static int countUpwardCrossings (const std::vector<float>& samples, int from, int count)
    {
        auto crossings = 0;

        for (int i = from + 1; i < from + count && i < (int) samples.size(); ++i)
            if (samples[(size_t) (i - 1)] <= 0.0f && samples[(size_t) i] > 0.0f)
                ++crossings;

        return crossings;
    }

    static void renderFor (DrumSynth& synth, double seconds)
    {
        constexpr int blockSize = 256;
        juce::AudioBuffer<float> buffer (2, blockSize);
        const auto total = (int) (seconds * 48000.0);

        for (int rendered = 0; rendered < total; )
        {
            buffer.clear();
            synth.renderAdding (buffer, 0, blockSize, 1.0f, 1.0f);
            rendered += blockSize;
        }
    }

    void runTest() override
    {
        beginTest (NI_TXT ("a note's pad survives the project round trip"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::drums;

            Note hit;
            hit.id = 1;
            hit.durationTicks = 480;
            hit.frequencyHz = drumPadBaseHz ((int) DrumPad::snare);
            hit.pad = (int) DrumPad::snare;
            hit.velocity = 111;
            track.clips.front().notes = { hit };

            Project loaded;
            juce::String error;
            expect (projectFromJson (projectToJson (project), loaded, error), error);
            expectEquals ((int) loaded.tracks.size(), 1);

            if (! loaded.tracks.empty() && ! loaded.tracks.front().clips.empty())
            {
                const auto& notes = loaded.tracks.front().clips.front().notes;
                expectEquals ((int) notes.size(), 1);

                if (! notes.empty())
                {
                    expectEquals (notes.front().pad, (int) DrumPad::snare);
                    expectEquals (notes.front().velocity, 111);
                }
            }
        }

        beginTest (NI_TXT ("a project written before the kit loads as ordinary notes"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::drums;

            Note hit;
            hit.id = 1;
            hit.durationTicks = 480;
            hit.pad = (int) DrumPad::kick;
            track.clips.front().notes = { hit };
            track.drums.gain = 0.42f;      // something a default kit would not have

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
                    trackObject->removeProperty ("drums");

                    auto* clips = trackObject->getProperty ("clips").getArray();
                    auto* clipObject = clips != nullptr && ! clips->isEmpty()
                                           ? (*clips)[0].getDynamicObject()
                                           : nullptr;
                    auto* notes = clipObject != nullptr
                                      ? clipObject->getProperty ("notes").getArray()
                                      : nullptr;

                    if (notes != nullptr && ! notes->isEmpty())
                        (*notes)[0].getDynamicObject()->removeProperty ("pad");
                }
            }

            Project loaded;
            juce::String error;
            expect (projectFromJson (juce::JSON::toString (parsed), loaded, error), error);
            expectEquals ((int) loaded.tracks.size(), 1);

            if (! loaded.tracks.empty())
            {
                expect (loaded.tracks.front().drums == DrumKitParams(),
                        NI_TXT ("a missing kit falls back to the defaults"));

                if (! loaded.tracks.front().clips.empty()
                    && ! loaded.tracks.front().clips.front().notes.empty())
                    expectEquals (loaded.tracks.front().clips.front().notes.front().pad, -1);
            }
        }

        beginTest (NI_TXT ("every kit field survives the round trip"));

        {
            DrumKitParams kit;
            kit.name = NI_TXT ("测试鼓组");
            kit.pads[(int) DrumPad::kick].enabled = false;
            kit.pads[(int) DrumPad::kick].level = 0.25f;
            kit.pads[(int) DrumPad::snare].pitch = -5.0f;
            kit.pads[(int) DrumPad::openHat].decay = 2.5f;
            kit.pads[(int) DrumPad::clap].tone = 0.8f;
            kit.pads[(int) DrumPad::ride].snap = 0.1f;
            kit.gain = 0.7f;
            kit.masterPitch = 3.0f;
            kit.masterDecay = 1.7f;
            kit.width = 0.9f;

            const auto restored = drumKitFromVar (drumKitToVar (kit));
            expect (restored == kit, NI_TXT ("the whole kit comes back unchanged"));
            expectEquals (restored.name, NI_TXT ("测试鼓组"));
            expect (! restored.pads[(int) DrumPad::kick].enabled);
        }

        beginTest (NI_TXT ("the drum kind and the older kinds all round trip"));

        {
            const TrackKind kinds[] = { TrackKind::synth, TrackKind::sampler, TrackKind::audio,
                                        TrackKind::subtractive, TrackKind::wavetable,
                                        TrackKind::drums };

            for (int value = 0; value <= (int) TrackKind::drums; ++value)
            {
                auto project = makeProjectWithSynthTrack();
                auto parsed = juce::JSON::parse (projectToJson (project));
                auto* root = parsed.getDynamicObject();
                auto* tracks = root->getProperty ("tracks").getArray();
                (*tracks)[0].getDynamicObject()->setProperty ("kind", value);

                Project loaded;
                juce::String error;
                expect (projectFromJson (juce::JSON::toString (parsed), loaded, error), error);
                expectEquals ((int) loaded.tracks.size(), 1);

                if (! loaded.tracks.empty())
                {
                    expectEquals ((int) loaded.tracks.front().kind, value,
                                  NI_TXT ("serialised value ") + juce::String (value)
                                      + NI_TXT (" reads as ") + trackKindLabel (kinds[value]));
                    expect (loaded.tracks.front().kind == kinds[value]);
                }
            }
        }

        beginTest (NI_TXT ("the drum kit preset library stores whole kits"));

        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nidaw-drum-preset-test.json");
            file.deleteFile();

            {
                PresetStore store (file);
                store.load();

                DrumKitParams preset;
                preset.name = NI_TXT ("我的鼓组");
                preset.pads[(int) DrumPad::lowTom].pitch = 4.0f;
                preset.masterDecay = 2.0f;
                store.addDrumPreset (preset);

                expectEquals ((int) store.getDrumPresets().size(), 1);
                expectEquals (store.findDrumPresetByName (NI_TXT ("我的鼓组")), 0);
            }

            PresetStore reloaded (file);
            expect (reloaded.load());
            expectEquals ((int) reloaded.getDrumPresets().size(), 1);

            if (! reloaded.getDrumPresets().empty())
            {
                expectEquals (reloaded.getDrumPresets().front().name, NI_TXT ("我的鼓组"));
                expectWithinAbsoluteError ((double) reloaded.getDrumPresets().front()
                                                    .pads[(int) DrumPad::lowTom].pitch,
                                           4.0, 1.0e-6);
            }

            reloaded.renameDrumPreset (0, NI_TXT ("改名鼓组"));
            expectEquals (reloaded.getDrumPresets().front().name, NI_TXT ("改名鼓组"));
            expectEquals (reloaded.findDrumPresetByName (NI_TXT ("我的鼓组")), -1);

            {
                DrumKitParams replacement;
                replacement.name = NI_TXT ("改名鼓组");
                replacement.gain = 0.5f;
                reloaded.replaceDrumPreset (0, replacement);
            }

            expectEquals ((int) reloaded.getDrumPresets().size(), 1);
            expectWithinAbsoluteError ((double) reloaded.getDrumPresets().front().gain, 0.5, 1.0e-6);

            reloaded.removeDrumPreset (0);
            expectEquals ((int) reloaded.getDrumPresets().size(), 0);

            // A presets.json written before the drum machine has no drumPatches.
            file.replaceWithText ("{\"fmPatches\":[]}");

            PresetStore legacy (file);
            expect (legacy.load());
            expectEquals ((int) legacy.getDrumPresets().size(), 0,
                          NI_TXT ("a missing drum section reads as empty"));

            file.deleteFile();
        }

        beginTest (NI_TXT ("all twelve drums make a sound, and no two are the same"));

        {
            const auto kit = DrumKitParams();
            std::vector<std::vector<float>> rendered;

            for (int pad = 0; pad < kDrumPads; ++pad)
            {
                auto samples = renderPad (kit, pad, 127.0f, 0.28);
                const auto rms = rmsOf (samples);

                expectGreaterThan (rms, 0.004,
                                   NI_TXT ("drum ") + drumPadName (pad)
                                       + NI_TXT (" makes a sound (rms ")
                                       + juce::String (rms, 4) + ")");
                rendered.push_back (std::move (samples));
            }

            auto identical = 0;

            for (int i = 0; i < kDrumPads; ++i)
                for (int j = i + 1; j < kDrumPads; ++j)
                    if (rendered[(size_t) i] == rendered[(size_t) j])
                        ++identical;

            expectEquals (identical, 0, NI_TXT ("no two drums render identically"));
        }

        beginTest (NI_TXT ("the kick sits at its base pitch"));

        {
            const auto kick = renderPad (DrumKitParams(), (int) DrumPad::kick, 127.0f, 1.0);

            // Once the pitch sweep has settled (after ~0.25 s) the sine runs at
            // 50 Hz, so half a second holds about twenty-five upward crossings.
            const auto crossings = countUpwardCrossings (kick, (int) (0.25 * 48000.0),
                                                         (int) (0.5 * 48000.0));

            expectWithinAbsoluteError ((double) crossings, 25.0, 4.0);
        }

        beginTest (NI_TXT ("pitch really moves the drum"));

        {
            const auto low = renderPad (DrumKitParams(), (int) DrumPad::kick, 127.0f, 1.0);

            auto highKit = DrumKitParams();
            highKit.pads[(int) DrumPad::kick].pitch = 12.0f;
            const auto high = renderPad (highKit, (int) DrumPad::kick, 127.0f, 1.0);

            const auto lowCrossings  = countUpwardCrossings (low, (int) (0.25 * 48000.0),
                                                             (int) (0.5 * 48000.0));
            const auto highCrossings = countUpwardCrossings (high, (int) (0.25 * 48000.0),
                                                             (int) (0.5 * 48000.0));

            expectGreaterThan ((double) highCrossings, (double) lowCrossings * 1.6,
                               NI_TXT ("an octave up doubles the crossings (")
                                   + juce::String (lowCrossings) + NI_TXT (" vs ")
                                   + juce::String (highCrossings) + ")");
        }

        beginTest (NI_TXT ("velocity scales a hit"));

        {
            const auto loud = peakOf (renderPad (DrumKitParams(), (int) DrumPad::kick, 127.0f, 0.3));
            const auto soft = peakOf (renderPad (DrumKitParams(), (int) DrumPad::kick, 40.0f, 0.3));

            expectGreaterThan (loud, 0.05, NI_TXT ("a full hit is loud"));
            expectGreaterThan (loud, soft * 2.0,
                               NI_TXT ("a soft hit is much quieter (")
                                   + juce::String (loud, 4) + NI_TXT (" vs ")
                                   + juce::String (soft, 4) + ")");
        }

        beginTest (NI_TXT ("a hit is a one-shot that ends on its own"));

        {
            DrumSynth synth;
            synth.prepare (48000.0);
            synth.setParams (DrumKitParams());
            synth.noteOn (1, (int) DrumPad::kick, 127.0f);

            // No note-off is ever sent: the voice has to ring out and free itself.
            renderFor (synth, 3.0);
            expectEquals (synth.activeVoiceCount(), 0,
                          NI_TXT ("the voice released itself without a note-off"));
        }

        beginTest (NI_TXT ("voice stealing keeps the count bounded"));

        {
            DrumSynth synth;
            synth.prepare (48000.0);
            synth.setParams (DrumKitParams());

            // Pads that never choke each other, so every hit keeps its voice.
            const int safePads[] = { 0, 1, 2, 3, 6, 7, 8, 9, 10, 11 };

            for (int i = 1; i <= kVoicesPerTrack + 8; ++i)
                synth.noteOn (i, safePads[i % 10], 100.0f);

            expectEquals (synth.activeVoiceCount(), kVoicesPerTrack);
        }

        beginTest (NI_TXT ("a closed hat chokes a ringing open hat"));

        {
            DrumSynth synth;
            synth.prepare (48000.0);
            synth.setParams (DrumKitParams());

            synth.noteOn (1, (int) DrumPad::openHat, 127.0f);
            renderFor (synth, 0.10);

            expectEquals (synth.getVoicePadForTest (1), (int) DrumPad::openHat,
                          NI_TXT ("the open hat is ringing"));

            synth.noteOn (2, (int) DrumPad::closedHat, 127.0f);
            renderFor (synth, 0.05);

            expectEquals (synth.getVoicePadForTest (1), -1,
                          NI_TXT ("the closed hat cut the open hat short"));
        }

        beginTest (NI_TXT ("a drum track reaches the schedule through its pads"));

        {
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

            auto& track = project.tracks.front();
            track.kind = TrackKind::drums;
            track.drums.gain = 0.6f;

            Note first;
            first.id = 1;
            first.startTick = 0;
            first.durationTicks = 480;
            first.frequencyHz = drumPadBaseHz ((int) DrumPad::kick);
            first.pad = (int) DrumPad::kick;
            first.velocity = 90;

            // Same pad, a wildly different frequency: the drum engine reads the
            // pad, not the frequency, so both hits are the same drum.
            Note second = first;
            second.id = 2;
            second.startTick = 960;
            second.frequencyHz = 4321.0;

            Note noPad = first;
            noPad.id = 3;
            noPad.startTick = 1920;
            noPad.pad = -1;

            track.clips.front().notes = { first, second, noPad };

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::drums);
            expectWithinAbsoluteError ((double) schedule.tracks.front().drums.gain, 0.6, 1.0e-6);

            // Two playable hits -> four events; the pad-less note is skipped.
            expectEquals ((int) schedule.tracks.front().events.size(), 4);

            for (const auto& event : schedule.tracks.front().events)
                expectEquals (event.pad, (int) DrumPad::kick);
        }
    }
};

class SamplerTests : public juce::UnitTest
{
public:
    SamplerTests() : juce::UnitTest ("Sampler", "nidaw") {}

    /** A sine at `hz`, `seconds` long - a stand-in for an imported recording. */
    static std::shared_ptr<const LoadedSample> makeSineSample (double hz, double seconds)
    {
        auto loaded = std::make_shared<LoadedSample>();
        loaded->sourceSampleRate = 48000.0;
        loaded->filePath = "（内置测试样本）";

        const auto length = (int) (48000.0 * seconds);
        loaded->audio.setSize (1, length);
        auto* data = loaded->audio.getWritePointer (0);

        for (int i = 0; i < length; ++i)
            data[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0);

        return loaded;
    }

    static std::shared_ptr<const LoadedSample> makeRampSample (double seconds)
    {
        auto loaded = std::make_shared<LoadedSample>();
        loaded->sourceSampleRate = 48000.0;
        loaded->filePath = "（内置斜坡样本）";
        const auto length = (int) (48000.0 * seconds);
        loaded->audio.setSize (1, length);
        auto* data = loaded->audio.getWritePointer (0);

        for (int i = 0; i < length; ++i)
            data[i] = (float) i / (float) juce::jmax (1, length - 1);

        return loaded;
    }

    static double measureFrequency (const juce::AudioBuffer<float>& buffer, int start, int end)
    {
        const auto* data = buffer.getReadPointer (0);
        double first = -1.0, last = -1.0;
        int crossings = 0;

        for (int i = start + 1; i < end; ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                const auto previous = (double) data[i - 1];
                const auto current = (double) data[i];
                const auto fraction = previous == current ? 0.0 : (-previous) / (current - previous);
                const auto time = ((double) (i - 1) + fraction) / 48000.0;

                if (first < 0.0)
                    first = time;
                else
                    last = time;

                ++crossings;
            }
        }

        if (crossings < 2 || last <= first)
            return 0.0;

        return (double) (crossings - 1) / (last - first);
    }

    /** A player set up to play the sample at its recorded pitch. */
    static void prepare (SamplePlayer& player, std::shared_ptr<const LoadedSample> sample)
    {
        player.prepare (48000.0);

        SamplerParams params;
        params.rootHz = 440.0;
        params.attack = 0.001f;
        params.decay = 0.0f;
        params.sustain = 1.0f;
        params.release = 0.02f;
        player.setParams (params);
        player.setSample (std::move (sample));
    }

    void runTest() override
    {
        beginTest ("a note plays the sample at the pitch it asks for");

        {
            // The sample is a 440 Hz sine; asking for 880 must play it twice as
            // fast, asking for 220 half as fast.
            for (const auto pair : { std::pair<double, double> { 880.0, 880.0 },
                                     { 220.0, 220.0 },
                                     { 440.0, 440.0 } })
            {
                SamplePlayer player;
                prepare (player, makeSineSample (440.0, 1.0));
                player.noteOn (1, (float) pair.first, 127.0f);

                juce::AudioBuffer<float> buffer (2, 48000);
                buffer.clear();
                player.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);

                const auto measured = measureFrequency (buffer, 480, 24000);
                expectWithinAbsoluteError (measured, pair.second, 1.0);
            }
        }

        beginTest ("the root pitch decides how fast the sample runs");

        {
            SamplePlayer player;
            prepare (player, makeSineSample (440.0, 1.0));

            // Marking the sample as an A3 means an A4 note plays it an octave up.
            auto params = player.getParams();
            params.rootHz = 220.0;
            player.setParams (params);
            player.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 48000);
            buffer.clear();
            player.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);

            expectWithinAbsoluteError (measureFrequency (buffer, 480, 24000), 880.0, 1.0);
        }

        beginTest ("looping keeps a short sample sounding");

        {
            auto paramsFor = [] (bool looped)
            {
                SamplerParams params;
                params.rootHz = 440.0;
                params.attack = 0.001f;
                params.decay = 0.0f;
                params.sustain = 1.0f;
                params.release = 0.02f;
                params.looped = looped;
                params.loopStart = 0.0;
                params.loopEnd = 1.0;
                return params;
            };

            SamplePlayer looped, oneShot;
            looped.prepare (48000.0);
            oneShot.prepare (48000.0);
            looped.setParams (paramsFor (true));
            oneShot.setParams (paramsFor (false));

            auto shortSample = makeSineSample (440.0, 0.05);
            looped.setSample (shortSample);
            oneShot.setSample (std::move (shortSample));

            looped.noteOn (1, 440.0f, 127.0f);
            oneShot.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 48000);

            for (int block = 0; block < 10; ++block)
            {
                buffer.clear();
                looped.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
                oneShot.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            }

            expect (looped.isActive(), "a looped sample keeps sounding past its end");
            expect (! oneShot.isActive(), "an unlooped sample stops at its end");

            buffer.clear();
            looped.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            expectGreaterThan (buffer.getRMSLevel (0, 0, 4800), 0.01f);
        }

        beginTest ("the sample region start and end are honoured");

        {
            SamplePlayer player;
            player.prepare (48000.0);
            SamplerParams params;
            params.rootHz = 440.0;
            params.attack = 0.001f;
            params.decay = 0.0f;
            params.sustain = 1.0f;
            params.release = 0.02f;
            params.sampleStart = 0.25;
            params.sampleEnd = 0.5;
            player.setParams (params);
            player.setSample (makeRampSample (1.0));
            player.noteOn (1, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 24000);
            buffer.clear();
            player.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);

            expectWithinAbsoluteError (buffer.getSample (0, 1000), 0.1625f, 0.01f);

            buffer.clear();
            player.renderAdding (buffer, 0, buffer.getNumSamples(), 1.0f, 1.0f);
            expect (! player.isActive(), "playback stops at the region end");
        }

        beginTest ("the envelope releases and frees the voice");

        {
            SamplePlayer player;
            prepare (player, makeSineSample (440.0, 2.0));
            player.noteOn (7, 440.0f, 127.0f);

            juce::AudioBuffer<float> buffer (2, 4800);
            buffer.clear();
            player.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            expect (player.isActive());

            player.noteOff (7);

            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                player.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            }

            expect (! player.isActive(), "the voice is freed after the release");
        }

        beginTest ("an audio file loads from disk and its sampler settings round trip");

        {
            // Write a short WAV, read it back through the loader, then push the
            // whole project through JSON.
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nidaw-sampler-test.wav");
            file.deleteFile();

            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                expect (stream != nullptr);

                if (stream != nullptr)
                {
                    const auto options = juce::AudioFormatWriterOptions{}
                                             .withSampleFormat (
                                                 juce::AudioFormatWriterOptions::SampleFormat::integral)
                                             .withSampleRate (44100.0)
                                             .withNumChannels (1)
                                             .withBitsPerSample (16);
                    auto writer = wav.createWriterFor (stream, options);

                    if (writer != nullptr)
                    {
                        juce::AudioBuffer<float> tone (1, 4410);
                        tone.clear();

                        for (int i = 0; i < tone.getNumSamples(); ++i)
                            tone.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                     * 220.0 * i / 44100.0));

                        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
                    }
                }
            }

            juce::String error;
            auto loaded = loadSampleFile (file.getFullPathName(), error);

            expect (loaded != nullptr, "the WAV loads: " + error);

            if (loaded != nullptr)
            {
                expectEquals (loaded->audio.getNumSamples(), 4410);
                expectWithinAbsoluteError (loaded->sourceSampleRate, 44100.0, 1.0e-9);
            }

            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();
            track.kind = TrackKind::sampler;
            track.sampler.filePath = file.getFullPathName();
            track.sampler.fileName = file.getFileName();
            track.sampler.rootHz = 220.0;
            track.sampler.looped = true;
            track.sampler.loopStart = 0.25;
            track.sampler.loopEnd = 0.75;
            track.sampler.attack = 0.03f;
            track.sampler.lfoRateHz = 5.5f;

            Project reloaded;
            expect (projectFromJson (projectToJson (project), reloaded, error), error);
            expectEquals ((int) reloaded.tracks.size(), 1);
            expect (reloaded.tracks.front().kind == TrackKind::sampler);
            expectEquals (reloaded.tracks.front().sampler.filePath, track.sampler.filePath);
            expectEquals (reloaded.tracks.front().sampler.fileName, track.sampler.fileName);
            expectWithinAbsoluteError (reloaded.tracks.front().sampler.rootHz, 220.0, 1.0e-9);
            expect (reloaded.tracks.front().sampler.looped);
            expectWithinAbsoluteError (reloaded.tracks.front().sampler.loopStart, 0.25, 1.0e-9);
            expectWithinAbsoluteError (reloaded.tracks.front().sampler.loopEnd, 0.75, 1.0e-9);
            expectWithinAbsoluteError ((double) reloaded.tracks.front().sampler.attack, 0.03, 1.0e-6);
            expectWithinAbsoluteError ((double) reloaded.tracks.front().sampler.lfoRateHz, 5.5, 1.0e-6);

            // The schedule carries the decoded sample for a sampler track.
            const auto schedule = buildSchedule (reloaded);
            expectEquals ((int) schedule.tracks.size(), 1);
            expect (schedule.tracks.front().kind == TrackKind::sampler);
            expect (schedule.tracks.front().sample != nullptr);
            expectEquals (schedule.tracks.front().sample->audio.getNumSamples(), 4410);

            file.deleteFile();
        }
    }
};

//==============================================================================
class GranularTests : public juce::UnitTest
{
public:
    GranularTests() : juce::UnitTest ("Granular", "nidaw") {}

    /** A sine at `hz`, `seconds` long - a stand-in for an imported recording. */
    static std::shared_ptr<const LoadedSample> makeSineSample (double hz, double seconds)
    {
        auto loaded = std::make_shared<LoadedSample>();
        loaded->sourceSampleRate = 48000.0;
        loaded->filePath = NI_TXT ("（内置测试样本）");

        const auto length = (int) (48000.0 * seconds);
        loaded->audio.setSize (1, length);
        auto* data = loaded->audio.getWritePointer (0);

        for (int i = 0; i < length; ++i)
            data[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0);

        return loaded;
    }

    /** A sample whose pitch climbs from 200 Hz to 1200 Hz: which way a grain reads
        it is then audible - and measurable. */
    static std::shared_ptr<const LoadedSample> makeRampSample (double seconds)
    {
        auto loaded = std::make_shared<LoadedSample>();
        loaded->sourceSampleRate = 48000.0;
        loaded->filePath = NI_TXT ("（内置测试扫频）");

        const auto length = (int) (48000.0 * seconds);
        loaded->audio.setSize (1, length);
        auto* data = loaded->audio.getWritePointer (0);

        auto phase = 0.0;

        for (int i = 0; i < length; ++i)
        {
            const auto hz = 200.0 + 1000.0 * (double) i / juce::jmax (1, length - 1);
            data[i] = (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
        }

        return loaded;
    }

    /** What the sampler tests use: positive-going zero crossings, sub-sample
        accurate. A windowed sine still crosses zero where the sine does. */
    static double measureFrequency (const std::vector<float>& data, int from, int to)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;

        for (int i = juce::jmax (1, from); i < to && i < (int) data.size(); ++i)
        {
            if (data[(size_t) (i - 1)] <= 0.0f && data[(size_t) i] > 0.0f)
            {
                const auto previous = (double) data[(size_t) (i - 1)];
                const auto current = (double) data[(size_t) i];
                const auto fraction = previous == current ? 0.0 : (-previous) / (current - previous);
                const auto time = ((double) (i - 1) + fraction) / 48000.0;

                if (first < 0.0)
                    first = time;
                else
                    last = time;

                ++crossings;
            }
        }

        if (crossings < 2 || last <= first)
            return 0.0;

        return (double) (crossings - 1) / (last - first);
    }

    /** Parameters with all the random bits off, so a test measures the arithmetic
        rather than the dice. */
    static GranularParams steadyParams()
    {
        GranularParams params;
        params.position   = 0.5f;
        params.scanSpeed  = 0.0f;
        params.grainMs    = 120.0f;
        params.densityHz  = 20.0f;
        params.spray      = 0.0f;
        params.spacingJitter = 0.0f;
        params.pitchJitterCents = 0.0f;
        params.reverseProb = 0.0f;
        params.posEnvAmount = 0.0f;
        params.filterCutoffHz = 20000.0f;
        params.filterSlopeDb  = 12;
        params.stereoSpread   = 0.0f;
        params.ampAttack  = 0.001f;
        params.ampDecay   = 0.0f;
        params.ampSustain = 1.0f;
        params.ampRelease = 0.02f;
        params.gain       = 1.0f;
        return params;
    }

    static GranularSynth makeSynth (const GranularParams& params,
                                    std::shared_ptr<const LoadedSample> sample)
    {
        GranularSynth synth;
        synth.prepare (48000.0);
        synth.setParams (params);
        synth.setSample (std::move (sample));
        return synth;
    }

    static std::vector<float> renderSeconds (GranularSynth& synth, double seconds)
    {
        constexpr int block = 512;
        const auto total = (int) (seconds * 48000.0);
        std::vector<float> samples;
        samples.reserve ((size_t) total);
        juce::AudioBuffer<float> buffer (2, block);

        // Exactly `total` samples, so a test that reads the scan position after one
        // second really is after one second (the synth counts what it renders).
        for (int done = 0; done < total; )
        {
            const auto n = juce::jmin (block, total - done);
            buffer.clear();
            synth.renderAdding (buffer, 0, n, 1.0f, 1.0f);

            for (int i = 0; i < n; ++i)
                samples.push_back (buffer.getSample (0, i));

            done += n;
        }

        return samples;
    }

    static double rmsOf (const std::vector<float>& samples, int from, int to)
    {
        auto sum = 0.0;
        auto count = 0;

        for (int i = juce::jmax (0, from); i < to && i < (int) samples.size(); ++i, ++count)
            sum += (double) samples[(size_t) i] * (double) samples[(size_t) i];

        return count == 0 ? 0.0 : std::sqrt (sum / (double) count);
    }

    /** One second of 440 Hz, 16 bit, written to a real file (buildSchedule reads
        files, so a path is not enough). */
    static juce::File writeTempWav()
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nidaw-granular-test.wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return {};

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleFormat (
                                     juce::AudioFormatWriterOptions::SampleFormat::integral)
                                 .withSampleRate (48000.0)
                                 .withNumChannels (1)
                                 .withBitsPerSample (16);
        auto writer = format.createWriterFor (stream, options);

        if (writer == nullptr)
            return {};

        juce::AudioBuffer<float> buffer (1, 4800);
        auto* data = buffer.getWritePointer (0);

        for (int i = 0; i < 4800; ++i)
            data[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0);

        writer->writeFromAudioSampleBuffer (buffer, 0, 4800);
        writer.reset();
        return file;
    }

    void runTest() override
    {
        beginTest (NI_TXT ("默认值与相等判断"));

        {
            const GranularParams defaults;
            expectEquals (defaults.position, 0.25f);
            expectEquals (defaults.grainMs, 120.0f);
            expectEquals (defaults.densityHz, 20.0f);
            expectEquals (defaults.spray, 0.05f);
            expect (defaults == GranularParams());

            auto changed = defaults;
            changed.grainMs = 250.0f;
            expect (! (changed == defaults));
        }

        beginTest (NI_TXT ("没有音源就一声不响"));

        {
            auto synth = makeSynth (steadyParams(), nullptr);
            synth.noteOn (1, 440.0f, 127.0f);

            expect (! synth.isActive(), NI_TXT ("没有文件就没有声部"));

            const auto rendered = renderSeconds (synth, 0.1);
            expectEquals (rmsOf (rendered, 0, (int) rendered.size()), 0.0);
        }

        beginTest (NI_TXT ("密度决定颗粒的疏密，粒度决定每颗多长"));

        {
            auto params = steadyParams();
            params.grainMs = 100.0f;

            auto synth = makeSynth (params, makeSineSample (440.0, 2.0));
            synth.noteOn (1, 440.0f, 127.0f);
            juce::ignoreUnused (renderSeconds (synth, 1.0));

            const auto started = synth.getStartedGrainCountForTest();
            expect (started >= 18 && started <= 22,
                    NI_TXT ("20 颗/秒 × 1 秒 ≈ 20 颗（实际 ") + juce::String ((int) started)
                        + NI_TXT ("）"));
            expectWithinAbsoluteError ((double) synth.getLongestGrainSecondsForTest(),
                                       0.1, 0.005);

            auto denser = params;
            denser.densityHz = 40.0f;

            auto second = makeSynth (denser, makeSineSample (440.0, 2.0));
            second.noteOn (1, 440.0f, 127.0f);
            juce::ignoreUnused (renderSeconds (second, 1.0));

            const auto more = second.getStartedGrainCountForTest();
            expect (more >= 36 && more <= 44,
                    NI_TXT ("密度翻倍，颗粒数也翻倍（实际 ") + juce::String ((int) more)
                        + NI_TXT ("）"));
        }

        beginTest (NI_TXT ("音符频率决定颗粒的播放速率（精确频率）"));

        {
            // The file is a 440 Hz sine and the root is 440 Hz, so asking for
            // 436.04 Hz must read it at 436.04 Hz - the sampler's own arithmetic.
            // One long grain at a time, measured in its middle: with a dense cloud
            // every grain restarts at the same source phase, and the zero crossings
            // then lock onto the grain rate instead of the pitch.
            auto params = steadyParams();
            params.densityHz = 2.0f;
            params.grainMs = 400.0f;
            params.position = 0.25f;

            auto synth = makeSynth (params, makeSineSample (440.0, 2.0));
            synth.noteOn (1, 436.043f, 127.0f);

            const auto rendered = renderSeconds (synth, 1.0);
            const auto measured = measureFrequency (rendered, 4800, 16800);

            expectWithinAbsoluteError (measured, 436.043, 1.5);

            // Twice the frequency, twice the rate.
            auto octave = makeSynth (params, makeSineSample (440.0, 2.0));
            octave.noteOn (1, 872.086f, 127.0f);

            const auto up = renderSeconds (octave, 1.0);
            expectWithinAbsoluteError (measureFrequency (up, 4800, 16800), 872.086, 3.0);
        }

        beginTest (NI_TXT ("扫描到端点折返（不是跳回另一头）"));

        {
            auto params = steadyParams();
            params.position = 0.0f;
            params.scanSpeed = 1.0f;

            auto synth = makeSynth (params, makeSineSample (440.0, 4.0));
            synth.noteOn (1, 440.0f, 127.0f);

            juce::ignoreUnused (renderSeconds (synth, 1.0));
            expectWithinAbsoluteError ((double) synth.getScanFractionForTest (1), 1.0, 0.001);

            juce::ignoreUnused (renderSeconds (synth, 0.5));
            expectWithinAbsoluteError ((double) synth.getScanFractionForTest (1), 0.5, 0.002);

            juce::ignoreUnused (renderSeconds (synth, 0.5));
            expectWithinAbsoluteError ((double) synth.getScanFractionForTest (1), 0.0, 0.002);
        }

        beginTest (NI_TXT ("位置抖动把颗粒散开"));

        {
            auto params = steadyParams();
            params.densityHz = 100.0f;
            params.spray = 1.0f;

            auto synth = makeSynth (params, makeSineSample (440.0, 4.0));
            synth.noteOn (1, 440.0f, 127.0f);
            juce::ignoreUnused (renderSeconds (synth, 0.5));

            auto lowest = 1.0f;
            auto highest = 0.0f;

            for (int i = 0; i < synth.getPublishedGrainCount(); ++i)
            {
                const auto position = synth.getPublishedGrainPosition (i);
                lowest = juce::jmin (lowest, position);
                highest = juce::jmax (highest, position);
            }

            expect (highest - lowest > 0.3f,
                    NI_TXT ("抖动 1.0 时颗粒的位置散得很开（跨 ")
                        + juce::String (highest - lowest, 2) + NI_TXT ("）"));

            // …and with the spray off they all sit on the same spot.
            auto still = makeSynth (steadyParams(), makeSineSample (440.0, 4.0));
            still.noteOn (1, 440.0f, 127.0f);
            juce::ignoreUnused (renderSeconds (still, 0.5));

            auto spread = 0.0f;

            for (int i = 0; i < still.getPublishedGrainCount(); ++i)
                spread = juce::jmax (spread,
                                     std::abs (still.getPublishedGrainPosition (i) - 0.5f));

            // Without the spray the only spread left is how far a grain has read
            // while it is alive: 120 ms of a 4 s file is 0.03.
            expect (spread < 0.05f,
                    NI_TXT ("抖动 0 时颗粒都挤在位置 0.5 附近（跨 ")
                        + juce::String (spread, 3) + NI_TXT ("）"));
        }

        beginTest (NI_TXT ("反向概率让颗粒倒着读"));

        {
            // The source climbs from 200 Hz to 1200 Hz, so a forward grain's pitch
            // rises across its life and a reversed one falls.
            auto params = steadyParams();
            params.grainMs = 400.0f;
            params.densityHz = 1.0f;
            params.position = 0.05f;

            auto forward = makeSynth (params, makeRampSample (2.0));
            forward.noteOn (1, 440.0f, 127.0f);
            const auto up = renderSeconds (forward, 0.4);

            // Measured inside the grain, away from the ends: a Hann window is
            // silent exactly at the two ends.
            const auto early = measureFrequency (up, 2400, 9600);
            const auto late  = measureFrequency (up, 14400, 18240);

            expect (late > early * 1.2, NI_TXT ("正向颗粒的扫频是往上走的（")
                                            + juce::String (early, 1) + NI_TXT (" → ")
                                            + juce::String (late, 1) + NI_TXT (" Hz）"));

            params.position = 0.55f;
            params.reverseProb = 1.0f;

            auto backwards = makeSynth (params, makeRampSample (2.0));
            backwards.noteOn (1, 440.0f, 127.0f);
            const auto down = renderSeconds (backwards, 0.4);

            const auto earlyBack = measureFrequency (down, 2400, 9600);
            const auto lateBack  = measureFrequency (down, 14400, 18240);

            expect (lateBack < earlyBack * 0.9, NI_TXT ("反向颗粒倒着读，扫频往下走（")
                                                     + juce::String (earlyBack, 1) + NI_TXT (" → ")
                                                     + juce::String (lateBack, 1) + NI_TXT (" Hz）"));
        }

        beginTest (NI_TXT ("松开以后衰减到静音"));

        {
            auto params = steadyParams();
            params.ampRelease = 0.05f;

            auto synth = makeSynth (params, makeSineSample (440.0, 2.0));
            synth.noteOn (1, 440.0f, 127.0f);
            const auto held = renderSeconds (synth, 0.2);

            expect (rmsOf (held, 0, (int) held.size()) > 0.01,
                    NI_TXT ("按住时在响（RMS ") + juce::String (rmsOf (held, 0, (int) held.size()), 4)
                        + NI_TXT ("）"));

            synth.noteOff (1);
            const auto tail = renderSeconds (synth, 0.6);

            expect (rmsOf (tail, (int) tail.size() - 4800, (int) tail.size()) < 0.0005,
                    NI_TXT ("释放之后衰减到听不见"));
            expect (! synth.isActive(), NI_TXT ("声部也回收了"));
        }

        beginTest (NI_TXT ("16 声部，每声部最多 32 颗颗粒"));

        {
            auto params = steadyParams();
            params.densityHz = 100.0f;
            params.grainMs = 500.0f;

            auto synth = makeSynth (params, makeSineSample (440.0, 4.0));

            for (int note = 0; note < 17; ++note)
                synth.noteOn (100 + note, 200.0f + (float) note * 20.0f, 100.0f);

            juce::ignoreUnused (renderSeconds (synth, 0.5));

            expect (synth.activeVoiceCount() <= kVoicesPerTrack,
                    NI_TXT ("同时响的声部不超过 16（实际 ")
                        + juce::String (synth.activeVoiceCount()) + NI_TXT ("）"));
            expect (synth.activeGrainCount() <= GranularSynth::kMaxGrainsPerVoice
                                                  * kVoicesPerTrack,
                    NI_TXT ("颗粒总数被每声部的上限压住"));
            expect (synth.activeGrainCount() >= 16 * 20,
                    NI_TXT ("每个声部都撒到了上限附近（总共 ")
                        + juce::String (synth.activeGrainCount()) + NI_TXT (" 颗）"));
        }

        beginTest (NI_TXT ("试听通道：松开以后云端停下来"));

        {
            // The editor's own keyboard goes through the audition voice: pressing a
            // key triggers it, letting the key go releases it. The release side used
            // to miss this instrument out, so the cloud simply never stopped.
            PlaybackEngine engine;
            engine.prepare (48000.0, 512);

            auto params = steadyParams();
            params.densityHz = 40.0f;

            engine.triggerPreviewGranularNote (params, makeSineSample (440.0, 2.0), 523.25, 110.0f);

            for (int i = 0; i < 40; ++i)
                engine.renderNextBlock (512);

            expect (engine.getPreviewGranularGrainCount() > 0,
                    NI_TXT ("按住的时候试听云端在响（")
                        + juce::String (engine.getPreviewGranularGrainCount()) + NI_TXT (" 颗）"));

            engine.releasePreviewNote();

            for (int i = 0; i < 80; ++i)
                engine.renderNextBlock (512);

            expect (engine.getPreviewGranularGrainCount() == 0,
                    NI_TXT ("松开以后云端停了（这正是「一直响」那个 bug）"));
            expect (engine.getActiveVoiceCount() == 0,
                    NI_TXT ("预览声部也回收了（实际 ")
                        + juce::String (engine.getActiveVoiceCount()) + NI_TXT (" 个）"));
        }

        beginTest (NI_TXT ("渲染两次逐样本一致（实时与导出同一条路）"));

        {
            auto params = steadyParams();
            params.densityHz = 40.0f;
            params.spray = 0.4f;
            params.pitchJitterCents = 30.0f;

            auto first = makeSynth (params, makeSineSample (440.0, 2.0));
            auto second = makeSynth (params, makeSineSample (440.0, 2.0));

            first.noteOn (7, 523.25f, 100.0f);
            second.noteOn (7, 523.25f, 100.0f);

            const auto a = renderSeconds (first, 0.5);
            const auto b = renderSeconds (second, 0.5);

            expect (a == b, NI_TXT ("两次渲染一模一样"));
        }

        beginTest (NI_TXT ("调度带上颗粒参数与音源"));

        {
            const auto wav = writeTempWav();

            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::granular);
            auto* track = project.findTrack (id);

            auto params = steadyParams();
            params.grainMs = 77.0f;
            params.densityHz = 33.0f;
            track->granular = params;

            // Nothing imported yet: the parameters still travel, the sample is null.
            auto schedule = buildSchedule (project);
            expect (schedule.tracks.size() == 1);
            expect (schedule.tracks.front().granular == params,
                    NI_TXT ("颗粒参数进了调度"));
            expect (schedule.tracks.front().sample == nullptr,
                    NI_TXT ("还没导入文件时音源是空的"));

            // A path that does not exist: still null, and no crash.
            track->granular.filePath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                           .getChildFile ("nidaw-not-here-9f8e.wav")
                                           .getFullPathName();
            expect (buildSchedule (project).tracks.front().sample == nullptr,
                    NI_TXT ("文件不在时不崩，只是没声音"));

            if (wav.existsAsFile())
            {
                track->granular.filePath = wav.getFullPathName();
                const auto loaded = buildSchedule (project).tracks.front().sample;

                expect (loaded != nullptr && loaded->audio.getNumSamples() > 0,
                        NI_TXT ("导入过的文件被解码进调度（") + juce::String (loaded != nullptr
                                                                       ? loaded->audio.getNumSamples()
                                                                       : 0)
                            + NI_TXT (" 点）"));
            }
        }

        beginTest (NI_TXT ("工程往返：颗粒段在，旧工程缺字段取默认"));

        {
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::granular);
            auto* track = project.findTrack (id);

            GranularParams params;
            params.name = NI_TXT ("云");
            params.filePath = "C:\\music\\pad.wav";
            params.fileName = "pad.wav";
            params.position = 0.123f;
            params.scanSpeed = -0.75f;
            params.grainMs = 250.0f;
            params.densityHz = 64.0f;
            params.spray = 0.75f;
            params.spacingJitter = 0.5f;
            params.pitchJitterCents = 120.0f;
            params.reverseProb = 0.25f;
            params.posEnvAmount = -0.5f;
            params.filterMode = FilterMode::bandPass;
            params.filterSlopeDb = 12;
            params.filterCutoffHz = 2500.0f;
            params.filterResonance = 3.5f;
            params.filterKeyTrack = 0.4f;
            params.stereoSpread = 0.9f;
            params.gain = 0.8f;
            track->granular = params;

            const auto json = projectToJson (project);
            Project reloaded;
            juce::String error;

            expect (projectFromJson (json, reloaded, error), error);
            expectEquals ((int) reloaded.tracks.size(), 1);

            if (! reloaded.tracks.empty())
            {
                expect (reloaded.tracks.front().kind == TrackKind::granular,
                        NI_TXT ("轨道类型还是粒子合成器"));
                expect (reloaded.tracks.front().granular == params,
                        NI_TXT ("每个字段都原样读回来"));
            }

            // An old project has no "granular" object at all: the defaults stand in.
            auto parsed = juce::JSON::parse (json);
            auto* trackObject = parsed.getDynamicObject() != nullptr
                                    ? parsed.getDynamicObject()->getProperty ("tracks").getArray()
                                    : nullptr;

            if (trackObject != nullptr && ! trackObject->isEmpty())
                if (auto* first = (*trackObject)[0].getDynamicObject())
                    first->removeProperty ("granular");

            Project withoutSection;
            expect (projectFromJson (juce::JSON::toString (parsed, false), withoutSection, error),
                    error);

            if (! withoutSection.tracks.empty())
                expect (withoutSection.tracks.front().granular == GranularParams(),
                        NI_TXT ("缺 granular 段时读成默认值"));
        }

        beginTest (NI_TXT ("预设库：增 / 查 / 改 / 删与往返"));

        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nidaw-granular-presets.json");
            file.deleteFile();

            GranularParams preset;
            preset.name = NI_TXT ("我的粒子音色 1");
            preset.filePath = "C:\\music\\vox.wav";
            preset.fileName = "vox.wav";
            preset.grainMs = 210.0f;
            preset.spray = 0.6f;

            {
                PresetStore store (file);
                store.load();

                expectEquals ((int) store.getGranularPresets().size(), 0);
                store.addGranularPreset (preset);
                expectEquals (store.findGranularPresetByName (preset.name), 0);
            }

            {
                PresetStore reloaded (file);
                expect (reloaded.load());
                expectEquals ((int) reloaded.getGranularPresets().size(), 1);

                if (reloaded.getGranularPresets().size() == 1)
                    expect (reloaded.getGranularPresets().front() == preset,
                            NI_TXT ("参数与文件名一起存了回来"));

                auto renamed = preset;
                renamed.name = NI_TXT ("改过名");
                reloaded.replaceGranularPreset (0, renamed);
                expectEquals (reloaded.findGranularPresetByName (NI_TXT ("改过名")), 0);

                reloaded.renameGranularPreset (0, NI_TXT ("又改了一次"));

                if (! reloaded.getGranularPresets().empty())
                    expectEquals (reloaded.getGranularPresets().front().name,
                                  juce::String (NI_TXT ("又改了一次")));

                reloaded.removeGranularPreset (0);
                expectEquals ((int) reloaded.getGranularPresets().size(), 0);
            }

            file.deleteFile();
        }
    }
};

namespace
{
    SynthTests          synthTests;
    SubtractiveTests    subtractiveTests;
    WavetableTests      wavetableTests;
    DrumTests           drumTests;
    SamplerTests        samplerTests;
    GranularTests       granularTests;
}
