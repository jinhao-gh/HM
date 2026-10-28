#pragma once

#include "../Core/Types.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace nidaw
{

juce::String hybridFxTypeName (HybridFxType type);
juce::String hybridFxParam1Name (HybridFxType type);
juce::String hybridFxParam2Name (HybridFxType type);
int hybridFxTypeCount() noexcept;

/** Four serial effect slots that live inside the hybrid instrument.

    This is deliberately independent from a track's insert rack: Serum-style
    effects belong to the sound, so they must follow a preset and stay in front
    of the DAW's own inserts. The chain processes the stereo buffer in place.
*/
class HybridFxChain
{
public:
    HybridFxChain();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset();
    void setParams (const std::array<HybridFxSlot, 4>& newSlots);
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    bool hasAnyEnabledSlot() const noexcept;
    bool isTailActive() const noexcept { return tailSamplesRemaining > 0; }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        std::array<float, 2> z1 {};
        std::array<float, 2> z2 {};

        void reset() noexcept;
        float process (int channel, float input) noexcept;
        void setLowShelf (double sampleRate, float frequency, float q, float gainDb) noexcept;
        void setHighShelf (double sampleRate, float frequency, float q, float gainDb) noexcept;
        void setPeak (double sampleRate, float frequency, float q, float gainDb) noexcept;
    };

    struct SlotState
    {
        HybridFxType type = HybridFxType::none;
        bool wasEnabled = false;

        std::array<juce::dsp::DelayLine<
            float, juce::dsp::DelayLineInterpolationTypes::Linear>, 2> chorusDelay;
        double chorusPhase = 0.0;

        struct AllPassStage
        {
            float a = 0.0f;
            float z = 0.0f;
            float process (float input) noexcept
            {
                const auto output = -a * input + z;
                z = input + a * output;
                return output;
            }
            void reset() noexcept { z = 0.0f; }
        };

        std::array<std::array<AllPassStage, 4>, 2> phaserStages;
        std::array<float, 2> phaserFeedback {};
        double phaserPhase = 0.0;

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;
        juce::dsp::Reverb reverb;

        std::array<Biquad, 3> eqBands;
        std::array<float, 2> toneState {};
        std::array<float, 2> crushHold {};
        std::array<int, 2> crushCounter {};
        std::array<float, 2> compressorEnvelope {};

        void reset (double sampleRate, int maximumBlockSize);
        void resetStateOnly();
    };

    void processSlot (int index, juce::AudioBuffer<float>& buffer, int numSamples);
    void processDistortion (int index, const HybridFxSlot& slot,
                            juce::AudioBuffer<float>& buffer, int numSamples);
    void processBitcrush (int index, const HybridFxSlot& slot,
                          juce::AudioBuffer<float>& buffer, int numSamples);
    void processChorus (int index, const HybridFxSlot& slot,
                        juce::AudioBuffer<float>& buffer, int numSamples);
    void processPhaser (int index, const HybridFxSlot& slot,
                        juce::AudioBuffer<float>& buffer, int numSamples);
    void processDelay (int index, const HybridFxSlot& slot,
                       juce::AudioBuffer<float>& buffer, int numSamples);
    void processReverb (int index, const HybridFxSlot& slot,
                        juce::AudioBuffer<float>& buffer, int numSamples);
    void processEq (int index, const HybridFxSlot& slot,
                    juce::AudioBuffer<float>& buffer, int numSamples);
    void processCompressor (int index, const HybridFxSlot& slot,
                            juce::AudioBuffer<float>& buffer, int numSamples);
    void updateEqCoefficients (SlotState& state, const HybridFxSlot& slot);

    double sampleRate = 48000.0;
    int maximumBlockSize = 2048;
    std::array<HybridFxSlot, 4> slots;
    std::array<SlotState, 4> states;
    juce::AudioBuffer<float> dryBuffer;
    int tailSamplesRemaining = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridFxChain)
};

} // namespace nidaw
