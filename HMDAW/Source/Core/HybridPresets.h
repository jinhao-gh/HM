#pragma once

#include "Types.h"

#include <array>
#include <vector>

namespace hmdaw::hybridpresets
{

/** Original factory patches. They only use built-in wavetables, Sub and Noise,
    so every patch can be auditioned without importing a file. */
struct FactoryPreset
{
    const char* name;
    const char* category;
    void (*build) (HybridParams&, int);
    int variant;
};

inline void clear (HybridParams& p)
{
    p = HybridParams();
    p.gain = 0.8f;

    for (auto& layer : p.layers)
    {
        layer.enabled = false;
        layer.mode = HybridLayerMode::wavetable;
        layer.level = 0.0f;
        layer.pan = 0.0f;
    }

    p.sub.enabled = false;
    p.noise.enabled = false;
    p.filters[1].mode = FilterMode::lowPass;
    p.filterRouting = HybridFilterRouting::series;
    p.envelopes[0].attack = 0.005f;
    p.envelopes[0].decay = 0.2f;
    p.envelopes[0].sustain = 0.7f;
    p.envelopes[0].release = 0.3f;
    p.envelopes[1].attack = 0.005f;
    p.envelopes[1].decay = 0.35f;
    p.envelopes[1].sustain = 0.25f;
    p.envelopes[1].release = 0.25f;
    p.envelopes[2] = p.envelopes[1];
    p.envelopes[3] = p.envelopes[1];
}

inline void layer (HybridParams& p, int index, int table, float position,
                   float level, int unison = 1, float detune = 12.0f,
                   int octave = 0, float pan = 0.0f)
{
    auto& l = p.layers[(size_t) index];
    l.enabled = true;
    l.mode = HybridLayerMode::wavetable;
    l.level = level;
    l.pan = pan;
    auto& o = l.wavetable.oscs[0];
    o.tableIndex = table;
    o.position = position;
    o.unisonVoices = unison;
    o.unisonDetuneCents = detune;
    o.octave = octave;
    o.level = 1.0f;
    l.wavetable.oscs[1].enabled = false;
    l.wavetable.ampAttack = 0.005f;
    l.wavetable.ampDecay = 0.3f;
    l.wavetable.ampSustain = 0.8f;
    l.wavetable.ampRelease = 0.3f;
    l.wavetable.filterCutoffHz = 20000.0f;
}

/** The second table oscillator of a layer: detuned body, an octave layer, or both. */
inline void osc2 (HybridParams& p, int index, int table, float position, float level,
                  float detuneCents = 0.0f, int octave = 0, int unison = 1,
                  float unisonDetune = 8.0f)
{
    auto& o = p.layers[(size_t) index].wavetable.oscs[1];
    o.enabled = true;
    o.tableIndex = table;
    o.position = position;
    o.level = level;
    o.detuneCents = detuneCents;
    o.octave = octave;
    o.unisonVoices = unison;
    o.unisonDetuneCents = unisonDetune;
}

/** The per-layer amp envelope of a wavetable layer (the one the engine reads first). */
inline void layerAmp (HybridParams& p, int index, float a, float d, float s, float r)
{
    auto& w = p.layers[(size_t) index].wavetable;
    w.ampAttack = a;
    w.ampDecay = d;
    w.ampSustain = s;
    w.ampRelease = r;
}

/** The position envelope: what makes a table sweep while the note is held. */
inline void posEnv (HybridParams& p, int index, float amount, float attack, float decay)
{
    auto& w = p.layers[(size_t) index].wavetable;
    w.posEnvAmount = amount;
    w.posAttack = attack;
    w.posDecay = decay;
    w.posSustain = 0.0f;
    w.posRelease = decay;
}

inline void adsr (HybridParams& p, float a, float d, float s, float r)
{
    p.envelopes[0].attack = a;
    p.envelopes[0].decay = d;
    p.envelopes[0].sustain = s;
    p.envelopes[0].release = r;
}

inline void env (HybridParams& p, int index, float a, float d, float s, float r)
{
    auto& e = p.envelopes[(size_t) index];
    e.attack = a;
    e.decay = d;
    e.sustain = s;
    e.release = r;
}

inline void filter (HybridParams& p, int which, FilterMode mode, float cutoff,
                    float resonance, float envAmount = 0.0f, float keyTrack = 0.0f)
{
    auto& f = p.filters[(size_t) which];
    f.mode = mode;
    f.slopeDb = 24;
    f.cutoffHz = cutoff;
    f.resonance = resonance;
    f.envAmount = envAmount;
    f.keyTrack = keyTrack;
}

inline void sub (HybridParams& p, int waveform, int octave, float level, float pan = 0.0f)
{
    p.sub.enabled = true;
    p.sub.waveform = waveform;
    p.sub.octave = octave;
    p.sub.level = level;
    p.sub.pan = pan;
}

inline void noise (HybridParams& p, int type, float level, float tone, float pan = 0.0f)
{
    p.noise.enabled = true;
    p.noise.type = type;
    p.noise.level = level;
    p.noise.toneHz = tone;
    p.noise.pan = pan;
}

inline void fx (HybridParams& p, int slot, HybridFxType type, float mix,
                float p1 = 0.5f, float p2 = 0.5f)
{
    auto& s = p.fxSlots[(size_t) slot];
    s.enabled = true;
    s.type = type;
    s.mix = mix;
    s.param1 = p1;
    s.param2 = p2;
}

inline void mod (HybridParams& p, HybridModSource source,
                 HybridModDestination destination, float amount)
{
    for (auto& slot : p.modSlots)
        if (! slot.enabled)
        {
            slot.enabled = true;
            slot.source = source;
            slot.destination = destination;
            slot.amount = amount;
            return;
        }
}

inline void lfo (HybridParams& p, int index, int waveform, float rateMs,
                 float depth, HybridModDestination destination, float amount,
                 float fadeMs = 0.0f)
{
    auto& l = p.lfos[(size_t) index];
    l.waveform = waveform;
    l.rateMs = rateMs;
    l.rateHz = 1000.0f / juce::jmax (1.0f, rateMs);
    l.depth = depth;
    l.fadeMs = fadeMs;
    l.retrigger = true;
    mod (p, (HybridModSource) ((int) HybridModSource::lfo1 + index),
         destination, amount);
}

inline void macro (HybridParams& p, int index, const char* name, float value = 0.0f)
{
    p.macroNames[(size_t) index] = juce::String::fromUTF8 (name);
    p.macros[(size_t) index] = value;
}

//==============================================================================
inline void piano (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.002f, 1.2f, 0.08f, 1.1f);
    filter (p, 0, FilterMode::lowPass, 9000.0f, 0.9f, 0.15f);
    fx (p, 0, HybridFxType::reverb, 0.22f, 0.55f, 0.35f);

