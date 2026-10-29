#pragma once

#include "FmPatchHelpers.h"

namespace nidaw::fm::dx7
{

/** The Yamaha DX7 factory bank ROM-1A, converted to this engine's six-operator
    FM model.

    Data source: the public DX7 patch collection at
    https://www.npmjs.com/package/dx7-patches (compiled by Glenn Scott from the
    DX7 factory ROM-1A). The voices are Yamaha's; the Chinese names, the
    category layout and the conversion are this project's. HM is not
    affiliated with Yamaha.

    The conversion is honest about what this engine can represent: the DX7
    has 32 fixed routings and this engine has its own 32, so each voice is
    placed on the closest topology and the operators are permuted into it
    (see engineAlgorithmFor). Genuinely exact for DX7 algorithms 3, 4, 5, 6,
    28, 29, 30, 31 and 32; the rest are close approximations.

    Fields the DX7 has and this engine has not are dropped: the pitch EG,
    keyboard level scaling, AM sensitivity and fixed-frequency operators
    (those take ratio 1).
*/

inline int engineAlgorithmFor (int dx7Algorithm) noexcept
{
    static constexpr int map[32] =
        { 3, 3, 9, 9, 17, 17, 9, 9, 9, 9, 9, 9, 9, 9, 9, 1, 1, 1, 13, 17, 22, 22, 22, 27, 27, 17, 17, 13, 22, 26, 27, 32 };

    return map[(dx7Algorithm < 1 || dx7Algorithm > 32) ? 0 : dx7Algorithm - 1];
}

struct FactoryVoice
{
    const char* name;
    const char* category;
    int dx7Algorithm;
    void (*build) (FmPatch&);
};

inline void build0 (FmPatch& p)
{
    p.algorithm = 13;   // DX7 algorithm 19
    p.feedback  = 6;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 99, -0.286f, 2, 4), 81, 25, 20, 48, 99, 82, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 87, 0.286f, 0, 5), 99, 0, 25, 0, 99, 75, 0, 0);
    p.ops[2] = withEnv (op (3.00f, 57, 0.000f, 0, 5), 81, 25, 25, 14, 99, 99, 99, 0);
    p.ops[3] = withEnv (op (1.00f, 99, 0.143f, 2, 5), 81, 23, 22, 45, 99, 78, 0, 0);
    p.ops[5] = withEnv (op (1.00f, 93, -0.143f, 1, 5), 81, 58, 36, 39, 99, 14, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 82, -0.143f, 0, 5), 99, 0, 25, 0, 99, 75, 0, 0);
}
inline void build1 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 18
    p.feedback  = 5;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (1.00f, 94, 0.000f, 2, 3), 80, 24, 10, 50, 99, 62, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 86, 0.286f, 1, 2), 95, 0, 25, 0, 99, 75, 0, 0);
    p.ops[5] = withEnv (op (5.00f, 83, 0.143f, 1, 5), 90, 27, 20, 50, 99, 85, 0, 0);
    p.ops[2] = withEnv (op (1.00f, 84, -0.143f, 1, 3), 97, 27, 10, 25, 99, 86, 48, 0);
    p.ops[3] = withEnv (op (0.50f, 94, 0.000f, 1, 3), 90, 71, 33, 31, 99, 0, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 78, 0.143f, 1, 3), 92, 71, 58, 36, 99, 0, 0, 0);
}
inline void build2 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 3
    p.feedback  = 4;
    p.lfoWaveform   = 0;
    p.lfoRate       = 45;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 86, -0.571f, 3, 3), 90, 30, 28, 45, 99, 95, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 85, 0.571f, 0, 2), 98, 36, 6, 32, 91, 90, 0, 0);
    p.ops[2] = withEnv (op (7.00f, 97, -0.429f, 3, 3), 94, 80, 19, 12, 83, 67, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 95, 0.429f, 2, 3), 90, 64, 28, 45, 99, 97, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 87, -0.286f, 1, 2), 98, 20, 6, 2, 91, 90, 0, 0);
    p.ops[5] = withEnv (op (0.50f, 84, 0.286f, 5, 3), 80, 73, 15, 10, 99, 19, 0, 0);
}
inline void build3 (FmPatch& p)
{
    p.algorithm = 17;   // DX7 algorithm 5
    p.feedback  = 6;
    p.lfoWaveform   = 0;
    p.lfoRate       = 34;
    p.lfoDelay      = 33;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 99, 0.429f, 2, 3), 96, 25, 25, 67, 99, 75, 0, 0);
    p.ops[1] = withEnv (op (14.00f, 58, 0.000f, 7, 3), 95, 50, 35, 78, 99, 75, 0, 0);
    p.ops[2] = withEnv (op (1.00f, 99, 0.000f, 2, 3), 95, 20, 20, 50, 99, 95, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 89, 0.000f, 6, 3), 95, 29, 20, 50, 99, 95, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 99, -1.000f, 0, 3), 95, 20, 20, 50, 99, 95, 0, 0);
    p.ops[5] = withEnv (op (1.00f, 79, 1.000f, 6, 3), 95, 29, 20, 50, 99, 95, 0, 0);
}
inline void build4 (FmPatch& p)
{
    p.algorithm = 32;   // DX7 algorithm 32
    p.feedback  = 0;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 94, -0.286f, 0, 0), 99, 80, 22, 90, 99, 99, 99, 0);
    p.ops[1] = withEnv (op (1.00f, 94, -0.857f, 0, 0), 99, 20, 22, 90, 99, 99, 97, 0);
    p.ops[2] = withEnv (op (1.00f, 94, 0.571f, 0, 0), 99, 80, 54, 82, 99, 99, 99, 0);
    p.ops[3] = withEnv (op (0.50f, 94, 0.714f, 0, 0), 99, 80, 22, 90, 99, 99, 99, 0);
    p.ops[4] = withEnv (op (1.00f, 94, 0.286f, 0, 0), 99, 80, 22, 90, 99, 99, 99, 0);
    p.ops[5] = withEnv (op (3.00f, 94, 0.000f, 0, 0), 99, 54, 22, 90, 99, 0, 0, 0);
}
inline void build5 (FmPatch& p)
{
    p.algorithm = 13;   // DX7 algorithm 19
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 34;
    p.lfoDelay      = 33;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (0.50f, 99, 0.000f, 0, 5), 45, 25, 25, 36, 99, 99, 98, 0);
    p.ops[1] = withEnv (op (0.50f, 90, 0.000f, 0, 4), 99, 97, 62, 47, 99, 99, 90, 0);
    p.ops[2] = withEnv (op (1.00f, 75, 0.000f, 0, 5), 99, 97, 62, 47, 99, 99, 90, 0);
    p.ops[3] = withEnv (op (4.00f, 88, 0.000f, 0, 3), 61, 25, 25, 50, 99, 99, 97, 0);
    p.ops[5] = withEnv (op (2.00f, 97, 0.000f, 0, 3), 61, 25, 25, 61, 99, 99, 93, 0);
    p.ops[4] = withEnv (op (10.00f, 76, 0.000f, 2, 3), 72, 25, 25, 70, 99, 99, 99, 0);
}
inline void build6 (FmPatch& p)
{
    p.algorithm = 17;   // DX7 algorithm 5
    p.feedback  = 1;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (4.00f, 89, 0.000f, 2, 3), 95, 28, 27, 47, 99, 90, 0, 0);
    p.ops[1] = withEnv (op (0.50f, 99, 0.000f, 0, 1), 95, 72, 71, 99, 99, 97, 91, 98);
    p.ops[2] = withEnv (op (1.00f, 85, -0.143f, 2, 3), 95, 28, 27, 47, 99, 90, 0, 0);
    p.ops[3] = withEnv (op (3.00f, 99, 0.000f, 0, 1), 95, 72, 71, 99, 99, 97, 91, 98);
    p.ops[4] = withEnv (op (4.00f, 83, -0.143f, 3, 3), 95, 28, 27, 47, 99, 90, 0, 0);
    p.ops[5] = withEnv (op (6.00f, 87, 0.000f, 0, 1), 95, 72, 71, 99, 99, 97, 91, 98);
}
inline void build7 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 3
    p.feedback  = 5;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 99, 0.143f, 3, 3), 95, 92, 28, 60, 99, 90, 0, 0);
    p.ops[1] = withEnv (op (0.50f, 99, -0.143f, 1, 3), 95, 95, 0, 0, 99, 96, 89, 0);
    p.ops[2] = withEnv (op (6.00f, 71, 0.000f, 1, 3), 98, 87, 0, 0, 87, 86, 0, 0);
    p.ops[3] = withEnv (op (2.00f, 99, 0.000f, 2, 3), 95, 92, 28, 60, 99, 90, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 99, -0.286f, 6, 3), 95, 95, 0, 0, 99, 96, 89, 0);
    p.ops[5] = withEnv (op (8.00f, 78, 0.000f, 7, 3), 98, 87, 0, 0, 87, 86, 0, 0);
}
inline void build8 (FmPatch& p)
{
    p.algorithm = 22;   // DX7 algorithm 22
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 37;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 5;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 98, 1.000f, 0, 0), 72, 76, 99, 71, 99, 88, 96, 0);
    p.ops[1] = withEnv (op (0.50f, 86, 1.000f, 0, 0), 62, 51, 29, 71, 82, 95, 96, 0);
    p.ops[2] = withEnv (op (1.00f, 99, -0.286f, 2, 0), 77, 76, 82, 71, 99, 98, 98, 0);
    p.ops[3] = withEnv (op (1.00f, 99, 0.000f, 2, 0), 77, 36, 41, 71, 99, 98, 98, 0);
    p.ops[4] = withEnv (op (1.00f, 98, 0.143f, 2, 0), 77, 36, 41, 71, 99, 98, 98, 0);
    p.ops[5] = withEnv (op (1.00f, 82, 0.000f, 2, 4), 49, 99, 28, 68, 98, 98, 91, 0);
}
inline void build9 (FmPatch& p)
{
    p.algorithm = 22;   // DX7 algorithm 22
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 37;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 99, 1.000f, 0, 0), 99, 39, 32, 71, 99, 98, 80, 0);
    p.ops[1] = withEnv (op (0.50f, 84, 1.000f, 0, 0), 99, 39, 32, 71, 99, 98, 80, 0);
    p.ops[2] = withEnv (op (0.50f, 99, -0.429f, 0, 0), 99, 39, 32, 71, 99, 98, 81, 0);
    p.ops[3] = withEnv (op (0.50f, 99, -0.286f, 0, 0), 99, 39, 32, 71, 99, 98, 81, 0);
    p.ops[4] = withEnv (op (0.50f, 99, 0.143f, 0, 0), 99, 39, 32, 71, 99, 98, 81, 0);
    p.ops[5] = withEnv (op (0.50f, 80, 0.000f, 0, 0), 99, 39, 32, 71, 99, 98, 88, 0);
}
inline void build10 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 18
    p.feedback  = 6;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 5;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 2, 2), 55, 24, 19, 55, 99, 86, 86, 0);
    p.ops[1] = withEnv (op (1.00f, 70, 0.000f, 1, 2), 37, 34, 15, 70, 85, 0, 0, 0);
    p.ops[5] = withEnv (op (1.00f, 77, 0.000f, 1, 1), 46, 35, 22, 50, 99, 86, 86, 0);
    p.ops[2] = withEnv (op (1.00f, 79, 0.000f, 0, 0), 66, 92, 22, 50, 53, 61, 62, 0);
    p.ops[3] = withEnv (op (3.00f, 70, -0.143f, 0, 0), 48, 55, 22, 50, 98, 61, 62, 0);
    p.ops[4] = withEnv (op (8.00f, 79, 0.000f, 0, 7), 77, 56, 20, 70, 99, 0, 0, 0);
}
inline void build11 (FmPatch& p)
{
    p.algorithm = 3;   // DX7 algorithm 2
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 8;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[4] = withEnv (op (1.00f, 99, 0.000f, 3, 2), 45, 24, 20, 41, 99, 85, 70, 0);
    p.ops[5] = withEnv (op (1.00f, 83, 0.000f, 0, 1), 75, 71, 17, 49, 82, 92, 62, 0);
    p.ops[0] = withEnv (op (1.00f, 86, 0.000f, 7, 0), 44, 45, 20, 54, 99, 85, 82, 0);
    p.ops[1] = withEnv (op (1.00f, 77, 0.000f, 2, 2), 96, 19, 20, 54, 99, 92, 86, 0);
    p.ops[2] = withEnv (op (3.00f, 84, 0.000f, 2, 2), 53, 19, 20, 54, 86, 92, 86, 0);
    p.ops[3] = withEnv (op (14.00f, 53, 0.000f, 2, 2), 53, 19, 20, 54, 99, 92, 86, 0);
}
inline void build12 (FmPatch& p)
{
    p.algorithm = 3;   // DX7 algorithm 2
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 81;
    p.lfoPitchDepth = 8;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[4] = withEnv (op (2.00f, 92, 0.000f, 0, 0), 48, 56, 10, 47, 98, 98, 36, 0);
    p.ops[5] = withEnv (op (2.00f, 74, -0.857f, 0, 0), 81, 13, 7, 25, 99, 92, 28, 0);
    p.ops[0] = withEnv (op (2.00f, 92, 0.857f, 0, 0), 51, 15, 10, 47, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (2.00f, 76, 0.000f, 0, 0), 49, 74, 10, 32, 98, 98, 36, 0);
    p.ops[2] = withEnv (op (2.00f, 66, 0.000f, 0, 0), 76, 73, 10, 28, 99, 92, 0, 0);
    p.ops[3] = withEnv (op (8.00f, 70, 0.000f, 0, 0), 72, 76, 10, 32, 99, 92, 0, 0);
}
inline void build13 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 15
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 28;
    p.lfoDelay      = 46;
    p.lfoPitchDepth = 30;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 1, 2), 52, 30, 25, 43, 99, 92, 90, 0);
    p.ops[1] = withEnv (op (1.00f, 86, 0.000f, 0, 1), 99, 71, 35, 51, 82, 92, 87, 0);
    p.ops[3] = withEnv (op (1.00f, 99, 0.000f, 1, 2), 50, 52, 35, 41, 99, 92, 91, 0);
    p.ops[4] = withEnv (op (1.00f, 75, 0.000f, 2, 2), 96, 19, 20, 54, 99, 92, 89, 0);
    p.ops[2] = withEnv (op (3.00f, 84, 0.000f, 1, 2), 53, 67, 38, 54, 86, 92, 74, 0);
    p.ops[5] = withEnv (op (14.00f, 54, 0.000f, 2, 2), 53, 64, 44, 54, 99, 92, 56, 0);
}
inline void build14 (FmPatch& p)
{
    p.algorithm = 3;   // DX7 algorithm 2
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 63;
    p.lfoPitchDepth = 6;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[4] = withEnv (op (1.00f, 99, 0.000f, 0, 0), 80, 56, 10, 45, 98, 98, 36, 0);
    p.ops[5] = withEnv (op (1.00f, 83, -0.857f, 0, 0), 53, 46, 32, 61, 99, 93, 90, 0);
    p.ops[0] = withEnv (op (2.00f, 96, 0.857f, 0, 0), 54, 15, 10, 47, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (2.00f, 72, 0.000f, 0, 0), 56, 74, 10, 45, 98, 98, 36, 0);
    p.ops[2] = withEnv (op (2.00f, 80, 0.000f, 0, 0), 76, 73, 10, 55, 99, 92, 0, 0);
    p.ops[3] = withEnv (op (2.00f, 82, 0.000f, 0, 0), 72, 76, 10, 32, 99, 92, 0, 0);
}
inline void build15 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 16
    p.feedback  = 5;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 23;
    p.lfoPitchDepth = 8;
    p.lfoAmpDepth   = 13;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 98, -0.286f, 2, 0), 61, 67, 70, 65, 93, 89, 98, 0);
    p.ops[1] = withEnv (op (1.00f, 75, 0.571f, 2, 4), 99, 97, 62, 54, 99, 99, 90, 0);
    p.ops[2] = withEnv (op (1.00f, 76, -0.429f, 0, 0), 53, 38, 75, 61, 88, 44, 24, 0);
    p.ops[3] = withEnv (op (2.00f, 0, 0.000f, 0, 3), 61, 25, 25, 60, 99, 99, 97, 0);
    p.ops[4] = withEnv (op (2.00f, 56, 0.000f, 0, 0), 65, 38, 0, 61, 99, 0, 0, 0);
    p.ops[5] = withEnv (op (1.00f, 83, 0.571f, 2, 0), 99, 64, 98, 61, 99, 67, 52, 0);
}
inline void build16 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 8
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 1;
    p.lfoAmpDepth   = 3;
    p.transpose     = 0;

    p.ops[3] = withEnv (op (1.00f, 99, 0.000f, 5, 4), 74, 85, 27, 70, 99, 95, 0, 0);
    p.ops[4] = withEnv (op (3.00f, 93, 0.000f, 7, 2), 91, 25, 39, 60, 99, 86, 0, 0);
    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 7, 3), 78, 87, 22, 75, 99, 92, 0, 0);
    p.ops[5] = withEnv (op (3.00f, 89, 0.000f, 4, 4), 81, 87, 22, 75, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (3.00f, 99, 0.000f, 7, 4), 81, 87, 22, 75, 99, 92, 0, 0);
    p.ops[2] = withEnv (op (12.00f, 57, 0.000f, 6, 0), 99, 57, 99, 75, 99, 0, 0, 0);
}
inline void build17 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 16
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (3.00f, 86, 0.000f, 2, 0), 95, 67, 99, 71, 99, 99, 99, 0);
    p.ops[1] = withEnv (op (1.00f, 87, 0.000f, 7, 1), 99, 99, 99, 42, 99, 99, 99, 99);
    p.ops[2] = withEnv (op (1.00f, 99, 0.000f, 7, 0), 99, 99, 99, 71, 99, 99, 99, 0);
    p.ops[3] = withEnv (op (0.50f, 70, 0.000f, 0, 0), 92, 99, 15, 71, 99, 96, 75, 0);
    p.ops[4] = withEnv (op (0.50f, 85, 0.000f, 7, 0), 99, 99, 12, 0, 99, 99, 76, 0);
    p.ops[5] = withEnv (op (3.00f, 73, 0.000f, 2, 0), 99, 44, 1, 71, 99, 96, 75, 0);
}
inline void build18 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 16
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (0.50f, 99, 0.000f, 0, 7), 95, 62, 17, 58, 99, 95, 32, 0);
    p.ops[1] = withEnv (op (0.50f, 80, 0.000f, 0, 7), 99, 20, 0, 0, 99, 0, 0, 0);
    p.ops[2] = withEnv (op (0.50f, 99, 0.000f, 3, 6), 88, 96, 32, 30, 79, 65, 0, 0);
    p.ops[3] = withEnv (op (5.00f, 93, 0.000f, 5, 5), 90, 42, 7, 55, 90, 30, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 62, 0.000f, 3, 7), 99, 0, 0, 0, 99, 0, 0, 0);
    p.ops[5] = withEnv (op (9.00f, 85, 0.000f, 7, 1), 94, 56, 24, 55, 93, 28, 0, 0);
}
inline void build19 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 17
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 31;
    p.lfoDelay      = 33;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = -12;

    p.ops[0] = withEnv (op (0.50f, 99, 0.000f, 2, 3), 75, 37, 18, 63, 99, 70, 0, 0);
    p.ops[5] = withEnv (op (0.50f, 80, 0.000f, 2, 1), 28, 37, 42, 50, 99, 0, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 68, 1.000f, 3, 3), 73, 25, 32, 30, 97, 78, 0, 0);
    p.ops[2] = withEnv (op (0.50f, 99, 0.000f, 2, 3), 80, 39, 28, 53, 93, 57, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 75, 0.000f, 2, 4), 99, 51, 0, 0, 99, 74, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 87, 0.143f, 7, 3), 25, 50, 24, 55, 96, 97, 0, 0);
}
inline void build20 (FmPatch& p)
{
    p.algorithm = 22;   // DX7 algorithm 23
    p.feedback  = 5;
    p.lfoWaveform   = 0;
    p.lfoRate       = 26;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[2] = withEnv (op (4.00f, 50, 0.000f, 7, 2), 99, 28, 99, 50, 99, 25, 0, 0);
    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 1, 2), 80, 85, 24, 50, 99, 90, 0, 0);
    p.ops[1] = withEnv (op (3.00f, 72, 0.000f, 4, 4), 80, 85, 43, 50, 99, 74, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 99, -1.000f, 1, 3), 80, 85, 24, 50, 99, 90, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 99, 1.000f, 5, 3), 80, 85, 24, 50, 99, 90, 42, 0);
    p.ops[5] = withEnv (op (14.00f, 57, 0.000f, 7, 5), 99, 48, 99, 50, 99, 32, 0, 0);
}
inline void build21 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 7
    p.feedback  = 0;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 95, 0.000f, 0, 3), 95, 40, 49, 55, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (3.00f, 96, 0.000f, 2, 0), 99, 72, 0, 0, 82, 48, 0, 0);
    p.ops[3] = withEnv (op (0.50f, 99, 0.000f, 1, 3), 95, 33, 49, 41, 99, 92, 0, 0);
    p.ops[2] = withEnv (op (5.00f, 85, 0.000f, 2, 0), 99, 75, 0, 82, 82, 48, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 93, 0.000f, 2, 0), 99, 75, 0, 8, 82, 48, 0, 0);
    p.ops[5] = withEnv (op (4.00f, 99, 0.000f, 2, 0), 0, 63, 55, 0, 78, 78, 0, 0);
}
inline void build22 (FmPatch& p)
{
    p.algorithm = 3;   // DX7 algorithm 2
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 40;
    p.lfoPitchDepth = 17;
    p.lfoAmpDepth   = 15;
    p.transpose     = 0;

    p.ops[4] = withEnv (op (1.00f, 90, 0.000f, 3, 6), 94, 62, 58, 34, 99, 92, 0, 0);
    p.ops[5] = withEnv (op (4.00f, 99, 0.000f, 0, 6), 99, 68, 28, 48, 99, 83, 0, 0);
    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 3, 5), 94, 64, 30, 33, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 82, 0.000f, 1, 6), 90, 28, 17, 39, 99, 76, 0, 0);
    p.ops[2] = withEnv (op (4.00f, 83, 0.000f, 1, 6), 91, 37, 29, 29, 99, 90, 0, 0);
    p.ops[3] = withEnv (op (3.00f, 81, 0.000f, 1, 6), 82, 53, 37, 48, 99, 81, 0, 0);
}
inline void build23 (FmPatch& p)
{
    p.algorithm = 17;   // DX7 algorithm 5
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 95, 0.286f, 0, 2), 95, 33, 71, 25, 99, 0, 32, 0);
    p.ops[1] = withEnv (op (3.00f, 78, 0.429f, 0, 2), 98, 12, 71, 28, 99, 0, 32, 0);
    p.ops[2] = withEnv (op (1.00f, 99, -0.714f, 0, 2), 95, 33, 71, 25, 99, 0, 32, 0);
    p.ops[3] = withEnv (op (3.00f, 75, -0.286f, 0, 2), 98, 12, 71, 28, 99, 0, 32, 0);
    p.ops[4] = withEnv (op (1.00f, 99, 0.000f, 5, 2), 76, 78, 71, 70, 99, 0, 0, 0);
    p.ops[5] = withEnv (op (2.00f, 85, -1.000f, 0, 2), 98, 91, 0, 28, 99, 0, 0, 0);
}
inline void build24 (FmPatch& p)
{
    p.algorithm = 17;   // DX7 algorithm 5
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 30;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 5;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 97, 0.714f, 0, 3), 34, 42, 71, 34, 99, 99, 99, 0);
    p.ops[1] = withEnv (op (0.50f, 87, 0.714f, 0, 7), 99, 0, 0, 0, 99, 99, 99, 0);
    p.ops[2] = withEnv (op (0.50f, 99, 0.000f, 2, 3), 80, 49, 17, 30, 99, 95, 0, 0);
    p.ops[3] = withEnv (op (3.00f, 91, 0.000f, 3, 3), 80, 70, 9, 12, 88, 80, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 98, 1.000f, 0, 3), 41, 42, 71, 34, 99, 99, 99, 0);
    p.ops[5] = withEnv (op (1.00f, 75, -1.000f, 0, 7), 99, 0, 0, 0, 99, 99, 99, 0);
}
inline void build25 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 15
    p.feedback  = 5;
    p.lfoWaveform   = 0;
    p.lfoRate       = 25;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 10;
    p.lfoAmpDepth   = 99;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 0, 4), 99, 40, 33, 38, 99, 92, 0, 0);
    p.ops[1] = withEnv (op (1.00f, 64, 0.000f, 2, 2), 99, 19, 20, 9, 99, 87, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 99, 0.000f, 3, 3), 99, 30, 35, 42, 99, 92, 0, 0);
    p.ops[4] = withEnv (op (2.00f, 88, 1.000f, 1, 3), 99, 44, 50, 21, 91, 82, 0, 0);
    p.ops[2] = withEnv (op (5.00f, 64, 0.000f, 0, 3), 99, 40, 38, 0, 91, 82, 0, 0);
    p.ops[5] = withEnv (op (1.00f, 49, 0.000f, 0, 3), 99, 49, 28, 12, 91, 82, 0, 0);
}
inline void build26 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 16
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 11;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 16;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (0.50f, 99, 0.000f, 1, 3), 99, 36, 98, 33, 99, 0, 0, 0);
    p.ops[1] = withEnv (op (0.50f, 86, 0.429f, 1, 1), 99, 74, 0, 0, 99, 0, 0, 0);
    p.ops[2] = withEnv (op (0.50f, 85, -0.429f, 0, 3), 99, 77, 26, 23, 99, 72, 0, 0);
    p.ops[3] = withEnv (op (0.50f, 87, 0.000f, 7, 7), 99, 31, 17, 30, 99, 75, 0, 0);
    p.ops[4] = withEnv (op (0.50f, 73, 0.000f, 1, 0), 99, 50, 26, 19, 99, 0, 0, 0);
    p.ops[5] = withEnv (op (0.50f, 73, 0.000f, 1, 3), 98, 2, 26, 27, 98, 0, 0, 0);
}
inline void build27 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 18
    p.feedback  = 2;
    p.lfoWaveform   = 0;
    p.lfoRate       = 99;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 90, 0.000f, 1, 4), 60, 39, 28, 49, 99, 99, 99, 0);
    p.ops[1] = withEnv (op (1.00f, 93, 0.000f, 0, 4), 60, 39, 28, 45, 99, 99, 99, 0);
    p.ops[5] = withEnv (op (1.00f, 66, 0.000f, 0, 4), 60, 39, 8, 0, 99, 99, 99, 0);
    p.ops[2] = withEnv (op (1.00f, 75, 0.000f, 0, 1), 94, 68, 24, 55, 96, 89, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 64, 0.000f, 0, 0), 99, 0, 0, 0, 99, 0, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 78, 0.000f, 0, 1), 94, 56, 24, 55, 96, 78, 0, 0);
}
inline void build28 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 7
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 35;
    p.lfoDelay      = 35;
    p.lfoPitchDepth = 11;
    p.lfoAmpDepth   = 2;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 87, -1.000f, 0, 0), 34, 20, 53, 57, 99, 94, 97, 0);
    p.ops[1] = withEnv (op (1.00f, 99, 0.000f, 2, 0), 19, 26, 53, 25, 51, 61, 76, 51);
    p.ops[3] = withEnv (op (1.00f, 99, 1.000f, 3, 0), 33, 20, 53, 39, 99, 94, 97, 0);
    p.ops[2] = withEnv (op (1.00f, 99, 0.429f, 1, 0), 72, 19, 41, 12, 48, 58, 20, 9);
    p.ops[4] = withEnv (op (1.00f, 53, -0.143f, 1, 0), 35, 21, 36, 63, 99, 90, 85, 0);
    p.ops[5] = withEnv (op (5.00f, 55, 0.143f, 0, 0), 99, 72, 48, 17, 99, 99, 99, 0);
}
inline void build29 (FmPatch& p)
{
    p.algorithm = 1;   // DX7 algorithm 18
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 37;
    p.lfoDelay      = 42;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 99;
    p.transpose     = 12;

    p.ops[0] = withEnv (op (1.00f, 99, 0.143f, 0, 1), 99, 0, 12, 70, 99, 95, 95, 0);
    p.ops[1] = withEnv (op (1.00f, 71, -0.143f, 0, 3), 99, 95, 0, 0, 99, 96, 89, 0);
    p.ops[5] = withEnv (op (1.00f, 82, 0.000f, 0, 3), 99, 87, 0, 0, 93, 90, 0, 0);
    p.ops[2] = withEnv (op (2.00f, 71, 0.286f, 0, 6), 99, 92, 28, 60, 99, 90, 0, 0);
    p.ops[3] = withEnv (op (3.00f, 43, -0.286f, 0, 1), 99, 99, 97, 0, 99, 65, 60, 0);
    p.ops[4] = withEnv (op (17.00f, 47, 0.000f, 0, 3), 99, 70, 60, 0, 99, 99, 97, 0);
}
inline void build30 (FmPatch& p)
{
    p.algorithm = 17;   // DX7 algorithm 5
    p.feedback  = 7;
    p.lfoWaveform   = 0;
    p.lfoRate       = 39;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 99;
    p.transpose     = 0;

    p.ops[0] = withEnv (op (1.00f, 99, 0.000f, 0, 3), 65, 24, 19, 57, 99, 85, 85, 0);
    p.ops[1] = withEnv (op (3.00f, 72, 0.000f, 0, 5), 39, 13, 12, 72, 99, 61, 66, 0);
    p.ops[2] = withEnv (op (1.00f, 99, 0.286f, 0, 0), 98, 29, 28, 33, 99, 0, 0, 0);
    p.ops[3] = withEnv (op (1.00f, 89, -0.286f, 0, 0), 98, 29, 28, 27, 99, 0, 0, 0);
    p.ops[4] = withEnv (op (9.00f, 83, 0.429f, 0, 0), 42, 17, 25, 53, 99, 99, 99, 99);
    p.ops[5] = withEnv (op (5.00f, 99, 0.000f, 0, 0), 49, 17, 25, 53, 99, 99, 99, 98);
}
inline void build31 (FmPatch& p)
{
    p.algorithm = 9;   // DX7 algorithm 10
    p.feedback  = 0;
    p.lfoWaveform   = 0;
    p.lfoRate       = 65;
    p.lfoDelay      = 0;
    p.lfoPitchDepth = 0;
    p.lfoAmpDepth   = 0;
    p.transpose     = -24;

    p.ops[3] = withEnv (op (4.00f, 99, 0.000f, 0, 0), 9, 14, 17, 34, 61, 96, 0, 0);
    p.ops[4] = withEnv (op (1.00f, 96, 0.000f, 0, 0), 82, 80, 19, 14, 80, 95, 0, 0);
    p.ops[5] = withEnv (op (6.00f, 99, 0.000f, 0, 0), 76, 35, 99, 11, 67, 38, 73, 0);
    p.ops[0] = withEnv (op (0.50f, 99, 0.000f, 0, 0), 13, 14, 20, 30, 99, 95, 99, 0);
    p.ops[1] = withEnv (op (2.00f, 96, 0.000f, 0, 0), 88, 24, 23, 37, 99, 90, 0, 0);
    p.ops[2] = withEnv (op (0.50f, 99, 0.000f, 0, 0), 89, 22, 20, 41, 99, 92, 0, 0);
}

