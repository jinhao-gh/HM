#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/GmPrograms.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace hmdaw
{

/** The General MIDI editor: one program per GM track, plus the GM drum kit. */
class GmEditor : public juce::Component,
                 private juce::ChangeListener,
                 private juce::ListBoxModel,
                 private juce::Timer
{
public:
    GmEditor (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets);
    ~GmEditor() override;

    void setTrack (juce::int64 newTrackId);
    juce::int64 getTrackId() const noexcept { return trackId; }
    void setShowDialogs (bool) {}

    static constexpr int kDesignWidth  = 900;
    static constexpr int kDesignHeight = 600;

    void paint (juce::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    // Test hooks.
    void selectProgramForTest (int program);
    void setDrumModeForTest (bool drums);
    int getSelectedProgramForTest() const noexcept { return selectedProgram; }
    int getProgramCountForTest() const noexcept { return (int) visiblePrograms.size(); }
    juce::Rectangle<int> getProgramListBoundsForTest() const { return programList.getBounds(); }
    juce::Rectangle<int> getDrumButtonBoundsForTest() const { return drumButton.getBounds(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    InstrumentTrack* currentTrack() const;
    void refresh();
    void rebuildProgramList();
    void applyProgram (int program);
    void applyEdit (const juce::String& name);
    int polyphonyToChoice (int voices) const;
    int choiceToPolyphony (int choice) const;

    ProjectDocument& document;
    AudioEngine& audio;
    PresetStore& presets;
    juce::int64 trackId = 0;
    bool updating = false;

    juce::Label titleLabel, fontLabel, programLabel, hintLabel;
    juce::ComboBox categoryBox, polyphonyBox, bendBox;
    juce::ListBox programList;
    juce::ToggleButton drumButton { NI_TXT ("GM 鼓组") };
    juce::Slider volumeSlider, panSlider;
    juce::TextButton auditionButton { NI_TXT ("试听") };
    juce::Label volumeLabel, panLabel, polyphonyLabel, bendLabel;

    std::vector<int> visiblePrograms;
    int selectedProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GmEditor)
};

} // namespace hmdaw
