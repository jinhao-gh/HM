#pragma once

#include "../Audio/AudioEngine.h"
#include "../Audio/SamplePlayer.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "PresetBar.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** Shows the loaded sample's waveform: a simple peak envelope, with the loop
    points marked. */
class WaveformView : public juce::Component
{
public:
    WaveformView() = default;

    void setSample (std::shared_ptr<const LoadedSample> newSample);
    void setLoop (bool looped, double start, double end);

    void paint (juce::Graphics& g) override;

private:
    std::shared_ptr<const LoadedSample> sample;
    bool   looped = false;
    double loopStart = 0.0;
    double loopEnd = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

//==============================================================================
/** The sampled instrument editor: load a file, say what pitch it is, then shape
    it with a loop, an ADSR, a filter and a little modulation.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class SamplerEditor : public juce::Component,
                      private juce::ChangeListener,
                      private juce::MidiKeyboardState::Listener
{
public:
    SamplerEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~SamplerEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Loads a file into the track's sampler. Returns an empty string on success. */
    juce::String loadFile (const juce::File& file);

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    /** All the row captions, joined with " · ", so a test can prove that every
        parameter is described in words. */
    juce::String getCaptionsForTest() const;
    void setRootHzForTest (double hz);
    void setLoopForTest (bool shouldLoop, double start, double end);
    int  getSelectedPresetIndex() const;
    bool savePresetForTest (const juce::String& name, bool overwrite);
    juce::String getFileLabelTextForTest() const { return fileLabel.getText(); }

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
    void pushParams (const SamplerParams& params);
    void beginParamEdit (const juce::String& name);
    void chooseFile();
    /** Reads the file if it changed. Returns the error when it cannot be read
        (empty when there is nothing wrong), so the caller can put it on the file
        line *after* the "which file" text - otherwise the message was overwritten. */
    juce::String reloadSample();

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
    WaveformView waveform;

    juce::Slider rootSlider, loopStartSlider, loopEndSlider;
    juce::TextButton loopButton;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Slider cutoffSlider, resonanceSlider, gainSlider;
    juce::Slider lfoRateSlider, lfoPitchSlider, lfoAmpSlider;

    /** Row captions are painted rather than being Label components: there is one
        per parameter and none is ever edited. The subtractive editor does the
        same, so the two instrument windows look alike. */
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};

} // namespace hmdaw
