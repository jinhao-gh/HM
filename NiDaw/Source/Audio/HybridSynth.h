#pragma once

#include "../Core/Types.h"
#include "GranularSynth.h"
#include "HybridFxChain.h"
#include "SamplePlayer.h"
#include "SubtractiveSynth.h"
#include "WavetableSynth.h"

#include <array>
#include <atomic>
#include <memory>

namespace nidaw
{

/** Three independent generator layers mixed into one instrument.

    This is the first hybrid engine slice: every layer has its own mode, level and
    pan and reuses the mature wavetable / subtractive / sample / granular engines
    underneath. The layers share the note id the playback engine gives them, so a
    note-on and its note-off reach every active layer at the same time.
*/
class HybridSynth
{
public:
    HybridSynth();

    void prepare (double newSampleRate, int maxBlockSize = 2048);
    void reset();
    void setTempo (double newBpm);

    void setParams (const HybridParams& newParams);
    const HybridParams& getParams() const noexcept { return params; }

    void setSources (const std::array<std::shared_ptr<const LoadedSample>, 3>& samples,
                     const std::array<std::array<std::shared_ptr<const wavetable::Wavetable>, 2>, 3>& tables);

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

    void renderAdding (juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       float gainLeft,
                       float gainRight);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;
    int  getGranularGrainCount (int layer) const noexcept;
    float getGranularGrainPosition (int layer, int index) const noexcept;
    float getGranularGrainAge (int layer, int index) const noexcept;
    /** The wavetable position the layer's oscillator is actually reading. */
    float getWavetablePosition (int layer) const noexcept;
    float getEnvelopePosition (int index) const noexcept;
    float getEnvelopeValue (int index) const noexcept;
    float getLfoPhase (int index) const noexcept;
    float getLfoValue (int index) const noexcept;

private:
    struct Layer
    {
        WavetableSynth wave;
        SubtractiveSynth subtractive;
        SamplePlayer sample;
        GranularSynth granular;
    };

    struct HybridVoice
    {
        bool active = false;
        bool released = false;
        juce::int64 noteId = 0;
        double frequency = 440.0;
        double midiNote = 69.0;
        float velocity = 1.0f;
        juce::uint32 startOrder = 0;

        std::array<juce::ADSR, 4> envelopes;
        std::array<double, 4> envelopeElapsed {};
        double ageSeconds = 0.0;
        std::array<double, 6> lfoPhases {};
        std::array<double, 6> lfoHold {};
        std::array<bool, 6> lfoHoldValid {};

        std::array<std::array<juce::dsp::StateVariableTPTFilter<float>, 2>, 2> filters;
        // The 24 dB modes cascade two independent 12 dB stages. Feeding a single
        // filter's output back through the same state is not a cascade and can
        // diverge for band-pass shapes.
        std::array<std::array<juce::dsp::StateVariableTPTFilter<float>, 2>, 2> filtersStage2;
        std::array<std::array<float, 2>, 2> notchX1 {};
        std::array<std::array<float, 2>, 2> notchX2 {};
        std::array<float, 2> notchCoeff {};
        std::array<std::array<juce::dsp::DelayLine<
            float, juce::dsp::DelayLineInterpolationTypes::Linear>, 2>, 2> combFilters;
        std::array<float, 2> appliedCutoff {};
        std::array<float, 2> appliedResonance {};
        std::array<int, 2> appliedMode { -1, -1 };
        std::array<int, 2> appliedSlope {};

        int controlCountdown = 0;
        float gainMod = 0.0f;
        float panMod = 0.0f;
        std::array<float, 3> layerLevelMod {};
        std::array<float, 3> layerPanMod {};
        float subLevelMod = 0.0f, subPanMod = 0.0f;
        float noiseLevelMod = 0.0f, noisePanMod = 0.0f;
        std::array<float, 4> fxMixMod {};
        std::array<float, 4> fxParam1Mod {};
        std::array<float, 4> fxParam2Mod {};
        std::array<float, 3> layerPositionMod {};
        std::array<float, 3> layerUnisonMod {};
        std::array<float, 3> layerDetuneMod {};
    };

    HybridVoice* findVoiceForNote (juce::int64 noteId);
    HybridVoice& findVoiceToUse();
    void applyEnvelopeParams (HybridVoice& voice);
    bool renderVoice (HybridVoice& voice, int numSamples,
                      float gainLeft, float gainRight);

    std::array<Layer, 3> layers;
    SubtractiveSynth subSynth;
    SubtractiveSynth noiseSynth;
    std::unique_ptr<HybridFxChain> fxChain;
    std::unique_ptr<std::array<HybridVoice, kVoicesPerTrack>> voices;
    juce::AudioBuffer<float> scratch;
    juce::AudioBuffer<float> voiceBuffer;
    juce::AudioBuffer<float> parallelBuffer;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random random;
    std::array<float, 4> latestFxMixMod {};
    std::array<float, 4> latestFxParam1Mod {};
    std::array<float, 4> latestFxParam2Mod {};
    double currentBpm = 120.0;

    struct Published
    {
        std::atomic<float> envelopePosition[4] {};
        std::atomic<float> envelopeValue[4] {};
        std::atomic<float> lfoPhase[6] {};
        std::atomic<float> lfoValue[6] {};

        Published() = default;
        Published (const Published& other) { copyFrom (other); }
        Published& operator= (const Published& other) { copyFrom (other); return *this; }
        Published (Published&& other) noexcept { copyFrom (other); }
        Published& operator= (Published&& other) noexcept { copyFrom (other); return *this; }

        void copyFrom (const Published& other)
        {
            for (int i = 0; i < 4; ++i)
            {
                envelopePosition[i].store (other.envelopePosition[i].load (std::memory_order_relaxed));
                envelopeValue[i].store (other.envelopeValue[i].load (std::memory_order_relaxed));
            }

            for (int i = 0; i < 6; ++i)
            {
                lfoPhase[i].store (other.lfoPhase[i].load (std::memory_order_relaxed));
                lfoValue[i].store (other.lfoValue[i].load (std::memory_order_relaxed));
            }
        }
    };

    Published published;
    HybridParams params;
};

} // namespace nidaw
