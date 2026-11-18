#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The drum kit editor: twelve pads down the left, the selected pad's six
    controls on the right, the kit wide controls along the bottom, and a preview
    keyboard whose keys trigger the pads.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class DrumEditor : public juce::Component,
                   private juce::ChangeListener,
                   private juce::MidiKeyboardState::Listener
{
public:
    DrumEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~DrumEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    int  getPadCountForTest() const { return kDrumPads; }
    int  getSelectedPadForTest() const { return selectedPad; }
    void selectPadForTest (int pad);
    void setPadParamForTest (int pad, int which, double value);
    int  getPresetCountForTest() const { return (int) presets.getDrumPresets().size(); }
    bool loadUserPresetForTest (int index);
    bool savePresetForTest (const juce::String& name, bool overwrite);
    bool renamePresetForTest (const juce::String& newName);
    bool deletePresetForTest();
    void triggerPadForTest (int pad);
    bool restoreDefaultsForTest();

    PresetBar& getPresetBar() noexcept { return presetBar; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void handleNoteOn (juce::MidiKeyboardState* state, int midiChannel,
                       int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState* state, int midiChannel,
                        int midiNoteNumber, float velocity) override;

    InstrumentTrack* currentTrack();
    const InstrumentTrack* currentTrack() const;

    void updateControlsFromTrack();
    void pushCurrentParams();
    void pushParams (const DrumKitParams& params);
    void beginParamEdit (const juce::String& name);

    std::vector<PresetBar::Entry> buildPresetEntries() const;
    bool loadPresetById (int id);
    bool savePresetByName (const juce::String& name, bool overwrite);
    bool renamePresetById (int id, const juce::String& newName);
    bool deletePresetById (int id);
    bool userPresetExists (const juce::String& name) const;

    ProjectDocument& document;
    AudioEngine& audio;
    PresetStore& presets;
    juce::int64 trackId = 0;
    int selectedPad = 0;

    juce::Label titleLabel;
    PresetBar presetBar { NI_TXT ("内置") };
    juce::TextButton restoreButton;

    juce::OwnedArray<juce::TextButton> padButtons;
    juce::Label padNameLabel;
    juce::TextButton padToggle;
    juce::TextButton previewButton;

    juce::Label padParamsHeader, globalHeader;
    juce::Slider padLevel, padPitch, padDecay, padTone, padSnap;
    juce::Slider masterGain, masterPitch, masterDecay, masterWidth;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    /** Row captions are painted, exactly like the other instrument editors. */
    struct RowLabel
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        bool header = false;
    };

    std::vector<RowLabel> rowLabels;

    int previewMidiNote = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEditor)
};

} // namespace hmdaw
