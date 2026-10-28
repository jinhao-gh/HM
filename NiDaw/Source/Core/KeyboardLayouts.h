#pragma once

#include "Types.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace nidaw::keyboard
{

/** Which computer-keyboard layout is playing the on-screen keyboard.

    `normal` is the two-row layout HM has always had (the bottom letter row is one
    octave from C, the letter row above it the next). `bayan` is the accordion
    layout: the three rows of the board are a chromatic run of three semitones per
    column, and the number row above them is the bass side, which walks the circle
    of fifths **zigzag**: a fifth up, a fourth down, a fifth up, a fourth down…
    (see `bassNoteFor`).
*/
enum class Layout { normal, bayan };

/** The piano the layouts have to stay inside. */
constexpr int kLowestNote  = 21;    // A0
constexpr int kHighestNote = 108;   // C8

/** Which physical thing started a live note.

    Two of them may hold the *same* note at once (a melody key and a bass key, or a
    hardware keyboard and the on-screen piano), and the app keeps one note per
    source: each sounds its own voice and each lets go of its own.
*/
constexpr juce::int64 kHardwareTrigger = 0;
constexpr juce::int64 kMouseTrigger    = 1;
/** A computer key: each key is its own source, so two keys that map to the same
    note still sound twice. */
constexpr juce::int64 kTypingTriggerBase = 100;

inline juce::int64 typingTriggerFor (int keyCode) noexcept
{
    return kTypingTriggerBase + keyCode;
}

/** One cap of the drawn computer keyboard. */
struct Cap
{
    /** The character printed on the cap (0x08 is Backspace, drawn as ⌫). */
    juce::juce_wchar key = 0;
    /** 0 = the number row, 1 = the letter row, 2 = the home row, 3 = the bottom row. */
    int   row = 0;
    /** Left edge and width in "key units" (one letter key wide), so the diagram can
        be drawn with the real stagger. */
    float x = 0.0f;
    float width = 1.0f;
    /** Melody: how many semitones above the melody row's base note this cap plays.
        -1 = this cap is not part of the melody row in this layout. */
    int   semitone = -1;
    /** Bass: where this cap sits in the circle of fifths (0 = the leftmost key of
        the row). -1 = this cap is not part of the bass row. */
    int   bassIndex = -1;
};

/** Every cap of the physical board (43 of them), in drawing order: the number row
    with Backspace, then the three letter rows, each with its own indent. The
    mapping fields are left at -1 - use `capsFor`. */
const std::vector<Cap>& physicalBoard();

/** The same board, with the melody / bass mapping of that layout filled in. */
const std::vector<Cap>& capsFor (Layout layout);

/** The bass side: 14 keys. `bassIndex` 0 = `·` (the key left of 1) and 13 =
    Backspace; every neighbouring pair is a fifth up (+7) then a fourth down (-5),
    so `5` is the anchor the row is written around. */
constexpr int kBassKeyCount = 14;
/** The anchor's own note with no shift: `5` = F3, and `6` is a fifth above it. */
constexpr int kBassAnchorNote = 53;    // F3
/** The note of that key of the bass row, with that shift applied (a shift is in
    semitones; the fifth buttons move it by 7). */
int bassNoteFor (int bassIndex, int shift) noexcept;

/** The melody row's base note: the note of its lowest key. */
int baseNoteFor (Layout layout, int shift) noexcept;

/** How far each row can be shifted before it would run off the piano. */
struct ShiftRange { int lowest = 0; int highest = 0; };
ShiftRange melodyShiftRange (Layout layout) noexcept;
ShiftRange bassShiftRange() noexcept;

/** The note a melody key plays, or -1 when that key is not part of the melody row. */
int melodyNoteFor (Layout layout, juce::juce_wchar key, int shift) noexcept;

/** The note a bass key plays, or -1 when that key is not part of the bass row. */
int bassNoteForKey (juce::juce_wchar key, int shift) noexcept;

/** The note a cap plays under the current layout and shifts, or -1. */
int noteForCap (Layout layout, const Cap& cap, int melodyShift, int bassShift) noexcept;

/** Every computer key that plays this note right now ("w·6"), for the piano. */
juce::String keyLettersFor (Layout layout, int melodyShift, int bassShift, int midiNote);

/** What the key cap prints / what the readouts say. */
juce::String keyName (juce::juce_wchar key);
juce::String layoutName (Layout layout);

//==============================================================================
/** The window's own state, remembered in `%APPDATA%\NiDaw\keyboard.json`. */
struct Settings
{
    bool   alwaysOnTop = false;
    bool   collapsed   = false;
    Layout layout      = Layout::normal;
    int    melodyShift = 0;
    int    bassShift   = 0;
    int    velocity    = 100;
};

void clamp (Settings& settings) noexcept;
juce::var settingsToVar (const Settings& settings);
Settings settingsFromVar (const juce::var& value);

} // namespace nidaw::keyboard
