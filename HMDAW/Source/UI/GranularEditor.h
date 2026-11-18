#pragma once

#include "../Audio/AudioEngine.h"
#include "../Audio/GranularSynth.h"
#include "../Audio/SamplePlayer.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "PresetBar.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <utility>
#include <vector>

namespace hmdaw
{

/** The particle editor's canvas: the file's waveform with the grain cloud
    scattered over it.

    Read-only on purpose: the picture follows the sound (the same numbers the engine
    publishes), it never sets a parameter. Grains are drawn as dots at
    (取样位置, 颗粒年龄), so a cloud that is scanning shows as a diagonal file of dots
    and a still one as a column.
*/
class GrainCloudView : public juce::Component
{
public:
    GrainCloudView() = default;

    void setSample (std::shared_ptr<const LoadedSample> newSample);
    /** Every sounding grain: its position through the file (0..1) and how far it is
        through its own life (0..1). */
    void setGrains (std::vector<std::pair<float, float>> newGrains);
    /** Where the scan line sits, or a negative number for "nothing sounding". */
    void setScanFraction (float fraction);

    void paint (juce::Graphics& g) override;

    int getGrainDotCountForTest() const noexcept { return (int) grains.size(); }

private:
    std::shared_ptr<const LoadedSample> sample;
    std::vector<std::pair<float, float>> grains;
    float scanFraction = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainCloudView)
};

//==============================================================================
/** The particle instrument editor: import a file, then shape the cloud that is cut
    out of it.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class GranularEditor : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer,
                       private juce::MidiKeyboardState::Listener
{
public:
    GranularEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~GranularEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Loads a file into the track's granular instrument. Empty string on success. */
    juce::String loadFile (const juce::File& file);

    /** The page is taller than the others: the cloud view needs the room. */
    static constexpr int kDesignWidth  = 780;
    static constexpr int kDesignHeightWithCloud = 640;

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    /** All the row captions, joined with " · ", so a test can prove that every
        parameter is described in words. */
    juce::String getCaptionsForTest() const;
    /** 0 position · 1 scanSpeed · 2 grainMs · 3 densityHz · 4 spray · 5 rootHz. */
    void setParamForTest (int which, double value);
    int  getSelectedPresetIndex() const;
    bool savePresetForTest (const juce::String& name, bool overwrite);
    int  getCloudDotCountForTest() const { return cloud.getGrainDotCountForTest(); }
    juce::String getFileLabelTextForTest() const { return fileLabel.getText(); }
    /** One pulse of the 30 Hz cloud refresh, so a test does not have to wait for it. */
    void tickCloudForTest() { updateCloud(); }

    PresetBar& getPresetBar() noexcept { return presetBar; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void handleNoteOn (juce::MidiKeyboardState* state, int midiChannel,
                       int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState* state, int midiChannel,
                        int midiNoteNumber, float velocity) override;

    InstrumentTrack* currentTrack();
    const InstrumentTrack* currentTrack() const;

    void updateControlsFromTrack();
    void pushCurrentParams();
    void pushParams (const GranularParams& params);
    void beginParamEdit (const juce::String& name);
    void chooseFile();
    /** Reads the file if it changed. Returns the error when it cannot be read
        (empty when there is nothing wrong), so the caller can put it on the file
        line *after* the "which file" text - otherwise the message was overwritten. */
    juce::String reloadSample();
    /** Pulls the sounding grains out of the engine and hands them to the view. */
    void updateCloud();

    // ---- presets ----------------------------------------------------------
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

    std::shared_ptr<const LoadedSample> sample;

    juce::Label titleLabel;
    PresetBar presetBar { NI_TXT ("内置") };
    juce::TextButton loadButton;
    juce::Label fileLabel;
    GrainCloudView cloud;

    juce::Slider positionSlider, scanSlider, rootSlider;
    juce::Slider grainSlider, densitySlider, spraySlider, spacingSlider;
    juce::Slider pitchJitterSlider, reverseSlider;
    juce::Slider posAttackSlider, posDecaySlider, posSustainSlider, posReleaseSlider;
    juce::Slider posAmountSlider;
    juce::Slider ampAttackSlider, ampDecaySlider, ampSustainSlider, ampReleaseSlider;
    juce::ComboBox filterModeBox, filterSlopeBox;
    juce::Slider cutoffSlider, resonanceSlider, keyTrackSlider;
    juce::Slider spreadSlider, gainSlider;

    /** Row captions are painted rather than being Label components: there is one
        per parameter and none is ever edited. */
    struct RowLabel
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        bool header = false;
    };

    std::vector<RowLabel> rowLabels;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    int previewMidiNote = -1;
    bool updating = false;
    /** The last cloud drawn, so an idle page never repaints. */
    std::vector<std::pair<float, float>> lastGrains;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularEditor)
};

} // namespace hmdaw