    if (v == 0)
    {
        layer (p, 0, 10, 0.45f, 0.70f);
        osc2 (p, 0, 4, 0.30f, 0.22f, 3.0f, 1, 2);
        mod (p, HybridModSource::velocity, HybridModDestination::layer1Position, 0.10f);
    }
    else if (v == 1)
    {
        layer (p, 0, 4, 0.35f, 0.66f, 2, 7.0f);
        osc2 (p, 0, 12, 0.55f, 0.16f, -3.0f, 1);
        sub (p, 0, -1, 0.12f);
        fx (p, 1, HybridFxType::chorus, 0.42f, 0.3f, 0.7f);
        mod (p, HybridModSource::velocity, HybridModDestination::filter1Cutoff, 0.18f);
    }
    else if (v == 2)
    {
        layer (p, 0, 13, 0.20f, 0.65f, 2, 9.0f);
        osc2 (p, 0, 11, 0.55f, 0.20f, 4.0f, 1);
        fx (p, 1, HybridFxType::chorus, 0.5f, 0.25f, 0.8f);
        lfo (p, 0, 0, 3400.0f, 0.4f, HybridModDestination::layer1Position, 0.08f, 900.0f);
    }
    else if (v == 3)
    {
        layer (p, 0, 11, 0.65f, 0.68f, 4, 9.0f);
        filter (p, 0, FilterMode::lowPass, 6200.0f, 1.4f, 0.30f);
        fx (p, 1, HybridFxType::delay, 0.16f, 0.3f, 0.25f);
        env (p, 1, 0.002f, 0.45f, 0.0f, 0.4f);
        mod (p, HybridModSource::env2, HybridModDestination::filter1Cutoff, 0.35f);
    }
    else if (v == 4)
    {
        layer (p, 0, 7, 0.80f, 0.55f);
        osc2 (p, 0, 2, 0.40f, 0.20f, 0.0f, 1);
        adsr (p, 0.002f, 0.5f, 0.02f, 0.6f);
        fx (p, 1, HybridFxType::bitcrush, 0.22f, 0.25f, 0.4f);
        fx (p, 2, HybridFxType::reverb, 0.2f, 0.5f, 0.3f);
    }
    else
    {
        layer (p, 0, 4, 0.30f, 0.55f);
        layer (p, 1, 10, 0.55f, 0.40f, 2, 8.0f, 1);
        fx (p, 1, HybridFxType::chorus, 0.45f, 0.25f, 0.75f);
        mod (p, HybridModSource::velocity, HybridModDestination::layer2Level, 0.20f);
        mod (p, HybridModSource::env2, HybridModDestination::layer2Position, 0.15f);
    }
}

