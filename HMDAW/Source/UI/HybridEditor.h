#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"
#include "SynthControls.h"
#include "HybridOscillatorWindow.h"
#include "HybridFxPanel.h"
#include "HybridMixerPanel.h"
#include "WavetableTableWindow.h"
#include "HybridModulationPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** First working hybrid page: three generator layers on one screen, with
    rotary controls and direct filter/envelope manipulation.

    The layer cards are deliberately compact; the selected layer's filter and
    amplifier envelope share the large interaction area below them. */
class HybridEditor : public juce::Component,
                     private juce::ChangeListener,
                     private juce::MidiKeyboardState::Listener
{
public:
    HybridEditor (ProjectDocument& document, AudioEngine& audio, PresetStore& presets,
                  AppState* appState = nullptr);
    ~HybridEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    static constexpr int kDesignWidth = 1000;
    static constexpr int kDesignHeight = 820;

    int getSelectedLayerForTest() const noexcept { return selectedLayer; }
    juce::Rectangle<int> getLayerCardBoundsForTest (int layer) const;
    juce::Rectangle<int> getFilterBoundsForTest() const { return filterView.getBounds(); }
    juce::Rectangle<int> getEnvelopeBoundsForTest() const { return envelopeView.getBounds(); }
    int getLayerModeForTest (int layer) const;
    bool setLayerModeForTest (int layer, int mode);
    void showFxPageForTest();
    HybridFxPanel* getFxPanelForTest() const noexcept { return fxPanel.get(); }
    void showMixerPageForTest();
    HybridMixerPanel* getMixerPanelForTest() const noexcept { return mixerPanel.get(); }
    void showModulationPageForTest();
    juce::Rectangle<int> getFxButtonBoundsForTest() const { return fxButton.getBounds(); }
    int getEnvelopeTabCountForTest() const noexcept { return 4; }
    int getLfoTabCountForTest() const noexcept { return 6; }
    int getFilterTabCountForTest() const noexcept { return 2; }
    juce::Rectangle<int> getEnvelopePanelBoundsForTest() const;
    juce::Rectangle<int> getFilterPanelBoundsForTest() const;
    juce::Rectangle<int> getLfoPanelBoundsForTest() const;
    void toggleThumbnailForTest (int source);
    juce::Rectangle<int> getEditButtonBoundsForTest (int layer) const;
    juce::Rectangle<int> getLayerKnobBoundsForTest (int layer, int which) const;
    juce::Rectangle<int> getLayerKnobLabelBoundsForTest (int layer, int which) const;
    void assignModulationForTest (int source, int destination);
    bool getPositionDiscreteForTest (int layer) const;
    juce::Rectangle<int> getThumbnailBoundsForTest (int source) const;
    juce::Rectangle<int> getLfoDepthBoundsForTest() const { return lfoDepth.getBounds(); }
    juce::Rectangle<int> getLfoFadeBoundsForTest() const { return lfoFade.getBounds(); }
    void selectFilterForTest (int index);
    float getActiveFilterCutoffForTest() const;
    bool getThumbnailThreeDForTest (int source) const;
    PresetBar& getPresetBarForTest() noexcept { return presetBar; }
    void openWaveEditorForTest (int layer);
    void closeWaveEditorForTest();
    WavetableTablePanel* getEmbeddedWaveEditorForTest() noexcept { return embeddedWaveEditor.get(); }
    juce::Rectangle<int> getEmbeddedWaveEditorBoundsForTest() const
    {
        return embeddedWaveEditor != nullptr ? embeddedWaveEditor->getBounds()
                                             : juce::Rectangle<int>();
    }
    juce::Rectangle<int> getEmbedBackBoundsForTest() const { return embedBack.getBounds(); }
    HybridOscillatorThumbnail* getThumbnailForTest (int source) noexcept;

private:
    struct LayerControls
    {
        juce::TextButton select;
        juce::TextButton enable;
        juce::TextButton edit;
        juce::ComboBox mode;
        juce::Label modeLabel;
        juce::Slider unison;
        juce::Slider detune;
        juce::Slider position;
        juce::Label unisonLabel;
        juce::Label detuneLabel;
        juce::Label positionLabel;
        juce::Slider level;
        juce::Slider pan;
        juce::Label levelLabel;
        juce::Label panLabel;
        juce::TextButton import;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity) override;
    InstrumentTrack* currentTrack();
    const InstrumentTrack* currentTrack() const;
    HybridLayerParams* selectedParams();

    void selectLayer (int layer);
    void updateControlsFromTrack();
    void pushCurrentParams();
    void pushParams (const HybridParams& params);
    void beginParamEdit (const juce::String& name);
    void showFilterModMenu (int filterIndex);
    void showModMenu (HybridModDestination destination, int positionLayer = -1);
    void addModMenu (juce::Slider& slider, HybridModDestination destination,
                     int positionLayer = -1);
    void updatePlayheads();
    void importForSelectedLayer();
    juce::int64 ensureCustomTableForLayer (int layer);
    std::vector<PresetBar::Entry> buildPresetEntries() const;
    bool loadPresetById (int id);
    bool savePresetByName (const juce::String& name, bool overwrite);
    bool renamePresetById (int id, const juce::String& newName);
    bool deletePresetById (int id);
    bool userPresetExists (const juce::String& name) const;

    ProjectDocument& document;
    AudioEngine& audio;
    PresetStore& presets;
    AppState* appState = nullptr;
    juce::int64 trackId = 0;

    juce::Label titleLabel;
    PresetBar presetBar { NI_TXT ("我的混合音色") };
    juce::Slider gain;
    juce::Label status;
    juce::TextButton layerButtons[3];
    std::array<LayerControls, 3> layers;
    juce::TextButton subEnable, noiseEnable;
    juce::TextButton subOpen, noiseOpen;
    juce::ComboBox subWave, subOctave, noiseType;
    juce::Slider subLevel, subPan, noiseLevel, noisePan, noiseTone;
    juce::Label subLevelLabel, subPanLabel, noiseLevelLabel, noisePanLabel, noiseToneLabel;
    FilterResponseView filterView;
    FilterResponseView filterView2;
    juce::Label filter1ModeLabel, filter2ModeLabel;
    juce::ComboBox filter1Mode, filter2Mode;
    std::array<juce::TextButton, 2> filterTabs;
    int activeFilter = 0;
    juce::ComboBox filterRoute;
    EnvelopeEditorView envelopeView;
    std::array<juce::TextButton, 4> envTabs;
    std::array<juce::Slider, 4> envKnobs;
    std::array<juce::Label, 4> envKnobLabels;
    juce::TextButton envMode;
    juce::TextButton envMs, envBpm;
    std::array<juce::ComboBox, 3> envDivisions;
    std::array<juce::Label, 3> envDivisionLabels;
    LfoEditorView lfoView;
    std::array<juce::TextButton, 6> lfoTabs;
    juce::TextButton lfoMode, lfoRetrigger;
    juce::TextButton lfoMs, lfoBpm;
    juce::ComboBox lfoWave;
    juce::Label lfoWaveLabel;
    juce::ComboBox lfoDivision;
    juce::Slider lfoRate, lfoPhase, lfoDepth, lfoFade;
    juce::Label lfoRateLabel, lfoPhaseLabel, lfoDivisionLabel;
    juce::Label lfoDepthLabel, lfoFadeLabel;
    std::array<juce::Slider, 2> filterCutoffKnobs;
    std::array<juce::Slider, 2> filterResonanceKnobs;
    std::array<juce::Label, 2> filterCutoffLabels;
    std::array<juce::Label, 2> filterResonanceLabels;
    int selectedEnvelope = 0;
    int selectedLfo = 0;
    class UiTimer;
    std::unique_ptr<UiTimer> uiTimer;
    std::vector<std::unique_ptr<juce::MouseListener>> modListeners;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState,
                                           juce::MidiKeyboardComponent::horizontalKeyboard };
    int previewMidiNote = -1;
    int selectedLayer = 0;
    bool updating = false;
    std::unique_ptr<WavetableTablePanel> embeddedWaveEditor;
    std::unique_ptr<HybridOscillatorPanel> embeddedOscillator;
    std::unique_ptr<HybridModulationPanel> modulationPanel;
    juce::TextButton modButton;
    std::unique_ptr<HybridFxPanel> fxPanel;
    juce::TextButton fxButton;
    std::unique_ptr<HybridMixerPanel> mixerPanel;
    juce::TextButton mixerButton;
    int embeddedLayer = 0;
    int embeddedSource = 0;
    juce::TextButton embedBack;
    std::array<std::unique_ptr<HybridOscillatorThumbnail>, 5> thumbnails;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridEditor)
};

} // namespace hmdaw
