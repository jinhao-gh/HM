#pragma once

#include "../Core/Types.h"

#include <juce_dsp/juce_dsp.h>

namespace nidaw
{

namespace subtractive
{
/** PolyBLEP: the correction that takes the corner off a discontinuous waveform.
    `t` is the phase 0..1 and `dt` how far the phase moves per sample. */
inline double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0;

    if (t < dt)
    {
        const auto x = t / dt;
        return x + x - x * x - 1.0;
    }

    if (t > 1.0 - dt)
    {
        const auto x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }

    return 0.0;
}

inline double foldPhase (double phase) noexcept
{
    return phase - std::floor (phase);
}

/** One oscillator sample, shared by the engine and the editor's waveform display
    so the picture cannot disagree with the sound.

    Saw, square and pulse get the PolyBLEP correction; the triangle's harmonics
    fall off as 1/n^2 so it is left naive on purpose.
*/
inline double oscillatorSample (OscWave wave, double phase, double phaseDelta,
                                double pulseWidth) noexcept
{
    const auto t = foldPhase (phase);

    switch (wave)
    {
        case OscWave::sine:
            return std::sin (juce::MathConstants<double>::twoPi * t);

        case OscWave::saw:
            return (2.0 * t - 1.0) - polyBlep (t, phaseDelta);

        case OscWave::square:
        {
            auto value = t < 0.5 ? 1.0 : -1.0;
            value += polyBlep (t, phaseDelta);
            value -= polyBlep (foldPhase (t + 0.5), phaseDelta);
            return value;
        }

        case OscWave::pulse:
        {
            const auto pw = juce::jlimit (0.05, 0.95, pulseWidth);
            auto value = t < pw ? 1.0 : -1.0;
            value += polyBlep (t, phaseDelta);
            value -= polyBlep (foldPhase (t - pw), phaseDelta);
            return value;
        }

        case OscWave::triangle:
            return 4.0 * std::abs (t - 0.5) - 1.0;
    }

    return 0.0;
}

/** The ratio an oscillator runs at: octave, semitone and detune folded in. */
inline double oscillatorRatio (const SubtractiveOsc& osc) noexcept
{
    const auto semitones = (double) osc.octave * 12.0 + (double) osc.semitone
                               + (double) osc.detuneCents / 100.0;
    return std::pow (2.0, semitones / 12.0);
}

/** LFO shape, phase 0..1. `hold` carries the sample & hold value. */
inline double lfoSample (int waveform, double phase, double& hold, bool& holdValid,
                         juce::Random& random) noexcept
{
    const auto t = foldPhase (phase);

    switch (waveform)
    {
        case 1:  return std::sin (juce::MathConstants<double>::twoPi * t);
        case 2:  return t < 0.5 ? 1.0 : -1.0;
        case 3:
            if (! holdValid)
            {
                hold = random.nextDouble() * 2.0 - 1.0;
                holdValid = true;
            }
            return hold;
        default: break;
    }

    // Triangle.
    return 4.0 * std::abs (t - 0.5) - 1.0;
}

} // namespace subtractive

/** A polyphonic subtractive (analogue style) instrument.

    Per voice: two oscillators plus noise -> a multimode filter with its own
    envelope -> the amplifier envelope. One LFO reaches the pitch, the filter
    and the amplitude. The interface mirrors TrackSynth so the playback engine
    can treat the three instrument kinds the same way.
*/
class SubtractiveSynth
{
public:
    SubtractiveSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const SubtractiveParams& newParams);
    const SubtractiveParams& getParams() const noexcept { return params; }

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

    /** Adds this instrument's output into the first two channels of the buffer. */
    void renderAdding (juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       float gainLeft,
                       float gainRight);

    bool renderVoiceAdding (juce::int64 noteId,
                            juce::AudioBuffer<float>& buffer,
                            int startSample,
                            int numSamples,
                            float gainLeft,
                            float gainRight);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;

    /** Diagnostic hook: the frequency the voice for this note plays, 0 if none. */
    double getVoiceFrequencyForTest (juce::int64 noteId) const;

private:
    struct Voice
    {
        bool        active    = false;
        bool        released  = false;
        juce::int64 noteId    = 0;
        double      frequency = 440.0;
        float       amplitude = 1.0f;      // velocity, 0..1
        juce::uint32 startOrder = 0;

        double oscPhase[2] { 0.0, 0.0 };
        double lfoPhase = 0.0;
        double lfoHold  = 0.0;
        bool   lfoHoldValid = false;

        juce::ADSR amp;
        juce::ADSR filterEnv;

        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::dsp::StateVariableTPTFilter<float> filter2;
        float appliedCutoff    = 0.0f;
        float appliedResonance = 0.0f;
        int   appliedMode      = -1;
        int   appliedSlope     = 0;

        /** Control rate state: pitch and cutoff are refreshed every few samples. */
        int    controlCountdown = 0;
        double pitchFactor = 1.0;
        double cutoffHz    = 2000.0;
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    void applyEnvelopes (Voice& voice);

    std::array<Voice, kVoicesPerTrack> voices;
    SubtractiveParams params;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random random;
};

} // namespace nidaw