inline void guitar (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.002f, 0.9f, 0.02f, 0.7f);
    filter (p, 0, FilterMode::lowPass, 5200.0f, 1.8f, 0.25f, 0.35f);
    fx (p, 0, HybridFxType::delay, 0.18f, 0.32f, 0.25f);

    if (v == 0)
    {
        layer (p, 0, 8, 0.55f, 0.70f, 2, 8.0f);
        fx (p, 1, HybridFxType::chorus, 0.35f, 0.3f, 0.6f);
        lfo (p, 0, 0, 4800.0f, 0.3f, HybridModDestination::filter1Cutoff, 0.12f, 600.0f);
    }
    else if (v == 1)
    {
        layer (p, 0, 1, 0.30f, 0.68f, 2, 5.0f);
        osc2 (p, 0, 8, 0.6f, 0.16f, 6.0f, 0, 2);
        fx (p, 1, HybridFxType::chorus, 0.42f, 0.25f, 0.65f);
        mod (p, HybridModSource::velocity, HybridModDestination::filter1Cutoff, 0.25f);
    }
    else if (v == 2)
    {
        layer (p, 0, 7, 0.72f, 0.62f, 3, 10.0f);
        adsr (p, 0.002f, 0.35f, 0.55f, 0.4f);
        filter (p, 0, FilterMode::lowPass, 4800.0f, 2.4f, 0.45f, 0.3f);
        fx (p, 1, HybridFxType::distortion, 0.55f, 0.7f, 0.45f);
    }
    else if (v == 3)
    {
        layer (p, 0, 0, 0.80f, 0.55f, 4, 14.0f);
        layer (p, 1, 2, 0.35f, 0.30f, 2, 11.0f, 1, 0.2f);
        fx (p, 1, HybridFxType::reverb, 0.35f, 0.5f, 0.4f);
        mod (p, HybridModSource::lfo2, HybridModDestination::layer2Pan, 0.3f);
    }
    else if (v == 4)
    {
        layer (p, 0, 6, 0.90f, 0.58f, 2, 6.0f);
        filter (p, 0, FilterMode::bandPass, 2400.0f, 3.2f, 0.40f, 0.4f);
        fx (p, 1, HybridFxType::delay, 0.28f, 0.42f, 0.3f);
        env (p, 2, 0.001f, 0.6f, 0.0f, 0.5f);
        mod (p, HybridModSource::env3, HybridModDestination::filter1Cutoff, 0.30f);
    }
    else
    {
        layer (p, 0, 3, 0.10f, 0.62f, 2, 7.0f);
        posEnv (p, 0, 0.7f, 0.35f, 0.9f);
        layerAmp (p, 0, 0.25f, 0.9f, 0.6f, 0.8f);
        fx (p, 1, HybridFxType::reverb, 0.4f, 0.6f, 0.5f);
        lfo (p, 0, 0, 5200.0f, 0.35f, HybridModDestination::layer1Position, 0.10f);
    }
}

