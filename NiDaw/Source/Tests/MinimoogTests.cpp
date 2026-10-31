#include "TestSupport.h"

#include "../Audio/MinimoogSynth.h"
#include "../Audio/SynthEngine.h"
#include "../Core/MinimoogPresets.h"
#include "../Core/PresetStore.h"

#include <cmath>
#include <map>
#include <set>
#include <utility>

class MinimoogTests : public juce::UnitTest
{
public:
    MinimoogTests() : juce::UnitTest ("Minimoog", "nidaw") {}

    static MinimoogParams singleOsc (MinimoogRange range)
    {
        MinimoogParams p;
        p.polyphony = 0;
        p.velocitySensitivity = 0.0f;

        for (auto& osc : p.oscs)
        {
            osc = MinimoogOsc();
            osc.enabled = false;
            osc.level = 0.0f;
        }

        p.oscs[0] = minimoog::osc (MinimoogWave::saw, range, 1.0f);
        p.noiseLevel = 0.0f;
        p.filterCutoffHz = 18000.0f;
        p.filterEmphasis = 0.0f;
        p.filterContourOctaves = 0.0f;
        p.filterKeyTrack1 = false;
        p.filterKeyTrack2 = false;
        p.filterAttack = 0.001f;
        p.filterDecay = 0.001f;
        p.filterDecayShort = true;
        p.filterSustainLevel = 0.8f;
        p.ampAttack = 0.001f;
        p.ampDecay = 0.001f;
        p.ampDecayShort = true;
        p.ampSustainLevel = 0.8f;
        p.releaseOn = true;
        p.volume = 0.7f;
        return p;
    }

