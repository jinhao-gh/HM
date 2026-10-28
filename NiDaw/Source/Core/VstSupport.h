#pragma once

#include "Types.h"

#include <array>

namespace nidaw
{

/** Everything about hosting an external instrument that is pure logic: how a
    frequency becomes MIDI, which channel a sounding note owns, and how the
    plugin's location and state travel through the project file.

    Kept free of any JUCE plugin dependency so the unit tests build both this and
    the project file without pulling the VST3 SDK in.
*/
namespace vst
{

/** The MIDI channels a track may spread its notes over.

    Each sounding note takes one channel so that it can carry its own pitch bend,
    which is how a frequency that is not exactly on a semitone still comes out at
    the right pitch (the MPE convention).
*/
constexpr int kMidiChannels = 16;

/** The pitch bend range this file assumes and programs into the plugin. */
constexpr double kBendRangeSemitones = 2.0;

/** A frequency expressed the way MIDI can carry it: a semitone index plus the
    cents left over, which go into the channel's pitch bend. */
struct MidiPitch
{
    /** The MIDI note number, 0..127 (clamped). */
    int    note  = 69;
    /** The offset from that note, -50..+50 cents. */
    double cents = 0.0;
};

/** The frequency of a 12 tone equal tempered note, A4 = 440 Hz. */
double hzForMidiNote (int note) noexcept;

/** Splits a frequency into the nearest note and the cents that are left over.
    Frequencies outside the MIDI range clamp to the end notes. */
MidiPitch midiPitchForHz (double hz) noexcept;

/** The inverse: the frequency a note plus a cents offset stands for. */
double hzForMidiPitch (int note, double cents) noexcept;

/** The equal tempered frequency nearest to `hz` - what a plugin that cannot take a
    per-note bend is played at instead (see `VstParams::equalTemperament`). */
double snapToSemitone (double hz) noexcept;

/** The 14 bit pitch bend value for a cents offset, at kBendRangeSemitones. */
int bendValueFor (double cents) noexcept;

/** One controller message: `controller` gets `value` on the note's channel. */
struct ControllerMessage
{
    int controller = 0;
    int value      = 0;
};

/** The controllers that set a channel's pitch bend range (RPN 0) to
    kBendRangeSemitones, sent once per channel when a plugin is created.

    The last two are the "RPN null" pair: without them the channel is left with RPN 0
    still selected, and a plugin that treats a later data entry as bend-range data
    would change the wrong thing. Nothing here is a sound controller - no volume, no
    expression, no pan (see the test that keeps it that way).
*/
std::array<ControllerMessage, 6> pitchBendRangeMessages() noexcept;

/** Hands every sounding note a MIDI channel of its own.

    A channel carries a note's own pitch bend, so **which channel a note gets** is
    what keeps a chord in tune. Two rules follow from that, and both of them exist
    because the plugin keeps sounding a released note (its tail) on the channel it
    was given:

      - a channel that has never been handed out is used before any channel that has
        (so the first sixteen notes of a take each get their own);
      - after that, the channel whose note was released **longest ago** is reused, so
        the previous note's tail has had the most time to decay before its bend is
        taken over by the new note.

    Sixteen notes at once is the hard limit. A seventeenth note evicts the channel
    that has been *held* the longest and reports which note it took the channel from:
    the host sends that note a real note-off, so it ends cleanly instead of being
    pulled around by the new note's bend.
*/
class ChannelAllocator
{
public:
    /** What `allocate` decided: the channel the note should sound on (-1 when there
        is no channel at all), plus the note that had to be let go of to get it
        (0 when nothing was evicted). */
    struct Allocation
    {
        int         channel = -1;
        juce::int64 evictedNoteId = 0;
    };

    /** The channel this note should sound on, taking one when needed. */
    Allocation allocate (juce::int64 noteId) noexcept;

    /** Gives the note's channel back. Safe to call for a note that has none. */
    void release (juce::int64 noteId) noexcept;

    /** The channel the note is on, or -1. */
    int channelFor (juce::int64 noteId) const noexcept;

    /** Forgets every note; the channels all become free again. */
    void reset() noexcept;

    int activeCount() const noexcept;

    /** How many channels have been handed out since the last `reset()`. A channel
        that was used and released still counts: this is what the tests read to see
        "the notes were spread out" rather than "the same channel came round again". */
    int channelsUsedSinceReset() const noexcept;

private:
    struct Slot
    {
        bool        active = false;
        juce::int64 noteId = 0;
        juce::uint32 used  = 0;   // when this channel was last handed out
        /** When its note was let go of; 0 while the channel has never been used
            since the last reset. */
        juce::uint32 released = 0;
    };

    std::array<Slot, kMidiChannels> slots {};
    juce::uint32 counter = 0;
};

/** The plugin location and state, as the project file holds them. */
juce::var vstParamsToVar (const VstParams& params);
VstParams vstParamsFromVar (const juce::var& value);

} // namespace vst
} // namespace nidaw
