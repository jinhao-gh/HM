#pragma once

#include "../Core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

class TopToolbar : public juce::Component,
                   private juce::ChangeListener
{
public:
    explicit TopToolbar (ProjectDocument& document);
    ~TopToolbar() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();

    /** Diagnostic hook: the title strip, e.g. "未命名 · HM". */
    juce::String getTitleTextForTest() const { return titleLabel.getText(); }

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onSaveProjectAs;
    std::function<void()> onExportWav;
    /** 混音台: the mixer window (also on F3). */
    std::function<void()> onOpenMixer;
    /** MIDI 输入: the mapping window. */
    std::function<void()> onOpenMidiInput;
    /** 虚拟键盘: the on-screen piano (also on Alt+K). */
    std::function<void()> onOpenVirtualKeyboard;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    ProjectDocument& document;

    juce::TextButton newButton, openButton, saveButton, saveAsButton, exportButton;
    juce::TextButton mixerButton, midiButton, keyboardButton;
    juce::Label titleLabel;
    juce::Label modifiedLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopToolbar)
};

} // namespace hmdaw
