#pragma once

#include "../Core/Types.h"
#include "SamplePlayer.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace hmdaw
{

/** 粒子合成器: a cloud of grains read out of one imported audio file.

    Every sounding note spawns grains at `densityHz`; each grain has its own start
    position (the scan position plus the spray), its own length (`grainMs`) and its
    own rate - and that rate is the note's exact frequency divided by `rootHz`, the
    same arithmetic the sampler uses, so HM's frequencies stay exact.

    A grain is windowed with a Hann window, so a cloud of them never clicks. The
    cloud's loudness is compensated for the overlap (density × grain length), which
    keeps turning the density up from turning into an explosion.

    Polyphony is the engine's usual 16 voices; each voice may have up to 32 grains in
    the air, and a voice that needs a 33rd steals its oldest one.
*/
class GranularSynth
{
public:
    GranularSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const GranularParams& newParams);
    /** The file to read grains out of; nullptr means "silent". */
    void setSample (std::shared_ptr<const LoadedSample> newSample);
    const GranularParams& getParams() const noexcept { return params; }
    const std::shared_ptr<const LoadedSample>& getSample() const noexcept { return sample; }

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

    /** Adds this player's output into the first two channels of the buffer. */
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
    /** How many grains are in the air right now, across every voice. */
    int  activeGrainCount() const noexcept;

    // ---- what the editor's scatter view reads ---------------------------------
    static constexpr int kMaxGrainsPerVoice = 32;
    /** How many grains the scatter view can see at once (the audio thread only ever
        writes that many, oldest first dropped). */
    static constexpr int kPublishedGrains = 64;

    /** The numbers the editor reads: an atomic cannot be copied, and the engine keeps
        its instruments in a std::vector (which moves them), so this carries its
        values across a move instead of deleting the move. */
    struct Published
    {
        std::atomic<float> positions[kPublishedGrains] {};
        std::atomic<float> ages[kPublishedGrains] {};
        std::atomic<int>   grains { 0 };
        std::atomic<int>   voices { 0 };

        Published() = default;
        Published (const Published& other) { copyFrom (other); }
        Published& operator= (const Published& other) { copyFrom (other); return *this; }
        Published (Published&& other) noexcept { copyFrom (other); }
        Published& operator= (Published&& other) noexcept { copyFrom (other); return *this; }

        void copyFrom (const Published& other)
        {
            for (int i = 0; i < kPublishedGrains; ++i)
            {
                positions[i].store (other.positions[i].load (std::memory_order_relaxed));
                ages[i].store (other.ages[i].load (std::memory_order_relaxed));
            }

            grains.store (other.grains.load (std::memory_order_relaxed));
            voices.store (other.voices.load (std::memory_order_relaxed));
        }
    };

    int getPublishedGrainCount() const noexcept
    {
        return published.grains.load (std::memory_order_relaxed);
    }

    float getPublishedGrainPosition (int index) const noexcept
    {
        return published.positions[juce::jlimit (0, kPublishedGrains - 1, index)]
            .load (std::memory_order_relaxed);
    }

    float getPublishedGrainAge (int index) const noexcept
    {
        return published.ages[juce::jlimit (0, kPublishedGrains - 1, index)]
            .load (std::memory_order_relaxed);
    }

    int getPublishedVoices() const noexcept
    {
        return published.voices.load (std::memory_order_relaxed);
    }

    // ---- diagnostic hooks ------------------------------------------------------
    /** Grains started since the last reset: the density test reads the difference. */
    juce::int64 getStartedGrainCountForTest() const noexcept { return startedGrains; }
    /** The fold of "position + scanSpeed × time + the position envelope" for that
        voice, 0..1 - the number a grain's position is drawn around. -1 = no voice. */
    float getScanFractionForTest (juce::int64 noteId) const noexcept;
    /** The longest grain in the air, in seconds (the grain-length test reads it). */
    float getLongestGrainSecondsForTest() const noexcept;

private:
    struct Grain
    {
        bool   active   = false;
        double position = 0.0;   // read position, in source samples
        double rate     = 1.0;   // source samples per output sample (negative = reverse)
        double length   = 1.0;   // this grain's whole life, in output samples
        double elapsed  = 0.0;   // output samples since it started
        float  panLeft  = 1.0f;
        float  panRight = 1.0f;
        juce::uint32 startOrder = 0;
    };

    struct Voice
    {
        bool        active   = false;
        bool        released = false;
        juce::int64 noteId   = 0;
        float       amplitude = 1.0f;
        double      elapsedSeconds = 0.0;   // since the note started (scan + envelope)
        double      scanFraction   = 0.0;   // this sample's folded scan position
        double      noteRate = 1.0;         // source samples per output sample
        double      nextGrainIn = 0.0;      // seconds until the next grain
        juce::uint32 startOrder = 0;
        juce::uint32 rng = 1u;
        juce::ADSR  envelope;
        juce::ADSR  positionEnvelope;
        /** One two-stage cascade per output channel, so the grains' own panning
            survives the filter. */
        std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filterStage1, filterStage2;
        double keyTrackRatio = 1.0;
        float appliedCutoff = 0.0f;
        float appliedResonance = 0.0f;
        std::array<Grain, kMaxGrainsPerVoice> grains;
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    void applyEnvelopes (Voice& voice);
    void spawnGrain (Voice& voice);
    void publishGrains() noexcept;

    double randomUnit (Voice& voice) noexcept;

    static constexpr float kMasterTrim = 0.7f;

    std::array<Voice, kVoicesPerTrack> voices;
    GranularParams params;
    std::shared_ptr<const LoadedSample> sample;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::int64 startedGrains = 0;
    Published published;
};

} // namespace hmdaw
