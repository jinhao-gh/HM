#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "PresetBar.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** The picture at the top of the wavetable editor: the table's frames as a 3D
    waterfall, with a live cursor on it.

    A fixed projection (no rotation): frame 0 is the nearest one, every frame
    further back is drawn a little narrower and higher, and they are drawn from the
    back forwards. The bright line is the wave the instrument is on *right now* -
    `WavetableSynth::sampleTable` with the same neighbouring-frame interpolation the
    engine uses, so the picture cannot drift away from the sound.

    It is also the instrument's direct manipulation: **drag** anywhere to move the
    position, **click** a frame to jump to it, **wheel** to nudge it, **double click**
    to go back to the middle. Custom tables are drawn when the oscillator uses one
    (the old display always drew the built-in table, whatever was playing).
*/
class WavetableWaterfallView : public juce::Component
{
public:
    /** How a gesture is applied: `begin` opens one undo step (the editor snapshots
        the project), `update` carries the new position. */
    enum class Gesture { begin, update };

    WavetableWaterfallView() = default;

    /** What to draw: the oscillator's table (a custom one wins over the built-in
        index, exactly like the engine does it), the position the parameters say, and
        the position the sound is really on (-1 while nothing is sounding). */
    void setContent (std::shared_ptr<const wavetable::Wavetable> customTable,
                     int builtInTableIndex, int oscIndex, double parameterPosition,
                     double livePosition);

    /** The live half on its own: cheap enough for a 30 Hz timer, and it only
        repaints when the value actually moved. */
    void setLivePosition (double newLivePosition);

    /** The position the picture is showing (the live one when there is one). */
    double getDisplayedPosition() const noexcept
    {
        return livePosition >= 0.0 ? livePosition : parameterPosition;
    }

    /** The name of the table being drawn: the project's own table when the
        oscillator uses one, otherwise the built-in bank's name. */
    juce::String getDisplayedTableName() const;

    int getDisplayedOscillator() const noexcept { return osc; }
    bool isShowingLivePosition() const noexcept { return livePosition >= 0.0; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    /** One gesture, one undo step: `begin` first, then an `update` per change. */
    std::function<void (Gesture, double)> onPositionGesture;

    // ---- self test ----------------------------------------------------------
    juce::Rectangle<int> getPlotBoundsForTest() const { return plot; }
    /** Where the cursor line sits on the plot, so a test can watch it move. */
    juce::Point<int> getCursorPointForTest() const;
    /** The depth band one frame owns: clicking in it means that frame. */
    juce::Rectangle<int> getFrameRowBoundsForTest (int frame) const;
    /** How many frames the picture is drawing. */
    int getFrameCountForTest() const;

private:
    const wavetable::Wavetable& table() const;
    /** How far back a slice is drawn: 0 = the nearest frame, frameCount-1 = the
        furthest. */
    double sliceForPosition (double position) const;
    /** The height of a slice's centre line. */
    float centreYForSlice (double slice) const;
    /** How much a slice is shrunk horizontally (the perspective). */
    float shrinkForSlice (double slice) const;
    /** The position the pointer's depth means. */
    double positionAt (juce::Point<int> point) const;
    /** The frame whose band this point falls in, or -1 outside the plot. */
    int frameAt (juce::Point<int> point) const;
    float amplitude() const;

    std::shared_ptr<const wavetable::Wavetable> custom;
    int builtInIndex = 0;
    int osc = 0;
    double parameterPosition = 0.5;
    double livePosition = -1.0;
    bool dragged = false;
    juce::Rectangle<int> plot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableWaterfallView)
};

