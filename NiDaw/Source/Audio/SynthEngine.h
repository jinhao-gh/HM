#pragma once

#include "../Core/MusicMath.h"
#include "../Core/PluginRendering.h"
#include "../Core/Types.h"
#include "../Core/Wavetables.h"
#include "SamplePlayer.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <set>
#include <vector>

namespace nidaw
{

/** One scheduled note-on / note-off, in seconds from the start of the project. */
struct SynthEvent
{
    double      timeSeconds = 0.0;
    juce::int64 noteId      = 0;
    bool        isNoteOn    = true;
    /** A pitch-only update for the voice named by noteId. */
    bool        isPitch     = false;
    float       frequency   = 440.0f;
    float       velocity    = 100.0f;
    /** Which drum this event hits; -1 for a tonal note. Drum tracks only. */
    int         pad         = -1;
};

/** One controller value the user drew, placed on the timeline.

    The lane's curve is turned into a stream of these when the schedule is built, so
    the audio thread never looks at the curve itself: it walks the events the same way
    it walks the notes, sends them to an external instrument, and uses two of them
    (expression, sustain) for the built-in ones.
*/
struct ControllerEvent
{
    double      timeSeconds = 0.0;
    int         controller  = 11;
    int         value       = 0;
};

struct TrackRenderInfo
{
    juce::int64 trackId      = 0;
    TrackKind   kind         = TrackKind::synth;
    SynthParams synth;
    SamplerParams sampler;
    SubtractiveParams subtractive;
    WavetableParams   wavetable;
    GranularParams    granular;
    HybridParams      hybrid;
    std::array<std::shared_ptr<const LoadedSample>, 3> hybridSamples;
    std::array<std::array<std::shared_ptr<const wavetable::Wavetable>, 2>, 3> hybridWavetables;
    /** The project's own tables for this instrument's two oscillators, when they
        use one (null = use the built-in table `oscs[i].tableIndex`). The pointers
        live inside this immutable schedule, exactly like `sample` does. */
    std::array<std::shared_ptr<const wavetable::Wavetable>, 2> wavetables;
    DrumKitParams     drums;
    /** A hosted external instrument, when this is a VST track and one is up.
        The engine treats it exactly like the built-in instruments. */
    std::shared_ptr<HostedInstrument> instrument;
    /** True when this VST track plays equal tempered semitones instead of exact
        frequencies (a plugin whose pitch bend is global cannot do per-note tuning -
        see `VstParams::equalTemperament`). The engine snaps the frequency before the
        instrument sees it, so no bend is ever sent for this track. */
    bool equalTemperament = false;
    /** The track's insert chain, in signal order. A slot whose plugin is missing
        (or still loading) has a null entry, which the renderer skips. */
    std::vector<std::shared_ptr<HostedEffect>> effects;
    /** The decoded sample for a sampler track (null when nothing is loaded). */
    std::shared_ptr<const LoadedSample> sample;
    /** Placed audio clips of an audio track, ready to mix. */
    std::vector<AudioPlacement> audioClips;
    float       volumeLinear = 1.0f;
    float       pan          = 0.0f;
    std::vector<SynthEvent> events;   // sorted by timeSeconds
    /** The drawn controller curves, also sorted by time (see ControllerEvent). */
    std::vector<ControllerEvent> controllers;
};

/** Immutable snapshot of everything the audio thread needs. */
struct RenderSchedule
{
    std::vector<TrackRenderInfo> tracks;
    double lengthSeconds = 0.0;
    /** The project's master fader, as a linear gain. Applied to the whole mix at the
        very end, so what you hear and what an export writes go through the same
        number (1.0 = 0 dB, which is what every project written before the mixer had). */
    float masterGain = 1.0f;
};

/** Builds the render snapshot of a project.

    `onlyTracks`, when not empty, keeps just those tracks: that is how a stem
    export renders one track at a time through exactly the same code path as the
    full mix. An empty set means "every track".
*/
RenderSchedule buildSchedule (const Project& project,
                             const std::set<juce::int64>& onlyTracks = {},
                             const HostedInstrumentFactory& instrumentFactory = {},
                             const HostedEffectFactory& effectFactory = {});


/** The routing of one FM algorithm.

    `modulators[i]` is a bitmask of the operators that modulate operator i+1, and
    `carriers` a bitmask of those that reach the output. Every modulator has a
    higher number than the operator it feeds, which is what lets the engine
    evaluate operators from 6 down to 1 in one pass.
*/
struct FmAlgorithm
{
    juce::uint8 modulators[kFmOperators];
    juce::uint8 carriers;
    const char* diagram;
    /** 0-based operator that feeds back into itself. Most DX7 algorithms put it
        on OP6, but a few (2, 8, 10, 12, 15, 18, 20, 21, 27) do not. */
    int feedbackOp = 5;
};

/** The algorithm routing, clamped to 1..64. 1..32 are HM's own routings,
    33..64 the DX7 factory ones (index = 32 + Yamaha algorithm). */
const FmAlgorithm& getFmAlgorithm (int index) noexcept;

//==============================================================================
/** A six-operator FM synth in the DX7 idiom, sixteen voices.

    Each voice runs six sine operators; the algorithm decides which of them
    modulate which and which reach the output. Every operator carries its own
    four-stage RATE/LEVEL envelope, so a note's start, its end and the velocity
    it was played at all shape both the loudness and the timbre.

    Deliberately free of any GUI dependency so the same core can be wrapped into
    a plugin later.
*/
class TrackSynth
{
public:
    TrackSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const SynthParams& newParams);
    const SynthParams& getParams() const noexcept { return params; }

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

