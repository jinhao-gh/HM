#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace nidaw::midimap
{

/** Which MIDI note plays which frequency.

    The mapping is stored as **frequencies**, not as "the third dot of that group":
    editing the reference group later must not quietly rewrite a mapping the user
    taught by hand.

    Two shapes, because a MIDI keyboard is a repeating thing and a mapping often is
    too:
      - looping: one tile of `loopKeys` semitones, tiled up and down the keyboard -
        that is how a scale or a small set of ratios is played over several octaves;
      - free: the whole MIDI range (0..127), each key with its own frequencies.

    **One key can play several frequencies at once** (one cable per dot it is tied
    to): pressing it sounds all of them, and recording writes one note per cable. An
    empty set (or a note that is not in the table) means "nothing here": the key is
    silent and is not recorded.
*/
struct Map
{
    enum class Mode { looping, free };

    static constexpr int kMinLoopKeys      = 1;
    static constexpr int kMaxLoopKeys      = 24;
    static constexpr int kDefaultLoopKeys  = 12;
    static constexpr int kDefaultAnchorNote = 60;   // C4
    static constexpr int kLowestNote       = 0;
    static constexpr int kHighestNote      = 127;

    Mode mode = Mode::looping;
    /** How many semitones one tile covers (looping mode only). */
    int  loopKeys = kDefaultLoopKeys;
    /** Which MIDI note the tile's first key sits on (looping mode only). */
    int  anchorNote = kDefaultAnchorNote;
    /** looping: `loopKeys` entries, one per step of the tile; each is the set of
        frequencies that step plays (empty = the step is silent). */
    std::vector<std::vector<double>> tile =
        std::vector<std::vector<double>> ((size_t) kDefaultLoopKeys);
    /** free: note -> the frequencies that key plays; a missing note is silent. */
    std::map<int, std::vector<double>> free;

    /** Every frequency this MIDI note plays, low to high. Empty = nothing bound. */
    std::vector<double> frequenciesFor (int midiNote) const;

    /** The lowest frequency this note plays, or 0 when nothing is bound to it.
        (What a caption or a single-value readout wants; the engine plays them all.) */
    double frequencyFor (int midiNote) const;

    /** How many cables leave this key. */
    int bindingCount (int midiNote) const;
    bool isBound (int midiNote) const { return bindingCount (midiNote) > 0; }

    /** Replaces every cable of that key with this one frequency (0 = clear them). */
    void setFrequency (int midiNote, double hz);
    /** Adds one more cable to that key (a frequency already there is left alone). */
    void addFrequency (int midiNote, double hz);
    /** Lets go of just this one frequency (the cable that was right-clicked). */
    void removeFrequency (int midiNote, double hz);
    /** Lets go of everything bound to that key. */
    void clearFrequency (int midiNote);

    /** The position of that note inside the tile (0..loopKeys-1), which is what the
        "click a key, then press a MIDI key" anchor flow teaches. */
    int tileOffsetOf (int midiNote) const;

    /** The keys the mapping editor shows: the tile's semitones while looping, the
        whole MIDI range when free. */
    std::vector<int> visibleNotes() const;

    /** How many of the visible keys have a frequency. */
    int boundKeyCount() const;

    /** How many cables the whole mapping has (one per key-and-frequency pair). */
    int cableCount() const;

private:
    /** The tile step a note lands on, grown to `loopKeys` first: the writable side
        of the wrapping rule the read side uses. */
    std::vector<double>& tileAt (int midiNote);
};

/** Changes the tile's width, keeping the keys that still fit and padding the rest
    with empty steps. */
void setLoopKeys (Map& map, int keys);

/** True when the two maps describe the same notes (used by the preset buttons to
    tell "unchanged" from "edited"). */
bool isSame (const Map& a, const Map& b);

/** "C4" / "F#3" style name for a MIDI note, so the status line can say where the
    anchor sits. */
juce::String noteName (int midiNote);

} // namespace nidaw::midimap
