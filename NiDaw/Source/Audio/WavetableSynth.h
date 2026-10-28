#pragma once

#include "../Core/Types.h"
#include "../Core/Wavetables.h"
#include "SubtractiveSynth.h"     // for the shared pitch-ratio / LFO helpers

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace nidaw
{

/** A polyphonic wavetable instrument.

    Two table oscillators plus noise, into the same multimode filter the
    subtractive instrument uses, then the amplifier envelope. A dedicated
    position envelope (and the LFO) can sweep across the table's frames.
*/
class WavetableSynth
{
public:
    WavetableSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const WavetableParams& newParams);
    const WavetableParams& getParams() const noexcept { return params; }

    /** The project's own tables for the two oscillators, or null to use the
        built-in table each oscillator names. Called from the message thread with
        pointers that live in the render schedule, so the audio thread only ever
        reads immutable data. */
    void setTables (std::shared_ptr<const wavetable::Wavetable> table0,
                    std::shared_ptr<const wavetable::Wavetable> table1);

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

    /** Adds only the internal voice carrying `noteId`. The hybrid instrument
        renders each of its own voices separately so the shared double filter can
        keep per-note state. Returns false when this note has no active voice. */
    bool renderVoiceAdding (juce::int64 noteId,
                            juce::AudioBuffer<float>& buffer,
                            int startSample,
                            int numSamples,
                            float gainLeft,
                            float gainRight);

    /** Per-voice position / unison / detune offsets coming from the hybrid
        modulation matrix. Offsets are normalised -1..+1. */
    void setVoiceOffsets (juce::int64 noteId,
                          float positionOffset,
                          float unisonOffset,
                          float detuneOffset);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;

    /** Diagnostic hook: the frequency the voice for this note plays, 0 if none. */
    double getVoiceFrequencyForTest (juce::int64 noteId) const;

    /** The numbers the editor reads: an atomic cannot be copied, and the engine keeps
        its instruments in a std::vector (which moves them), so this carries its
        values across a move instead of deleting the move. */
    struct Published
    {
        std::atomic<float> position[2] {};
        std::atomic<int>   voices { 0 };

        Published() = default;
        Published (const Published& other) { copyFrom (other); }
        Published& operator= (const Published& other) { copyFrom (other); return *this; }
        Published (Published&& other) noexcept { copyFrom (other); }
        Published& operator= (Published&& other) noexcept { copyFrom (other); return *this; }

        void copyFrom (const Published& other)
        {
            position[0].store (other.position[0].load (std::memory_order_relaxed));
            position[1].store (other.position[1].load (std::memory_order_relaxed));
            voices.store (other.voices.load (std::memory_order_relaxed));
        }
    };

    /** What the instrument is playing *right now*, for the editor's waterfall view:
        the table position the newest voice computed (position slider + position
        envelope + LFO, exactly the number the sound is made from) and how many
        voices are sounding. Plain atomics the audio thread only writes, so reading
        them from the message thread never blocks it. */
    float getPublishedPosition (int osc) const noexcept
    {
        return published.position[juce::jlimit (0, 1, osc)].load (std::memory_order_relaxed);
    }

    int getPublishedVoices() const noexcept
    {
        return published.voices.load (std::memory_order_relaxed);
    }

    /** Reads one sample from a built-in table, interpolating between the two
        neighbouring frames and, inside a frame, between two samples. Shared with
        the editor's display so the picture matches the sound. */
    static double sampleTable (const wavetable::Wavetable& table, int level,
                               double phase, double framePosition,
                               bool discrete = false) noexcept;

private:
    struct Voice
    {
        bool        active    = false;
        bool        released  = false;
        juce::int64 noteId    = 0;
        double      frequency = 440.0;
        float       amplitude = 1.0f;      // velocity, 0..1
        juce::uint32 startOrder = 0;

        std::array<std::array<double, kMaxWavetableUnisonVoices>, 2> phase {};
        float positionOffset = 0.0f;
        float unisonOffset = 0.0f;
        float detuneOffset = 0.0f;
        double lfoPhase = 0.0;
        double lfoHold  = 0.0;
        bool   lfoHoldValid = false;

        juce::ADSR amp;
        juce::ADSR positionEnv;

        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::dsp::StateVariableTPTFilter<float> filter2;
        float appliedCutoff    = 0.0f;
        float appliedResonance = 0.0f;
        int   appliedMode      = -1;
        int   appliedSlope     = 0;

        /** Control rate state: pitch, cutoff and table position. */
        int    controlCountdown = 0;
        double pitchFactor = 1.0;
        double cutoffHz    = 8000.0;
        double position[2] { 0.0, 0.0 };
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    void applyEnvelopes (Voice& voice);

    std::array<Voice, kVoicesPerTrack> voices;
    WavetableParams params;
    /** The custom tables currently in use (null = the built-in one by index). */
    std::array<std::shared_ptr<const wavetable::Wavetable>, 2> customTables;
    /** See `getPublishedPosition` / `getPublishedVoices`. */
    Published published;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random random;
};

} // namespace nidaw
