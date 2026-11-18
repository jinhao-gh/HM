#pragma once

#include "MusicMath.h"
#include "Types.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hmdaw
{

/** The controller-lane arithmetic.

    Pure functions over plain data, so the lane's curve, its editing rules and the
    event stream the engine plays can all be checked without a window or a device.
    `ControllerCurve`'s invariants (sorted by tick, one point per tick, values 0..127)
    are kept here and nowhere else.
*/
namespace controllers
{

/** How finely a drawn segment is turned into events: one sixteenth of a quarter
    note. A two-point ramp over a bar therefore becomes a staircase of 16 values,
    which is what a MIDI instrument hears anyway - a stream of CC messages - and
    keeps the drawn line and the played line the same shape. */
inline constexpr juce::int64 kDefaultStepTicks = kTicksPerQuarter / 16;

/** Controllers the host itself uses, so a curve must never be drawn on them:
    0 = bank select, 6 / 38 = RPN data entry, 100 / 101 = RPN select,
    123 = all notes off (the host sends all of these of its own accord). */
inline bool isReservedController (int controller) noexcept
{
    return controller == 0 || controller == 6 || controller == 38
               || controller == 100 || controller == 101 || controller == 123;
}

inline int clampController (int controller) noexcept
{
    return juce::jlimit (0, 127, controller);
}

inline int clampValue (int value) noexcept
{
    return juce::jlimit (0, 127, value);
}

/** The lane for a controller inside a clip, creating nothing. */
inline const ControllerCurve* findLane (const Clip& clip, int controller) noexcept
{
    for (const auto& curve : clip.controllerCurves)
        if (curve.controller == controller)
            return &curve;

    return nullptr;
}

inline ControllerCurve* findLane (Clip& clip, int controller) noexcept
{
    return const_cast<ControllerCurve*> (findLane (const_cast<const Clip&> (clip), controller));
}

/** The lane for a controller, created empty when the clip has none yet. */
inline ControllerCurve& laneFor (Clip& clip, int controller)
{
    if (auto* existing = findLane (clip, controller))
        return *existing;

    ControllerCurve curve;
    curve.controller = clampController (controller);
    clip.controllerCurves.push_back (std::move (curve));
    return clip.controllerCurves.back();
}

/** Puts a point into a lane, keeping the invariants: same tick replaces, everything
    stays sorted. Returns true when the lane actually changed. */
inline bool upsertControllerPoint (ControllerCurve& lane, juce::int64 tick, int value)
{
    const auto clamped = clampValue (value);

    for (auto& point : lane.points)
        if (point.tick == tick)
        {
            const auto changed = point.value != clamped;
            point.value = clamped;
            return changed;
        }

    lane.points.push_back ({ juce::jmax ((juce::int64) 0, tick), clamped });
    std::sort (lane.points.begin(), lane.points.end(),
               [] (const ControllerPoint& a, const ControllerPoint& b)
               { return a.tick < b.tick; });
    return true;
}

/** The point within `toleranceTicks` of a tick, or nullptr. Used by the mouse (which
    works in pixels) and by the "is there a knee here" question. */
inline ControllerPoint* findPointNear (ControllerCurve& lane, juce::int64 tick,
                                       juce::int64 toleranceTicks) noexcept
{
    ControllerPoint* best = nullptr;

    for (auto& point : lane.points)
        if (std::abs (point.tick - tick) <= toleranceTicks)
            if (best == nullptr || std::abs (point.tick - tick) < std::abs (best->tick - tick))
                best = &point;

    return best;
}

/** Removes the point nearest to a tick, if one is within reach. */
inline bool removeControllerPoint (ControllerCurve& lane, juce::int64 tick,
                                   juce::int64 toleranceTicks) noexcept
{
    for (auto it = lane.points.begin(); it != lane.points.end(); ++it)
        if (std::abs (it->tick - tick) <= toleranceTicks)
        {
            lane.points.erase (it);
            return true;
        }

    return false;
}

/** Drops a curve that has nothing left to say. */
inline void pruneEmptyCurves (Clip& clip)
{
    clip.controllerCurves.erase (
        std::remove_if (clip.controllerCurves.begin(), clip.controllerCurves.end(),
                        [] (const ControllerCurve& curve) { return curve.points.empty(); }),
        clip.controllerCurves.end());
}

/** The curve's value at a tick, or -1 when the lane has no points at all.

    Before the first point the curve takes the first point's value, and after the
    last point the last point's value - the same thing a stepped MIDI controller
    does, but interpolated between the knees so a drawn ramp really ramps.
*/
inline double laneValueAt (const ControllerCurve& lane, double tick) noexcept
{
    if (lane.points.empty())
        return -1.0;

    if (tick <= (double) lane.points.front().tick)
        return (double) lane.points.front().value;

    if (tick >= (double) lane.points.back().tick)
        return (double) lane.points.back().value;

    for (size_t i = 1; i < lane.points.size(); ++i)
    {
        const auto& previous = lane.points[i - 1];
        const auto& current = lane.points[i];

        if (tick <= (double) current.tick)
        {
            const auto span = (double) (current.tick - previous.tick);

            if (span <= 0.0)
                return (double) current.value;

            const auto t = (tick - (double) previous.tick) / span;
            return (double) previous.value + t * ((double) current.value - (double) previous.value);
        }
    }

    return (double) lane.points.back().value;
}

/** One controller value on the timeline: the form the engine plays and the form the
    VST host sends. `timeSeconds` is absolute (the clip has already been placed). */
struct ControllerValue
{
    double      timeSeconds = 0.0;
    juce::int64 tick        = 0;
    int         value       = 0;
};

/** One period's worth of a lane, in **clip-relative** ticks: the curve's knees plus
    the interpolated steps between them, plus the held value up to the end of the
    period. Points at or past `periodTicks` take no part, exactly as notes past one
    period take no part while a clip is looping. */
inline std::vector<ControllerValue> laneStepsForOnePeriod (const ControllerCurve& lane,
                                                           juce::int64 periodTicks,
                                                           juce::int64 stepTicks)
{
    std::vector<ControllerValue> values;

    if (lane.points.empty() || periodTicks <= 0)
        return values;

    const auto step = juce::jmax ((juce::int64) 1, stepTicks);

    /** `isKnee` points are the curve's own shape and are always kept (the lane draws
        them); the values filled in between two knees are only worth sending while
        they actually change, so a curve that holds at one value costs nothing after
        its last knee. */
    auto push = [&values, periodTicks] (juce::int64 tick, int value, bool isKnee)
    {
        if (tick < 0 || tick >= periodTicks)
            return;

        if (! values.empty() && values.back().tick == tick)
        {
            values.back().value = value;
            return;
        }

        if (! isKnee && ! values.empty() && values.back().value == clampValue (value))
            return;

        values.push_back ({ 0.0, tick, clampValue (value) });
    };

    // Every knee, and the steps along the segment that ends at it.
    for (size_t i = 0; i < lane.points.size(); ++i)
    {
        const auto& point = lane.points[i];

        // While looping, only the content of one period takes part - exactly what
        // the notes inside the clip do.
        if (point.tick >= periodTicks)
            continue;

        if (i > 0)
        {
            const auto& previous = lane.points[i - 1];
            const auto previousTick = juce::jmax (previous.tick, (juce::int64) 0);

            for (auto tick = previousTick + step; tick < point.tick; tick += step)
                push (tick, (int) std::lround (laneValueAt (lane, (double) tick)), false);
        }

        push (point.tick, point.value, true);
    }

    // After the last knee the curve holds: nothing changes, so nothing more is sent
    // (the value the instrument is left on is the last one it was told).
    if (! lane.points.empty())
    {
        const auto last = lane.points.back();
        const auto start = juce::jmax (last.tick, (juce::int64) 0);

        for (auto tick = start + step; tick < periodTicks; tick += step)
            push (tick, last.value, false);
    }

    std::sort (values.begin(), values.end(),
               [] (const ControllerValue& a, const ControllerValue& b)
               { return a.tick < b.tick; });
    return values;
}

/** The whole curve of a clip, tiled exactly the way the notes are: one period of
    `loopLengthTicks` repeated across the clip's window when it loops, else one pass
    over the window. Returns absolute ticks starting at the clip's own `startTick`.
*/
inline std::vector<ControllerValue> expandControllers (const Clip& clip, const ControllerCurve& lane,
                                                       juce::int64 stepTicks = kDefaultStepTicks)
{
    std::vector<ControllerValue> values;

    if (clip.lengthTicks <= 0 || lane.points.empty())
        return values;

    const auto period = clip.looped ? juce::jmax ((juce::int64) 1, clip.loopLengthTicks)
                                    : clip.lengthTicks;
    const auto repeats = music::clipRepeatCount (clip);
    const auto windowEnd = clip.endTick();

    const auto onePeriod = laneStepsForOnePeriod (lane, period, stepTicks);

    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        const auto offset = clip.startTick + (juce::int64) repeat * period;

        for (const auto& value : onePeriod)
        {
            const auto tick = offset + value.tick;

            if (tick < clip.startTick || tick >= windowEnd)
                continue;

            values.push_back ({ 0.0, tick, value.value });
        }
    }

    std::sort (values.begin(), values.end(),
               [] (const ControllerValue& a, const ControllerValue& b)
               { return a.tick < b.tick; });

    // Two repeats can land on the same tick (a knee exactly on a period boundary):
    // the later repeat's value is the one that is playing.
    values.erase (std::unique (values.begin(), values.end(),
                               [] (const ControllerValue& a, const ControllerValue& b)
                               { return a.tick == b.tick; }),
                  values.end());
    return values;
}

/** The same tiling, but only the knees - no steps between them. This is what the
    lane draws: the polyline through the drawn points, repeated across the clip
    exactly where the played stream is. */
inline std::vector<ControllerValue> expandControllerPoints (const Clip& clip,
                                                            const ControllerCurve& lane)
{
    const auto period = clip.looped ? juce::jmax ((juce::int64) 1, clip.loopLengthTicks)
                                    : juce::jmax (kMinClipTicks, clip.lengthTicks);

    return expandControllers (clip, lane, period);
}

} // namespace controllers
} // namespace hmdaw
