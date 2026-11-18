#include "KeyboardLayouts.h"

#include <algorithm>
#include <cmath>

namespace hmdaw::keyboard
{

namespace
{
    constexpr juce::juce_wchar kBackspace = 0x08;

    /** The real board, without any mapping: the number row (with Backspace at its
        right end) and the three letter rows with the usual indent. */
    const std::vector<Cap>& board()
    {
        static const std::vector<Cap> caps = []
        {
            std::vector<Cap> result;

            const auto add = [&result] (juce::juce_wchar key, int row, float x, float width = 1.0f)
            {
                Cap cap;
                cap.key = key;
                cap.row = row;
                cap.x = x;
                cap.width = width;
                result.push_back (cap);
            };

            // Number row: · 1 2 3 4 5 6 7 8 9 0 - = ⌫
            add (0x0060, 0, 0.0f);                       // the ·/` key

            for (int i = 0; i < 10; ++i)
                add ((juce::juce_wchar) (U'0' + (i + 1) % 10), 0, 1.0f + (float) i);

            add (U'-', 0, 11.0f);
            add (U'=', 0, 12.0f);
            add (kBackspace, 0, 13.0f, 2.0f);

            // Letter rows. The indents are the widths of Tab / Caps / Shift, so the
            // picture is the one people see under their fingers.
            const juce::juce_wchar top[]    = { U'Q', U'W', U'E', U'R', U'T', U'Y', U'U', U'I', U'O', U'P' };
            const juce::juce_wchar home[]   = { U'A', U'S', U'D', U'F', U'G', U'H', U'J', U'K', U'L', U';' };
            const juce::juce_wchar bottom[] = { U'Z', U'X', U'C', U'V', U'B', U'N', U'M', U',', U'.', U'/' };

            for (int i = 0; i < 10; ++i)
                add (top[i], 1, 1.5f + (float) i);

            for (int i = 0; i < 10; ++i)
                add (home[i], 2, 1.75f + (float) i);

            for (int i = 0; i < 10; ++i)
                add (bottom[i], 3, 2.25f + (float) i);

            return result;
        }();

        return caps;
    }

    /** The two-row layout HM has always had: the bottom letter row is C..B, the
        letter row above it the next octave, and the digits in between fill the
        black keys of that upper octave. */
    int normalSemitoneFor (juce::juce_wchar key) noexcept
    {
        switch (key)
        {
            case U'Z': return 0;   case U'S': return 1;   case U'X': return 2;
            case U'D': return 3;   case U'C': return 4;   case U'V': return 5;
            case U'G': return 6;   case U'B': return 7;   case U'H': return 8;
            case U'N': return 9;   case U'J': return 10;  case U'M': return 11;

            case U'Q': return 12;  case U'2': return 13;
            case U'W': return 14;  case U'3': return 15;
            case U'E': return 16;  case U'R': return 17;
            case U'5': return 18;  case U'T': return 19;
            case U'6': return 20;  case U'Y': return 21;
            case U'7': return 22;  case U'U': return 23;
            default: break;
        }

        return -1;
    }

    /** 巴扬: each column of the three letter rows is three consecutive semitones
        (top, home, bottom), so the order written on the keys - q a z w s x … - is a
        plain chromatic run. */
    int bayanSemitoneFor (int row, float x) noexcept
    {
        if (row < 1 || row > 3)
            return -1;

        const auto indent = row == 1 ? 1.5f : (row == 2 ? 1.75f : 2.25f);
        const auto columnOffset = x - indent;

        if (columnOffset < -0.01f || columnOffset > 9.01f)
            return -1;

        const auto column = (int) std::lround (columnOffset);
        return column * 3 + (row - 1);
    }

