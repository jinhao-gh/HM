#pragma once

/** The test instrument's "what has the host sent me?" report, shared by the plugin
    and by the offscreen self test that reads it.

    It travels as one integer parameter, because that is the only channel a plugin's
    private bookkeeping can reach a host through: HM's own state blob is the VST3
    wrapper's, not the plugin's. Ten bits are enough for what the self test asks -
    "did the pitch bend range arrive?", "was there ever a volume controller?" - and
    the answer is written whenever a message arrives.
*/
namespace hmtests
{

/** The parameter's name, as the host sees it. */
constexpr const char* kReceiptsParameterName = "MIDI received";

/** The largest value the parameter can hold. */
constexpr int kReceiptsMax = 1023;

// ---- what the host did with the channels -----------------------------------
//
// The host spreads a track's sounding notes over MIDI channels, one note per
// channel, so each note can carry its own pitch bend. These four parameters are how
// the offscreen self test reads back whether that really happened.

/** How many times a note-on landed on a channel that was already holding a *held*
    (not released) note at a different pitch. Two pitches in one channel means one
    pitch bend is being shared, which is the bug this reports. A released note that
    is still ringing does not count: the pool is sixteen channels, and a seventeenth
    note has to take one from somebody. */
constexpr const char* kConflictsParameterName = "Channel conflicts";

/** How many different channels have been handed a note-on (0..16). */
constexpr const char* kChannelsUsedParameterName = "Channels used";

/** How many different channels have been sent CC64 (0..16). */
constexpr const char* kPedalChannelsParameterName = "Pedal channels";

/** The most voices that were ever sounding at the same time (0..16). */
constexpr const char* kPeakVoicesParameterName = "Peak voices";

/** Writing 1 here clears the four counters above; the plugin puts it back to 0. */
constexpr const char* kResetCountersParameterName = "Reset counters";

enum ReceiptBits
{
    kSawCc6       = 1 << 0,
    kSawCc38      = 1 << 1,
    kSawCc100     = 1 << 2,
    kSawCc101     = 1 << 3,
    kSawCc123     = 1 << 4,
    /** The three a host must never send: they move a plugin's own volume, pan or
        expression, which is the one thing this side may not touch. */
    kSawCc7       = 1 << 5,
    kSawCc10      = 1 << 6,
    kSawCc11      = 1 << 7,
    kSawNoteOn    = 1 << 8,
    kSawPitchBend = 1 << 9
};

/** The bit a controller number stands for, or 0 when the report ignores it. */
inline int receiptBitForController (int controller) noexcept
{
    switch (controller)
    {
        case 6:   return kSawCc6;
        case 38:  return kSawCc38;
        case 100: return kSawCc100;
        case 101: return kSawCc101;
        case 123: return kSawCc123;
        case 7:   return kSawCc7;
        case 10:  return kSawCc10;
        case 11:  return kSawCc11;
        default:  break;
    }

    return 0;
}

} // namespace hmtests
