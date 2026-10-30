#pragma once

#include "Types.h"

namespace nidaw
{

// JSON conversion shared by the project file and the preset library.
juce::var referenceGroupToVar (const ReferenceGroup& group);
ReferenceGroup referenceGroupFromVar (const juce::var& value);
juce::var referenceLineGroupToVar (const ReferenceLineGroup& group);
ReferenceLineGroup referenceLineGroupFromVar (const juce::var& value);

// The MIDI input mapping: the project file and the preset library keep it in the
// same shape, so a mapping can be dragged between the two without a second format.
juce::var midiInputStateToVar (const MidiInputState& state);
MidiInputState midiInputStateFromVar (const juce::var& value);

// A note and a clip in the one shape they are written in everywhere: the project
// file and the chain library both keep clip-shaped content (a generator unit's own
// phrase is a clip), so the conversion lives here rather than in either of them.
juce::var noteToVar (const Note& note);
Note noteFromVar (const juce::var& value);
juce::var clipToVar (const Clip& clip);
Clip clipFromVar (const juce::var& value);

// The instrument formats are shared too: a project writes the current sound and
// the preset library writes saved ones, and both have to agree byte for byte.
juce::var synthParamsToVar (const SynthParams& params);
SynthParams synthParamsFromVar (const juce::var& value);
juce::var samplerParamsToVar (const SamplerParams& params);
SamplerParams samplerParamsFromVar (const juce::var& value);
juce::var subtractiveParamsToVar (const SubtractiveParams& params);
SubtractiveParams subtractiveParamsFromVar (const juce::var& value);
juce::var minimoogParamsToVar (const MinimoogParams& params);
MinimoogParams minimoogParamsFromVar (const juce::var& value);
juce::var wavetableParamsToVar (const WavetableParams& params);
WavetableParams wavetableParamsFromVar (const juce::var& value);
juce::var customWavetableToVar (const CustomWavetable& table);
CustomWavetable customWavetableFromVar (const juce::var& value);
juce::var drumKitToVar (const DrumKitParams& kit);
DrumKitParams drumKitFromVar (const juce::var& value);
juce::var granularParamsToVar (const GranularParams& params);
GranularParams granularParamsFromVar (const juce::var& value);

/** One saved wavetable sound: the parameters, plus copies of every custom table
    they use.

    Carrying the tables is what makes a preset self-contained - the person who
    receives the file hears the sound the author heard. The tables are written as
    16-bit PCM (the mipmaps are derived data and are rebuilt on load).
*/
struct WavetablePatch
{
    WavetableParams params;
    std::vector<CustomWavetable> tables;
};

//==============================================================================
/** The user level preset library.

    Kept outside any project (in the application data directory) so that presets
    can be reused across projects. The project file only stores the reference
    group currently laid out on the segment, plus the enabled reference line groups.
*/
class PresetStore
{
public:
    static juce::File getDefaultFile();

    /** Points the default library somewhere else. The offscreen self test uses
        this so a run never touches the real per-user library. */
    static void setDefaultFileForTesting (juce::File file);

    explicit PresetStore (juce::File fileToUse = getDefaultFile());

    /** Missing file is not an error (an empty library is fine). A corrupt file
        leaves the library empty and reports through getLastError(). */
    bool load();
    bool save() const;

    const std::vector<ReferenceGroup>& getReferenceGroups() const noexcept { return referenceGroups; }
    const std::vector<ReferenceLineGroup>& getReferenceLineGroups() const noexcept { return referenceLineGroups; }
    const std::vector<MidiInputState>& getMidiMaps() const noexcept { return midiMaps; }

    void addReferenceGroup (ReferenceGroup group);
    void addReferenceLineGroup (ReferenceLineGroup group);
    /** Overwrites one saved group in place - what the group editor's 保存 does.
        An index that is out of range is ignored (the library may have changed under
        the editor while it was open). */
    void replaceReferenceGroup (int index, ReferenceGroup group);
    void replaceReferenceLineGroup (int index, ReferenceLineGroup group);

    void renameReferenceGroup (int index, const juce::String& newName);
    void renameReferenceLineGroup (int index, const juce::String& newName);

    void removeReferenceGroup (int index);
    void removeReferenceLineGroup (int index);

