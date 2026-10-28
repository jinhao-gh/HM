#pragma once

#include "Types.h"

namespace nidaw::fm
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

} // namespace nidaw::fm
