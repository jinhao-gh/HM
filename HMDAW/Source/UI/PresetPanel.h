#pragma once

#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The beside panel opened by the 历史 / 预设 buttons.

    In history mode it lists the reference line groups built this session; in
    preset mode it shows the user level reference group and reference line group
    preset libraries, with rename, delete and use.
*/
class PresetPanel : public juce::Component,
                    private juce::ChangeListener
{
public:
    PresetPanel (ProjectDocument& document, AppState& appState, PresetStore& presets);
    ~PresetPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** A group row's 编辑 button: open the group editor on that preset. */
    std::function<void (bool lineGroup, int index)> onEditPreset;

    /** Diagnostic hooks: what the list currently shows. */
    int getRowCountForTest() const { return rows.size(); }
    juce::String getRowTitleForTest (int index) const;
    /** The text on that row's 编辑 button (empty when the row has none). */
    juce::String getRowEditTextForTest (int index) const;
    /** Presses that row's 编辑 button the way a click would. */
    void triggerEditForTest (int index);

    static constexpr int defaultWidth = 210;
    static constexpr int minWidth = 190;
    static constexpr int rowHeight = 30;

private:
    class Row;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void rebuildRows();

    void useReferenceGroupPreset (int index);
    void useReferenceLineGroupPreset (int index);
    void enableHistoryEntry (int index);
    void saveHistoryEntryAsPreset (int index);
    void renamePreset (bool lineGroup, int index, const juce::String& newName);
    void removePreset (bool lineGroup, int index);

    bool showsHistory() const { return appState.sidePanel == AppState::SidePanel::history; }
    bool showsLineGroups() const { return appState.sidePanelShowsLineGroups; }

    ProjectDocument& document;
    AppState& appState;
    PresetStore& presets;

    juce::Label titleLabel;
    juce::TextButton referenceTab, lineGroupTab;
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
    juce::Label emptyLabel;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetPanel)
};

} // namespace hmdaw