    /** MIDI input mapping presets: name + the mapping + the group it was built on. */
    void addMidiMap (MidiInputState map);
    void replaceMidiMap (int index, MidiInputState map);
    void renameMidiMap (int index, const juce::String& newName);
    void removeMidiMap (int index);

    juce::File getFile() const { return file; }
    juce::String getLastError() const { return lastError; }

    //==========================================================================
    // Instrument presets. Kept in the same file as the reference presets, under
    // their own sections, so one library holds everything the user saved.

    const std::vector<SynthParams>& getFmPresets() const noexcept { return fmPresets; }
    const std::vector<SamplerParams>& getSamplerPresets() const noexcept { return samplerPresets; }
    const std::vector<SubtractiveParams>& getSubtractivePresets() const noexcept
    {
        return subtractivePresets;
    }
    const std::vector<MinimoogParams>& getMinimoogPresets() const noexcept
    {
        return minimoogPresets;
    }
    const std::vector<WavetablePatch>& getWavetablePatches() const noexcept
    {
        return wavetablePresets;
    }
    const std::vector<DrumKitParams>& getDrumPresets() const noexcept { return drumPresets; }
    const std::vector<GranularParams>& getGranularPresets() const noexcept
    {
        return granularPresets;
    }
    const std::vector<HybridParams>& getHybridPresets() const noexcept
    {
        return hybridPresets;
    }

    int findFmPresetByName (const juce::String& name) const;
    int findSamplerPresetByName (const juce::String& name) const;
    int findSubtractivePresetByName (const juce::String& name) const;
    int findMinimoogPresetByName (const juce::String& name) const;
    int findWavetablePresetByName (const juce::String& name) const;
    int findDrumPresetByName (const juce::String& name) const;
    int findGranularPresetByName (const juce::String& name) const;
    int findHybridPresetByName (const juce::String& name) const;

    void addFmPreset (SynthParams preset);
    void replaceFmPreset (int index, SynthParams preset);
    void renameFmPreset (int index, const juce::String& newName);
    void removeFmPreset (int index);

    void addSamplerPreset (SamplerParams preset);
    void replaceSamplerPreset (int index, SamplerParams preset);
    void renameSamplerPreset (int index, const juce::String& newName);
    void removeSamplerPreset (int index);

    void addSubtractivePreset (SubtractiveParams preset);
    void replaceSubtractivePreset (int index, SubtractiveParams preset);
    void renameSubtractivePreset (int index, const juce::String& newName);
    void removeSubtractivePreset (int index);

    void addMinimoogPreset (MinimoogParams preset);
    void replaceMinimoogPreset (int index, MinimoogParams preset);
    void renameMinimoogPreset (int index, const juce::String& newName);
    void removeMinimoogPreset (int index);

    void addWavetablePreset (WavetablePatch preset);
    void replaceWavetablePreset (int index, WavetablePatch preset);
    void renameWavetablePreset (int index, const juce::String& newName);
    void removeWavetablePreset (int index);

    void addDrumPreset (DrumKitParams preset);
    void replaceDrumPreset (int index, DrumKitParams preset);
    void renameDrumPreset (int index, const juce::String& newName);
    void removeDrumPreset (int index);

    void addGranularPreset (GranularParams preset);
    void replaceGranularPreset (int index, GranularParams preset);
    void renameGranularPreset (int index, const juce::String& newName);
    void removeGranularPreset (int index);

    void addHybridPreset (HybridParams preset);
    void replaceHybridPreset (int index, HybridParams preset);
    void renameHybridPreset (int index, const juce::String& newName);
    void removeHybridPreset (int index);

private:
    juce::File file;
    mutable juce::String lastError;
    std::vector<ReferenceGroup> referenceGroups;
    std::vector<ReferenceLineGroup> referenceLineGroups;
    std::vector<MidiInputState> midiMaps;
    std::vector<SynthParams> fmPresets;
    std::vector<SamplerParams> samplerPresets;
    std::vector<SubtractiveParams> subtractivePresets;
    std::vector<MinimoogParams> minimoogPresets;
    std::vector<WavetablePatch> wavetablePresets;
    std::vector<DrumKitParams> drumPresets;
    std::vector<GranularParams> granularPresets;
    std::vector<HybridParams> hybridPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetStore)
};

} // namespace nidaw
