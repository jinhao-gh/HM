#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The right hand column of the editor: the reference control panel.

    It lists one row per "参考线状态" - the four kinds of reference line (hand-typed
    hertz, the selected note's ratio, applied reference groups, and a multi-note
    selection) - each with lock / show / solo / 编辑 / delete, and the rows can be
    dragged into a different order.

    编辑 only means something for a 参考线组 row: it opens the group editor on that
    entry. The other three kinds are not editable, so their button is greyed out.
*/
class OptionsPanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    OptionsPanel (ProjectDocument& document, AppState& appState);
    ~OptionsPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void refresh();

    /** The row-level setVisible(long, bool) below would otherwise hide the plain
        Component one, which the editor needs to fold this column away. */
    using juce::Component::setVisible;

    /** False in the offscreen self test, where a modal dialog would block the run. */
    bool showDialogs = true;

    /** Creates a 1st-kind entry. The + button and the self test both come through
        here, so the dialog only decides the numbers. Returns the new entry's id. */
    juce::int64 addFixedFrequency (double hz, const juce::String& name);

    /** Moves the panel selection by delta rows and scrolls it into view. */
    void moveSelection (int delta);

    /** Locks or unlocks the selected entry (the "n" shortcut). */
    void toggleSelectedLock();

    /** Diagnostic hooks for the offscreen self test. */
    juce::Rectangle<int> getRowBounds (int index) const;
    int getRowCount() const;
    juce::int64 getSelectedLineId() const { return appState.selectedReferenceLineId; }

    /** The 指示线 row at the bottom of the panel: it turns the ratio arrow drawn
        on hover on and off. */
    void triggerIndicator();
    bool isIndicatorVisible() const { return document.getProject().view.ratioArrowVisible; }
    juce::Rectangle<int> getIndicatorRowBounds() const;
    juce::String getIndicatorButtonText() const { return indicatorButton.getButtonText(); }
    /** The second line of the row: 已显示 / 已隐藏. Same string the painter uses. */
    juce::String getIndicatorStateTextForTest() const { return indicatorStateText(); }

    /** Presses one of a row's buttons the way a click would.
        which: 0 lock, 1 show/hide, 2 solo, 3 edit, 4 delete. */
    void triggerRowButton (int rowIndex, int which);
    juce::int64 getRowLineId (int index) const;
    juce::String getRowButtonText (int rowIndex, int which) const;
    /** True when that row's 编辑 button is live (a 参考线组 entry). */
    bool isRowEditEnabled (int rowIndex) const;
    /** The type line of a row: 赫兹 / 选中音符频率-选中比率 / 参考线组 / 选中多个音符. */
    juce::String getRowTypeText (int rowIndex) const;

    /** 编辑 was pressed on a 参考线组 row: open the group editor on that entry. */
    std::function<void (juce::int64 lineId)> onEditLineGroup;

    static constexpr int rowHeight         = 54;
    /** Same height as a status row: the 指示线 row is drawn in that style. */
    static constexpr int indicatorRowHeight = rowHeight;

private:
    class EntryRow;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void rebuildRows();
    int  indexOfLine (juce::int64 lineId) const;
    void showAddDialog();
    void removeLine (juce::int64 lineId);
    void setVisible (juce::int64 lineId, bool shouldShow);
    void setLocked (juce::int64 lineId, bool shouldLock);
    void toggleSolo (juce::int64 lineId);
    void toggleIndicator();
    /** 已显示 / 已隐藏 - the second line of the 指示线 row. */
    juce::String indicatorStateText() const;
    void paintIndicatorRow (juce::Graphics& g);
    /** The row's type line, name line and button strip, in this panel's coordinates. */
    void getIndicatorBands (juce::Rectangle<int>& typeLine,
                            juce::Rectangle<int>& nameLine,
                            juce::Rectangle<int>& buttonStrip) const;

    /** Drag reordering: live drop indicator, one undo when the mouse is released. */
    void beginRowDrag (juce::int64 lineId);
    void updateRowDrag (juce::int64 lineId, int mouseYInHolder);
    void endRowDrag();

    friend class EntryRow;

    ProjectDocument& document;
    AppState& appState;

    juce::Label titleLabel;
    juce::TextButton addButton;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<EntryRow> rows;
    juce::TextButton indicatorButton;
    juce::Rectangle<int> indicatorBounds;
    juce::Label hint;

    juce::int64 dragLineId = 0;
    int dragSourceIndex = -1;
    int dropTargetIndex = -1;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsPanel)
};

} // namespace hmdaw
