#pragma once

#include "Types.h"

namespace hmdaw::subtractive
{

/** Builds one oscillator, so a preset only spells out what it uses. */
inline SubtractiveOsc osc (OscWave wave, float level, int octave = 0, int semitone = 0,
                           float detuneCents = 0.0f, float pulseWidth = 0.5f)
{
    SubtractiveOsc o;
    o.wave        = wave;
    o.level       = level;
    o.octave      = octave;
    o.semitone    = semitone;
    o.detuneCents = detuneCents;
    o.pulseWidth  = pulseWidth;
    return o;
}

/** A named factory sound. */
struct Preset
{
    const char* name;
    void (*build) (SubtractiveParams&);
};

// The bank below is written for this project: original analogue-style patches
// (a warm pad, a bright lead, a deep bass and so on), not copies of any
// manufacturer's factory sounds.

inline void warmPad (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.80f);
    p.oscs[1] = osc (OscWave::saw, 0.70f, 0, 0, 7.0f);
    p.filterCutoffHz  = 1200.0f;
    p.filterResonance = 0.80f;
    p.filterEnvAmount = 0.35f;
    p.ampAttack = 0.60f; p.ampDecay = 1.20f; p.ampSustain = 0.80f; p.ampRelease = 1.50f;
    p.filterAttack = 0.80f; p.filterDecay = 1.50f; p.filterSustain = 0.50f; p.filterRelease = 1.20f;
    p.lfoRateHz = 4.5f; p.lfoPitchCents = 6.0f; p.lfoCutoffDepth = 0.10f;
}

inline void brightLead (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::square, 0.90f);
    p.oscs[1] = osc (OscWave::saw, 0.50f, 0, 0, -6.0f);
    p.filterCutoffHz  = 6000.0f;
    p.filterResonance = 2.50f;
    p.filterEnvAmount = 0.50f;
    p.filterKeyTrack  = 0.50f;
    p.ampAttack = 0.010f; p.ampDecay = 0.250f; p.ampSustain = 0.80f; p.ampRelease = 0.250f;
    p.filterAttack = 0.010f; p.filterDecay = 0.350f; p.filterSustain = 0.50f; p.filterRelease = 0.300f;
    p.lfoRateHz = 5.5f; p.lfoPitchCents = 8.0f;
}

inline void deepBass (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.90f, -1);
    p.oscs[1] = osc (OscWave::pulse, 0.60f, -1, 0, -5.0f, 0.40f);
    p.filterCutoffHz  = 500.0f;
    p.filterResonance = 3.00f;
    p.filterEnvAmount = 0.80f;
    p.ampAttack = 0.005f; p.ampDecay = 0.300f; p.ampSustain = 0.50f; p.ampRelease = 0.150f;
    p.filterAttack = 0.005f; p.filterDecay = 0.250f; p.filterSustain = 0.20f; p.filterRelease = 0.200f;
}

inline void pluckSynth (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.90f);
    p.oscs[1] = osc (OscWave::square, 0.40f, 1);
    p.filterCutoffHz  = 3000.0f;
    p.filterResonance = 4.00f;
    p.filterEnvAmount = 0.90f;
    p.filterKeyTrack  = 0.40f;
    p.ampAttack = 0.002f; p.ampDecay = 0.350f; p.ampSustain = 0.00f; p.ampRelease = 0.250f;
    p.filterAttack = 0.002f; p.filterDecay = 0.180f; p.filterSustain = 0.00f; p.filterRelease = 0.200f;
}

inline void strings (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.80f);
    p.oscs[1] = osc (OscWave::saw, 0.80f, 0, 0, 9.0f);
    p.filterCutoffHz  = 2400.0f;
    p.filterResonance = 1.00f;
    p.filterEnvAmount = 0.30f;
    p.ampAttack = 0.35f; p.ampDecay = 0.80f; p.ampSustain = 0.85f; p.ampRelease = 0.80f;
    p.filterAttack = 0.50f; p.filterDecay = 1.00f; p.filterSustain = 0.60f; p.filterRelease = 0.60f;
    p.lfoRateHz = 5.0f; p.lfoPitchCents = 5.0f;
}

inline void brass (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.90f);
    p.oscs[1] = osc (OscWave::pulse, 0.70f, 0, 0, -8.0f, 0.45f);
    p.filterCutoffHz  = 3500.0f;
    p.filterResonance = 2.00f;
    p.filterEnvAmount = 0.75f;
    p.filterKeyTrack  = 0.30f;
    p.ampAttack = 0.060f; p.ampDecay = 0.300f; p.ampSustain = 0.80f; p.ampRelease = 0.250f;
    p.filterAttack = 0.120f; p.filterDecay = 0.500f; p.filterSustain = 0.55f; p.filterRelease = 0.300f;
}

inline void rockOrgan (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::square, 0.90f);
    p.oscs[1] = osc (OscWave::square, 0.70f, 1);
    p.filterCutoffHz  = 5000.0f;
    p.filterResonance = 1.20f;
    p.filterEnvAmount = 0.20f;
    p.ampAttack = 0.005f; p.ampDecay = 0.050f; p.ampSustain = 1.00f; p.ampRelease = 0.060f;
    p.filterAttack = 0.005f; p.filterDecay = 0.100f; p.filterSustain = 0.80f; p.filterRelease = 0.100f;
    p.lfoRateHz = 6.5f; p.lfoAmpDepth = 0.12f;
}

