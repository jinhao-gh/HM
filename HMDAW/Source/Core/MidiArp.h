#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace hmdaw::midiarp
{

/** How far apart the steps are.

    Everything but `free` is a note value: the player works the seconds out from
    the tempo at the playhead, so the arpeggio follows a tempo change instead of
    running at whatever the speed was when the key went down.
*/
enum class Rate
{
    quarter,
    quarterTriplet,
    eighth,
    eighthTriplet,
    sixteenth,
    sixteenthTriplet,
    thirtySecond,
    thirtySecondTriplet,
    free
};

/** The order the held notes are played in. `asPlayed` keeps the order the keys
    went down in; every other direction works on the notes low to high. */
enum class Direction { up, down, upDown, downUp, random, asPlayed };

/** What velocity the generated notes carry. */
enum class VelocityMode
{
    /** Every step the same (the 力度 control below). */
    fixed,
    /** Whatever the key was struck with. */
    played,
    /** Each step of the cycle a little softer, so the phrase breathes. */
    decrescendo
};

/** Everything the arpeggiator does, as plain data: it lives in the project's MIDI
    input state (so it is saved with the project and with a mapping preset) and is
    handed to the player as a copy whenever it changes. */
struct Settings
{
    static constexpr int kMinOctaves     = 1;
    static constexpr int kMaxOctaves     = 4;
    static constexpr int kMinGatePercent = 5;
    static constexpr int kMaxGatePercent = 200;
    static constexpr int kMinSwingPercent = 0;
    static constexpr int kMaxSwingPercent = 75;
    static constexpr double kMinFreeHz   = 0.5;
    static constexpr double kMaxFreeHz   = 50.0;
    /** What a decrescendo step keeps of the one before it. */
    static constexpr int kMinDecrescendoVelocity = 20;

    bool          enabled      = false;
    Rate          rate         = Rate::sixteenth;
    double        freeHz       = 8.0;
    Direction     direction    = Direction::up;
    int           octaves      = 1;
    int           gatePercent  = 50;
    int           swingPercent = 0;
    VelocityMode  velocityMode = VelocityMode::played;
    int           fixedVelocity = 100;
    bool          hold         = false;
};

/** Puts every field back inside its range (a hand-edited project file, or a
    preset written by an older build, cannot put the player out of step). */
void clamp (Settings& settings) noexcept;

/** True when the two describe the same playing (the preset buttons use it to tell
    "unchanged" from "edited"). */
bool isSame (const Settings& a, const Settings& b) noexcept;

/** `1/16`, `1/8T`, `自由 8.0 Hz`-style name for the rate, for the readouts. */
juce::String rateName (const Settings& settings);
/** The rate list the combo box offers, in the order it shows them. */
const std::vector<Rate>& allRates();

/** How long one step lasts, in ticks. `ticksPerQuarter` is the project's own PPQ
    (see `kTicksPerQuarter`), passed in so this header stays free of the project
    types - `Types.h` includes *this* file, not the other way round.

    A note value is a **musical** length, so it is counted in PPQ ticks and does not
    care about the tempo or the time signature (1/4 is always the whole PPQ count).
    `自由 Hz` is the other way round: a frequency has to be turned into ticks through
    the tempo, so that one needs the bpm at the playhead.

    This is what the recorded note's own tick and length are worked out from, so the
    take is as tight as the grid rather than as tight as the audio block.
*/
juce::int64 stepTicksFor (const Settings& settings, double bpm,
                          int ticksPerQuarter) noexcept;

/** The same length in seconds - how far apart the steps are when they are heard. */
double stepSecondsFor (const Settings& settings, double bpm,
                       int ticksPerQuarter) noexcept;

/** How long the note of one step sounds (the gate). */
juce::int64 gateTicksFor (const Settings& settings, juce::int64 stepTicks) noexcept;

/** How far into its slot this step really lands: swing pushes the off-beats later,
    which is what turns an even 1/8 into a shuffle. */
juce::int64 offsetTicksForStep (const Settings& settings, juce::int64 stepTicks,
                                int stepIndex) noexcept;

/** The notes one cycle plays, in order: the held keys, low to high (or in the
    order they were played), repeated over the octaves, then arranged by the
    direction. `seed` only matters for Direction::random. */
std::vector<int> orderFor (const Settings& settings, const std::vector<int>& heldNotes,
                           juce::uint32 seed = 1u);

/** Which note step `stepIndex` plays, or -1 when there is nothing to play. */
int noteForStep (const std::vector<int>& order, int stepIndex) noexcept;

/** The velocity step `stepIndex` carries. `playedVelocity` is the one the key was
    struck with, and `cycleLength` is how many steps one pass of the phrase takes
    (`order.size()`), so a decrescendo starts over with each pass. */
int velocityForStep (const Settings& settings, int playedVelocity, int stepIndex,
                     int cycleLength) noexcept;

/** Which notes the arpeggiator plays right now, given the keys that are really
    down. Without `hold` it is simply the held keys; with it, the set you played
    keeps running after you let go, and the next key you press starts a new one. */
struct LatchState
{
    std::vector<int> notes;
    /** Every key was let go while latched: the next press starts a new set. */
    bool startFreshOnNextPress = false;
};

void updateLatch (LatchState& state, const std::vector<int>& heldNotes, bool hold);

/** The project/preset form of the settings. */
juce::var settingsToVar (const Settings& settings);
Settings settingsFromVar (const juce::var& value);

} // namespace hmdaw::midiarp
