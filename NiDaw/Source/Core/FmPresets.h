#pragma once

#include "Types.h"

namespace nidaw::fm
{

/** Builds an operator with sensible defaults, so a preset only spells out what it
    actually uses. */
inline FmOperator op (float ratio, int outputLevel, float detune = 0.0f,
                      int velSens = 0, int rateScale = 0)
{
    FmOperator o;
    o.ratio       = ratio;
    o.outputLevel = outputLevel;
    o.detune      = detune;
    o.velSens     = velSens;
    o.rateScale   = rateScale;
    return o;
}

inline FmOperator withEnv (FmOperator o, int r1, int r2, int r3, int r4,
                           int l1, int l2, int l3, int l4)
{
    o.rate[0] = r1; o.rate[1] = r2; o.rate[2] = r3; o.rate[3] = r4;
    o.level[0] = l1; o.level[1] = l2; o.level[2] = l3; o.level[3] = l4;
    return o;
}

/** A named factory sound. */
struct Preset
{
    const char* name;
    void (*build) (FmPatch&);
};

// The bank below is written for this project - these are original patches in the
// FM idiom (an electric piano, a bell, brass, and so on), not copies of any
// manufacturer's factory sounds.

inline void electricPiano (FmPatch& p)
{
    p.algorithm = 5;        // 6-5-4-3-2 with a tap at operator 2
    p.feedback  = 4;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 55, 45, 60, 99, 78, 62, 0);
    p.ops[1] = withEnv (op (1.0f, 74), 99, 62, 50, 60, 92, 70, 55, 0);
    p.ops[2] = withEnv (op (3.0f, 62), 99, 70, 55, 70, 80, 60, 45, 0);
    p.ops[3] = withEnv (op (14.0f, 46), 99, 80, 60, 75, 70, 40, 20, 0);
    p.ops[4] = withEnv (op (1.0f, 40), 99, 75, 60, 75, 60, 40, 25, 0);
    p.ops[5] = withEnv (op (1.0f, 30), 99, 85, 70, 80, 40, 20, 10, 0);
}

inline void bell (FmPatch& p)
{
    p.algorithm = 3;
    p.feedback  = 0;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 60, 40, 55, 99, 20, 0, 0);
    p.ops[1] = withEnv (op (3.5f, 78), 99, 65, 45, 60, 95, 30, 0, 0);
    p.ops[2] = withEnv (op (7.0f, 66), 99, 70, 50, 65, 90, 25, 0, 0);
    p.ops[3] = withEnv (op (1.0f, 55), 99, 62, 42, 58, 80, 20, 0, 0);
    p.ops[4] = withEnv (op (1.41f, 45), 99, 75, 55, 70, 70, 15, 0, 0);
    p.ops[5] = withEnv (op (2.0f, 50), 99, 68, 48, 62, 85, 18, 0, 0);
    p.transpose = 12;
}

inline void brass (FmPatch& p)
{
    p.algorithm = 2;
    p.feedback  = 5;
    p.lfoRate = 42; p.lfoPitchDepth = 6; p.lfoDelay = 40;
    p.ops[0] = withEnv (op (1.0f, 99), 55, 45, 35, 55, 85, 92, 88, 0);
    p.ops[1] = withEnv (op (1.0f, 82), 60, 50, 40, 60, 80, 90, 84, 0);
    p.ops[2] = withEnv (op (2.0f, 70), 65, 55, 45, 65, 75, 85, 80, 0);
    p.ops[3] = withEnv (op (1.0f, 62), 70, 60, 50, 70, 70, 80, 75, 0);
    p.ops[4] = withEnv (op (3.0f, 52), 75, 65, 55, 75, 65, 75, 70, 0);
    p.ops[5] = withEnv (op (1.0f, 44), 80, 70, 60, 80, 60, 70, 65, 0);
}

