#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "ArpControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace nidaw
{

/** The mapping editor's canvas: the piano keys on the left, the patch area in the
    middle, and the chosen reference line group's own line on the right.

    A key is bound to a frequency by dragging it onto one of the group's dots (the
    magnet pulls within 14 px); the line that appears is drawn in that dot's colour.
    The right-hand column is **read-only** - editing the group itself still happens
    in the reference group editor.
*/
class MidiInputCanvas : public juce::Component
{
public:
    MidiInputCanvas (ProjectDocument& document, AppState& appState);

    /** A key was bound or unbound (the owner saves / redraws what it needs to). */
    std::function<void()> onMapChanged;
    /** Play this frequency, now (the owner routes it to the selected track). */
    std::function<void (double frequencyHz)> onAudition;
    /** A key was clicked while the anchor is being taught. */
    std::function<void (int midiNote)> onKeyPicked;

    /** 应用 was pressed: the next MIDI note teaches where the tile starts. */
    void setLearning (bool shouldLearn);
    bool isLearning() const noexcept { return learning; }
    /** The key the user picked to teach the anchor, or -1. */
    int getLearningKey() const noexcept { return learningKey; }
    /** Scrolls the keyboard so this note is on screen (free mode). */
    void showPlayedNote (int midiNote);

    void refresh();
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // ---- diagnostic hooks ---------------------------------------------------
    static constexpr int kKeyStripWidth   = 96;
    static constexpr int kGroupStripWidth = 300;
    static constexpr int kKeyHeight       = 13;

    std::vector<int> getVisibleNotesForTest() const;
    juce::Rectangle<int> getKeyBoundsForTest (int midiNote) const;
    int getDotCountForTest() const;
    juce::Rectangle<int> getDotBoundsForTest (int index) const;
    double getDotFrequencyForTest (int index) const;
    /** The colour that dot is drawn in - the same resolution the canvas uses for a
        参考线组 entry, so the self test can prove the two agree. */
    juce::Colour getDotColourForTest (int index) const;
    /** Which dot a release at this point would land on (-1 = none). Uses the
        forgiving rule: anywhere in the right-hand column snaps to the nearest dot. */
    int getDropDotForTest (juce::Point<int> point) const;
    /** The caption the key strip draws in that key, and whether that step of the
        tile is still empty ("空"). The self test reads exactly what is painted. */
    juce::String getKeyCaptionForTest (int midiNote) const;
    bool isStepEmptyForTest (int midiNote) const;
    /** How many of the group's dots this tile is long enough to take. Dots past
        that are drawn grey and cannot be dropped on ("装不下就绑不了"). */
    int getBindableDotCountForTest() const;
    /** Whether that dot can be bound at all (its place inside one octave has to fit
        inside one tile). */
    bool isDotBindableForTest (int index) const;
    int getHoverDotForTest() const noexcept { return hoverDot; }
    bool isKeyBoundForTest (int midiNote) const;
    /** Drags a key onto a dot the way the mouse does. */
    void bindKeyForTest (int midiNote, int dotIndex);
    void unbindKeyForTest (int midiNote);
    /** Lets go of one cable of that key (the right-click on a line does this). */
    void unbindCableForTest (int midiNote, int dotIndex);
    /** How many cables leave this key (one per dot it is tied to). */
    int getCableCountForTest (int midiNote) const;
    /** The key whose cable runs under this point, or -1. */
    int getCableNoteAtForTest (juce::Point<int> point) const;
    /** The frequency text the middle area draws beside that key's row. */
    juce::String getKeyFrequencyTextForTest (int midiNote) const;
    /** The right-hand column's own view: 1 = the whole group is visible (the
        default), larger = magnified, and the scroll is how far it is pushed down. */
    float getGroupZoomForTest() const noexcept { return groupZoom; }
    float getGroupScrollForTest() const noexcept { return groupScroll; }
    /** The two gestures the wheel over that column runs (Ctrl+wheel zooms). */
    void scrollGroupForTest (float deltaY);
    void zoomGroupForTest (float factor, float anchorY);
    void resetGroupViewForTest() { setGroupView (1.0f, 0.0f); }
    /** Every key currently bound to that dot's frequency (low to high). */
    std::vector<int> getKeysBoundToDotForTest (int dotIndex) const;

private:
    const midimap::Map& map() const;
    MidiInputState& state();
    const ReferenceLineGroup& group() const;

    /** One dot of the chosen group as the editor offers it: a frequency, plus the
        colour of the reference group dot it came from (the canvas draws each of a
        group's lines in its own dot's colour, and this window has to agree). */
    struct Dot
    {
        double hz = 0.0;
        int    colourIndex = 0;
        /** Which dot of the group this is, counted upwards inside its own octave
            (0 = the group's lowest dot). The tile has to be long enough to hold
            that many dots, so this is what "装不下" is measured against. */
        int    slot = 0;
    };

    /** Every dot of the chosen group, low to high: the group drawn over the octave
        above and below its anchor as well (three octaves), so a key can be bound to
        the same scale degree at a lower or higher pitch. */
    std::vector<Dot> dots() const;
    std::vector<double> dotFrequencies() const;
    /** How many dots the group has inside one octave - the number the tile has to
        be long enough for. */
    int dotSlotCount() const;
    /** How many steps one tile has (the same clamp the map uses). */
    int tileLength() const;
    /** True when this dot's place inside its octave fits inside one tile, so it can
        be bound at all. */
    bool isDotBindable (int index) const;
    /** The note name drawn inside a key - every key is labelled, so "which key is
        which" is never a guess. */
    juce::String keyCaption (int midiNote) const;
    /** The colour a dot is painted in: the entry's colour if the source set one,
        otherwise that dot's own colour - exactly what the reference canvas does. */
    juce::Colour colourForDot (const Dot& dot) const;
    /** The colour of the dot a frequency belongs to (the nearest one). */
    juce::Colour colourForHz (double hz) const;
    /** The dot a release here means: inside the right-hand column it is the nearest
        dot vertically, so a drag only has to get close rather than land inside a
        14 px magnet. -1 outside that column. */
    int dropDotAt (juce::Point<int> point) const;
    /** The key a release here means, for the opposite drag (dot -> key). */
    int dropKeyAt (juce::Point<int> point) const;
    /** The cable under a point, as "which key row / which of its frequencies".
        This is what the right-click that lets go of a single line uses. */
    struct CableHit
    {
        int midiNote = -1;
        int frequencyIndex = -1;
    };

    CableHit cableHitAt (juce::Point<int> point) const;
    /** "261.63 · 523.25": every frequency of that key on one line. */
    juce::String keyFrequencyText (int midiNote) const;
    void setGroupView (float zoom, float scroll);
    void scrollGroupBy (float pixels);
    /** Zooms the column about a point on the canvas, so what is under the pointer
        stays under it. */
    void zoomGroupAt (float anchorY, float factor);
    /** Keeps the zoom in range and the content from being scrolled off the canvas. */
    void clampGroupView();
    double lowestHz() const;
    double highestHz() const;
    float yForHz (double hz) const;
    juce::Rectangle<int> keyRect (int midiNote) const;
    int noteAt (juce::Point<int> point) const;
    /** The dot the magnet would catch at this point, or -1. */
    int dotAt (juce::Point<int> point) const;
    void commit (const std::function<void (midimap::Map&)>& mutation);

    ProjectDocument& document;
    AppState& appState;

    bool learning = false;
    int  learningKey = -1;
    int  dragNote = -1;
    /** The opposite gesture: pressed on a dot, looking for a key to drop on. */
    int  dragDot = -1;
    /** The key that drag is currently over (for the preview line). */
    int  hoverNote = -1;
    int  hoverDot = -1;
    int  firstVisibleNote = 48;
    /** The group column's own view: 1 = the whole group fits the canvas (how the
        window has always drawn it), and the wheel scrolls / Ctrl+wheel zooms from
        there. The keyboard strip keeps its own note-range scrolling. */
    float groupZoom = 1.0f;
    float groupScroll = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputCanvas)
};

