#include "MidiArp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hmdaw::midiarp
{

namespace
{
    /** The note-value part of a rate, in quarter notes: 1/4 = 1, 1/8 = 0.5, and a
        triplet is two thirds of the plain value. */
    double quartersFor (Rate rate) noexcept
    {
        switch (rate)
        {
            case Rate::quarter:              return 1.0;
            case Rate::quarterTriplet:       return 2.0 / 3.0;
            case Rate::eighth:               return 0.5;
            case Rate::eighthTriplet:        return 1.0 / 3.0;
            case Rate::sixteenth:            return 0.25;
            case Rate::sixteenthTriplet:     return 1.0 / 6.0;
            case Rate::thirtySecond:         return 0.125;
            case Rate::thirtySecondTriplet:  return 1.0 / 12.0;
            case Rate::free:                 return 0.0;
        }

        return 0.25;
    }

    bool contains (const std::vector<int>& notes, int note) noexcept
    {
        return std::find (notes.begin(), notes.end(), note) != notes.end();
    }

    /** Sorts low to high and drops the duplicates (two cables on one key would
        otherwise play the same note twice). */
    std::vector<int> sortedUnique (const std::vector<int>& notes)
    {
        auto result = notes;
        std::sort (result.begin(), result.end());
        result.erase (std::unique (result.begin(), result.end()), result.end());
        return result;
    }
}

void clamp (Settings& settings) noexcept
{
    settings.octaves = juce::jlimit (Settings::kMinOctaves, Settings::kMaxOctaves,
                                     settings.octaves);
    settings.gatePercent = juce::jlimit (Settings::kMinGatePercent, Settings::kMaxGatePercent,
                                         settings.gatePercent);
    settings.swingPercent = juce::jlimit (Settings::kMinSwingPercent, Settings::kMaxSwingPercent,
                                          settings.swingPercent);
    settings.freeHz = juce::jlimit (Settings::kMinFreeHz, Settings::kMaxFreeHz, settings.freeHz);
    settings.fixedVelocity = juce::jlimit (1, 127, settings.fixedVelocity);
}

bool isSame (const Settings& a, const Settings& b) noexcept
{
    return a.enabled == b.enabled
               && a.rate == b.rate
               && std::abs (a.freeHz - b.freeHz) < 1.0e-9
               && a.direction == b.direction
               && a.octaves == b.octaves
               && a.gatePercent == b.gatePercent
               && a.swingPercent == b.swingPercent
               && a.velocityMode == b.velocityMode
               && a.fixedVelocity == b.fixedVelocity
               && a.hold == b.hold;
}

juce::String rateName (const Settings& settings)
{
    switch (settings.rate)
    {
        case Rate::quarter:             return juce::String ("1/4");
        case Rate::quarterTriplet:      return juce::String ("1/4T");
        case Rate::eighth:              return juce::String ("1/8");
        case Rate::eighthTriplet:       return juce::String ("1/8T");
        case Rate::sixteenth:           return juce::String ("1/16");
        case Rate::sixteenthTriplet:    return juce::String ("1/16T");
        case Rate::thirtySecond:        return juce::String ("1/32");
        case Rate::thirtySecondTriplet: return juce::String ("1/32T");
        case Rate::free:
            // ASCII, so a plain String is right here (this header stays free of the
            // project's NI_TXT, which lives in Types.h).
            return juce::String (settings.freeHz, 2) + juce::String (" Hz");
    }

    return juce::String ("1/16");
}

const std::vector<Rate>& allRates()
{
    static const std::vector<Rate> rates {
        Rate::quarter, Rate::quarterTriplet, Rate::eighth, Rate::eighthTriplet,
        Rate::sixteenth, Rate::sixteenthTriplet, Rate::thirtySecond,
        Rate::thirtySecondTriplet, Rate::free
    };

    return rates;
}

double stepSecondsFor (const Settings& settings, double bpm, int ticksPerQuarter) noexcept
{
    const auto safeBpm = bpm > 1.0 ? bpm : 120.0;
    const auto ticks = stepTicksFor (settings, safeBpm, ticksPerQuarter);

    // The free rate was already turned into ticks through the tempo, so this lands
    // back on exactly one period of the frequency.
    return (double) ticks * 60.0 / (safeBpm * (double) juce::jmax (1, ticksPerQuarter));
}

juce::int64 stepTicksFor (const Settings& settings, double bpm, int ticksPerQuarter) noexcept
{
    const auto ppq = juce::jmax (1, ticksPerQuarter);

    if (settings.rate != Rate::free)
        return juce::jmax ((juce::int64) 1,
                           (juce::int64) std::llround (quartersFor (settings.rate)
                                                       * (double) ppq));

    const auto safeBpm = bpm > 1.0 ? bpm : 120.0;
    const auto hz = juce::jlimit (Settings::kMinFreeHz, Settings::kMaxFreeHz, settings.freeHz);

    return juce::jmax ((juce::int64) 1,
                       (juce::int64) std::llround ((1.0 / hz) * safeBpm / 60.0
                                                   * (double) ppq));
}

juce::int64 gateTicksFor (const Settings& settings, juce::int64 stepTicks) noexcept
{
    const auto gate = juce::jlimit (Settings::kMinGatePercent, Settings::kMaxGatePercent,
                                    settings.gatePercent);

    return juce::jmax ((juce::int64) 1, stepTicks * (juce::int64) gate / 100);
}

juce::int64 offsetTicksForStep (const Settings& settings, juce::int64 stepTicks,
                                int stepIndex) noexcept
{
    if (stepIndex % 2 == 0)
        return 0;

    const auto swing = juce::jlimit (Settings::kMinSwingPercent, Settings::kMaxSwingPercent,
                                     settings.swingPercent);

    return stepTicks * (juce::int64) swing / 100;
}

std::vector<int> orderFor (const Settings& settings, const std::vector<int>& heldNotes,
                           juce::uint32 seed)
{
    if (heldNotes.empty())
        return {};

    const auto octaves = juce::jlimit (Settings::kMinOctaves, Settings::kMaxOctaves,
                                       settings.octaves);

    // The shape one octave plays: low to high, except 按弹下顺序 which keeps the
    // order the keys were pressed in.
    const auto base = settings.direction == Direction::asPlayed ? heldNotes
                                                                : sortedUnique (heldNotes);

    std::vector<int> expanded;
    expanded.reserve (base.size() * (size_t) octaves);

    for (int octave = 0; octave < octaves; ++octave)
        for (const auto note : base)
            expanded.push_back (note + 12 * octave);

    if (expanded.size() <= 1)
        return expanded;

    switch (settings.direction)
    {
        case Direction::up:
        case Direction::asPlayed:
            return expanded;

        case Direction::down:
            std::reverse (expanded.begin(), expanded.end());
            return expanded;

        case Direction::upDown:
        {
            // Down again without playing the turning points twice: 1 2 3 2.
            std::vector<int> result = expanded;

            for (auto i = expanded.size() - 1; i-- > 1;)
                result.push_back (expanded[i]);

            return result;
        }

        case Direction::downUp:
        {
            std::vector<int> descending = expanded;
            std::reverse (descending.begin(), descending.end());

            std::vector<int> result = descending;

            for (auto i = descending.size() - 1; i-- > 1;)
                result.push_back (descending[i]);

            return result;
        }

        case Direction::random:
        {
            // A deterministic shuffle, so the same chord and seed give the same
            // phrase - the self test can assert the sequence is a permutation.
            auto state = seed != 0u ? seed : 1u;

            for (auto i = expanded.size(); i > 1; --i)
            {
                state = state * 1664525u + 1013904223u;
                const auto j = (size_t) (state >> 16) % i;
                std::swap (expanded[i - 1], expanded[j]);
            }

            return expanded;
        }
    }

    return expanded;
}

int noteForStep (const std::vector<int>& order, int stepIndex) noexcept
{
    if (order.empty())
        return -1;

    const auto size = (int) order.size();
    auto index = stepIndex % size;

    if (index < 0)
        index += size;

    return order[(size_t) index];
}

int velocityForStep (const Settings& settings, int playedVelocity, int stepIndex,
                     int cycleLength) noexcept
{
    switch (settings.velocityMode)
    {
        case VelocityMode::fixed:
            return juce::jlimit (1, 127, settings.fixedVelocity);

        case VelocityMode::played:
            return juce::jlimit (1, 127, playedVelocity > 0 ? playedVelocity : 100);

        case VelocityMode::decrescendo:
        {
            // Each step keeps 85% of the one before it, over one pass of the
            // phrase; the next pass starts again from the played velocity.
            auto value = (double) juce::jlimit (1, 127, playedVelocity > 0 ? playedVelocity : 100);
            const auto within = cycleLength > 0 ? stepIndex % cycleLength : stepIndex;

            for (int i = 0; i < within; ++i)
                value *= 0.85;

            return juce::jlimit (Settings::kMinDecrescendoVelocity, 127, (int) std::lround (value));
        }
    }

    return 100;
}

void updateLatch (LatchState& state, const std::vector<int>& heldNotes, bool hold)
{
    if (! hold)
    {
        state.notes = heldNotes;
        state.startFreshOnNextPress = false;
        return;
    }

    if (heldNotes.empty())
    {
        // Everything let go: the set that was playing keeps playing, and the next
        // key press is the start of a new one.
        state.startFreshOnNextPress = true;
        return;
    }

    if (state.notes.empty() || state.startFreshOnNextPress)
    {
        state.notes = heldNotes;
        state.startFreshOnNextPress = false;
        return;
    }

    // Held down while latched: the keys add to what is already playing.
    for (const auto note : heldNotes)
        if (! contains (state.notes, note))
            state.notes.push_back (note);
}

juce::var settingsToVar (const Settings& settings)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("enabled", settings.enabled);
    object->setProperty ("rate", (int) settings.rate);
    object->setProperty ("freeHz", settings.freeHz);
    object->setProperty ("direction", (int) settings.direction);
    object->setProperty ("octaves", settings.octaves);
    object->setProperty ("gate", settings.gatePercent);
    object->setProperty ("swing", settings.swingPercent);
    object->setProperty ("velocityMode", (int) settings.velocityMode);
    object->setProperty ("fixedVelocity", settings.fixedVelocity);
    object->setProperty ("hold", settings.hold);
    return juce::var (object);
}

