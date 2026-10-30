#pragma once

#include "Types.h"

#include <vector>

namespace nidaw::minimoog
{

/** The controls the factory bank is built with. They are plain parameter
    setters, written in the units the engine uses, so a preset reads like a
    patch sheet rather than a list of knob positions. */

inline MinimoogOsc osc (MinimoogWave wave, MinimoogRange range, float level,
                        float tuneSemitones = 0.0f)
{
    MinimoogOsc o;
    o.wave = wave;
    o.range = range;
    o.level = level;
    o.tuneSemitones = tuneSemitones;
    return o;
}

inline void filter (MinimoogParams& p, float cutoffHz, float emphasis,
                    float contourOctaves, bool keyTrack1 = false, bool keyTrack2 = false)
{
    p.filterCutoffHz = cutoffHz;
    p.filterEmphasis = emphasis;
    p.filterContourOctaves = contourOctaves;
    p.filterKeyTrack1 = keyTrack1;
    p.filterKeyTrack2 = keyTrack2;
}

inline void filterEnv (MinimoogParams& p, float attack, float decay, bool shortDecay,
                       bool sustainOn)
{
    p.filterAttack = attack;
    p.filterDecay = decay;
    p.filterDecayShort = shortDecay;
    p.filterSustainOn = sustainOn;
}

inline void ampEnv (MinimoogParams& p, float attack, float decay, bool shortDecay,
                    bool sustainOn, bool releaseOn = true)
{
    p.ampAttack = attack;
    p.ampDecay = decay;
    p.ampDecayShort = shortDecay;
    p.ampSustainOn = sustainOn;
    p.releaseOn = releaseOn;
}

inline void glide (MinimoogParams& p, float seconds, bool on = true)
{
    p.glideSeconds = seconds;
    p.glideOn = on;
}

inline void modulation (MinimoogParams& p, float mix, float depth,
                        bool toPitch, bool toFilter)
{
    p.modMix = mix;
    p.modDepth = depth;
    p.oscModOn = toPitch;
    p.filterModOn = toFilter;
}

/** A named factory sound. `category` is the second-level menu the editor shows. */
struct Preset
{
    const char* name;
    const char* category;
    void (*build) (MinimoogParams&);
};

//==============================================================================
// The bank. Six categories of five; the sound names are the classic roles a
// Minimoog is used for, the parameter values are this project's own.
//==============================================================================

inline void fatBass (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.95f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.70f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::feet32, 0.40f);
    filter (p, 300.0f, 0.35f, 3.5f, true, false);
    filterEnv (p, 0.002f, 0.35f, false, true);
    ampEnv (p, 0.002f, 0.40f, false, true);
    p.volume = 0.80f;
}

inline void funkBass (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::square, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.50f);
    filter (p, 250.0f, 0.50f, 4.5f);
    filterEnv (p, 0.001f, 0.18f, true, false);
    ampEnv (p, 0.001f, 0.25f, true, false);
    p.volume = 0.78f;
}

inline void subBass (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet32, 1.00f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.60f);
    filter (p, 180.0f, 0.10f, 2.0f);
    filterEnv (p, 0.005f, 0.60f, false, true);
    ampEnv (p, 0.005f, 0.60f, false, true);
    p.volume = 0.75f;
}

inline void resonantBass (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.90f);
    p.oscs[1] = osc (MinimoogWave::widePulse, MinimoogRange::feet8, 0.50f);
    filter (p, 220.0f, 0.80f, 5.0f, true, false);
    filterEnv (p, 0.002f, 0.50f, false, true);
    ampEnv (p, 0.002f, 0.50f, false, true);
    p.volume = 0.70f;
}

inline void glideBass (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.65f, 0.12f);
    filter (p, 350.0f, 0.40f, 3.0f, true, false);
    filterEnv (p, 0.003f, 0.30f, false, true);
    ampEnv (p, 0.003f, 0.35f, false, true);
    glide (p, 0.18f);
    p.volume = 0.78f;
}