    static float rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length)
    {
        return buffer.getRMSLevel (0, start, length);
    }

    static double positiveCrossingHz (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                      int start)
    {
        const auto* data = buffer.getReadPointer (0);
        int crossings = 0;
        double first = -1.0, last = -1.0;

        for (int i = juce::jmax (1, start); i < buffer.getNumSamples(); ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                const auto fraction = (double) -data[i - 1]
                                          / (double) (data[i] - data[i - 1]);
                const auto time = ((double) (i - 1) + fraction) / sampleRate;

                if (first < 0.0)
                    first = time;

                last = time;
                ++crossings;
            }
        }

        return crossings > 1 && last > first ? (double) (crossings - 1) / (last - first) : 0.0;
    }

    static float highFrequencyEnergy (const juce::AudioBuffer<float>& buffer)
    {
        const auto* data = buffer.getReadPointer (0);
        auto sum = 0.0;

        for (int i = 1; i < buffer.getNumSamples(); ++i)
        {
            const auto difference = (double) data[i] - data[i - 1];
            sum += difference * difference;
        }

        return (float) std::sqrt (sum / (double) buffer.getNumSamples());
    }


    static double crossingRateBetween (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                        int start, int end)
    {
        const auto* data = buffer.getReadPointer (0);
        int crossings = 0;
        double first = -1.0, last = -1.0;

        for (int i = juce::jmax (1, start); i < juce::jmin (end, buffer.getNumSamples()); ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                const auto fraction = (double) -data[i - 1]
                                          / (double) (data[i] - data[i - 1]);
                const auto time = ((double) (i - 1) + fraction) / sampleRate;

                if (first < 0.0)
                    first = time;

                last = time;
                ++crossings;
            }
        }

        return crossings > 1 && last > first ? (double) (crossings - 1) / (last - first) : 0.0;
    }

    static double periodJitter (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (0);
        std::vector<double> periods;
        double previous = -1.0;

        for (int i = 1; i < buffer.getNumSamples(); ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                const auto fraction = (double) -data[i - 1]
                                          / (double) (data[i] - data[i - 1]);
                const auto time = ((double) (i - 1) + fraction) / sampleRate;

                if (previous >= 0.0)
                    periods.push_back (time - previous);

                previous = time;
            }
        }

        if (periods.size() < 3)
            return 0.0;

        auto mean = 0.0;

        for (const auto p : periods)
            mean += p;

        mean /= (double) periods.size();

        auto variance = 0.0;

        for (const auto p : periods)
            variance += (p - mean) * (p - mean);

        return std::sqrt (variance / (double) periods.size()) / juce::jmax (1.0e-6, mean);
    }


    static double periodRoughness (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (0);
        std::vector<double> periods;
        double previous = -1.0;

        for (int i = 1; i < buffer.getNumSamples(); ++i)
        {
            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            {
                const auto fraction = (double) -data[i - 1]
                                          / (double) (data[i] - data[i - 1]);
                const auto time = ((double) (i - 1) + fraction) / sampleRate;

                if (previous >= 0.0)
                    periods.push_back (time - previous);

                previous = time;
            }
        }

        if (periods.size() < 4)
            return 0.0;

        auto mean = 0.0;

        for (const auto p : periods)
            mean += p;

        mean /= (double) periods.size();

        auto rough = 0.0;

        for (size_t i = 2; i < periods.size(); ++i)
            rough += std::abs (periods[i] - 2.0 * periods[i - 1] + periods[i - 2]);

        return rough / ((double) periods.size() - 2.0) / juce::jmax (1.0e-6, mean);
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("the LO range is slow enough for an LFO");

        {
            expectWithinAbsoluteError (minimoog::rangeMultiplier (MinimoogRange::lo),
                                       1.0 / 256.0, 1.0e-9);
            expectLessThan (minimoog::rangeMultiplier (MinimoogRange::lo), 1.0 / 64.0);
        }

        beginTest ("the oscillator ranges multiply the note pitch exactly");

        {
            const std::pair<MinimoogRange, double> cases[] = {
                { MinimoogRange::feet32, 110.0 },
                { MinimoogRange::feet16, 220.0 },
                { MinimoogRange::feet8, 440.0 },
                { MinimoogRange::feet4, 880.0 },
                { MinimoogRange::feet2, 1760.0 }
            };

            for (const auto& [range, expected] : cases)
            {
                MinimoogSynth synth;
                synth.prepare (sampleRate);
                synth.setParams (singleOsc (range));
                synth.noteOn (1, 440.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 9600);
                buffer.clear();
                synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);

                // The first ten milliseconds are the attack: measure after it.
                const auto measured = positiveCrossingHz (buffer, sampleRate, 2400);
                expectWithinAbsoluteError (measured, expected, 1.0);
            }
        }

        beginTest ("the frequency tune and master tune shift the pitch");

        {
            auto params = singleOsc (MinimoogRange::feet8);
            params.oscs[0].tuneSemitones = 7.0f;

            MinimoogSynth synth;
            synth.prepare (sampleRate);
            synth.setParams (params);
            synth.noteOn (1, 440.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, 9600);
            buffer.clear();
            synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);

            const auto measured = positiveCrossingHz (buffer, sampleRate, 2400);
            expectWithinAbsoluteError (measured, 440.0 * std::pow (2.0, 7.0 / 12.0), 2.0);
        }

        beginTest ("monophonic last-note priority and release fallback");

        {
            auto params = singleOsc (MinimoogRange::feet8);
            params.glideOn = false;

            MinimoogSynth synth;
            synth.prepare (sampleRate);
            synth.setParams (params);

            synth.noteOn (1, 220.0f, 100.0f);
            synth.noteOn (2, 440.0f, 100.0f);

            expectEquals (synth.activeVoiceCount(), 1);
            expectWithinAbsoluteError (synth.getTargetHzForTest(), 440.0, 0.5);

            synth.noteOff (2);
            expectWithinAbsoluteError (synth.getTargetHzForTest(), 220.0, 0.5);

            juce::AudioBuffer<float> buffer (2, 9600);
            buffer.clear();
            synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            expectGreaterThan (synth.activeVoiceCount(), 0);

            synth.noteOff (1);
            buffer.clear();
            synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);
            expectEquals (synth.activeVoiceCount(), 0);
        }

        beginTest ("polyphony limits and unlimited voices");

        {
            auto params = singleOsc (MinimoogRange::feet8);
            params.polyphony = 4;

            MinimoogSynth synth;
            synth.prepare (sampleRate);
            synth.setParams (params);

            for (int i = 1; i <= 4; ++i)
                synth.noteOn (i, 220.0f * (float) i, 100.0f);

            expectEquals (synth.activeVoiceCount(), 4);

            synth.noteOff (2);

            juce::AudioBuffer<float> buffer (2, 4800);
            buffer.clear();
            synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);   // let the release finish
            expectEquals (synth.activeVoiceCount(), 3);

            // A fifth note has to steal a voice: the count stays at four.
            synth.noteOn (5, 880.0f, 100.0f);
            buffer.clear();
            synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
            expectEquals (synth.activeVoiceCount(), 4);
            expectGreaterThan (synth.getVoiceFrequencyForTest (5), 0.0);
        }

        {
            auto params = singleOsc (MinimoogRange::feet8);
            params.polyphony = -1;

            MinimoogSynth synth;
            synth.prepare (sampleRate);
            synth.setParams (params);

            for (int i = 1; i <= 20; ++i)
                synth.noteOn (i, 110.0f + 10.0f * (float) i, 100.0f);

            expectEquals (synth.activeVoiceCount(), 20);
        }

        beginTest ("glide moves the pitch over the requested time");

        {
            auto params = singleOsc (MinimoogRange::feet8);
            params.glideOn = true;
            params.glideSeconds = 0.2f;

            MinimoogSynth synth;
            synth.prepare (sampleRate);
            synth.setParams (params);

            synth.noteOn (1, 220.0f, 100.0f);

            juce::AudioBuffer<float> buffer (2, 24000);
            buffer.clear();
            synth.renderAdding (buffer, 0, 256, 1.0f, 1.0f);

            const auto startHz = synth.getCurrentGlideHzForTest();
            expectWithinAbsoluteError (startHz, 220.0, 1.0);

            synth.noteOn (2, 440.0f, 100.0f);
            buffer.clear();
            synth.renderAdding (buffer, 0, 2400, 1.0f, 1.0f);   // 50 ms

            const auto after50 = synth.getCurrentGlideHzForTest();
            expectGreaterThan (after50, 220.0);
            expectLessThan (after50, 440.0);

            buffer.clear();
            synth.renderAdding (buffer, 0, 14400, 1.0f, 1.0f);  // 300 ms more
            const auto after350 = synth.getCurrentGlideHzForTest();
            expectGreaterThan (after350, after50);
            expectWithinAbsoluteError (after350, 440.0, 1.0);
        }

        beginTest ("velocity scales the loudness and can be switched off");

        {
            auto render = [] (float velocity, float sensitivity)
            {
                auto params = singleOsc (MinimoogRange::feet8);
                params.velocitySensitivity = sensitivity;

                MinimoogSynth synth;
                synth.prepare (sampleRate);
                synth.setParams (params);
                synth.noteOn (1, 440.0f, velocity);

                juce::AudioBuffer<float> buffer (2, 4800);
                buffer.clear();
                synth.renderAdding (buffer, 0, 4800, 1.0f, 1.0f);
                return rmsOf (buffer, 2400, 2000);
            };

            const auto loud = render (127.0f, 1.0f);
            const auto soft = render (30.0f, 1.0f);
            expectGreaterThan (loud, 0.0005f);
            expectGreaterThan (loud, soft * 2.0f);

            const auto flatLoud = render (127.0f, 0.0f);
            const auto flatSoft = render (30.0f, 0.0f);
            expectWithinAbsoluteError (flatLoud, flatSoft, flatLoud * 0.05f);
        }

        beginTest ("the ladder filter follows the cutoff and the contour");

        {
            auto render = [] (float cutoff, float contour)
            {
                auto params = singleOsc (MinimoogRange::feet8);
                params.filterCutoffHz = cutoff;
                params.filterContourOctaves = contour;
                params.filterAttack = 0.001f;
                params.filterDecay = 0.001f;
                params.filterSustainLevel = 0.8f;

                MinimoogSynth synth;
                synth.prepare (sampleRate);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 9600);
                buffer.clear();
                synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);
                return highFrequencyEnergy (buffer);
            };

            expectGreaterThan (render (8000.0f, 0.0f), render (200.0f, 0.0f) * 2.0f);
        }

        beginTest ("white and pink noise differ in their high end");

        {
            auto render = [] (bool pink)
            {
                auto params = singleOsc (MinimoogRange::feet8);

                for (auto& osc : params.oscs)
                    osc.level = 0.0f;

                params.noiseLevel = 0.6f;
                params.noisePink = pink;
                params.filterCutoffHz = 18000.0f;

                MinimoogSynth synth;
                synth.prepare (sampleRate);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 9600);
                buffer.clear();
                synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);

                return highFrequencyEnergy (buffer) / juce::jmax (1.0e-6f, rmsOf (buffer, 2400, 7000));
            };

            expectGreaterThan (render (false), render (true) * 1.2f);
        }

        beginTest ("unison thickens the mono voice and distortion adds harmonics");

        {
            auto render = [] (bool useUnison, bool useDistortion, float amount)
            {
                auto params = singleOsc (MinimoogRange::feet8);
                params.unison = useUnison;
                params.distortionOn = useDistortion;
                params.distortion = amount;
                params.filterCutoffHz = 1200.0f;
                params.oscs[0] = minimoog::osc (MinimoogWave::triangle, MinimoogRange::feet8, 1.0f);

                MinimoogSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 9600);
                buffer.clear();
                synth.renderAdding (buffer, 0, 9600, 1.0f, 1.0f);
                return buffer;
            };

            const auto plain = render (false, false, 0.0f);
            const auto thick = render (true, false, 0.0f);

            double difference = 0.0;

            for (int i = 0; i < plain.getNumSamples(); ++i)
            {
                const auto d = (double) thick.getReadPointer (0)[i]
                                   - (double) plain.getReadPointer (0)[i];
                difference += d * d;
            }

            difference = std::sqrt (difference / (double) plain.getNumSamples());
            expectGreaterThan ((float) difference, 0.0005f);
            expectGreaterThan (rmsOf (thick, 2400, 6000), 0.01f);

            const auto clean = render (false, false, 0.0f);
            const auto dirty = render (false, true, 1.0f);
            expectGreaterThan (highFrequencyEnergy (dirty), highFrequencyEnergy (clean) * 1.2f);
        }

        beginTest ("the release switch makes the release as long as the decay");

        {
            auto voicesAfterRelease = [] (bool releaseOn, int samples)
            {
                auto params = singleOsc (MinimoogRange::feet8);
                params.ampDecay = 0.4f;
                params.ampDecayShort = false;
                params.ampSustainLevel = 1.0f;
                params.releaseOn = releaseOn;

                MinimoogSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 440.0f, 100.0f);

                juce::AudioBuffer<float> warmUp (2, 9600);
                warmUp.clear();
                synth.renderAdding (warmUp, 0, 9600, 1.0f, 1.0f);
                synth.noteOff (1);

                juce::AudioBuffer<float> tail (2, samples);
                tail.clear();
                synth.renderAdding (tail, 0, samples, 1.0f, 1.0f);
                return synth.activeVoiceCount();
            };

            expectEquals (voicesAfterRelease (true, 9600), 1);
            expectEquals (voicesAfterRelease (true, 72000), 0);
            expectEquals (voicesAfterRelease (false, 2400), 0);
        }


        beginTest ("the oscillator 3 filter envelope switch bends oscillator 3");

        {
            auto render = [] (bool ecgOn)
            {
                auto params = singleOsc (MinimoogRange::feet8);

                for (auto& osc : params.oscs)
                {
                    osc.enabled = false;
                    osc.level = 0.0f;
                }

                params.oscs[2] = minimoog::osc (MinimoogWave::saw, MinimoogRange::feet8, 1.0f);
                params.oscs[2].enabled = true;
                params.osc3FilterEg = ecgOn;
                params.filterCutoffHz = 18000.0f;
                params.filterContourOctaves = 4.0f;
                params.filterAttack = 0.001f;
                params.filterDecay = 0.25f;
                params.filterDecayShort = false;
                params.filterSustainLevel = 0.0f;
                params.ampAttack = 0.001f;
                params.ampDecay = 0.001f;
                params.ampSustainLevel = 1.0f;

                MinimoogSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 440.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 48000);
                buffer.clear();
                synth.renderAdding (buffer, 0, 48000, 1.0f, 1.0f);
                return buffer;
            };

            const auto swept = render (true);
            const auto flat = render (false);

            const auto early = crossingRateBetween (swept, 48000.0, 1200, 7200);
            const auto late = crossingRateBetween (swept, 48000.0, 36000, 46000);
            const auto flatLate = crossingRateBetween (flat, 48000.0, 36000, 46000);

            expectGreaterThan (early, late * 2.0);
            expectWithinAbsoluteError (late, flatLate, flatLate * 0.1);
        }

        beginTest ("the noise LFO switch slows the random modulation");

        {
            auto render = [] (bool lfo)
            {
                auto params = singleOsc (MinimoogRange::feet8);
                params.oscModOn = true;
                params.modMix = 1.0f;
                params.modDepth = 1.0f;
                params.noiseLfo = lfo;
                params.noiseLevel = 0.0f;
                params.filterCutoffHz = 18000.0f;
                params.ampDecay = 0.001f;
                params.ampSustainLevel = 1.0f;

                MinimoogSynth synth;
                synth.prepare (48000.0);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 24000);
                buffer.clear();
                synth.renderAdding (buffer, 0, 24000, 1.0f, 1.0f);
                return buffer;
            };

            const auto white = render (false);
            const auto slow = render (true);

            expectGreaterThan (periodRoughness (white, 48000.0), periodRoughness (slow, 48000.0) * 1.5);
        }

        beginTest ("the factory bank is 30 sounds in six categories");

        {
            int count = 0;
            const auto* presets = minimoog::bank (count);
            expectEquals (count, 30);

            std::map<juce::String, int> perCategory;
            std::set<juce::String> names;

            for (int i = 0; i < count; ++i)
            {
                const auto presetName = juce::String::fromUTF8 (presets[i].name);
                const auto presetCategory = juce::String::fromUTF8 (presets[i].category);

                expect (presetName.isNotEmpty());
                expect (presetCategory.isNotEmpty());
                expect (names.insert (presetName).second, "duplicate preset name: " + presetName);
                ++perCategory[presetCategory];
            }

            expectEquals ((int) perCategory.size(), 6);

            for (const auto& entry : perCategory)
                expectEquals (entry.second, 5);

            for (int i = 0; i < count; ++i)
            {
                MinimoogParams params;
                minimoog::applyPreset (params, i);

                expectEquals (params.name, juce::String::fromUTF8 (presets[i].name));

                MinimoogSynth synth;
                synth.prepare (sampleRate);
                synth.setParams (params);
                synth.noteOn (1, 220.0f, 100.0f);

                juce::AudioBuffer<float> buffer (2, 144000);
                buffer.clear();
                synth.renderAdding (buffer, 0, 144000, 1.0f, 1.0f);

                auto peak = 0.0f;

                for (int s = 0; s < 144000; ++s)
                    peak = juce::jmax (peak, std::abs (buffer.getSample (0, s)));

                expect (std::isfinite (peak), "preset " + params.name + " is not finite");
                expectGreaterThan (peak, 0.001f, "preset " + params.name + " is silent");
                expectLessThan (peak, 4.0f, "preset " + params.name + " peaks at "
                                                + juce::String (peak, 3));
            }
        }

        beginTest ("loading a preset keeps the performance settings");

        {
            MinimoogParams params;
            params.polyphony = 8;
            params.velocitySensitivity = 0.2f;

            minimoog::applyPreset (params, 0);

            expectEquals (params.polyphony, 8);
            expectWithinAbsoluteError ((double) params.velocitySensitivity, 0.2, 1.0e-6);
            expectEquals (params.name, juce::String::fromUTF8 ("经典胖贝斯"));
        }

        beginTest ("the Minimoog patch survives the preset and project round trip");

        {
            MinimoogParams params;
            params.name = NI_TXT ("测试迷你");
            params.polyphony = 16;
            params.velocitySensitivity = 0.35f;
            params.oscs[1] = minimoog::osc (MinimoogWave::narrowPulse, MinimoogRange::feet16,
                                            0.42f, -3.5f);
            params.noiseLevel = 0.2f;
            params.noisePink = true;
            params.glideOn = true;
            params.glideSeconds = 0.12f;
            params.filterCutoffHz = 1234.0f;
            params.filterEmphasis = 0.6f;
            params.filterContourOctaves = 2.5f;
            params.filterKeyTrack1 = true;
            params.osc3FilterEg = true;
            params.noiseLfo = true;
            params.filterDecayShort = true;
            params.ampSustainLevel = 0.0f;
            params.releaseOn = false;
            params.volume = 0.4f;

            const auto restored = minimoogParamsFromVar (minimoogParamsToVar (params));
            expect (restored == params);
        }

        beginTest ("a Minimoog track reaches the schedule and the label");

        {
            expectEquals (trackKindLabel (TrackKind::minimoog), NI_TXT ("迷你穆格"));

            auto project = makeProjectWithSynthTrack();
            auto* track = project.findTrack (project.tracks.front().id);
            expect (track != nullptr);

            if (track != nullptr)
            {
                track->kind = TrackKind::minimoog;
                track->minimoog.filterCutoffHz = 777.0f;
                track->minimoog.polyphony = 4;

                const auto schedule = buildSchedule (project);
                expectEquals ((int) schedule.tracks.size(), 1);

                if (! schedule.tracks.empty())
                {
                    expect (schedule.tracks.front().kind == TrackKind::minimoog);
                    expectWithinAbsoluteError ((double) schedule.tracks.front().minimoog.filterCutoffHz,
                                               777.0, 1.0e-3);
                    expectEquals (schedule.tracks.front().minimoog.polyphony, 4);
                }
            }
        }
    }
};

static MinimoogTests minmoogTests;