inline void strings (FmPatch& p)
{
    p.algorithm = 8;
    p.feedback  = 2;
    p.lfoRate = 28; p.lfoPitchDepth = 9; p.lfoDelay = 55;
    p.ops[0] = withEnv (op (1.0f, 99, -0.15f), 40, 45, 40, 45, 88, 92, 90, 0);
    p.ops[1] = withEnv (op (1.0f, 78, 0.15f), 42, 48, 42, 48, 85, 90, 88, 0);
    p.ops[2] = withEnv (op (2.0f, 58), 45, 50, 45, 50, 80, 85, 82, 0);
    p.ops[3] = withEnv (op (3.0f, 46), 48, 55, 48, 55, 75, 80, 78, 0);
    p.ops[4] = withEnv (op (1.0f, 52), 50, 55, 50, 55, 78, 82, 80, 0);
    p.ops[5] = withEnv (op (5.0f, 34), 55, 60, 55, 60, 70, 75, 72, 0);
}

inline void bass (FmPatch& p)
{
    p.algorithm = 4;
    p.feedback  = 6;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 60, 50, 65, 99, 85, 80, 0);
    p.ops[1] = withEnv (op (1.0f, 86), 99, 58, 48, 62, 96, 80, 72, 0);
    p.ops[2] = withEnv (op (2.0f, 62), 99, 70, 55, 70, 88, 60, 45, 0);
    p.ops[3] = withEnv (op (1.0f, 70), 99, 66, 52, 66, 90, 70, 60, 0);
    p.ops[4] = withEnv (op (4.0f, 44), 99, 75, 60, 75, 80, 50, 35, 0);
    p.ops[5] = withEnv (op (1.0f, 38), 99, 80, 65, 80, 70, 40, 30, 0);
    p.transpose = -12;
}

inline void marimba (FmPatch& p)
{
    p.algorithm = 6;
    p.feedback  = 0;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 72, 55, 70, 99, 30, 0, 0);
    p.ops[1] = withEnv (op (1.0f, 72), 99, 76, 58, 72, 95, 25, 0, 0);
    p.ops[2] = withEnv (op (4.0f, 60), 99, 80, 60, 75, 88, 20, 0, 0);
    p.ops[3] = withEnv (op (9.0f, 48), 99, 84, 65, 80, 80, 15, 0, 0);
    p.ops[4] = withEnv (op (1.0f, 40), 99, 78, 60, 74, 70, 18, 0, 0);
    p.ops[5] = withEnv (op (1.0f, 34), 99, 82, 64, 78, 65, 12, 0, 0);
}

inline void organ (FmPatch& p)
{
    p.algorithm = 32;      // six sines straight to the output
    p.ops[0] = withEnv (op (1.0f, 99), 50, 55, 50, 55, 99, 99, 99, 0);
    p.ops[1] = withEnv (op (2.0f, 88), 50, 55, 50, 55, 98, 98, 98, 0);
    p.ops[2] = withEnv (op (3.0f, 74), 50, 55, 50, 55, 96, 96, 96, 0);
    p.ops[3] = withEnv (op (4.0f, 66), 50, 55, 50, 55, 94, 94, 94, 0);
    p.ops[4] = withEnv (op (6.0f, 52), 50, 55, 50, 55, 90, 90, 90, 0);
    p.ops[5] = withEnv (op (8.0f, 44), 50, 55, 50, 55, 85, 85, 85, 0);
}

inline void fmPiano (FmPatch& p)
{
    p.algorithm = 1;
    p.feedback  = 5;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 55, 45, 58, 99, 70, 55, 0);
    p.ops[1] = withEnv (op (2.0f, 80), 99, 60, 45, 62, 92, 45, 20, 0);
    p.ops[2] = withEnv (op (3.0f, 68), 99, 66, 50, 68, 86, 35, 12, 0);
    p.ops[3] = withEnv (op (5.0f, 54), 99, 72, 55, 72, 78, 25, 8, 0);
    p.ops[4] = withEnv (op (8.0f, 44), 99, 78, 60, 78, 70, 18, 5, 0);
    p.ops[5] = withEnv (op (11.0f, 36), 99, 82, 65, 82, 62, 12, 0, 0);
}

