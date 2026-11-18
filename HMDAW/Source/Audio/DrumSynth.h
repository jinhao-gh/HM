#pragma once

#include "../Core/Types.h"

#include <juce_dsp/juce_dsp.h>

namespace hmdaw
{

/** A twelve pad drum machine, every drum synthesised on the spot.

    Hits are one-shots: a note-off does not stop them, they decay on their own.
    The one exception is the classic hi-hat rule - a closed hat (or another open
    hat) chokes a ringing open hat.
*/
class DrumSynth
{
public:
    DrumSynth();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const DrumKitParams& newParams);
    const DrumKitParams& getParams() const noexcept { return params; }

    /** `pad` picks the drum; `velocity` 0..127 scales it. */
    void noteOn (juce::int64 noteId, int pad, float velocity);
    void noteOff (juce::int64 noteId);
    void allNotesOff (bool immediately);

    /** Adds this kit's output into the first two channels of the buffer. */
    void renderAdding (juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       float gainLeft,
                       float gainRight);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;

    /** Diagnostic hook: the pad the voice for this note is playing, -1 if none. */
    int getVoicePadForTest (juce::int64 noteId) const;

    /** How long a pad rings with the given kit settings, in seconds. Useful for
        tests and for the editor's "试听" buttons. */
    static double decaySecondsFor (int pad, const DrumKitParams& kit);

private:
    struct Voice
    {
        bool        active    = false;
        juce::int64 noteId    = 0;
        int         pad       = 0;
        float       amplitude = 1.0f;
        juce::uint32 startOrder = 0;

        double age        = 0.0;    // seconds since the hit
        double phase[2]   = { 0.0, 0.0 };
        double metalPhase = 0.0;
        double noise      = 0.0;    // last white sample, reused by the body
        double choke      = 1.0;    // scaled down when the hat is choked
        bool   choked     = false;

        juce::dsp::StateVariableTPTFilter<float> filter;
        float appliedCutoff    = 0.0f;
        float appliedResonance = 0.0f;
        int   appliedMode      = -1;
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    /** One sample of a voice, before the kit's pan and level are applied. */
    double renderVoiceSample (Voice& voice);

    std::array<Voice, kVoicesPerTrack> voices;
    DrumKitParams params;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
    juce::Random random;
};

} // namespace hmdaw
