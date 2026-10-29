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

} // namespace nidaw::fm
