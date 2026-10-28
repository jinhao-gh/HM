#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "OctaveLadder.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** The block of controls that sits under an octave segment: the status line, the
    colour swatches, the ratio keys and the equal-division row.

    Both columns that carry a segment lay this out in the same place, so the
    always-visible 参考组栏 and the group editor really do offer the same controls.
    Each panel only owns its own top and bottom rows; everything between them is
    here, which is also why the two cannot drift apart.

    The column's own 1..5 shortcuts and the ratio buttons both come through
    triggerRatioButton(), so the keys and the buttons share one code path.
*/
class GroupControls : public juce::Component,
                      private juce::ChangeListener
{
public:
    GroupControls (ProjectDocument& document, AppState& appState, OctaveLadder& ladder);
    ~GroupControls() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Re-reads the selection and rewrites the status line and the button states. */
    void refresh();

    /** Diagnostic hook: whether the colour swatches can be applied right now.
        That is true whenever at least one dot is selected - the colour lands on
        every selected dot at once (the frame dots included). */
    bool isPaletteEnabled() const;

    /** Diagnostic hooks: the two enable rules. A ratio lens needs exactly one dot;
        equal division needs exactly two. */
    bool isRatioEnabledForTest() const;
    bool isDivisionEnabledForTest() const { return divisionButton.isEnabled(); }

    /** Presses ratio key `index` (0..4 = 1:2, 1:3, 1:5, 1:7, 1:9). The buttons and
        the 1..5 shortcuts both come through here. */
    void triggerRatioButton (int index);
    /** Presses one of the colour swatches the way a click would. */
    void triggerColourForTest (int index);

    /** Diagnostic hook: what the status line currently says. */
    juce::String getStatusText() const { return statusLabel.getText(); }

    /** The row bounds, for the offscreen self test. */
    juce::Rectangle<int> getRatioButtonBoundsForTest (int index) const;
    juce::Rectangle<int> getStatusBoundsForTest() const { return statusLabel.getBounds(); }

    /** The height the four rows want: status 16 + colour 20 + ratio 22 + division
        22 plus the three 3 px gaps between them. */
    static constexpr int preferredHeight = 89;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyRatio (double ratio);
    void applyPaletteColour (int index);
    void applyDivision();
    void updateControlsFromState();

    ProjectDocument& document;
    AppState& appState;
    OctaveLadder& ladder;

    juce::OwnedArray<juce::TextButton> colourButtons;
    juce::OwnedArray<juce::TextButton> ratioButtons;
    juce::TextEditor customRatioEditor;
    juce::TextEditor divisionEditor;
    juce::TextButton divisionButton;
    juce::Label statusLabel;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupControls)
};

} // namespace nidaw