inline void bass (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.003f, 0.45f, 0.55f, 0.2f);
    filter (p, 0, FilterMode::lowPass, 700.0f, 3.2f, 0.55f, 0.35f);

    if (v == 0)
    {
        layer (p, 0, 13, 0.25f, 0.65f, 1, 0.0f, -1);
        sub (p, 0, -1, 0.35f);
        mod (p, HybridModSource::velocity, HybridModDestination::filter1Cutoff, 0.30f);
    }
    else if (v == 1)
    {
        layer (p, 0, 1, 0.55f, 0.70f, 2, 7.0f, -1);
        osc2 (p, 0, 0, 0.2f, 0.18f, -5.0f, 0);
        fx (p, 0, HybridFxType::distortion, 0.38f, 0.55f, 0.35f);
        mod (p, HybridModSource::env2, HybridModDestination::filter1Cutoff, 0.45f);
    }
    else if (v == 2)
    {
        sub (p, 0, -2, 0.60f);
        layer (p, 0, 0, 0.10f, 0.30f, 1, 0.0f, -1);
        adsr (p, 0.004f, 0.6f, 0.7f, 0.25f);
        filter (p, 0, FilterMode::lowPass, 450.0f, 1.2f);
    }
    else if (v == 3)
    {
        layer (p, 0, 1, 0.45f, 0.65f, 2, 5.0f, -1);
        sub (p, 0, -1, 0.25f);
        filter (p, 0, FilterMode::lowPass, 450.0f, 5.0f, 0.80f, 0.4f);
        fx (p, 0, HybridFxType::distortion, 0.45f, 0.8f, 0.55f);
        macro (p, 0, "酸度", 0.6f);
        mod (p, HybridModSource::macro1, HybridModDestination::filter1Cutoff, 0.25f);
    }
    else if (v == 4)
    {
        layer (p, 0, 0, 0.15f, 0.55f, 5, 18.0f, -1);
        osc2 (p, 0, 1, 0.60f, 0.30f, -9.0f, 0, 3, 14.0f);
        sub (p, 0, -1, 0.22f);
        filter (p, 0, FilterMode::lowPass, 900.0f, 2.2f, 0.30f, 0.3f);
        fx (p, 0, HybridFxType::chorus, 0.30f, 0.3f, 0.7f);
        lfo (p, 0, 0, 2200.0f, 0.5f, HybridModDestination::layer1Detune, 0.10f, 400.0f);
    }
    else
    {
        layer (p, 0, 7, 0.80f, 0.60f, 2, 6.0f, -1);
        filter (p, 0, FilterMode::bandPass, 1200.0f, 4.0f, 0.50f, 0.2f);
        adsr (p, 0.001f, 0.30f, 0.35f, 0.15f);
        fx (p, 0, HybridFxType::distortion, 0.35f, 0.6f, 0.4f);
        fx (p, 1, HybridFxType::bitcrush, 0.20f, 0.35f, 0.6f);
    }
}

