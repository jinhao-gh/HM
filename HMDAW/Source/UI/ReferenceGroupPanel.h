#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "GroupControls.h"
#include "OctaveLadder.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The always-visible reference group column on the left of the editor.

    Top row opens the history / preset beside panels, the middle is the octave
    segment itself, and the bottom row drives the two "pick a dot, then pick a
    note" flows.
*/
class ReferenceGroupPanel : public juce::Component,
                            private juce::ChangeListener
{
public:
    ReferenceGroupPanel (ProjectDocument& document, AppState& appState);
    ~ReferenceGroupPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    OctaveLadder& getLadder() noexcept { return ladder; }
    /** The colour / ratio / division block under the segment. */
    GroupControls& getControls() noexcept { return controls; }

    /** Diagnostic hook: whether the palette can currently be applied. The frame
        dots are colour-able too, so this is true whenever a single dot is selected. */
    bool isPaletteEnabled() const;

    /** Asked to save the current segment as a reference group preset. */
    std::function<void()> onSavePreset;

    static constexpr int panelWidth = 210;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateControlsFromState();

    ProjectDocument& document;
    AppState& appState;

    juce::TextButton historyButton, presetButton;
    OctaveLadder ladder;
    GroupControls controls;
    juce::TextButton referenceButton, savePresetButton, applyButton;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceGroupPanel)
};

} // namespace hmdaw