inline void classicLead (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.75f, 0.08f);
    p.oscs[2] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.60f, -0.08f);
    filter (p, 900.0f, 0.55f, 4.5f, true, true);
    filterEnv (p, 0.010f, 0.45f, false, true);
    ampEnv (p, 0.005f, 0.40f, false, true);
    glide (p, 0.12f);
    p.volume = 0.72f;
}

inline void fifthLead (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.80f, 7.0f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.40f);
    filter (p, 1200.0f, 0.45f, 3.5f);
    filterEnv (p, 0.010f, 0.40f, false, true);
    ampEnv (p, 0.005f, 0.40f, false, true);
    glide (p, 0.08f);
    p.volume = 0.72f;
}

inline void whistleLead (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.90f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet2, 0.50f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::lo, 0.0f);
    p.osc3Control = false;
    modulation (p, 0.0f, 0.35f, true, false);
    filter (p, 2500.0f, 0.30f, 2.0f);
    filterEnv (p, 0.050f, 0.50f, false, true);
    ampEnv (p, 0.060f, 0.60f, false, true);
    glide (p, 0.15f);
    p.volume = 0.68f;
}

inline void emersonLead (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 1.00f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.85f, 0.10f);
    p.oscs[2] = osc (MinimoogWave::square, MinimoogRange::feet16, 0.50f);
    filter (p, 1500.0f, 0.70f, 5.5f, true, true);
    filterEnv (p, 0.008f, 0.80f, false, true);
    ampEnv (p, 0.005f, 0.70f, false, true);
    glide (p, 0.05f);
    p.volume = 0.70f;
}

inline void brassLead (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::widePulse, MinimoogRange::feet8, 0.70f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.40f);
    filter (p, 700.0f, 0.40f, 5.0f, true, false);
    filterEnv (p, 0.030f, 0.50f, false, true);
    ampEnv (p, 0.020f, 0.40f, false, true);
    p.volume = 0.74f;
}

inline void warmPad (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::widePulse, MinimoogRange::feet8, 0.70f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.50f, 0.10f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.40f);
    filter (p, 1200.0f, 0.20f, 2.0f);
    filterEnv (p, 0.80f, 2.00f, false, true);
    ampEnv (p, 0.60f, 1.50f, false, true);
    p.volume = 0.62f;
}

inline void stringPad (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.75f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.65f, -0.10f);
    p.oscs[2] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.55f, 0.12f);
    filter (p, 1800.0f, 0.15f, 1.5f);
    filterEnv (p, 0.90f, 2.50f, false, true);
    ampEnv (p, 0.80f, 2.00f, false, true);
    p.volume = 0.60f;
}

inline void choirPad (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::widePulse, MinimoogRange::feet8, 0.70f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet8, 0.60f, 0.07f);
    p.oscs[2] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.40f);
    p.osc3Control = false;
    modulation (p, 0.0f, 0.12f, true, false);
    filter (p, 1500.0f, 0.25f, 2.5f);
    filterEnv (p, 1.00f, 2.50f, false, true);
    ampEnv (p, 1.20f, 2.50f, false, true);
    p.volume = 0.60f;
}

inline void glassPad (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.70f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet8, 0.50f, 7.0f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::feet2, 0.30f);
    filter (p, 4000.0f, 0.15f, 1.0f);
    filterEnv (p, 0.50f, 2.00f, false, true);
    ampEnv (p, 0.50f, 2.00f, false, true);
    p.volume = 0.55f;
}

inline void stormPad (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet32, 0.50f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.30f);
    p.noiseLevel = 0.50f;
    p.noisePink = true;
    filter (p, 500.0f, 0.30f, 3.0f);
    filterEnv (p, 2.50f, 4.00f, false, true);
    ampEnv (p, 2.00f, 4.00f, false, true);
    modulation (p, 1.0f, 0.25f, false, true);
    p.volume = 0.55f;
}

inline void brassEnsemble (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.80f, 0.06f);
    p.oscs[2] = osc (MinimoogWave::widePulse, MinimoogRange::feet16, 0.50f);
    filter (p, 600.0f, 0.45f, 5.5f);
    filterEnv (p, 0.060f, 0.40f, false, true);
    ampEnv (p, 0.030f, 0.40f, false, true);
    p.volume = 0.72f;
}

