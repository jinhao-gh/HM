#pragma once

#include "Types.h"
#include "Wavetables.h"

namespace nidaw::wavetable
{

/** Builds one oscillator, so a preset only spells out what it uses. */
inline WavetableOsc osc (int table, float position, float level, int octave = 0,
                         int semitone = 0, float detuneCents = 0.0f)
{
    WavetableOsc o;
    o.tableIndex  = table;
    o.position    = position;
    o.level       = level;
    o.octave      = octave;
    o.semitone    = semitone;
    o.detuneCents = detuneCents;
    return o;
}

/** A named factory sound. */
struct Preset
{
    const char* name;
    void (*build) (WavetableParams&);
};

// The bank below is written for this project: original wavetable patches, not
// copies of any manufacturer's factory sounds.

inline void warmPad (WavetableParams& p)
{
    p.oscs[0] = osc (0, 0.25f, 0.80f);                 // 正弦→锯齿，偏正弦
    p.oscs[1] = osc (2, 0.55f, 0.60f, 0, 0, 8.0f);     // 奇次谐波，轻微失谐
    p.filterCutoffHz  = 3500.0f;
    p.filterResonance = 0.80f;
    p.ampAttack = 0.60f; p.ampDecay = 1.20f; p.ampSustain = 0.80f; p.ampRelease = 1.50f;
    p.posAttack = 1.20f; p.posDecay = 1.50f; p.posSustain = 0.35f; p.posRelease = 1.00f;
    p.posEnvAmount = 0.25f;
    p.lfoRateHz = 4.0f; p.lfoPositionDepth = 0.10f; p.lfoPitchCents = 5.0f;
}

inline void brightLead (WavetableParams& p)
{
    p.oscs[0] = osc (7, 0.75f, 0.90f);                 // 谐波堆叠
    p.oscs[1] = osc (1, 0.50f, 0.50f, 0, 0, -7.0f);    // 锯齿→方波
    p.filterCutoffHz  = 9000.0f;
    p.filterResonance = 2.20f;
    p.filterKeyTrack  = 0.40f;
    p.ampAttack = 0.010f; p.ampDecay = 0.250f; p.ampSustain = 0.80f; p.ampRelease = 0.250f;
    p.posAttack = 0.005f; p.posDecay = 0.350f; p.posSustain = 0.20f; p.posRelease = 0.300f;
    p.posEnvAmount = 0.45f;
    p.lfoRateHz = 5.5f; p.lfoPitchCents = 8.0f;
}

inline void deepBass (WavetableParams& p)
{
    p.oscs[0] = osc (13, 0.30f, 0.90f, -1);            // 锯齿堆叠
    p.oscs[1] = osc (5, 0.35f, 0.45f, -1, 0, -4.0f);   // 脉冲宽度
    p.filterCutoffHz  = 700.0f;
    p.filterResonance = 3.00f;
    p.filterKeyTrack  = 0.30f;
    p.ampAttack = 0.005f; p.ampDecay = 0.300f; p.ampSustain = 0.55f; p.ampRelease = 0.150f;
    p.posAttack = 0.005f; p.posDecay = 0.200f; p.posSustain = 0.0f; p.posRelease = 0.200f;
    p.posEnvAmount = 0.35f;
}

inline void pluckedTable (WavetableParams& p)
{
    p.oscs[0] = osc (8, 0.60f, 0.90f);                 // 五度堆叠
    p.oscs[1].enabled = false;
    p.filterCutoffHz  = 5000.0f;
    p.filterResonance = 4.00f;
    p.filterKeyTrack  = 0.50f;
    p.ampAttack = 0.002f; p.ampDecay = 0.400f; p.ampSustain = 0.0f; p.ampRelease = 0.300f;
    p.posAttack = 0.002f; p.posDecay = 0.150f; p.posSustain = 0.0f; p.posRelease = 0.200f;
    p.posEnvAmount = 0.70f;
}

inline void stringEnsemble (WavetableParams& p)
{
    p.oscs[0] = osc (0, 0.70f, 0.75f);                 // 正弦→锯齿，偏锯齿
    p.oscs[1] = osc (0, 0.70f, 0.75f, 0, 0, 12.0f);
    p.filterCutoffHz  = 4200.0f;
    p.filterResonance = 1.00f;
    p.ampAttack = 0.35f; p.ampDecay = 0.80f; p.ampSustain = 0.85f; p.ampRelease = 0.80f;
    p.posAttack = 0.50f; p.posDecay = 1.00f; p.posSustain = 0.45f; p.posRelease = 0.60f;
    p.posEnvAmount = 0.20f;
    p.lfoRateHz = 5.0f; p.lfoPitchCents = 6.0f;
}

inline void brassStack (WavetableParams& p)
{
    p.oscs[0] = osc (1, 0.85f, 0.85f);                 // 锯齿→方波，偏方波
    p.oscs[1] = osc (7, 0.45f, 0.55f, 0, 0, -9.0f);
    p.filterCutoffHz  = 5200.0f;
    p.filterResonance = 2.00f;
    p.filterKeyTrack  = 0.30f;
    p.ampAttack = 0.060f; p.ampDecay = 0.300f; p.ampSustain = 0.80f; p.ampRelease = 0.250f;
    p.posAttack = 0.020f; p.posDecay = 0.400f; p.posSustain = 0.30f; p.posRelease = 0.300f;
    p.posEnvAmount = 0.55f;
}

inline void glassKeys (WavetableParams& p)
{
    p.oscs[0] = osc (10, 0.40f, 0.80f);                // 玻璃
    p.oscs[1] = osc (4, 0.30f, 0.45f, 1);              // 铃声高八度
    p.filterCutoffHz  = 11000.0f;
    p.filterResonance = 1.20f;
    p.ampAttack = 0.002f; p.ampDecay = 0.900f; p.ampSustain = 0.10f; p.ampRelease = 0.900f;
    p.posAttack = 0.002f; p.posDecay = 0.500f; p.posSustain = 0.25f; p.posRelease = 0.600f;
    p.posEnvAmount = 0.35f;
}

inline void sweepingPad (WavetableParams& p)
{
    p.oscs[0] = osc (3, 0.00f, 0.80f);                 // 共振峰：从低扫到高
    p.oscs[1] = osc (11, 0.30f, 0.55f, 0, 0, 10.0f);   // 人声
    p.filterCutoffHz  = 6000.0f;
    p.filterResonance = 1.40f;
    p.ampAttack = 0.80f; p.ampDecay = 1.50f; p.ampSustain = 0.85f; p.ampRelease = 1.80f;
    p.posAttack = 1.50f; p.posDecay = 3.00f; p.posSustain = 0.80f; p.posRelease = 1.50f;
    p.posEnvAmount = 0.80f;
    p.lfoRateHz = 0.8f; p.lfoPositionDepth = 0.25f; p.lfoCutoffDepth = 0.20f;
}

inline void sawLeadWt (WavetableParams& p)
{
    p.oscs[0] = osc (0, 1.00f, 0.90f);                 // 纯锯齿端
    p.oscs[1] = osc (0, 1.00f, 0.90f, 0, 0, 16.0f);
    p.filterCutoffHz  = 7000.0f;
    p.filterResonance = 1.60f;
    p.ampAttack = 0.010f; p.ampDecay = 0.300f; p.ampSustain = 0.85f; p.ampRelease = 0.300f;
    p.posAttack = 0.005f; p.posDecay = 0.250f; p.posSustain = 0.10f; p.posRelease = 0.300f;
    p.posEnvAmount = 0.30f;
    p.lfoRateHz = 6.0f; p.lfoPitchCents = 10.0f;
}

inline void softLeadWt (WavetableParams& p)
{
    p.oscs[0] = osc (0, 0.15f, 0.90f);                 // 接近纯正弦
    p.oscs[1] = osc (2, 0.20f, 0.50f, 0, 0, 6.0f);
    p.filterCutoffHz  = 6000.0f;
    p.filterResonance = 0.90f;
    p.ampAttack = 0.050f; p.ampDecay = 0.400f; p.ampSustain = 0.85f; p.ampRelease = 0.400f;
    p.posAttack = 0.150f; p.posDecay = 0.600f; p.posSustain = 0.30f; p.posRelease = 0.400f;
    p.posEnvAmount = 0.25f;
    p.lfoRateHz = 4.5f; p.lfoPitchCents = 7.0f;
}

inline void digitalBell (WavetableParams& p)
{
    p.oscs[0] = osc (4, 0.55f, 0.85f);                 // 铃声
    p.oscs[1] = osc (9, 0.70f, 0.45f, 1, 7);           // 相位失真，高八度加五度
    p.filterCutoffHz  = 12000.0f;
    p.filterResonance = 0.90f;
    p.ampAttack = 0.001f; p.ampDecay = 1.400f; p.ampSustain = 0.0f; p.ampRelease = 1.200f;
    p.posAttack = 0.001f; p.posDecay = 0.800f; p.posSustain = 0.0f; p.posRelease = 0.800f;
    p.posEnvAmount = 0.60f;
}

inline void spaceTable (WavetableParams& p)
{
    p.oscs[0] = osc (11, 0.45f, 0.70f);                // 人声
    p.oscs[1] = osc (14, 0.55f, 0.55f, 0, 0, 20.0f);   // 谐波噪声
    p.filterMode      = FilterMode::bandPass;
    p.filterCutoffHz  = 1500.0f;
    p.filterResonance = 1.80f;
    p.ampAttack = 0.90f; p.ampDecay = 1.60f; p.ampSustain = 0.80f; p.ampRelease = 2.00f;
    p.posAttack = 1.80f; p.posDecay = 2.60f; p.posSustain = 0.60f; p.posRelease = 1.80f;
    p.posEnvAmount = 0.40f;
    p.lfoRateHz = 1.2f; p.lfoPositionDepth = 0.30f; p.lfoCutoffDepth = 0.25f; p.lfoAmpDepth = 0.10f;
}

/** The factory bank, in menu order. */
inline const Preset* bank (int& countOut)
{
    static const Preset presets[] =
    {
        { "温暖铺底", &warmPad },
        { "明亮主音", &brightLead },
        { "深潜低音", &deepBass },
        { "弹拨波表", &pluckedTable },
        { "弦乐合奏", &stringEnsemble },
        { "铜管齐奏", &brassStack },
        { "玻璃键盘", &glassKeys },
        { "扫频铺底", &sweepingPad },
        { "锯齿主音", &sawLeadWt },
        { "柔和领奏", &softLeadWt },
        { "数字铃", &digitalBell },
        { "空间铺底", &spaceTable },
    };

    countOut = (int) (sizeof (presets) / sizeof (presets[0]));
    return presets;
}

inline int builtInPresetCount()
{
    int count = 0;
    bank (count);
    return count;
}

/** Applies one factory preset. Index is clamped, so -1 and 999 are safe. */
inline void applyPreset (WavetableParams& params, int index)
{
    int count = 0;
    const auto* presets = bank (count);

    if (count <= 0)
        return;

    // Start from a clean patch so nothing from the previous sound leaks in.
    params = WavetableParams();

    const auto i = juce::jlimit (0, count - 1, index);
    presets[i].build (params);
    params.name = juce::String::fromUTF8 (presets[i].name);
}

} // namespace nidaw::wavetable
