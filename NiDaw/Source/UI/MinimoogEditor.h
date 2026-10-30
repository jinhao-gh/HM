#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/MinimoogPresets.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"
#include "SynthControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace nidaw
{

/** A discrete rotary switch: the labels sit above the knob and the pointer clicks
    from one position to the next. */
class MinimoogRotarySwitch : public juce::Component
{
public:
    MinimoogRotarySwitch() = default;

    void setLabels (juce::StringArray newLabels);
    /** The waveform switch shows its name above the knob instead of a label row. */
    void setShowLabels (bool shouldShow);
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
    bool showLabels = true;
    int dragStartSelected = 0;
    juce::Point<int> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogRotarySwitch)
};

/** A two-position rocker switch. */
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

//==============================================================================
/** The Minimoog Model D editor: four wide sections - controllers, oscillator
    bank, mixer and modifiers - laid out on a dark panel with a wooden frame. */
class MinimoogEditor : public juce::Component,
                       private juce::ChangeListener
{
public:
    MinimoogEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~MinimoogEditor() override;

    static constexpr int kDesignWidth  = 1120;
    static constexpr int kDesignHeight = 620;

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
    juce::Slider mainVolume;

    juce::Slider masterTune, glideKnob, modMix;
    MinimoogRocker unison, osc3Control;
    MinimoogRocker osc3FilterEg, noiseLfo;     // original panel, not driven yet

    MinimoogRotarySwitch rangeSwitch[3], waveSwitch[3];
    std::array<juce::Label, 3> waveName;
    juce::Slider tune[3];
    juce::Slider oscLevel[3];
    MinimoogRocker oscOn[3], distortionOn, noiseType;

    juce::Slider noiseLevelSlider, distortion;

    juce::Slider cutoff, emphasis, contour;
    juce::Slider filterAttack, filterDecay, filterSustainLevel;
    juce::Slider ampAttack, ampDecay, ampSustainLevel;
    MinimoogRocker keyTrack1, keyTrack2, releaseSwitch;

    std::vector<PrintedLabel> printedLabels;
    juce::Rectangle<int> panelArea, controllersArea, oscillatorArea, mixerArea, modifiersArea;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimoogEditor)
};

} // namespace nidaw
