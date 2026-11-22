#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

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
    void setWorkspace (AppState::Workspace workspace);

    /** Diagnostic hook: the title strip, e.g. "未命名 · HM". */
    juce::String getTitleTextForTest() const { return titleLabel.getText(); }

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onSaveProject;
    std::function<void()> onSaveProjectAs;
    std::function<void()> onExportWav;
    std::function<void (AppState::Workspace)> onWorkspaceChanged;

    /** MIDI 输入窗口仍由 MainComponent 提供（编排右栏的入口会调用它）。 */
    std::function<void()> onOpenMidiInput;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void showFileMenu();
    void selectWorkspace (AppState::Workspace workspace);

    ProjectDocument& document;
    AppState::Workspace currentWorkspace = AppState::Workspace::arrange;

    juce::TextButton fileButton;
    juce::TextButton studioButton, arrangeButton, routingButton, mixerButton;
    juce::Label titleLabel;
    juce::Label modifiedLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopToolbar)
};

} // namespace hmdaw