inline void glassPad (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::triangle, 0.80f);
    p.oscs[1] = osc (OscWave::sine, 0.70f, 1);
    p.filterMode      = FilterMode::highPass;
    p.filterCutoffHz  = 300.0f;
    p.filterResonance = 1.20f;
    p.filterEnvAmount = 0.30f;
    p.ampAttack = 0.50f; p.ampDecay = 1.00f; p.ampSustain = 0.80f; p.ampRelease = 1.40f;
    p.filterAttack = 0.60f; p.filterDecay = 1.20f; p.filterSustain = 0.50f; p.filterRelease = 1.00f;
    p.lfoRateHz = 3.5f; p.lfoPitchCents = 4.0f; p.lfoCutoffDepth = 0.15f;
}

inline void sawLead (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.90f);
    p.oscs[1] = osc (OscWave::saw, 0.90f, 0, 0, 14.0f);
    p.filterCutoffHz  = 5200.0f;
    p.filterResonance = 1.60f;
    p.filterEnvAmount = 0.40f;
    p.ampAttack = 0.010f; p.ampDecay = 0.300f; p.ampSustain = 0.85f; p.ampRelease = 0.300f;
    p.filterAttack = 0.020f; p.filterDecay = 0.400f; p.filterSustain = 0.60f; p.filterRelease = 0.350f;
    p.lfoRateHz = 6.0f; p.lfoPitchCents = 10.0f;
}

inline void softLead (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::sine, 0.90f);
    p.oscs[1] = osc (OscWave::triangle, 0.60f, 0, 0, 5.0f);
    p.filterCutoffHz  = 4000.0f;
    p.filterResonance = 0.90f;
    p.filterEnvAmount = 0.25f;
    p.ampAttack = 0.050f; p.ampDecay = 0.400f; p.ampSustain = 0.85f; p.ampRelease = 0.400f;
    p.filterAttack = 0.080f; p.filterDecay = 0.500f; p.filterSustain = 0.60f; p.filterRelease = 0.400f;
    p.lfoRateHz = 4.5f; p.lfoPitchCents = 7.0f; p.lfoCutoffDepth = 0.10f;
}

inline void punchBass (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::square, 0.90f, -1);
    p.oscs[1] = osc (OscWave::saw, 0.50f, -1);
    p.noiseLevel      = 0.06f;
    p.filterCutoffHz  = 900.0f;
    p.filterResonance = 5.00f;
    p.filterEnvAmount = 1.00f;
    p.ampAttack = 0.001f; p.ampDecay = 0.220f; p.ampSustain = 0.25f; p.ampRelease = 0.120f;
    p.filterAttack = 0.001f; p.filterDecay = 0.120f; p.filterSustain = 0.00f; p.filterRelease = 0.150f;
}

inline void spacePad (SubtractiveParams& p)
{
    p.oscs[0] = osc (OscWave::saw, 0.75f);
    p.oscs[1] = osc (OscWave::saw, 0.75f, 0, 0, 18.0f);
    p.filterCutoffHz  = 1800.0f;
    p.filterResonance = 1.40f;
    p.filterEnvAmount = 0.40f;
    p.ampAttack = 0.90f; p.ampDecay = 1.50f; p.ampSustain = 0.85f; p.ampRelease = 2.00f;
    p.filterAttack = 1.20f; p.filterDecay = 2.00f; p.filterSustain = 0.55f; p.filterRelease = 1.60f;
    p.lfoRateHz = 2.2f; p.lfoPitchCents = 3.0f; p.lfoCutoffDepth = 0.25f; p.lfoAmpDepth = 0.10f;
}

/** The factory bank, in menu order. */
inline const Preset* bank (int& countOut)
{
    static const Preset presets[] =
    {
        { "温暖铺底", &warmPad },
        { "明亮主音", &brightLead },
        { "深潜低音", &deepBass },
        { "弹拨合成", &pluckSynth },
        { "弦乐合奏", &strings },
        { "铜管齐奏", &brass },
        { "摇滚风琴", &rockOrgan },
        { "玻璃铺底", &glassPad },
        { "锯齿主音", &sawLead },
        { "柔和领奏", &softLead },
        { "打击低音", &punchBass },
        { "空间铺底", &spacePad },
    };

    countOut = (int) (sizeof (presets) / sizeof (presets[0]));
    return presets;
}

inline int builtInCount()
{
    int count = 0;
    bank (count);
    return count;
}

/** Applies one factory preset. Index is clamped, so -1 and 999 are safe. */
inline void applyPreset (SubtractiveParams& params, int index)
{
    int count = 0;
    const auto* presets = bank (count);

    if (count <= 0)
        return;

    // Start from a clean patch so nothing from the previous sound leaks in.
    params = SubtractiveParams();

    const auto i = juce::jlimit (0, count - 1, index);
    presets[i].build (params);
    params.name = juce::String::fromUTF8 (presets[i].name);
}

} // namespace hmdaw::subtractive