inline const FactoryVoice* voices (int& countOut)
{
    static const FactoryVoice list[] =
    {
        { "钢琴 1", "琴键", 19, &build0 },
        { "钢琴 2", "琴键", 18, &build1 },
        { "钢琴 3", "琴键", 3, &build2 },
        { "电钢琴 1", "琴键", 5, &build3 },
        { "电风琴 1", "琴键", 32, &build4 },
        { "管风琴 1", "琴键", 19, &build5 },
        { "羽管键琴 1", "琴键", 5, &build6 },
        { "击弦古钢琴 1", "琴键", 3, &build7 },
        { "铜管 1", "管乐与弦乐", 22, &build8 },
        { "铜管 2", "管乐与弦乐", 22, &build9 },
        { "铜管 3", "管乐与弦乐", 18, &build10 },
        { "弦乐 1", "管乐与弦乐", 2, &build11 },
        { "弦乐 2", "管乐与弦乐", 2, &build12 },
        { "弦乐 3", "管乐与弦乐", 15, &build13 },
        { "管弦乐", "管乐与弦乐", 2, &build14 },
        { "长笛 1", "管乐与弦乐", 16, &build15 },
        { "吉他 1", "拨弦与钟", 8, &build16 },
        { "吉他 2", "拨弦与钟", 16, &build17 },
        { "贝斯 1", "拨弦与钟", 16, &build18 },
        { "贝斯 2", "拨弦与钟", 17, &build19 },
        { "颤音琴 1", "拨弦与钟", 23, &build20 },
        { "马林巴", "拨弦与钟", 7, &build21 },
        { "古筝", "拨弦与钟", 2, &build22 },
        { "管钟", "拨弦与钟", 5, &build23 },
        { "管钟琴", "打击与特效", 5, &build24 },
        { "钢鼓", "打击与特效", 15, &build25 },
        { "定音鼓", "打击与特效", 16, &build26 },
        { "口哨", "打击与特效", 18, &build27 },
        { "人声 1", "打击与特效", 7, &build28 },
        { "合成主音 1", "打击与特效", 18, &build29 },
        { "火车", "打击与特效", 5, &build30 },
        { "起飞", "打击与特效", 10, &build31 },
    };

    countOut = (int) (sizeof (list) / sizeof (list[0]));
    return list;
}

} // namespace nidaw::fm::dx7
