#pragma once

#include "Types.h"

namespace nidaw
{

/** The track insert chain: what it holds, how it moves, and how it is stored.

    Pure logic, so the unit tests cover the ordering rules and the project file
    without an audio device or a plugin.
*/
namespace effects
{

/** How many effects one track may hold. */
constexpr int kMaxPerTrack = 16;

/** Appends a slot at the end. False (and nothing changed) when the chain is full
    or the slot names no plugin. */
bool add (std::vector<EffectSlot>& chain, EffectSlot slot);

/** Removes one slot. False when the index is not in the chain. */
bool remove (std::vector<EffectSlot>& chain, int index);

/** Moves the slot at `from` so it ends up at `to` (both clamped). False when the
    move would do nothing. */
bool move (std::vector<EffectSlot>& chain, int from, int to);

/** The two-button versions of the same thing. */
bool moveUp (std::vector<EffectSlot>& chain, int index);
bool moveDown (std::vector<EffectSlot>& chain, int index);

/** Flips one slot's bypass. False when the index is not in the chain. */
bool toggleBypass (std::vector<EffectSlot>& chain, int index);

/** The chain as the project file stores it. */
juce::var toVar (const std::vector<EffectSlot>& chain);
std::vector<EffectSlot> fromVar (const juce::var& value);

} // namespace effects
} // namespace nidaw