Settings settingsFromVar (const juce::var& value)
{
    Settings settings;

    const auto* object = value.getDynamicObject();

    if (object == nullptr)
        return settings;   // an older file: the arpeggiator is simply off

    const auto readInt = [object] (const char* name, int fallback)
    {
        const auto property = object->getProperty (name);
        return property.isVoid() ? fallback : (int) property;
    };

    const auto readDouble = [object] (const char* name, double fallback)
    {
        const auto property = object->getProperty (name);
        return property.isVoid() ? fallback : (double) property;
    };

    settings.enabled = (bool) object->getProperty ("enabled");
    settings.rate = (Rate) juce::jlimit (0, (int) Rate::free, readInt ("rate", 4));
    settings.freeHz = readDouble ("freeHz", 8.0);
    settings.direction = (Direction) juce::jlimit (0, (int) Direction::asPlayed,
                                                   readInt ("direction", 0));
    settings.octaves = readInt ("octaves", 1);
    settings.gatePercent = readInt ("gate", 50);
    settings.swingPercent = readInt ("swing", 0);
    settings.velocityMode = (VelocityMode) juce::jlimit (0, (int) VelocityMode::decrescendo,
                                                         readInt ("velocityMode", 1));
    settings.fixedVelocity = readInt ("fixedVelocity", 100);
    settings.hold = (bool) object->getProperty ("hold");

    clamp (settings);
    return settings;
}

} // namespace hmdaw::midiarp
