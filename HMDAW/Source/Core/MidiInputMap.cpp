#include "MidiInputMap.h"

#include <algorithm>
#include <cmath>

namespace hmdaw::midimap
{

namespace
{
    /** The step of the tile a note lands on, wrapped the way a keyboard repeats:
        everything below the anchor folds back up. */
    int wrappedOffset (const Map& map, int midiNote) noexcept
    {
        const auto keys = juce::jlimit (Map::kMinLoopKeys, Map::kMaxLoopKeys, map.loopKeys);
        auto offset = (midiNote - map.anchorNote) % keys;

        if (offset < 0)
            offset += keys;

        return offset;
    }

    /** How many whole tiles the note sits above (or below) the anchor.

        The keyboard is a repeating thing, so the tile repeats - but the *pitch*
        keeps climbing: everything in the tile above the anchor's one is the same
        pattern an octave higher (×2 per tile), and below it is an octave lower
        (÷2). floor() (not integer division) so the notes under the anchor fold
        downwards rather than towards zero.
    */
    int octavesFromAnchor (const Map& map, int midiNote) noexcept
    {
        const auto keys = juce::jlimit (Map::kMinLoopKeys, Map::kMaxLoopKeys, map.loopKeys);
        return (int) std::floor ((double) (midiNote - map.anchorNote) / (double) keys);
    }

    /** Drops the junk (0 and below), sorts low to high and folds duplicates: the one
        shape every read of a key's frequencies can rely on. */
    void normaliseFrequencies (std::vector<double>& values)
    {
        values.erase (std::remove_if (values.begin(), values.end(),
                                      [] (double hz) { return hz <= 0.0; }),
                      values.end());
        std::sort (values.begin(), values.end());
        values.erase (std::unique (values.begin(), values.end(),
                                   [] (double a, double b)
                                   { return std::abs (a - b) < 1.0e-6; }),
                      values.end());
    }

    bool holdsFrequency (const std::vector<double>& values, double hz)
    {
        return std::any_of (values.begin(), values.end(),
                            [hz] (double candidate)
                            { return std::abs (candidate - hz) < 1.0e-6; });
    }

    bool sameFrequencies (const std::vector<double>& a, const std::vector<double>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-9)
                return false;

        return true;
    }
}

std::vector<double>& Map::tileAt (int midiNote)
{
    // The tile is always `loopKeys` long: a map that arrived from an older file (or
    // from a hand-written one) grows to fit rather than silently swallowing the
    // binding.
    const auto wanted = juce::jlimit (kMinLoopKeys, kMaxLoopKeys, loopKeys);

    if ((int) tile.size() != wanted)
        tile.resize ((size_t) wanted);

    return tile[(size_t) wrappedOffset (*this, midiNote)];
}

std::vector<double> Map::frequenciesFor (int midiNote) const
{
    std::vector<double> result;

    if (midiNote < kLowestNote || midiNote > kHighestNote)
        return result;

    if (mode == Mode::free)
    {
        const auto found = free.find (midiNote);
        return found != free.end() ? found->second : result;
    }

    const auto offset = wrappedOffset (*this, midiNote);

    if (offset >= (int) tile.size())
        return result;

    // A step nobody bound is silent in every octave - the holes stay holes.
    const auto& step = tile[(size_t) offset];

    if (step.empty())
        return result;

    // ...and the whole tile rides up and down with the keyboard, so playing up
    // the keyboard is playing the same scale higher, not the same notes again.
    const auto octaves = std::pow (2.0, (double) octavesFromAnchor (*this, midiNote));
    result.reserve (step.size());

    for (const auto hz : step)
        result.push_back (hz * octaves);

    return result;
}

double Map::frequencyFor (int midiNote) const
{
    const auto all = frequenciesFor (midiNote);

    return all.empty() ? 0.0 : all.front();
}

int Map::bindingCount (int midiNote) const
{
    return (int) frequenciesFor (midiNote).size();
}

void Map::addFrequency (int midiNote, double hz)
{
    if (midiNote < kLowestNote || midiNote > kHighestNote || hz <= 0.0)
        return;

    auto& values = mode == Mode::free ? free[midiNote] : tileAt (midiNote);

    if (! holdsFrequency (values, hz))
        values.push_back (hz);

    normaliseFrequencies (values);
}

void Map::removeFrequency (int midiNote, double hz)
{
    if (midiNote < kLowestNote || midiNote > kHighestNote)
        return;

    auto drop = [hz] (std::vector<double>& values)
    {
        values.erase (std::remove_if (values.begin(), values.end(),
                                      [hz] (double candidate)
                                      { return std::abs (candidate - hz) < 1.0e-6; }),
                      values.end());
    };

    if (mode == Mode::free)
    {
        const auto found = free.find (midiNote);

        if (found == free.end())
            return;

        drop (found->second);

        if (found->second.empty())
            free.erase (found);

        return;
    }

    drop (tileAt (midiNote));
}

void Map::clearFrequency (int midiNote)
{
    if (midiNote < kLowestNote || midiNote > kHighestNote)
        return;

    if (mode == Mode::free)
    {
        free.erase (midiNote);
        return;
    }

    tileAt (midiNote).clear();
}

void Map::setFrequency (int midiNote, double hz)
{
    clearFrequency (midiNote);

    if (hz > 0.0)
        addFrequency (midiNote, hz);
}

int Map::tileOffsetOf (int midiNote) const
{
    return wrappedOffset (*this, midiNote);
}

std::vector<int> Map::visibleNotes() const
{
    std::vector<int> notes;

    if (mode == Mode::free)
    {
        for (int note = kLowestNote; note <= kHighestNote; ++note)
            notes.push_back (note);

        return notes;
    }

    const auto keys = juce::jlimit (kMinLoopKeys, kMaxLoopKeys, loopKeys);
    notes.reserve ((size_t) keys);

    for (int i = 0; i < keys; ++i)
        notes.push_back (anchorNote + i);

    return notes;
}

int Map::boundKeyCount() const
{
    auto count = 0;

    for (const auto note : visibleNotes())
        if (isBound (note))
            ++count;

    return count;
}

int Map::cableCount() const
{
    auto count = 0;

    for (const auto note : visibleNotes())
        count += bindingCount (note);

    return count;
}

void setLoopKeys (Map& map, int keys)
{
    const auto wanted = juce::jlimit (Map::kMinLoopKeys, Map::kMaxLoopKeys, keys);
    auto resized = map.tile;
    resized.resize ((size_t) wanted);
    map.tile = std::move (resized);
    map.loopKeys = wanted;
}

bool isSame (const Map& a, const Map& b)
{
    if (a.mode != b.mode || a.loopKeys != b.loopKeys || a.anchorNote != b.anchorNote)
        return false;

    if (a.mode == Map::Mode::free)
    {
        if (a.free.size() != b.free.size())
            return false;

        for (const auto& [note, hz] : a.free)
        {
            const auto found = b.free.find (note);

            if (found == b.free.end() || ! sameFrequencies (hz, found->second))
                return false;
        }

        return true;
    }

    if (a.tile.size() != b.tile.size())
        return false;

    for (size_t i = 0; i < a.tile.size(); ++i)
        if (! sameFrequencies (a.tile[i], b.tile[i]))
            return false;

    return true;
}

juce::String noteName (int midiNote)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };

    if (midiNote < 0)
        return juce::String::fromUTF8 ("—");

    const auto octave = midiNote / 12 - 1;      // C4 = MIDI 60, like the app's own keyboards
    return juce::String (names[midiNote % 12]) + juce::String (octave);
}

} // namespace hmdaw::midimap
