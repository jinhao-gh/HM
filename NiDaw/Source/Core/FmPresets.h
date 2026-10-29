#pragma once

#include "FmPatchHelpers.h"
#include "Dx7FactoryPresets.h"

#include <vector>

namespace nidaw::fm
{

/** A named factory sound. `category` is the second-level menu the editor shows,
    `dx7Algorithm` the original Yamaha routing (1..32) the builder uses. */
struct Preset
{
    const char* name;
    const char* category;
    int dx7Algorithm;
    void (*build) (FmPatch&);
};

/** The factory bank, in the order the editor lists it.

    The 32 sounds are the Yamaha DX7 factory ROM-1A voices, converted to this
    engine's six-operator model by `Core/Dx7FactoryPresets.h`. Since the engine
    learned the DX7 parameter set (algorithms 33..64, fixed frequency, keyboard
    scaling, AM sensitivity, pitch EG) the conversion uses each voice's real
    routing instead of an approximation; see the note in that header.
*/
inline const Preset* bank (int& countOut)
{
    static const std::vector<Preset> presets = []
    {
        int count = 0;
        const auto* voices = dx7::voices (count);

        std::vector<Preset> list;
        list.reserve ((size_t) count);

        for (int i = 0; i < count; ++i)
            list.push_back ({ voices[i].name, voices[i].category, voices[i].dx7Algorithm,
                              voices[i].build });

        return list;
    }();

    countOut = (int) presets.size();
    return presets.data();
}

inline int presetCount()
{
    int count = 0;
    bank (count);
    return count;
}

inline juce::String category (int index)
{
    int count = 0;
    const auto* presets = bank (count);

    return count == 0 ? juce::String()
                      : juce::String::fromUTF8 (presets[juce::jlimit (0, count - 1, index)].category);
}

/** Applies one factory preset to a patch. Index is clamped, so -1 and 999 are safe. */
inline void applyPreset (FmPatch& patch, int index)
{
    int count = 0;
    const auto* presets = bank (count);

    if (count <= 0)
        return;

    // Start from a clean, quiet patch so nothing from the previous sound leaks in.
    patch = FmPatch();

    const auto i = juce::jlimit (0, count - 1, index);
    presets[i].build (patch);
    patch.name = juce::String::fromUTF8 (presets[i].name);
}

/** The index of the factory sound with that name, or -1. */
inline int findPresetByName (const juce::String& name)
{
    int count = 0;
    const auto* presets = bank (count);

    for (int i = 0; i < count; ++i)
        if (name == juce::String::fromUTF8 (presets[i].name))
            return i;

    return -1;
}

} // namespace nidaw::fm
