#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The "音符编辑" strip: frequency, velocity, comment, and the six frequency
    ratio buttons (1:2 / 1:3 / 1:5 / 1:7 / 1:9 / custom, hotkeys 1 to 6).
*/
class NoteEditBar : public juce::Component,
                    private juce::ChangeListener
{
public:
    NoteEditBar (ProjectDocument& document, AppState& appState);
    ~NoteEditBar() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();
    void focusFrequencyEditor();

    /** Height the strip needs for the per-note list when several notes are
        selected: 0 with one note selected (the strip keeps its normal height). */
    int getExtraHeight() const;

    /** Height of the strip itself: two rows whenever a note is selected, one row
        otherwise (nothing selected). */
    int getBaseHeight() const;

    /** Diagnostic hooks for the offscreen self test. */
    bool isRatioRowVisible() const { return ratioLabel.isVisible(); }
    /** Whether the ratio row is on screen right now (any note selected, on a track
        that has a frequency axis). */
    bool isRatioRowVisibleForTest() const { return ratioRowVisible(); }
    /** Whether the ratio keys are live: the lens needs exactly one selected note. */
    bool areRatioKeysEnabledForTest() const;
    juce::Rectangle<int> getRatioRowBoundsForTest() const { return ratioRowBounds; }
    /** Bounds of one ratio key: index 0..4 are the presets, 5 is the custom one. */
    juce::Rectangle<int> getRatioButtonBoundsForTest (int index) const;
    /** Whether that key is on screen right now (self test). */
    bool isRatioButtonVisibleForTest (int index) const
    {
        return juce::isPositiveAndBelow (index, ratioButtons.size())
                   && ratioButtons[index]->isVisible();
    }
    /** Whether the row's read-out (the "what would this ratio give" label) is on
        screen right now. */
    bool isTargetVisibleForTest() const { return targetLabel.isVisible(); }
    /** Clicks one ratio key exactly as the mouse would. */
    bool triggerRatioButtonForTest (int index);
    juce::String getTargetTextForTest() const { return targetLabel.getText(); }
    juce::Rectangle<int> getTargetLabelBoundsForTest() const { return targetLabel.getBounds(); }
    bool isCustomRatioEnabledForTest() const { return customRatioEditor.isEnabled(); }
    juce::Rectangle<int> getGroupButtonBoundsForTest() const { return groupButton.getBounds(); }
    /** Clicks 应用为参考线组 exactly as the mouse would. */
    void triggerGroupApplyForTest();
    /** The frequency field's text (a drum name on a drum track) and whether it is
        read only there. */
    juce::String getFrequencyTextForTest() const { return frequencyEditor.getText(); }
    juce::String getTailFrequencyTextForTest() const { return tailFrequencyEditor.getText(); }
    void setHeadFrequencyForTest (double hz)
    {
        frequencyEditor.setText (juce::String (hz, 4), true);
        applyFrequency();
    }
    void setTailFrequencyForTest (double hz)
    {
        tailFrequencyEditor.setText (juce::String (hz, 4), true);
        applyTailFrequency();
    }
    juce::Rectangle<int> getTailFrequencyBoundsForTest() const { return tailFrequencyEditor.getBounds(); }
    juce::Rectangle<int> getEndpointToggleBoundsForTest() const { return endpointButton.getBounds(); }
    bool isTailEndpointSelectedForTest() const
    {
        return appState.currentPitchEndpoint == AppState::PitchEndpoint::tail;
    }
    void toggleEndpointForTest() { endpointButton.triggerClick(); }
    bool isShapeRowVisibleForTest() const { return shapeRowVisible(); }
    juce::Rectangle<int> getCurveBoundsForTest() const { return pitchCurveSlider.getBounds(); }
    juce::Rectangle<int> getTiltStartBoundsForTest() const { return tiltStartSlider.getBounds(); }
    juce::Rectangle<int> getTiltEndBoundsForTest() const { return tiltEndSlider.getBounds(); }
    void setPitchShapeForTest (float curve, float start, float end);
    bool isFrequencyReadOnlyForTest() const { return frequencyEditor.isReadOnly(); }
    void triggerMute();
    bool isMuteOn() const { return muteButton.getToggleState(); }
    void triggerMultiRowMute (int index);
    bool isMultiRowMuted (int index) const;
    juce::Rectangle<int> getMultiRowMuteBounds (int index) const;

    static constexpr int multiRowHeight = 22;
    static constexpr int multiMaxRows   = 4;
    static constexpr int singleNoteHeight = 62;
    static constexpr int multiNoteHeight  = 34;

    /** The strip the editor always reserves for this bar.

        A note selection must never resize the canvas: the height of the editor
        changes the y <-> frequency mapping, so a bar that appears and grows while
        the pointer is dragging makes everything jump (and makes the drag's own
        maths use a mapping that moved under it). The bar is laid out in this slot
        and, with several notes selected, its per-note list floats over the canvas
        below - `getExtraHeight()` is how far it hangs over. */
    static constexpr int slotHeight = singleNoteHeight;

    /** Diagnostic hooks for the offscreen self test. */
    int getMultiRowCount() const { return multiRows.size(); }
    juce::Rectangle<int> getMultiRowBounds (int index) const;
    /** Edits one row's frequency through the same path its editor uses. */
    void applyMultiRowFrequency (int index, double hz);

    /** Applies one of the five preset ratios; index 0..4 -> 1:2, 1:3, 1:5, 1:7, 1:9. */
    void applyPresetRatio (int index);

    /** Reads the custom ratio editor and applies it. */
    void applyCustomRatio();

private:
    class MultiRow;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    Note* primaryNote();
    const Note* primaryNote() const;
    void applyFrequency();
    void applyTailFrequency();
    void applyVelocity();
    void applyComment (const juce::String& text);
    void applyRatio (double ratio);
    void applyMute (bool shouldMute);
    void applyGroupFromSelection();
    void updateRatioButtons();
    void rebuildMultiRows();
    int  multiRowCount() const;
    bool shapeRowVisible() const;
    bool vstMode() const;
    void applyPitchShapeControls();

    /** The row is on screen whenever there is a frequency to talk about: any note
        selection on a track that is not a drum kit. Both refresh() and resized() ask
        this, so the visibility and the geometry can never disagree.

        The *lens* only works on a single origin, so with several notes selected the
        keys are greyed out (and the row says why) instead of disappearing - a row
        that vanishes leaves the user looking for options that were never gone. */
    bool ratioRowVisible() const;
    /** True when the ratio lens has exactly one note to work on. */
    bool ratioLensEnabled() const;
    /** True when the open track is a drum kit: the first field names the drum
        instead of showing a frequency. */
    bool drumMode() const;

    ProjectDocument& document;
    AppState& appState;

    juce::Label titleLabel;
    juce::Label frequencyLabel;
    juce::Label tailFrequencyLabel;
    juce::Label velocityLabel;
    juce::Label commentLabel;
    juce::Label ratioLabel;
    juce::Label targetLabel;

    juce::TextEditor frequencyEditor;
    juce::TextEditor tailFrequencyEditor;
    juce::TextEditor commentEditor;
    juce::TextEditor customRatioEditor;
    juce::Slider velocitySlider;
    juce::OwnedArray<juce::TextButton> ratioButtons;
    juce::TextButton muteButton;
    juce::TextButton groupButton;
    juce::TextButton endpointButton;
    juce::Label pitchCurveLabel;
    juce::Label tiltStartLabel;
    juce::Label tiltEndLabel;
    juce::Slider pitchCurveSlider;
    juce::Slider tiltStartSlider;
    juce::Slider tiltEndSlider;
    bool shapeEditOpen = false;

    /** The per-note list shown while several notes are selected. */
    juce::Viewport multiViewport;
    juce::Component multiHolder;
    juce::OwnedArray<MultiRow> multiRows;

    /** Where the ratio row was laid out last; diagnostics only. */
    juce::Rectangle<int> ratioRowBounds;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEditBar)
};

} // namespace hmdaw
