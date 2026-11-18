#pragma once

#include "../Audio/PluginHost.h"
#include "../Core/EffectChain.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The strip down the right hand side of the arrangement area: the insert chain of
    the selected track.

    The chain is pre-fader (the instrument goes through these, then through the
    track's volume and pan), and it lives in the project, so it is there again when
    the project is opened. Each row can be bypassed, moved up or down, opened in the
    plugin's own window, or removed; the row also shows the latency the plugin
    reports (HM does not compensate for it).
*/
class EffectsBar : public juce::Component,
                   private juce::ChangeListener
{
public:
    EffectsBar (ProjectDocument& document, AppState& appState, PluginHostManager& pluginHost);
    ~EffectsBar() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** Pins the bar to one track (the effects window's track); 0 follows the
        selection, which is what the bar did when it lived in the editor's layout. */
    void setTrack (juce::int64 trackId);

    /** How tall the bar wants to be: its header, its rows and the add button. */
    int getPreferredHeight() const;

    /** Set by MainComponent: asks which effect to add to this track. */
    std::function<void (juce::int64 trackId)> onAddEffect;
    /** Set by MainComponent: opens this slot's plugin window (or the picker again
        when the plugin is missing). */
    std::function<void (juce::int64 trackId, int slotIndex)> onEditEffect;

    static constexpr int panelWidth = 220;
    static constexpr int rowHeight  = 30;
    static constexpr int headerHeight = 22;
    static constexpr int addButtonHeight = 26;

    /** Adds a slot for this plugin, exactly as picking it in the picker would. */
    void addEffect (const juce::PluginDescription& description);

    // ---- self test hooks ----
    int getRowCountForTest() const { return rows.size(); }
    juce::Rectangle<int> getRowBoundsForTest (int index) const;
    /** "名字 · 延迟 3 ms"，和行上画的一样。 */
    juce::String getRowTextForTest (int index) const;
    bool isBypassOnForTest (int index) const;
    juce::String getHeaderTextForTest() const { return headerText(); }
    juce::Rectangle<int> getAddButtonBoundsForTest() const { return addButton.getBounds(); }
    void pressBypassForTest (int index);
    void pressUpForTest (int index);
    void pressDownForTest (int index);
    void pressDeleteForTest (int index);
    void pressAddForTest() { addButton.onClick(); }

private:
    class Row;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    const InstrumentTrack* selectedTrack() const;
    /** The line at the top: 效果器 · 轨道名 (N). */
    juce::String headerText() const;
    void toggleBypass (int index);
    void moveSlot (int index, int delta);
    void removeSlot (int index);
    void editSlot (int index);

    ProjectDocument& document;
    AppState& appState;
    PluginHostManager& host;
    /** 0 = follow the selection; otherwise the track this bar was opened for. */
    juce::int64 pinnedTrackId = 0;

    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;
    juce::TextButton addButton;
    juce::Label emptyLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsBar)
};

} // namespace hmdaw