inline void lead (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.01f, 0.35f, 0.75f, 0.3f);
    filter (p, 0, FilterMode::lowPass, 7000.0f, 2.0f, 0.2f, 0.35f);
    fx (p, 0, HybridFxType::delay, 0.2f, 0.28f, 0.3f);

    if (v == 0)
    {
        layer (p, 0, 0, 0.6f, 0.62f, 8, 18.0f);
        osc2 (p, 0, 4, 0.4f, 0.18f, 7.0f, 1);
        fx (p, 1, HybridFxType::chorus, 0.32f, 0.25f, 0.7f);
        lfo (p, 0, 0, 5200.0f, 0.25f, HybridModDestination::layer1Detune, 0.05f, 1200.0f);
        macro (p, 0, "宽度", 0.5f);
    }
    else if (v == 1)
    {
        layer (p, 0, 0, 0.18f, 0.65f, 3, 6.0f);
        filter (p, 0, FilterMode::lowPass, 5200.0f, 1.0f);
        mod (p, HybridModSource::velocity, HybridModDestination::filter1Cutoff, 0.22f);
    }
    else if (v == 2)
    {
        layer (p, 0, 1, 0.78f, 0.58f, 2, 8.0f);
        filter (p, 0, FilterMode::bandPass, 1800.0f, 3.0f, 0.4f, 0.4f);
        env (p, 2, 0.002f, 0.5f, 0.1f, 0.4f);
        mod (p, HybridModSource::env3, HybridModDestination::filter1Cutoff, 0.35f);
    }
    else if (v == 3)
    {
        layer (p, 0, 1, 0.50f, 0.64f, 4, 10.0f);
        filter (p, 0, FilterMode::lowPass, 3800.0f, 2.2f, 0.35f, 0.3f);
        fx (p, 1, HybridFxType::reverb, 0.18f, 0.4f, 0.3f);
        lfo (p, 0, 0, 4300.0f, 0.3f, HybridModDestination::filter1Cutoff, 0.10f, 800.0f);
    }
    else if (v == 4)
    {
        layer (p, 0, 7, 0.90f, 0.55f, 2, 9.0f);
        filter (p, 0, FilterMode::bandPass, 2500.0f, 6.0f, 0.55f, 0.3f);
        fx (p, 1, HybridFxType::distortion, 0.40f, 0.65f, 0.5f);
        env (p, 1, 0.002f, 0.4f, 0.2f, 0.35f);
        mod (p, HybridModSource::env2, HybridModDestination::filter1Cutoff, 0.45f);
    }
    else
    {
        layer (p, 0, 2, 0.40f, 0.66f);
        osc2 (p, 0, 2, 0.42f, 0.20f, 8.0f, 0);
        filter (p, 0, FilterMode::lowPass, 3000.0f, 1.6f, 0.25f, 0.25f);
        adsr (p, 0.02f, 0.25f, 0.65f, 0.25f);
        fx (p, 1, HybridFxType::chorus, 0.30f, 0.2f, 0.6f);
    }
}

inline void pad (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.8f, 1.5f, 0.85f, 1.8f);
    filter (p, 0, FilterMode::lowPass, 4200.0f, 1.1f);
    fx (p, 0, HybridFxType::reverb, 0.28f, 0.6f, 0.35f);
    fx (p, 1, HybridFxType::chorus, 0.3f, 0.2f, 0.65f);

    if (v == 0)
    {
        layer (p, 0, 0, 0.35f, 0.55f, 3, 10.0f);
        layer (p, 1, 2, 0.55f, 0.30f, 2, 12.0f, 0, -0.25f);
        lfo (p, 0, 0, 6400.0f, 0.35f, HybridModDestination::filter1Cutoff, 0.18f, 1500.0f);
    }
    else if (v == 1)
    {
        layer (p, 0, 10, 0.45f, 0.52f, 4, 9.0f);
        layer (p, 1, 3, 0.70f, 0.24f, 1, 0.0f, 1);
        filter (p, 0, FilterMode::lowPass, 6000.0f, 0.8f);
        mod (p, HybridModSource::keyTrack, HybridModDestination::filter1Cutoff, 0.15f);
    }
    else if (v == 2)
    {
        layer (p, 0, 11, 0.30f, 0.50f);
        noise (p, 1, 0.12f, 1800.0f);
        filter (p, 0, FilterMode::bandPass, 1600.0f, 2.0f);
        lfo (p, 0, 4, 6200.0f, 0.55f, HybridModDestination::filter1Cutoff, 0.35f, 1200.0f);
    }
    else if (v == 3)
    {
        layer (p, 0, 0, 0.65f, 0.50f, 4, 12.0f);
        layer (p, 1, 2, 0.50f, 0.28f, 2, 8.0f, -1);
        adsr (p, 0.5f, 1.2f, 0.9f, 1.6f);
        filter (p, 0, FilterMode::lowPass, 3600.0f, 0.9f, 0.2f);
    }
    else if (v == 4)
    {
        layer (p, 0, 14, 0.70f, 0.48f, 2, 9.0f);
        osc2 (p, 0, 11, 0.20f, 0.22f, 5.0f, 0);
        posEnv (p, 0, 0.35f, 2.5f, 4.0f);
        lfo (p, 0, 0, 9000.0f, 0.5f, HybridModDestination::layer1Position, 0.10f);
    }
    else
    {
        layer (p, 0, 4, 0.50f, 0.55f, 5, 12.0f);
        env (p, 1, 1.2f, 2.0f, 0.6f, 2.5f);
        mod (p, HybridModSource::env2, HybridModDestination::layer1Position, 0.25f);
        fx (p, 2, HybridFxType::delay, 0.18f, 0.45f, 0.4f);
    }
}