inline void clav (FmPatch& p)
{
    p.algorithm = 9;
    p.feedback  = 6;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 55, 40, 55, 99, 60, 40, 0);
    p.ops[1] = withEnv (op (3.0f, 84), 99, 60, 42, 58, 94, 45, 22, 0);
    p.ops[2] = withEnv (op (7.0f, 70), 99, 66, 48, 62, 88, 35, 15, 0);
    p.ops[3] = withEnv (op (2.0f, 74), 99, 58, 44, 56, 92, 50, 25, 0);
    p.ops[4] = withEnv (op (1.0f, 66), 99, 62, 48, 60, 90, 40, 20, 0);
    p.ops[5] = withEnv (op (5.0f, 52), 99, 70, 52, 66, 82, 30, 12, 0);
}

inline void flute (FmPatch& p)
{
    p.algorithm = 16;      // three pairs
    p.feedback  = 2;
    p.lfoRate = 30; p.lfoPitchDepth = 5; p.lfoDelay = 50;
    p.ops[0] = withEnv (op (1.0f, 99), 45, 48, 45, 50, 90, 95, 92, 0);
    p.ops[1] = withEnv (op (2.0f, 34), 50, 55, 50, 55, 70, 78, 74, 0);
    p.ops[2] = withEnv (op (1.0f, 62, 0.2f), 45, 48, 45, 50, 88, 92, 90, 0);
    p.ops[3] = withEnv (op (1.0f, 28), 55, 60, 55, 60, 60, 70, 66, 0);
    p.ops[4] = withEnv (op (1.0f, 40, -0.2f), 45, 48, 45, 50, 85, 90, 88, 0);
    p.ops[5] = withEnv (op (4.0f, 22), 60, 65, 60, 65, 55, 60, 55, 0);
}

inline void pad (FmPatch& p)
{
    p.algorithm = 10;
    p.feedback  = 3;
    p.lfoRate = 20; p.lfoPitchDepth = 7; p.lfoDelay = 65; p.lfoAmpDepth = 12;
    p.ops[0] = withEnv (op (1.0f, 99, -0.3f), 30, 40, 35, 40, 85, 92, 90, 0);
    p.ops[1] = withEnv (op (2.0f, 62, 0.3f), 32, 42, 36, 42, 80, 88, 85, 0);
    p.ops[2] = withEnv (op (3.0f, 50), 35, 45, 38, 45, 75, 82, 80, 0);
    p.ops[3] = withEnv (op (1.0f, 70), 30, 42, 36, 42, 84, 90, 88, 0);
    p.ops[4] = withEnv (op (1.5f, 48), 38, 48, 40, 48, 72, 80, 76, 0);
    p.ops[5] = withEnv (op (4.0f, 36), 45, 55, 45, 55, 65, 72, 68, 0);
}

inline void pluck (FmPatch& p)
{
    p.algorithm = 11;
    p.feedback  = 3;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 70, 45, 65, 99, 45, 15, 0);
    p.ops[1] = withEnv (op (2.0f, 78), 99, 74, 50, 70, 92, 35, 10, 0);
    p.ops[2] = withEnv (op (5.0f, 64), 99, 80, 55, 75, 86, 25, 6, 0);
    p.ops[3] = withEnv (op (1.0f, 60), 99, 72, 48, 68, 88, 30, 8, 0);
    p.ops[4] = withEnv (op (7.0f, 46), 99, 84, 60, 78, 78, 18, 4, 0);
    p.ops[5] = withEnv (op (1.0f, 40), 99, 78, 52, 72, 82, 22, 5, 0);
}