    /** Adds this synth's output into the first two channels of the buffer. */
    void renderAdding (juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       float gainLeft,
                       float gainRight);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;

private:
    struct Voice
    {
        bool        active    = false;
        bool        released  = false;
        juce::int64 noteId    = 0;
        double      frequency = 440.0;
        float       amplitude = 1.0f;   // velocity, 0..1
        int         midiNote  = 60;     // for the keyboard rate scaling
        juce::uint32 startOrder = 0;

        /** One operator's oscillator and envelope. */
        struct Operator
        {
            double phase      = 0.0;
            double envelope   = 0.0;   // raw 0..99 level being traced
            double step       = 0.0;   // per-sample change of `envelope`
            int    stage      = 0;     // 0..3 = R1..R4, 4 = finished
            double remaining  = 0.0;   // samples left in the current stage
            double output     = 0.0;   // last sample, used as a modulator
            bool   holding    = false; // sitting on the sustain level
            bool   finished   = false;
        };

        Operator ops[kFmOperators];

        /** Keyboard level scaling + velocity offset per operator, in 0..99 level
            units, worked out once per note. */
        float opLevelOffset[kFmOperators] = { 0, 0, 0, 0, 0, 0 };

        /** The DX7 pitch envelope (classic engine only). */
        double pitchEnvelope   = 50.0;
        double pitchStep       = 0.0;
        double pitchRemaining  = 0.0;
        int    pitchStage      = 0;
        bool   pitchHolding    = false;
        bool   pitchFinished   = false;

        double feedbackMemory = 0.0;
        double lfoPhase       = 0.0;
        double lfoDelayTime   = 0.0;
        double lfoHold        = 0.0;
        bool   lfoHoldValid   = false;

        juce::dsp::StateVariableTPTFilter<float> filter;
        float appliedCutoff = 0.0f;
        float appliedResonance = 0.0f;
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    void startVoice (Voice& voice);
    /** Points an operator's envelope at the next segment (R1..R4). */
    void beginSegment (Voice& voice, int opIndex, int stage);
    /** The same for the patch pitch envelope (classic engine). */
    void beginPitchSegment (Voice& voice, int stage);

    std::array<Voice, kVoicesPerTrack> voices;
    SynthParams params;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random lfoRandom;
    /** Classic engine with LFO key sync off: one phase shared by every voice. */
    double globalLfoPhase = 0.0;
};

} // namespace nidaw
