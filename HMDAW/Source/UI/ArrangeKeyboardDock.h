#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>
#include <vector>

namespace hmdaw
{

/** The compact playable keyboard at the bottom of the arrangement's right rail. */
class ArrangeKeyboardDock : public juce::Component,
                            private juce::MidiKeyboardState::Listener
{
public:
    ArrangeKeyboardDock (ProjectDocument& document, AppState& appState);
    ~ArrangeKeyboardDock() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setKeysDown (const std::vector<int>& notes);
    void releaseAllNotes();

    std::function<void (int midiNote, float velocity, juce::int64 trigger)> onNoteOn;
    std::function<void (int midiNote, juce::int64 trigger)> onNoteOff;
    std::function<void()> onOpenMidiInput;
    std::function<void()> onOpenArp;

    static constexpr int kDesignWidth = 330;
    static constexpr int kDesignHeight = 280;

    // Test hooks.
    juce::Rectangle<int> getKeyboardBoundsForTest() const { return keyboard.getBounds(); }
    int getOctaveForTest() const noexcept { return octave; }
    void setOctaveForTest (int newOctave);
    void pressKeyForTest (int midiNote, float velocity = 100.0f);
    void releaseKeyForTest (int midiNote);

private:
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void applyRange();

    ProjectDocument& document;
    AppState& appState;
    juce::MidiKeyboardState state;
    juce::MidiKeyboardComponent keyboard;
    juce::Slider velocity;
    juce::TextButton octaveDown { "-" }, octaveUp { "+" };
    juce::TextButton midiButton { NI_TXT ("MIDI 输入") };
    juce::TextButton arpButton { NI_TXT ("琶音") };
    juce::Label titleLabel, octaveLabel;
    int octave = 3;
    juce::int64 triggerBase = 0x70000000;
    std::set<int> heldNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeKeyboardDock)
};

} // namespace hmdaw
