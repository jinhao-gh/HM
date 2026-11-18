#pragma once

#include "Types.h"

namespace hmdaw::fm
{

/** The RATE/LEVEL envelope timeline used by the FM engine.

    RATE 99 is nearly instant and RATE 0 takes about seven and a half seconds; the
    curve in between matches the way the original's rates bunch up at the fast end.
    These two live in Core so both the engine and the project format can use them
    without the document layer having to depend on the audio layer.
*/
inline double rateToSeconds (int rate) noexcept
{
    const auto r = juce::jlimit (0, 99, rate) / 99.0;
    return 0.0015 * std::pow (5000.0, 1.0 - r);
}

/** The RATE that makes a segment take roughly `seconds` (the inverse of above). */
inline int rateForSeconds (double seconds) noexcept
{
    const auto clamped = juce::jlimit (0.0015, 7.5, seconds);
    const auto r = 1.0 - std::log (clamped / 0.0015) / std::log (5000.0);
    return juce::jlimit (0, 99, (int) std::lround (r * 99.0));
}

/** The DX7 envelope timeline: RATE 99 is about a millisecond, RATE 0 about
    twenty seconds, and the steps bunch up towards the fast end exactly like the
    original's rate table. Used when `FmPatch::classicEngine` is on. */
inline double rateToSecondsClassic (int rate) noexcept
{
    const auto r = juce::jlimit (0, 99, rate) / 99.0;
    return 0.001 * std::pow (20000.0, 1.0 - r);
}

/** The classic RATE that makes a segment take roughly `seconds`. */
inline int classicRateForSeconds (double seconds) noexcept
{
    const auto clamped = juce::jlimit (0.001, 20.0, seconds);
    const auto r = 1.0 - std::log (clamped / 0.001) / std::log (20000.0);
    return juce::jlimit (0, 99, (int) std::lround (r * 99.0));
}

} // namespace hmdaw::fm