inline void pluck (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.001f, 0.35f, 0.0f, 0.22f);
    filter (p, 0, FilterMode::lowPass, 6000.0f, 3.0f, 0.5f, 0.5f);
    fx (p, 0, HybridFxType::delay, 0.16f, 0.22f, 0.2f);

    if (v == 0)
    {
        layer (p, 0, 8, 0.65f, 0.72f, 2, 6.0f);
        mod (p, HybridModSource::velocity, HybridModDestination::filter1Cutoff, 0.30f);
    }
    else if (v == 1)
    {
        layer (p, 0, 4, 0.30f, 0.66f);
        layer (p, 1, 10, 0.50f, 0.24f, 2, 7.0f, 1);
        fx (p, 1, HybridFxType::reverb, 0.22f, 0.4f, 0.3f);
    }
    else if (v == 2)
    {
        layer (p, 0, 8, 0.60f, 0.68f, 3, 12.0f);
        osc2 (p, 0, 11, 0.5f, 0.20f, 0.0f, 1);
        fx (p, 1, HybridFxType::reverb, 0.30f, 0.55f, 0.45f);
    }
    else if (v == 3)
    {
        layer (p, 0, 7, 0.80f, 0.60f, 2, 9.0f);
        posEnv (p, 0, -0.6f, 0.05f, 0.45f);
        fx (p, 1, HybridFxType::reverb, 0.38f, 0.6f, 0.5f);
    }
    else if (v == 4)
    {
        layer (p, 0, 10, 0.85f, 0.64f, 2, 7.0f);
        filter (p, 0, FilterMode::bandPass, 3000.0f, 4.0f, 0.45f, 0.4f);
        fx (p, 1, HybridFxType::delay, 0.26f, 0.5f, 0.32f);
    }
    else
    {
        layer (p, 0, 2, 0.40f, 0.66f, 2, 8.0f);
        fx (p, 0, HybridFxType::delay, 0.30f, 0.55f, 0.45f);
        lfo (p, 0, 0, 1500.0f, 0.4f, HybridModDestination::fx1Mix, 0.18f, 0.0f);
    }
}

