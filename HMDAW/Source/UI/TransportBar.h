#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The transport strip along the bottom: play, stop, return to start, readouts. */
class TransportBar : public juce::Component
{
public:
    TransportBar (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine);
    ~TransportBar() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    std::function<void()> onOpenMidiInput;
    std::function<void()> onOpenVirtualKeyboard;
    void showAudioSettings();

    /** Diagnostics for the offscreen self test. */
    juce::Rectangle<int> getLoopButtonBoundsForTest() const { return loopButton.getBounds(); }
    juce::Rectangle<int> getMetronomeButtonBoundsForTest() const { return metronomeButton.getBounds(); }
    juce::String getLoopTextForTest() const { return loopLabel.getText(); }
    void triggerLoopButtonForTest() { toggleLoop(); }
    void triggerMetronomeButtonForTest() { toggleMetronome(); }
    juce::Rectangle<int> getRecordButtonBoundsForTest() const { return recordButton.getBounds(); }
    juce::Rectangle<int> getRecordTargetBoundsForTest() const { return recordTargetButton.getBounds(); }
    juce::String getRecordTextForTest() const { return recordButton.getButtonText(); }
    juce::String getRecordTargetTextForTest() const { return recordTargetButton.getButtonText(); }
    void triggerRecordButtonForTest();
    void triggerRecordTargetForTest();

private:
    void toggleLoop();
    void toggleMetronome();

    ProjectDocument& document;
    AppState& appState;
    AudioEngine& audio;

    juce::TextButton returnButton, playButton, stopButton, settingsButton;
    juce::TextButton midiInputButton, virtualKeyboardButton;
    juce::TextButton loopButton, metronomeButton;
    /** ● 录制: arms MIDI recording (the `*` key does the same). */
    juce::TextButton recordButton;
    /** 片段内 / 片段外: where a recorded note lands. */
    juce::TextButton recordTargetButton;
    juce::Label positionLabel, positionCaption;
    juce::Label tempoLabel, tempoCaption;
    juce::Label loopLabel, loopCaption;
    juce::Label deviceLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace hmdaw
