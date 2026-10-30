#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/MinimoogPresets.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"
#include "SynthControls.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace nidaw
{

/** A discrete rotary switch like the Minimoog's range and waveform selectors:
    the labels sit around the knob and the pointer clicks from one to the next. */
class MinimoogRotarySwitch : public juce::Component
{
public:
    MinimoogRotarySwitch() = default;

    void setLabels (juce::StringArray newLabels);
    void setSelected (int index, bool notify);
    int  getSelected() const noexcept { return selected; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void(int)> onChange;

private:
    void step (int delta);

    juce::StringArray labels;
    int selected = 0;
    bool dragging = false;
    int dragStartSelected = 0;
    juce::Point<int> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogRotarySwitch)
};

/** The two-position rocker switches of the panel. */
class MinimoogRocker : public juce::Component
{
public:
    MinimoogRocker() = default;

    void setLabels (juce::String top, juce::String bottom);
    void setState (bool on, bool notify);
    bool getState() const noexcept { return state; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void(bool)> onChange;

private:
    juce::String topLabel, bottomLabel;
    bool state = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogRocker)
};

/** A vertical performance wheel: the mod wheel stays where it is put, the pitch
    wheel springs back to the middle when the mouse is released. */
class MinimoogWheel : public juce::Component
{
public:
    MinimoogWheel() = default;

    void setSpringLoaded (bool shouldSpring) { springLoaded = shouldSpring; }
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; }
    void setValue (float newValue) { value = juce::jlimit (-1.0f, 1.0f, newValue); repaint(); }
    float getValue() const noexcept { return value; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void(float)> onChange;

private:
    void updateFromPoint (juce::Point<int> point);

    float value = 0.0f;
    bool springLoaded = false;
    bool bipolar = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogWheel)
};

//==============================================================================
/** The Minimoog Model D editor.

    The panel follows the original's five sections left to right - controllers,
    oscillator bank, mixer, modifiers and output - and draws them on a dark panel
    with wood cheeks. A small HM strip above it carries the settings the original
    does not have: polyphony and velocity sensitivity.
*/
class MinimoogEditor : public juce::Component,
                       private juce::ChangeListener,
                       private juce::MidiKeyboardState::Listener
{
public:
    MinimoogEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~MinimoogEditor() override;

    static constexpr int kDesignWidth  = 1120;
    static constexpr int kDesignHeight = 680;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    std::pair<int, int> getPresetCountsForTest() const;
    bool selectPresetForTest (int id);
    void setParamForTest (int which, double value);
    void setPolyphonyForTest (int polyphony);
    int  getPolyphonyForTest() const;
    int  getActiveVoiceCountForTest() const;
    int  getSelectedPresetIdForTest() const { return presetBar.getSelectedIdForTest(); }
    bool loadUserPresetForTest (int index);
    bool savePresetForTest (const juce::String& name, bool overwrite);
    bool renamePresetForTest (const juce::String& newName);
    bool deletePresetForTest();

    PresetBar& getPresetBar() noexcept { return presetBar; }

private:
    struct PrintedLabel
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        bool title = false;
        juce::Justification justification = juce::Justification::centred;
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void handleNoteOn (juce::MidiKeyboardState* state, int midiChannel,
                       int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState* state, int midiChannel,
                        int midiNoteNumber, float velocity) override;

    InstrumentTrack* currentTrack();
    const InstrumentTrack* currentTrack() const;

    void updateControlsFromTrack();
    void pushCurrentParams();
    void pushParams (const MinimoogParams& params);
    void beginParamEdit (const juce::String& name);
    void setPolyphony (int polyphony);

    void configureKnob (juce::Slider& slider, double minimum, double maximum,
                        double interval, double defaultValue, const juce::String& suffix = {});

    std::vector<PresetBar::Entry> buildPresetEntries() const;
    bool loadPresetById (int id);
    bool savePresetByName (const juce::String& name, bool overwrite);
    bool renamePresetById (int id, const juce::String& newName);
    bool deletePresetById (int id);
    bool userPresetExists (const juce::String& name) const;

    // Panel-unit conversions.
    static float cutoffToKnob (float hz) noexcept;
    static float knobToCutoff (float knob) noexcept;
    static float secondsToKnob (float seconds) noexcept;
    static float knobToSeconds (float knob) noexcept;

    ProjectDocument& document;
    AudioEngine& audio;
    PresetStore& presets;
    juce::int64 trackId = 0;

    PresetBar presetBar { NI_TXT ("内置") };
    juce::ComboBox polyphonyBox;
    juce::Slider velocityKnob;

    MinimoogWheel pitchWheel, modWheel;

    MinimoogRotarySwitch rangeSwitch[3], waveSwitch[3];
    std::array<juce::Label, 3> waveName;
    MinimoogRocker oscOn[3], osc3Control;

    juce::Slider tune[3];
    juce::Slider oscLevel[3];
    juce::Slider masterTune, glideKnob, modMix;
    MinimoogRocker glideSwitch, oscMod, filterMod;

    juce::Slider noiseLevelSlider;
    MinimoogRocker noiseType;

    juce::Slider cutoff, emphasis, contour;
    MinimoogRocker keyTrack1, keyTrack2;
    juce::Slider filterAttack, filterDecay;
    MinimoogRocker filterDecayShort, filterSustain;
    juce::Slider ampAttack, ampDecay;
    MinimoogRocker ampDecayShort, ampSustain, releaseSwitch;

    juce::Slider volume;
    MinimoogRocker mainOut;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    std::vector<PrintedLabel> printedLabels;
    juce::Rectangle<int> panelArea, controllersArea, oscillatorArea, mixerArea;
    juce::Rectangle<int> modifiersArea, outputArea;

    int previewMidiNote = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogEditor)
};

} // namespace nidaw
