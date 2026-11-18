#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The instrument entry on the left of one arrangement lane.

    This is the only place instruments are listed: name, volume, pan, the timbre
    button and delete, plus the track's kind and clip count.
*/
class LaneHeader : public juce::Component,
                   private juce::ChangeListener
{
public:
    LaneHeader (ProjectDocument& document, AppState& appState, juce::int64 trackId);
    ~LaneHeader() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    void refresh();

    juce::int64 getTrackId() const noexcept { return trackId; }

    /** Set by TrackArea: the header itself does not own these flows. */
    std::function<void (juce::int64 trackId)> onSelect;
    std::function<void (juce::int64 trackId)> onEditTimbre;
    /** 效果: this track's insert chain in its own window. */
    std::function<void (juce::int64 trackId)> onEditEffects;
    std::function<void (juce::int64 trackId)> onImportAudio;
    std::function<void (juce::int64 trackId)> onDelete;
    /** A right click on the header: the track's own settings (what it can offer
        depends on the kind - a VST track, and only a VST track, can be switched to
        按半音演奏). */
    std::function<void (juce::int64 trackId)> onMenu;

    /** Diagnostic hooks for the offscreen self test.
        which: 0 name, 1 timbre, 2 volume, 3 pan, 4 delete, 5 mute, 6 solo. */
    juce::Rectangle<int> getControlBoundsForTest (int which) const;
    void setNameForTest (const juce::String& name);
    void setVolumeForTest (double db);
    void setPanForTest (double pan);
    void pressTimbreForTest();
    void pressDeleteForTest();
    void pressMuteForTest();
    void pressSoloForTest();
    bool isMuteOnForTest() const { return muteButton.getToggleState(); }
    bool isSoloOnForTest() const { return soloButton.getToggleState(); }
    /** The type text drawn on the third row, e.g. "鼓机". */
    juce::String getTypeTextForTest() const;
    /** The whole third row as it is painted, including the plugin name and the
        「插件缺失」 marker on a VST track. */
    juce::String getTypeLineForTest() const { return typeLineText(); }

    /** The rename flow: type into the editor, then press Return (apply) or Escape
        (cancel and leave the editing state). */
    void typeNameForTest (const juce::String& text) { nameEditor.setText (text, juce::dontSendNotification); }
    juce::String getEditedNameForTest() const { return nameEditor.getText(); }
    void pressRenameReturnForTest();
    void pressRenameEscapeForTest();

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    const InstrumentTrack* track() const;
    /** The third row's text (the kind line). */
    juce::String typeLineText() const;
    void applyName();
    /** Leaves the name editor without changing anything (Escape). */
    void cancelNameEdit();
    void applyVolume();
    void applyPan();
    void toggleMute();
    void toggleSolo();

    ProjectDocument& document;
    AppState& appState;
    juce::int64 trackId;

    juce::TextEditor nameEditor;
    juce::TextButton timbreButton;
    juce::TextButton importButton;
    juce::TextButton effectsButton;
    juce::Slider volumeSlider;
    juce::Slider panSlider;
    juce::TextButton deleteButton;
    juce::TextButton muteButton, soloButton;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneHeader)
};

} // namespace hmdaw