inline void brassStrings (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.06f, 0.45f, 0.75f, 0.35f);
    filter (p, 0, FilterMode::lowPass, 4800.0f, 1.8f, 0.3f, 0.25f);

    if (v == 0)
    {
        layer (p, 0, 1, 0.78f, 0.64f, 4, 9.0f);
        fx (p, 0, HybridFxType::reverb, 0.18f, 0.4f, 0.3f);
        mod (p, HybridModSource::env2, HybridModDestination::filter1Cutoff, 0.30f);
    }
    else if (v == 1)
    {
        layer (p, 0, 0, 0.70f, 0.55f, 5, 14.0f);
        layer (p, 1, 2, 0.50f, 0.30f, 2, 8.0f, 0, 0.2f);
        fx (p, 1, HybridFxType::chorus, 0.35f, 0.2f, 0.6f);
        lfo (p, 0, 0, 5600.0f, 0.3f, HybridModDestination::layer1Position, 0.10f, 1000.0f);
    }
    else if (v == 2)
    {
        layer (p, 0, 7, 0.35f, 0.58f, 2, 8.0f, -1);
        sub (p, 1, -1, 0.18f);
        filter (p, 0, FilterMode::lowPass, 2400.0f, 1.4f, 0.30f);
    }
    else if (v == 3)
    {
        layer (p, 0, 0, 0.55f, 0.58f, 3, 7.0f);
        adsr (p, 0.25f, 0.8f, 0.8f, 0.7f);
        fx (p, 0, HybridFxType::reverb, 0.30f, 0.5f, 0.4f);
        lfo (p, 0, 0, 7000.0f, 0.25f, HybridModDestination::layer1Detune, 0.06f, 1500.0f);
    }
    else if (v == 4)
    {
        layer (p, 0, 2, 0.45f, 0.60f, 4, 11.0f);
        fx (p, 0, HybridFxType::chorus, 0.35f, 0.25f, 0.7f);
        fx (p, 1, HybridFxType::reverb, 0.22f, 0.5f, 0.35f);
        mod (p, HybridModSource::lfo2, HybridModDestination::filter1Cutoff, 0.12f);
    }
    else
    {
        layer (p, 0, 1, 0.85f, 0.58f, 3, 9.0f);
        osc2 (p, 0, 8, 0.60f, 0.22f, 4.0f, 1);
        filter (p, 0, FilterMode::lowPass, 6000.0f, 2.0f, 0.40f, 0.3f);
        fx (p, 0, HybridFxType::distortion, 0.20f, 0.5f, 0.35f);
    }
}

inline void texture (HybridParams& p, int v)
{
    clear (p);
    adsr (p, 0.7f, 1.8f, 0.7f, 2.2f);
    filter (p, 0, FilterMode::bandPass, 1600.0f, 2.0f);
    fx (p, 0, HybridFxType::reverb, 0.38f, 0.75f, 0.45f);

    if (v == 0)
    {
        noise (p, 2, 0.28f, 2400.0f);
        filter (p, 0, FilterMode::highPass, 500.0f, 1.0f);
        lfo (p, 0, 4, 5000.0f, 0.6f, HybridModDestination::filter1Cutoff, 0.40f, 800.0f);
    }
    else if (v == 1)
    {
        layer (p, 0, 14, 0.70f, 0.42f);
        filter (p, 0, FilterMode::comb, 600.0f, 6.0f);
        fx (p, 1, HybridFxType::delay, 0.30f, 0.4f, 0.45f);
    }
    else if (v == 2)
    {
        layer (p, 0, 11, 0.55f, 0.42f);
        fx (p, 1, HybridFxType::delay, 0.42f, 0.55f, 0.55f);
        lfo (p, 0, 1, 9000.0f, 0.35f, HybridModDestination::fx1Mix, 0.40f, 1500.0f);
    }
    else if (v == 3)
    {
        noise (p, 1, 0.18f, 900.0f);
        layer (p, 0, 3, 0.30f, 0.32f);
        lfo (p, 0, 2, 1200.0f, 0.7f, HybridModDestination::layer1Level, 0.25f, 200.0f);
    }
    else if (v == 4)
    {
        noise (p, 0, 0.16f, 1600.0f);
        filter (p, 0, FilterMode::bandPass, 1400.0f, 3.0f);
        fx (p, 1, HybridFxType::bitcrush, 0.30f, 0.45f, 0.6f);
        lfo (p, 0, 3, 700.0f, 0.5f, HybridModDestination::noiseLevel, 0.30f);
    }
    else
    {
        layer (p, 0, 14, 0.90f, 0.40f, 2, 10.0f);
        filter (p, 0, FilterMode::lowPass, 1200.0f, 3.5f, 0.85f, 0.2f);
        fx (p, 1, HybridFxType::delay, 0.30f, 0.5f, 0.5f);
        env (p, 1, 2.5f, 3.0f, 0.2f, 1.5f);
        mod (p, HybridModSource::env2, HybridModDestination::filter1Cutoff, 0.60f);
    }
}

