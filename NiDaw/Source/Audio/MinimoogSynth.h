#pragma once

#include "../Core/Types.h"

#include <array>
#include <vector>

namespace nidaw
{

namespace minimoog
{
/** The four-pole transistor ladder. Four one-pole sections inside a negative
    feedback loop, with a tanh saturator at the input: the classic Moog response,
    and it stays bounded even at full emphasis. */
class LadderFilter
{
public:
    void prepare (double newSampleRate) noexcept { sampleRate = newSampleRate; reset(); }
    void reset() noexcept { y[0] = y[1] = y[2] = y[3] = 0.0; }

    double processSample (double input, double cutoffHz, double emphasis) noexcept
    {
        const auto fc = juce::jlimit (10.0, sampleRate * 0.45, cutoffHz);
        const auto g = juce::jlimit (0.0001, 0.85,
                                     1.0 - std::exp (-2.0 * 3.141592653589793 * fc / sampleRate));
        const auto k = juce::jlimit (0.0, 3.95, emphasis * 4.0);

        const auto saturated = std::tanh (input - k * y[3]);

        y[0] += g * (saturated - y[0]);
        y[1] += g * (y[0] - y[1]);
        y[2] += g * (y[1] - y[2]);
        y[3] += g * (y[2] - y[3]);

        return y[3];
    }

private:
    double sampleRate = 48000.0;
    double y[4] { 0.0, 0.0, 0.0, 0.0 };
};

/** One oscillator sample with PolyBLEP band limiting for the discontinuous
    shapes. `phase` 0..1, `phaseDelta` the per-sample phase step. */
inline double oscillatorSample (MinimoogWave wave, double phase, double phaseDelta) noexcept
{
    const auto t = phase - std::floor (phase);

    auto polyBlep = [phaseDelta] (double x) noexcept
    {
        if (phaseDelta <= 0.0)
            return 0.0;

        if (x < phaseDelta)
        {
            const auto u = x / phaseDelta;
            return u + u - u * u - 1.0;
        }

        if (x > 1.0 - phaseDelta)
        {
            const auto u = (x - 1.0) / phaseDelta;
            return u * u + u + u + 1.0;
        }

        return 0.0;
    };

    switch (wave)
    {
        case MinimoogWave::triangle:
            return 4.0 * std::abs (t - 0.5) - 1.0;

        case MinimoogWave::triSaw:
            // The classic "triangle-sawtooth": a steep rise and a slow fall.
            return t < 0.25 ? (8.0 * t - 1.0) : (1.0 - (t - 0.25) * 8.0 / 3.0);

        case MinimoogWave::saw:
            return (2.0 * t - 1.0) - polyBlep (t);

        case MinimoogWave::square:
        {
            auto value = t < 0.5 ? 1.0 : -1.0;
            value += polyBlep (t);
            value -= polyBlep (t < 0.5 ? t + 0.5 : t - 0.5);
            return value;
        }

        case MinimoogWave::widePulse:
        {
            constexpr double pw = 0.35;
            auto value = t < pw ? 1.0 : -1.0;
            value += polyBlep (t);
            value -= polyBlep (t < pw ? t + (1.0 - pw) : t - pw);
            return value;
        }

        case MinimoogWave::narrowPulse:
        {
            constexpr double pw = 0.15;
            auto value = t < pw ? 1.0 : -1.0;
            value += polyBlep (t);
            value -= polyBlep (t < pw ? t + (1.0 - pw) : t - pw);
            return value;
        }
    }

    return 0.0;
}

/** The multiplier the range switch applies, relative to 8'. */
inline double rangeMultiplier (MinimoogRange range) noexcept
{
    switch (range)
    {
        case MinimoogRange::lo:     return 1.0 / 64.0;
        case MinimoogRange::feet32: return 0.25;
        case MinimoogRange::feet16: return 0.5;
        case MinimoogRange::feet8:  return 1.0;
        case MinimoogRange::feet4:  return 2.0;
        case MinimoogRange::feet2:  return 4.0;
    }

    return 1.0;
}

} // namespace minimoog

/** The Minimoog Model D instrument.

    Mono by default (last-note priority, legato glide), but the player can pick a
    fixed polyphony or "unlimited" - a performance setting, not part of a sound.
    Velocity scales the loudness and the filter contour depth; at sensitivity 0
    the instrument behaves like the original and ignores it.
*/
class MinimoogSynth
{
public:
    MinimoogSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const MinimoogParams& newParams);
    const MinimoogParams& getParams() const noexcept { return params; }

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

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

    /** Diagnostic hooks for the unit tests. */
    double getVoiceFrequencyForTest (juce::int64 noteId) const;
    double getCurrentGlideHzForTest() const noexcept { return monoCurrentHz; }
    double getTargetHzForTest() const noexcept { return voices[0].targetHz; }
    bool   isGlidingForTest() const noexcept;

private:
    struct Envelope
    {
        enum class Stage { idle, attack, decay, sustain, release };

        Stage stage = Stage::idle;
        float level = 0.0f;
        float attack = 0.005f;
        float decay = 0.3f;
        float sustainLevel = 0.8f;
        bool  decayShort = false;
        bool  releaseOn = true;
        double sampleRate = 48000.0;

        void prepare (double sr);
        /** Recomputes the per-sample coefficients after the times changed. */
        void configure() noexcept;
        void trigger() noexcept { stage = Stage::attack; }
        void release() noexcept;
        void kill() noexcept { stage = Stage::idle; level = 0.0f; }
        bool finished() const noexcept { return stage == Stage::idle; }
        float next() noexcept;

    private:
        static float coefficient (double seconds, double sr) noexcept;

        float attackCoef = 0.01f, decayCoef = 0.01f, releaseCoef = 0.01f;
    };

    struct HeldNote
    {
        juce::int64 id = 0;
        double hz = 440.0;
    };

    struct Voice
    {
        bool active = false;
        bool released = false;
        juce::int64 noteId = 0;
        juce::uint32 startOrder = 0;

        double targetHz = 440.0;
        double currentHz = 440.0;
        int    midiNote = 69;

        double phase[3] { 0.0, 0.0, 0.0 };
        double driftPhase[3] { 0.0, 0.0, 0.0 };

        float velocityGain = 1.0f;

        Envelope ampEnv, filterEnv;
        minimoog::LadderFilter filter;

        double pinkStateA = 0.0, pinkStateB = 0.0;
    };

    void startVoice (Voice& voice, juce::int64 noteId, double hz, float velocity);
    void setVoiceTarget (Voice& voice, double hz);
    void configureEnvelopes (Voice& voice);
    void renderVoice (Voice& voice, juce::AudioBuffer<float>& buffer, int startSample,
                      int numSamples, float gainLeft, float gainRight);
    Voice* findVoiceForNote (juce::int64 noteId);
    Voice& findFreeVoice();
    int voiceLimit() const noexcept;

    std::array<Voice, kMaxMinimoogVoices> voices;
    std::vector<HeldNote> heldNotes;         // monophonic stack, last note last
    juce::int64 monoNoteId = 0;

    MinimoogParams params;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random random;

    double monoCurrentHz = 440.0;
    double monoLastHz = 0.0;
};

} // namespace nidaw
