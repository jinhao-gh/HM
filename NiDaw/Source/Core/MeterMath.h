#pragma once

#include <cmath>

namespace nidaw
{

/** The level meter's maths.

    The audio thread only ever reports the peak of the block it just rendered; all
    the "how fast does the needle fall" work happens here, on the message thread, so
    the audio thread stays a pure number. Pure arithmetic, so the unit tests can
    check the fall, the hold and the clip flag without a device.
*/
namespace meter
{

/** How fast the bar falls back, in dB per second. */
inline constexpr double kFallDbPerSecond = 20.0;
/** How long the little peak marker stays where it was. */
inline constexpr double kPeakHoldSeconds = 1.5;

/** The bar's next value: it jumps up to a new peak at once, and otherwise slides
    down at kFallDbPerSecond. */
inline float nextMeterLevel (float level, float peak, double dtSeconds) noexcept
{
    const auto clampedPeak = peak > 0.0f ? peak : 0.0f;

    if (clampedPeak >= level)
        return clampedPeak;

    const auto fall = (float) std::pow (10.0, -kFallDbPerSecond * dtSeconds / 20.0);
    const auto fallen = level * fall;

    return fallen < 0.0001f ? 0.0f : fallen;
}

/** The hold marker: it follows the bar up, stays put for kPeakHoldSeconds and then
    starts falling with it. Returns the new hold value. */
inline float nextPeakHold (float hold, float level, double& holdSecondsLeft,
                           double dtSeconds, double holdFor = kPeakHoldSeconds) noexcept
{
    if (level >= hold)
    {
        holdSecondsLeft = holdFor;
        return level;
    }

    if (holdSecondsLeft > 0.0)
    {
        holdSecondsLeft -= dtSeconds;
        return hold;
    }

    return nextMeterLevel (hold, 0.0f, dtSeconds);
}

/** True when this block went past full scale: what the strip's red clip dot shows. */
inline bool isClipping (float peak) noexcept
{
    return peak > 1.0f;
}

} // namespace meter
} // namespace nidaw