inline void trumpet (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.95f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.70f, 0.08f);
    filter (p, 1400.0f, 0.55f, 4.0f);
    filterEnv (p, 0.050f, 0.30f, false, true);
    ampEnv (p, 0.040f, 0.30f, false, true);
    p.volume = 0.72f;
}

inline void trombone (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::square, MinimoogRange::feet16, 0.50f);
    filter (p, 500.0f, 0.40f, 4.5f);
    filterEnv (p, 0.080f, 0.50f, false, true);
    ampEnv (p, 0.060f, 0.50f, false, true);
    p.volume = 0.70f;
}

inline void oboe (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::widePulse, MinimoogRange::feet8, 0.80f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet8, 0.50f, 0.05f);
    filter (p, 1300.0f, 0.35f, 3.0f);
    filterEnv (p, 0.050f, 0.35f, false, true);
    ampEnv (p, 0.050f, 0.35f, false, true);
    p.volume = 0.66f;
}

inline void horn (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet8, 0.80f);
    p.oscs[1] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.50f);
    filter (p, 900.0f, 0.20f, 2.5f);
    filterEnv (p, 0.120f, 0.60f, false, true);
    ampEnv (p, 0.100f, 0.60f, false, true);
    p.volume = 0.66f;
}

inline void pluck (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::square, MinimoogRange::feet8, 0.50f);
    filter (p, 400.0f, 0.60f, 6.0f, true, false);
    filterEnv (p, 0.001f, 0.15f, true, false);
    ampEnv (p, 0.001f, 0.30f, true, false);
    p.volume = 0.75f;
}

inline void xylophone (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.90f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet2, 0.50f);
    filter (p, 6000.0f, 0.20f, 2.0f);
    filterEnv (p, 0.001f, 0.16f, true, false);
    ampEnv (p, 0.001f, 0.25f, true, false);
    p.volume = 0.70f;
}

inline void bells (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet2, 0.60f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.40f, 7.0f);
    p.oscs[2] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.30f);
    filter (p, 5000.0f, 0.55f, 4.0f);
    filterEnv (p, 0.001f, 1.20f, false, false);
    ampEnv (p, 0.001f, 1.20f, false, false);
    p.volume = 0.62f;
}

inline void steelDrum (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::square, MinimoogRange::feet8, 0.70f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.50f);
    filter (p, 2500.0f, 0.50f, 5.0f);
    filterEnv (p, 0.001f, 0.50f, true, false);
    ampEnv (p, 0.001f, 0.80f, true, false);
    p.volume = 0.66f;
}

inline void marimba (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet8, 0.90f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet4, 0.40f);
    filter (p, 3000.0f, 0.25f, 3.0f);
    filterEnv (p, 0.001f, 0.35f, true, false);
    ampEnv (p, 0.001f, 0.50f, true, false);
    p.volume = 0.68f;
}

inline void rain (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.20f);
    p.noiseLevel = 0.80f;
    p.noisePink = false;
    filter (p, 3000.0f, 0.20f, 1.0f);
    filterEnv (p, 0.40f, 1.50f, false, true);
    ampEnv (p, 0.40f, 1.50f, false, true);
    modulation (p, 1.0f, 0.50f, false, true);
    p.volume = 0.60f;
}

inline void wind (MinimoogParams& p)
{
    p.noiseLevel = 0.90f;
    p.noisePink = true;
    filter (p, 800.0f, 0.40f, 3.0f);
    filterEnv (p, 1.20f, 2.50f, false, true);
    ampEnv (p, 1.00f, 2.50f, false, true);
    modulation (p, 1.0f, 0.80f, false, true);
    p.volume = 0.55f;
}

