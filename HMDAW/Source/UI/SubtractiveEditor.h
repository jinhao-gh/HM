#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"
#include "SynthControls.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The two pictures at the top of the subtractive editor: one cycle of the
    oscillator mix, and the filter's magnitude response.

    The waveform uses the same oscillator function the engine renders with, so
    the drawing cannot disagree with the sound.
*/
class SubtractiveDisplay : public juce::Component
{
public:
    SubtractiveDisplay() = default;

    void setParams (const SubtractiveParams& newParams);

    void paint (juce::Graphics& g) override;

    /** Diagnostic: where the waveform / the filter curve was drawn last. */
    juce::Rectangle<int> getWaveBoundsForTest() const { return waveBounds; }
    juce::Rectangle<int> getFilterBoundsForTest() const { return filterBounds; }

private:
    SubtractiveParams params;
    juce::Rectangle<int> waveBounds, filterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubtractiveDisplay)
};

//==============================================================================
/** The subtractive instrument editor: two oscillators plus noise into a
    multimode filter with its own envelope, then the amplifier envelope, with one
    LFO reaching pitch, filter and amplitude.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class SubtractiveEditor : public juce::Component,
                          private juce::ChangeListener,
                          private juce::MidiKeyboardState::Listener
{
public:
    SubtractiveEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~SubtractiveEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    /** Text of one combo entry: which 0 = 振荡器波形, 1 = 滤波模式, 2 = 斜率, 3 = LFO 波形. */
    juce::String getChoiceTextForTest (int which, int index) const;
    std::pair<int, int> getPresetCountsForTest() const;
    bool selectPresetForTest (int id);
    void setParamForTest (int which, double value);
    int  getSelectedPresetIdForTest() const { return presetBar.getSelectedIdForTest(); }
    bool loadUserPresetForTest (int index);
    bool savePresetForTest (const juce::String& name, bool overwrite);
    bool renamePresetForTest (const juce::String& newName);
    bool deletePresetForTest();

    PresetBar& getPresetBar() noexcept { return presetBar; }
    SubtractiveDisplay& getDisplayForTest() noexcept { return display; }

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
    void pushParams (const SubtractiveParams& params);
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

    juce::Label titleLabel;
    PresetBar presetBar { NI_TXT ("内置") };
    SubtractiveDisplay display;
    FilterResponseView filterView;
    EnvelopeEditorView ampEnvelopeView;

    juce::Label osc1Header, osc2Header, noiseHeader;
    juce::Label osc1WaveLabel, osc2WaveLabel;
    juce::ComboBox osc1Wave, osc2Wave;
    juce::TextButton osc2Enable;
    juce::Slider osc1Octave, osc1Semitone, osc1Detune, osc1Level, osc1PulseWidth;
    juce::Slider osc2Octave, osc2Semitone, osc2Detune, osc2Level, osc2PulseWidth;
    juce::Slider noiseLevel;

    juce::Label filterHeader, filterEnvHeader;
    juce::Label filterModeLabel, filterSlopeLabel;
    juce::ComboBox filterMode, filterSlope;
    juce::Slider cutoff, resonance, filterEnvAmount, filterKeyTrack;
    juce::Slider filterAttack, filterDecay, filterSustain, filterRelease;

    juce::Label ampHeader, lfoHeader, outputHeader, lfoWaveLabel;
    juce::Slider ampAttack, ampDecay, ampSustain, ampRelease;
    juce::ComboBox lfoWave;
    juce::Slider lfoRate, lfoPitch, lfoCutoff, lfoAmp;
    juce::Slider gain;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    /** Row captions are painted rather than being Label components: there are
        about two dozen of them and none is ever edited. */
    struct RowLabel
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        bool header = false;
    };

    std::vector<RowLabel> rowLabels;

    int previewMidiNote = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubtractiveEditor)
};

} // namespace hmdaw