//==============================================================================
/** The wavetable instrument editor: two table oscillators plus noise into the
    same multimode filter the subtractive instrument uses, with an extra envelope
    that sweeps the table position and one LFO that can reach the position, the
    pitch, the filter and the amplitude.

    Kept independent of the window that hosts it so the offscreen self test can
    build one directly and take a snapshot.
*/
class WavetableEditor : public juce::Component,
                        private juce::ChangeListener,
                        private juce::MidiKeyboardState::Listener,
                        private juce::Timer
{
public:
    WavetableEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~WavetableEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }

    /** Set by MainComponent: 波表编辑器… - the window that imports a WAV, samples
        the open clip and draws the project's own tables. */
    std::function<void()> onEditTable;
    /** Set by MainComponent: 删除波表 - drops the custom table the first oscillator
        is using (the oscillators fall back to their built-in tables). */
    std::function<void()> onDeleteTable;

    void setShowDialogs (bool shouldShow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    juce::Rectangle<int> getEditTableButtonBoundsForTest() const { return editTableButton.getBounds(); }
    juce::Rectangle<int> getDeleteTableButtonBoundsForTest() const { return deleteTableButton.getBounds(); }
    /** The waterfall view and the two 振荡器 buttons above it. */
    juce::Rectangle<int> getWaterfallBoundsForTest() const { return display.getBounds(); }
    juce::Rectangle<int> getWaterfallPlotBoundsForTest() const { return display.getPlotBoundsForTest(); }
    juce::Rectangle<int> getOscillatorButtonBoundsForTest (int which) const;
    juce::String getDisplayedTableNameForTest() const { return display.getDisplayedTableName(); }
    double getDisplayedPositionForTest() const { return display.getDisplayedPosition(); }
    juce::Point<int> getCursorPointForTest() const { return display.getCursorPointForTest(); }
    bool isShowingLivePositionForTest() const { return display.isShowingLivePosition(); }
    int  getDisplayedOscillatorForTest() const { return display.getDisplayedOscillator(); }
    void selectDisplayedOscillatorForTest (int which) { selectDisplayedOscillator (which); }
    /** Runs the 30 Hz cursor tick right now, so a test does not have to wait for it. */
    void tickWaterfallCursorForTest() { timerCallback(); }
    bool isDeleteTableEnabledForTest() const { return deleteTableButton.isEnabled(); }
    void triggerDeleteTableForTest();
    /** How many 自定义 entries the 波表 choice offers (0 for a project with none). */
    int getCustomTableChoiceCountForTest() const;
    /** Picks the nth entry of a table choice (0/1 = osc1/osc2) by item index; the
        built-in tables come first, then the project's own. */
    bool selectTableChoiceForTest (int which, int itemIndex);
    void triggerEditTableForTest();
    /** Text of one combo entry: which 0 = 振荡器 1 波表, 1 = 振荡器 2 波表,
        2 = 滤波模式, 3 = 斜率, 4 = LFO 波形. */
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
    WavetableWaterfallView& getDisplayForTest() noexcept { return display; }

    /** The height this page is laid out at: 80 px more than the other instrument
        pages, which is exactly the room the waterfall view takes. */
    static constexpr int kDesignHeightWithWaterfall = 640;

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
    /** Rebuilds both 波表 choices from the built-in bank plus the project's tables. */
    void refreshTableChoices();
    /** Hands the preview synth the custom tables this track's oscillators use. */
    void pushPreviewTables();
    /** Which oscillator the waterfall view shows (and which one its gestures move). */
    void selectDisplayedOscillator (int which);
    /** Points the view at the oscillator's table (custom or built-in) and at the
        position its parameters say. */
    void updateWaterfall();
    void pushCurrentParams();
    void pushParams (const WavetableParams& params);
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
    juce::TextButton editTableButton;
    juce::TextButton deleteTableButton;
    PresetBar presetBar { NI_TXT ("内置") };
    WavetableWaterfallView display;
    /** 振荡器 1 / 振荡器 2: which table the waterfall view shows. */
    juce::TextButton osc1ViewButton, osc2ViewButton;
    int displayedOscillator = 0;

    juce::Label osc1Header, osc2Header, noiseHeader;
    juce::Label osc1TableLabel, osc2TableLabel;
    juce::ComboBox osc1Table, osc2Table;
    juce::TextButton osc2Enable;
    juce::Slider osc1Position, osc1Octave, osc1Semitone, osc1Detune, osc1Level;
    juce::Slider osc2Position, osc2Octave, osc2Semitone, osc2Detune, osc2Level;
    juce::Slider noiseLevel;

    juce::Label filterHeader, posEnvHeader;
    juce::Label filterModeLabel, filterSlopeLabel;
    juce::ComboBox filterMode, filterSlope;
    juce::Slider cutoff, resonance, filterKeyTrack;
    juce::Slider posAttack, posDecay, posSustain, posRelease, posEnvAmount;

    juce::Label ampHeader, lfoHeader, outputHeader, lfoWaveLabel;
    juce::Slider ampAttack, ampDecay, ampSustain, ampRelease;
    juce::ComboBox lfoWave;
    juce::Slider lfoRate, lfoPosition, lfoPitch, lfoCutoff, lfoAmp;
    juce::Slider gain;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    /** Row captions are painted rather than being Label components, exactly like
        the subtractive editor. */
    struct RowLabel
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        bool header = false;
    };

    std::vector<RowLabel> rowLabels;

    int previewMidiNote = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableEditor)
};

} // namespace nidaw