//==============================================================================
/** The window's content: the device, the mode, the group picker, the presets and
    the mapping canvas. */
class MidiInputPanel : public juce::Component,
                       private juce::ChangeListener
{
public:
    MidiInputPanel (ProjectDocument& document, AppState& appState, PresetStore& presets,
                    AudioEngine& audio, class MidiInputManager& midi);
    ~MidiInputPanel() override;

    /** False in the offscreen self test, where a dialog would block the run. */
    bool showDialogs = true;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    void refresh();
    /** The 琶音 controls changed (the owner re-reads the settings and re-arms the
        player: this is configuration, so nothing here is undoable). */
    std::function<void()> onArpSettingsChanged;
    /** Refreshes just the 琶音 row and the status line. A key press must not rebuild
        the whole window, which is what refresh() does. */
    void refreshArpControls();

    /** A MIDI note arrived (message thread). Returns true when the input window used
        it for teaching the anchor rather than for playing. */
    bool handleMidiMessage (const juce::MidiMessage& message);

    /** True while 应用 is waiting for "click a key, then press a MIDI key". */
    bool isLearningAnchor() const noexcept { return learningAnchor; }

    MidiInputCanvas& getCanvas() noexcept { return canvas; }
    /** 琶音器's controls (the same set the virtual keyboard window shows). */
    ArpControls& getArpControlsForTest() noexcept { return arpControls; }

    // ---- diagnostic hooks ---------------------------------------------------
    juce::Component& getDeviceBoxForTest() noexcept { return deviceBox; }
    juce::Component& getGroupBoxForTest() noexcept { return groupBox; }
    juce::Component& getApplyButtonForTest() noexcept { return applyButton; }
    juce::Component& getSaveButtonForTest() noexcept { return saveButton; }
    juce::Component& getSaveAsButtonForTest() noexcept { return saveAsButton; }
    juce::Component& getPresetsButtonForTest() noexcept { return presetsButton; }
    juce::Component& getDeleteButtonForTest() noexcept { return deleteButton; }
    juce::Component& getLoopModeButtonForTest() noexcept { return loopModeButton; }
    juce::Component& getFreeModeButtonForTest() noexcept { return freeModeButton; }
    juce::Component& getLoopKeysBoxForTest() noexcept { return loopKeysBox; }
    juce::String getStatusTextForTest() const { return statusLabel.getText(); }
    juce::String getTrackTextForTest() const { return trackLabel.getText(); }
    void setDeviceForTest (int index);
    void setModeForTest (bool looping);
    void setLoopKeysForTest (int keys);
    void setGroupForTest (int comboId);
    void triggerApplyForTest();
    void savePresetForTest();
    void saveAsForTest (const juce::String& name);
    /** 改名…: gives the mapping's own library entry a new name (the project's
        `presetName` follows it). */
    void renamePresetForTest (const juce::String& newName);
    void loadPresetForTest (int index);
    void deletePresetForTest();
    juce::Rectangle<int> getRenameButtonBoundsForTest() const { return renameButton.getBounds(); }
    int  getPresetCountForTest() const;
    juce::String getPresetNameForTest() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void rebuildDeviceBox();
    void rebuildGroupBox();
    void applyMode (bool looping);
    void showRenameDialog();
    void audition (double frequencyHz);
    void updateStatus();
    /** The list the 预设 menu shows: one entry per saved mapping. */
    std::vector<juce::String> presetNames() const;

    ProjectDocument& document;
    AppState& appState;
    PresetStore& presets;
    AudioEngine& audio;
    MidiInputManager& midi;

    juce::Label deviceLabel, trackLabel, keysLabel, groupLabel;
    juce::ComboBox deviceBox, loopKeysBox, groupBox;
    juce::TextButton loopModeButton, freeModeButton;
    juce::TextButton applyButton, saveButton, saveAsButton, presetsButton, deleteButton;
    /** 改名…: renames the mapping's own library entry. */
    juce::TextButton renameButton;
    juce::Label statusLabel;
    MidiInputCanvas canvas;
    /** 琶音器: the same controls the virtual keyboard window shows. */
    ArpControls arpControls;

    bool learningAnchor = false;
    /** 应用 was pressed and a MIDI key arrived before any left-hand key: the status
        line says which half of the gesture is still missing. */
    bool learningNeedsKey = false;
    bool updating = false;
    /** Hands every audition its own voice id, so two quick clicks never fight over
        one voice. Negative, because real notes use positive ids. */
    int auditionSerial = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputPanel)
};

//==============================================================================
/** The MIDI input window: reused, hidden when closed, placed like the others. */
class MidiInputWindow : public juce::DocumentWindow
{
public:
    MidiInputWindow (ProjectDocument& document, AppState& appState, PresetStore& presets,
                     AudioEngine& audio, MidiInputManager& midi, juce::Component* context);
    ~MidiInputWindow() override;

    void closeButtonPressed() override;

    /** Brings the window up (creating its scheme the first time) and shows it. */
    void show();
    void refresh();

    MidiInputPanel& getPanel() noexcept { return *panel; }

    static constexpr int kDesignWidth  = 900;
    static constexpr int kDesignHeight = 560;

private:
    MidiInputPanel* panel = nullptr;
    juce::Component* placementContext = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputWindow)
};

} // namespace nidaw