    /** The distance from the `5` key (the row's anchor) for each of the 14 keys,
        in the order `· 1 2 3 4 5 6 7 8 9 0 - = ⌫`.

        Every neighbouring pair is a fifth up (+7) then a fourth down (-5) - the
        accordion's own zigzag: 4 -> 5 is down a fourth, 5 -> 6 up a fifth,
        6 -> 7 down a fourth, and so on to the end of the row.
    */
    constexpr int kBassOffsets[kBassKeyCount] =
    {
        1, -4, 3, -2, 5, 0, 7, 2, 9, 4, 11, 6, 13, 8
    };
}

const std::vector<Cap>& physicalBoard()
{
    return board();
}

const std::vector<Cap>& capsFor (Layout layout)
{
    static const std::vector<Cap> normal = []
    {
        auto caps = board();

        for (auto& cap : caps)
            cap.semitone = normalSemitoneFor (cap.key);

        return caps;
    }();

    static const std::vector<Cap> bayan = []
    {
        auto caps = board();

        for (auto& cap : caps)
        {
            // 巴扬: the number row is the bass side, the three letter rows are the
            // melody (and the digits that the normal layout uses are free again).
            cap.semitone = bayanSemitoneFor (cap.row, cap.x);

            if (cap.row == 0)
            {
                // The bass row runs ` 1 2 3 4 5 6 7 8 9 0 - = ⌫: the circle of fifths
                // starts at the key left of 1.
                if (cap.key == 0x0060)      cap.bassIndex = 0;
                else if (cap.key == kBackspace) cap.bassIndex = 13;
                else if (cap.key >= U'0' && cap.key <= U'9') cap.bassIndex = 1 + (int) (cap.key - U'0' + 9) % 10;
                else if (cap.key == U'-')   cap.bassIndex = 11;
                else if (cap.key == U'=')   cap.bassIndex = 12;
            }
        }

        return caps;
    }();

    return layout == Layout::bayan ? bayan : normal;
}

int bassNoteFor (int bassIndex, int shift) noexcept
{
    if (bassIndex < 0 || bassIndex >= kBassKeyCount)
        return -1;

    // `5` is F3 with no shift, so 6 lands on C4 and the whole row sits in
    // C#3-F#4: the low side of the melody rows, which start on A3.
    return kBassAnchorNote + kBassOffsets[bassIndex] + shift;
}

int baseNoteFor (Layout layout, int shift) noexcept
{
    const auto base = layout == Layout::bayan ? 57 : 60;
    return juce::jlimit (kLowestNote, kHighestNote, base + shift);
}

ShiftRange melodyShiftRange (Layout layout) noexcept
{
    const auto base = layout == Layout::bayan ? 57 : 60;
    const auto span = layout == Layout::bayan ? 29 : 23;

    return { kLowestNote - base, kHighestNote - span - base };
}

ShiftRange bassShiftRange() noexcept
{
    // The row is a fixed table (nothing folds any more, since the fourth-ninth batch),
    // so the only limit is the piano itself: at the extreme shifts the lowest and the
    // highest key of the row still have to be on the keyboard. That leaves room for the
    // octave buttons (±12) on top of the semitone (±1) and fifth (±7) ones.
    const auto lowest  = *std::min_element (std::begin (kBassOffsets), std::end (kBassOffsets));
    const auto highest = *std::max_element (std::begin (kBassOffsets), std::end (kBassOffsets));

    return { kLowestNote - (kBassAnchorNote + lowest),
             kHighestNote - (kBassAnchorNote + highest) };
}

int melodyNoteFor (Layout layout, juce::juce_wchar key, int shift) noexcept
{
    // Both layouts are looked up through the caps, so there is exactly one table.
    for (const auto& cap : capsFor (layout))
        if (cap.key == key && cap.semitone >= 0)
            return baseNoteFor (layout, shift) + cap.semitone;

    return -1;
}

int bassNoteForKey (juce::juce_wchar key, int shift) noexcept
{
    for (const auto& cap : capsFor (Layout::bayan))
        if (cap.key == key && cap.bassIndex >= 0)
            return bassNoteFor (cap.bassIndex, shift);

    return -1;
}

int noteForCap (Layout layout, const Cap& cap, int melodyShift, int bassShift) noexcept
{
    if (layout == Layout::bayan && cap.bassIndex >= 0)
        return bassNoteFor (cap.bassIndex, bassShift);

    if (cap.semitone >= 0)
        return baseNoteFor (layout, melodyShift) + cap.semitone;

    return -1;
}

juce::String keyName (juce::juce_wchar key)
{
    if (key == kBackspace)
        return juce::String::fromUTF8 ("\u232B");   // ⌫

    if (key == 0x0060)
        return juce::String::fromUTF8 ("\u00B7");   // · (the key left of 1)

    return juce::String::charToString (key).toUpperCase();
}

juce::String layoutName (Layout layout)
{
    return layout == Layout::bayan ? NI_TXT ("巴扬") : NI_TXT ("常规");
}

juce::String keyLettersFor (Layout layout, int melodyShift, int bassShift, int midiNote)
{
    juce::String text;

    for (const auto& cap : capsFor (layout))
    {
        if (noteForCap (layout, cap, melodyShift, bassShift) != midiNote)
            continue;

        const auto name = keyName (cap.key);

        if (text.isNotEmpty())
            text += juce::String::fromUTF8 ("\u00B7");

        text += name;
    }

    return text;
}

void clamp (Settings& settings) noexcept
{
    const auto melody = melodyShiftRange (settings.layout);
    const auto bass = bassShiftRange();

    settings.melodyShift = juce::jlimit (melody.lowest, melody.highest, settings.melodyShift);
    settings.bassShift = juce::jlimit (bass.lowest, bass.highest, settings.bassShift);
    settings.velocity = juce::jlimit (1, 127, settings.velocity);
}

juce::var settingsToVar (const Settings& settings)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("alwaysOnTop", settings.alwaysOnTop);
    object->setProperty ("collapsed", settings.collapsed);
    object->setProperty ("layout", settings.layout == Layout::bayan ? 1 : 0);
    object->setProperty ("melodyShift", settings.melodyShift);
    object->setProperty ("bassShift", settings.bassShift);
    object->setProperty ("velocity", settings.velocity);
    return juce::var (object);
}

Settings settingsFromVar (const juce::var& value)
{
    Settings settings;
    const auto* object = value.getDynamicObject();

    if (object == nullptr)
        return settings;   // no file yet: the defaults are the defaults

    settings.alwaysOnTop = (bool) object->getProperty ("alwaysOnTop");
    settings.collapsed = (bool) object->getProperty ("collapsed");
    settings.layout = (int) object->getProperty ("layout") == 1 ? Layout::bayan : Layout::normal;

    const auto readInt = [object] (const char* name, int fallback)
    {
        const auto property = object->getProperty (name);
        return property.isVoid() ? fallback : (int) property;
    };

    settings.melodyShift = readInt ("melodyShift", 0);
    settings.bassShift = readInt ("bassShift", 0);
    settings.velocity = readInt ("velocity", 100);

    clamp (settings);
    return settings;
}

} // namespace hmdaw::keyboard