inline void siren (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet8, 0.80f);
    p.oscs[2] = osc (MinimoogWave::square, MinimoogRange::lo, 0.0f);
    p.osc3Control = false;
    modulation (p, 0.0f, 0.60f, true, false);
    filter (p, 2000.0f, 0.50f, 2.0f);
    filterEnv (p, 0.050f, 0.40f, false, true);
    ampEnv (p, 0.010f, 0.40f, false, true);
    p.volume = 0.65f;
}

inline void rocket (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::saw, MinimoogRange::feet16, 0.40f);
    p.noiseLevel = 0.70f;
    p.noisePink = false;
    filter (p, 200.0f, 0.70f, 7.0f);
    filterEnv (p, 0.010f, 2.00f, false, true);
    ampEnv (p, 0.010f, 2.00f, false, true);
    modulation (p, 1.0f, 0.40f, false, true);
    p.volume = 0.60f;
}

inline void underwater (MinimoogParams& p)
{
    p.oscs[0] = osc (MinimoogWave::triangle, MinimoogRange::feet16, 0.70f);
    p.oscs[1] = osc (MinimoogWave::triangle, MinimoogRange::feet32, 0.40f);
    p.oscs[2] = osc (MinimoogWave::triangle, MinimoogRange::lo, 0.0f);
    p.osc3Control = false;
    modulation (p, 0.0f, 0.20f, true, false);
    filter (p, 400.0f, 0.60f, 3.0f);
    filterEnv (p, 0.30f, 1.20f, false, true);
    ampEnv (p, 0.30f, 1.20f, false, true);
    p.volume = 0.60f;
}

//==============================================================================

inline const Preset* bank (int& countOut)
{
    static const Preset presets[] =
    {
        { "经典胖贝斯", "贝斯", &fatBass },
        { "放克贝斯", "贝斯", &funkBass },
        { "深潜贝斯", "贝斯", &subBass },
        { "共振贝斯", "贝斯", &resonantBass },
        { "滑音贝斯", "贝斯", &glideBass },

        { "经典主音", "主音", &classicLead },
        { "第五主音", "主音", &fifthLead },
        { "哨音主音", "主音", &whistleLead },
        { "埃默森主音", "主音", &emersonLead },
        { "铜管主音", "主音", &brassLead },

        { "温暖铺底", "铺底", &warmPad },
        { "弦乐铺底", "铺底", &stringPad },
        { "合唱铺底", "铺底", &choirPad },
        { "玻璃铺底", "铺底", &glassPad },
        { "风暴铺底", "铺底", &stormPad },

        { "铜管合奏", "铜管", &brassEnsemble },
        { "小号", "铜管", &trumpet },
        { "长号", "铜管", &trombone },
        { "双簧管", "铜管", &oboe },
        { "圆号", "铜管", &horn },

        { "拨弦", "拨弦与钟", &pluck },
        { "木琴", "拨弦与钟", &xylophone },
        { "钟声", "拨弦与钟", &bells },
        { "钢鼓", "拨弦与钟", &steelDrum },
        { "马林巴", "拨弦与钟", &marimba },

        { "雨声", "特效", &rain },
        { "风声", "特效", &wind },
        { "警报", "特效", &siren },
        { "火箭", "特效", &rocket },
        { "水下", "特效", &underwater }
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

/** Applies a factory sound. The performance settings - polyphony and velocity
    sensitivity - belong to the player, not the patch, so they are kept. */
inline void applyPreset (MinimoogParams& patch, int index)
{
    int count = 0;
    const auto* presets = bank (count);

    if (count <= 0)
        return;

    const auto keepPolyphony = patch.polyphony;
    const auto keepVelocity = patch.velocitySensitivity;

    patch = MinimoogParams();
    patch.polyphony = keepPolyphony;
    patch.velocitySensitivity = keepVelocity;

    const auto i = juce::jlimit (0, count - 1, index);
    presets[i].build (patch);
    patch.name = juce::String::fromUTF8 (presets[i].name);
}

inline int findPresetByName (const juce::String& name)
{
    int count = 0;
    const auto* presets = bank (count);

    for (int i = 0; i < count; ++i)
        if (name == juce::String::fromUTF8 (presets[i].name))
            return i;

    return -1;
}

} // namespace nidaw::minimoog