inline void percussion (FmPatch& p)
{
    p.algorithm = 13;
    p.feedback  = 7;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 85, 60, 80, 99, 25, 0, 0);
    p.ops[1] = withEnv (op (2.0f, 88), 99, 88, 65, 82, 95, 20, 0, 0);
    p.ops[2] = withEnv (op (9.0f, 76), 99, 90, 70, 85, 90, 15, 0, 0);
    p.ops[3] = withEnv (op (1.0f, 68), 99, 86, 62, 82, 88, 18, 0, 0);
    p.ops[4] = withEnv (op (13.0f, 60), 99, 92, 72, 88, 82, 12, 0, 0);
    p.ops[5] = withEnv (op (1.0f, 54), 99, 90, 68, 85, 80, 15, 0, 0);
    p.transpose = -12;
}

inline void tubularBell (FmPatch& p)
{
    p.algorithm = 14;
    p.feedback  = 2;
    p.ops[0] = withEnv (op (1.0f, 99), 99, 65, 45, 60, 99, 55, 30, 0);
    p.ops[1] = withEnv (op (2.0f, 80), 99, 68, 48, 62, 92, 45, 22, 0);
    p.ops[2] = withEnv (op (5.0f, 66), 99, 72, 52, 66, 86, 35, 16, 0);
    p.ops[3] = withEnv (op (9.0f, 58), 99, 78, 56, 70, 80, 28, 10, 0);
    p.ops[4] = withEnv (op (1.0f, 62), 99, 70, 50, 64, 84, 40, 18, 0);
    p.ops[5] = withEnv (op (3.0f, 52), 99, 74, 54, 68, 78, 32, 12, 0);
    p.transpose = 12;
}

inline void harmonica (FmPatch& p)
{
    p.algorithm = 18;
    p.feedback  = 4;
    p.lfoRate = 46; p.lfoPitchDepth = 8; p.lfoDelay = 25;
    p.ops[0] = withEnv (op (1.0f, 99), 60, 50, 42, 55, 90, 95, 92, 0);
    p.ops[1] = withEnv (op (2.0f, 70), 62, 52, 44, 58, 86, 90, 86, 0);
    p.ops[2] = withEnv (op (3.0f, 58), 65, 55, 48, 60, 80, 85, 82, 0);
    p.ops[3] = withEnv (op (1.0f, 62), 60, 50, 42, 55, 88, 92, 90, 0);
    p.ops[4] = withEnv (op (4.0f, 44), 68, 58, 50, 62, 74, 80, 76, 0);
    p.ops[5] = withEnv (op (6.0f, 36), 72, 62, 54, 66, 68, 74, 70, 0);
}

/** The factory bank, in the order the editor lists it. */
inline const Preset* bank (int& countOut)
{
    static const Preset presets[] =
    {
        { "电钢琴",   &electricPiano },
        { "FM 钢琴",  &fmPiano },
        { "钟琴",     &bell },
        { "管钟",     &tubularBell },
        { "铜管",     &brass },
        { "弦乐",     &strings },
        { "贝斯",     &bass },
        { "马林巴",   &marimba },
        { "风琴",     &organ },
        { "击弦古钢琴", &clav },
        { "长笛",     &flute },
        { "铺底",     &pad },
        { "拨弦",     &pluck },
        { "打击乐",   &percussion },
        { "口琴",     &harmonica },
    };

    countOut = (int) (sizeof (presets) / sizeof (presets[0]));
    return presets;
}

/** Applies one factory preset to a patch. Index is clamped, so -1 and 999 are safe. */
inline void applyPreset (FmPatch& patch, int index)
{
    int count = 0;
    const auto* presets = bank (count);

    if (count <= 0)
        return;

    // Start from a clean, quiet patch so nothing from the previous sound leaks in.
    patch = FmPatch();

    const auto i = juce::jlimit (0, count - 1, index);
    presets[i].build (patch);
    patch.name = juce::String::fromUTF8 (presets[i].name);
}

/** True when the patch still matches the factory sound of that name. */
inline int findPresetByName (const juce::String& name)
{
    int count = 0;
    const auto* presets = bank (count);

    for (int i = 0; i < count; ++i)
        if (name == juce::String::fromUTF8 (presets[i].name))
            return i;

    return -1;
}

} // namespace nidaw::fm