//==============================================================================
inline std::vector<FactoryPreset>& bank()
{
    static std::vector<FactoryPreset> presets = {
        { "玻璃大钢琴", "钢琴与键盘", &piano, 0 },
        { "绒毯电钢", "钢琴与键盘", &piano, 1 },
        { "磁带电钢琴", "钢琴与键盘", &piano, 2 },
        { "机械音叉琴", "钢琴与键盘", &piano, 3 },
        { "玩具钢琴", "钢琴与键盘", &piano, 4 },
        { "层叠 FM 电钢", "钢琴与键盘", &piano, 5 },

        { "尼龙拨弦", "吉他", &guitar, 0 },
        { "电吉他清音", "吉他", &guitar, 1 },
        { "失真主音吉他", "吉他", &guitar, 2 },
        { "十二弦回响", "吉他", &guitar, 3 },
        { "泛音吉他", "吉他", &guitar, 4 },
        { "反向吉他", "吉他", &guitar, 5 },

        { "深潜正弦贝斯", "贝斯", &bass, 0 },
        { "锯齿咬合贝斯", "贝斯", &bass, 1 },
        { "子低音铺底", "贝斯", &bass, 2 },
        { "滑音酸性贝斯", "贝斯", &bass, 3 },
        { "Reese 宽贝斯", "贝斯", &bass, 4 },
        { "点击金属贝斯", "贝斯", &bass, 5 },

        { "齐奏宽主音", "主音", &lead, 0 },
        { "柔和领奏", "主音", &lead, 1 },
        { "方波穿刺", "主音", &lead, 2 },
        { "弯音铜管主音", "主音", &lead, 3 },
        { "尖叫主音", "主音", &lead, 4 },
        { "复古单音主音", "主音", &lead, 5 },

        { "温暖模拟铺底", "铺底", &pad, 0 },
        { "玻璃空气铺底", "铺底", &pad, 1 },
        { "颗粒云铺底", "铺底", &pad, 2 },
        { "电影弦乐铺底", "铺底", &pad, 3 },
        { "缓慢演进铺底", "铺底", &pad, 4 },
        { "合唱铺底", "铺底", &pad, 5 },

        { "短促拨弦", "拨弦", &pluck, 0 },
        { "木琴颗粒", "拨弦", &pluck, 1 },
        { "五度拨弦", "拨弦", &pluck, 2 },
        { "反向拨弦", "拨弦", &pluck, 3 },
        { "玻璃拨弦", "拨弦", &pluck, 4 },
        { "琶音拨弦", "拨弦", &pluck, 5 },

        { "合成铜管", "铜管与弦乐", &brassStrings, 0 },
        { "弦乐合奏", "铜管与弦乐", &brassStrings, 1 },
        { "低音铜管", "铜管与弦乐", &brassStrings, 2 },
        { "独奏弦乐", "铜管与弦乐", &brassStrings, 3 },
        { "复古弦乐", "铜管与弦乐", &brassStrings, 4 },
        { "铜管齐奏", "铜管与弦乐", &brassStrings, 5 },

        { "噪声风暴", "纹理与音效", &texture, 0 },
        { "金属共振", "纹理与音效", &texture, 1 },
        { "反向混响纹理", "纹理与音效", &texture, 2 },
        { "颗粒心跳", "纹理与音效", &texture, 3 },
        { "无线电静电", "纹理与音效", &texture, 4 },
        { "上升音效", "纹理与音效", &texture, 5 }
    };
    return presets;
}

inline int factoryPresetCount()
{
    return (int) bank().size();
}

inline juce::String category (int index)
{
    const auto& list = bank();
    return list.empty() ? juce::String()
                        : juce::String::fromUTF8 (list[(size_t) juce::jlimit (
                              0, (int) list.size() - 1, index)].category);
}

inline juce::String name (int index)
{
    const auto& list = bank();
    return list.empty() ? juce::String()
                        : juce::String::fromUTF8 (list[(size_t) juce::jlimit (
                              0, (int) list.size() - 1, index)].name);
}

inline void apply (HybridParams& params, int index)
{
    const auto& list = bank();

    if (list.empty())
        return;

    const auto i = juce::jlimit (0, (int) list.size() - 1, index);
    clear (params);
    list[(size_t) i].build (params, list[(size_t) i].variant);
    params.name = juce::String::fromUTF8 (list[(size_t) i].name);
}

} // namespace hmdaw::hybridpresets
