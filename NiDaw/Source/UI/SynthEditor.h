#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/FmPresets.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AlgorithmDiagram.h"
#include "AppState.h"
#include "EnvelopeCurve.h"
#include "PresetBar.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace nidaw
{

/** The FM instrument editor.

    Three levels, so that a six-operator stack stays readable:

      - the algorithm library: all 32 routings as little diagrams, one click picks
        one and goes down a level;
      - the operator overview: the chosen algorithm drawn large, every operator a
        box with its ratio, level and on/off switch; clicking a box goes deeper;
      - the operator detail: the RATE/LEVEL envelope drawn as a curve above the
        sliders that edit it.

    LFO and output are two side screens off the top bar. The audition keyboard and
    the preset row are visible the whole time.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class SynthEditor : public juce::Component,
                    private juce::ChangeListener,
                    private juce::MidiKeyboardState::Listener
{
public:
    enum class View
    {
        library = 0,       // the algorithm pictures (two pages: HM 1-32, DX7 33-64)
        overview,          // the chosen algorithm, large
        operatorDetail,    // one operator's envelope and sliders
        lfo,
        output,
        extras             // fixed frequency, keyboard scaling, AMS, pitch EG
    };

    SynthEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~SynthEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Diagnostic hooks for the offscreen self test. */
    void setViewForTest (int view);
    int  getViewForTest() const { return (int) currentView; }
    void selectAlgorithmForTest (int algorithm);
    void setOperatorForTest (int index);
    int  getOperatorForTest() const noexcept { return selectedOperator; }
    void setRateForTest (int index, int value);
    void toggleOperatorForTest (int index);
    void setLibraryPageForTest (int page);
    int  getLibraryPageForTest() const noexcept { return libraryPage; }
    void setOscModeForTest (bool fixed);
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    juce::Rectangle<int> getAlgorithmCellBoundsForTest (int index) const;
    /** The operator box **inside** a library thumbnail, in that cell's own
        coordinates. Empty means the picture drew nothing. */
    juce::Rectangle<int> getAlgorithmCellBoxForTest (int cellIndex, int operatorIndex) const;
    juce::Rectangle<int> getOperatorBoxBoundsForTest (int index) const;

    void selectPresetForTest (int index);
    int  getSelectedPresetIndex() const;
    int  getSelectedPresetIdForTest() const { return presetBar.getSelectedIdForTest(); }
    bool loadUserPresetForTest (int index);
    bool savePresetForTest (const juce::String& name, bool overwrite);
    bool renamePresetForTest (const juce::String& newName);
    bool deletePresetForTest();

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
    void pushCurrentParamsToPreview();
    void pushParams (const SynthParams& params);
    void beginParamEdit (const juce::String& name);

    void setView (View view);
    void updateBreadcrumb();
    void selectAlgorithm (int algorithm);     // library click
    void openOperator (int index);            // overview click
    void setOperatorEnabled (int index, bool enabled);
    int  algorithmForCell (int cellIndex) const noexcept;
    void updateLibraryPage();

    // ---- presets ----------------------------------------------------------
    std::vector<PresetBar::Entry> buildPresetEntries() const;
    juce::String currentPatchName() const;
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

    juce::TextButton backButton, lfoButton, outputButton, extrasButton, libraryPageButton;
    juce::Label breadcrumbLabel;
    View currentView = View::library;

    // ---- algorithm library ----
    /** 0 shows algorithms 1..32, 1 shows 33..64 (the DX7 bank). */
    int libraryPage = 0;
    std::vector<std::unique_ptr<AlgorithmDiagram>> algorithmCells;

    // ---- operator overview ----
    AlgorithmDiagram diagram;

    // ---- operator detail ----
    int selectedOperator = 0;
    EnvelopeCurve envelopeCurve;
    juce::TextButton operatorPowerButton;

    juce::Label ratioLabel, detuneLabel, levelLabel, velSensLabel, rateScaleLabel;
    juce::Slider ratioSlider, detuneSlider, levelSlider, velSensSlider, rateScaleSlider;

    juce::Label envelopeLabel;
    std::array<juce::Label, 8> envelopeValueLabels;   // R1..R4 then L1..L4
    std::array<juce::Slider, 4> rateSliders;
    std::array<juce::Slider, 4> envLevelSliders;

    // ---- LFO screen ----
    juce::Label lfoRateLabel, lfoDelayLabel, lfoWaveLabel, lfoPitchLabel, lfoAmpLabel;
    juce::Label lfoPitchModSensLabel;
    juce::Slider lfoRateSlider, lfoDelaySlider, lfoPitchSlider, lfoAmpSlider;
    juce::Slider lfoPitchModSensSlider;
    juce::TextButton lfoSyncButton;
    std::array<juce::TextButton, 6> lfoWaveButtons;

    // ---- extras screen: fixed frequency, keyboard scaling, AMS, pitch EG ----
    juce::Label modeLabel, fixedHzLabel, amSensLabel, breakPointLabel;
    juce::Label leftCurveLabel, rightCurveLabel, leftDepthLabel, rightDepthLabel;
    juce::TextButton ratioModeButton, fixedModeButton;
    juce::Slider fixedHzSlider, amSensSlider, breakPointSlider;
    juce::Slider leftDepthSlider, rightDepthSlider;
    std::array<juce::TextButton, 4> leftCurveButtons;
    std::array<juce::TextButton, 4> rightCurveButtons;

    juce::Label pitchEgLabel;
    std::array<juce::Label, 8> pitchEgValueLabels;    // R1..R4 then L1..L4
    std::array<juce::Slider, 4> pitchRateSliders;
    std::array<juce::Slider, 4> pitchLevelSliders;

    // ---- output screen ----
    juce::Label gainLabel, cutoffLabel, resonanceLabel, transposeLabel, feedbackLabel;
    juce::Slider gainSlider, cutoffSlider, resonanceSlider, transposeSlider, feedbackSlider;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    int previewMidiNote = -1;
    bool updating = false;
    bool showDialogs = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEditor)
};

} // namespace nidaw
