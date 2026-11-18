#pragma once

#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "GroupControls.h"
#include "OctaveLadder.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The reference-group editor column: what the left column turns into when a
    group row says 编辑.

    It is the reference group column with a different top and bottom: the name box
    and 返回 at the top, 保存 / 另存为 at the bottom, and the same octave segment,
    status line, colours, ratio keys and division row in between. The two modes only
    differ in how the segment is drawn (see OctaveLadder::Look): a reference line
    group is one plain straight line with frequencies beside its dots, and shows
    the anchor it is pinned at; a reference group keeps the brighter frame octave
    and its ratios.

    The panel writes nothing itself. While the editor is open the project's
    reference group holds the working copy (that is what gives the live preview),
    and MainComponent owns the preset / project writes the three buttons ask for.
*/
class GroupEditorPanel : public juce::Component,
                         private juce::ChangeListener
{
public:
    GroupEditorPanel (ProjectDocument& document, AppState& appState, PresetStore& presets);
    ~GroupEditorPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    /** 1..5 pick a ratio while this column has the keyboard, exactly like the
        reference group column; the segment handles them (see OctaveLadder). */
    bool keyPressed (const juce::KeyPress& key) override;

    void refresh();

    OctaveLadder& getLadder() noexcept { return ladder; }
    GroupControls& getControls() noexcept { return controls; }

    /** 返回: drop whatever was changed and put the previous group back. */
    std::function<void()> onBack;
    /** 保存: write the working copy back where it came from. */
    std::function<void (const juce::String& name)> onSave;
    /** 另存为: keep the working copy as a new preset under the typed name. */
    std::function<void (const juce::String& name)> onSaveAs;

    /** Diagnostic hooks for the offscreen self test. */
    juce::String getNameForTest() const { return nameEditor.getText(); }
    juce::String getHeadingForTest() const { return headingLabel.getText(); }
    juce::String getAnchorTextForTest() const { return anchorLabel.getText(); }
    bool isAnchorRowVisibleForTest() const { return anchorLabel.isVisible(); }
    juce::Rectangle<int> getBackBoundsForTest() const { return backButton.getBounds(); }
    juce::Rectangle<int> getSaveBoundsForTest() const { return saveButton.getBounds(); }
    juce::Rectangle<int> getSaveAsBoundsForTest() const { return saveAsButton.getBounds(); }
    void triggerBackForTest();
    void triggerSaveForTest();
    void triggerSaveAsForTest();
    /** Types into the name box the way the keyboard would. */
    void setNameForTest (const juce::String& name);

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    /** Re-reads the session from AppState and puts the segment into the right look. */
    void applySession();

    ProjectDocument& document;
    AppState& appState;
    PresetStore& presets;

    juce::Label headingLabel;
    juce::TextEditor nameEditor;
    juce::TextButton backButton;
    juce::Label anchorLabel;
    OctaveLadder ladder;
    GroupControls controls;
    juce::TextButton saveButton, saveAsButton;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupEditorPanel)
};

} // namespace hmdaw
