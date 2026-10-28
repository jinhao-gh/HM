#include "OptionsPanel.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    /** The type text on the first line of every reference line status row. */
    juce::String kindText (ReferenceLineKind kind)
    {
        switch (kind)
        {
            case ReferenceLineKind::fixedHz:   return NI_TXT ("赫兹");
            case ReferenceLineKind::noteRatio: return NI_TXT ("选中音符频率-选中比率");
            case ReferenceLineKind::group:     return NI_TXT ("参考线组");
            case ReferenceLineKind::multiNote: return NI_TXT ("选中多个音符");
        }

        return {};
    }

    juce::Colour roleColour (const ReferenceLineEntry& entry)
    {
        switch (entry.kind)
        {
            case ReferenceLineKind::fixedHz:   return theme::referenceLineManual;
            case ReferenceLineKind::noteRatio: return theme::referenceLineTarget;
            case ReferenceLineKind::group:     return theme::referenceLine;
            case ReferenceLineKind::multiNote: return theme::referenceLine;
        }

        return theme::referenceLine;
    }

    juce::Colour entryColour (const ReferenceLineEntry& entry)
    {
        if (entry.colourIndex >= 0)
            return theme::trackColour (entry.colourIndex);

        if (entry.kind == ReferenceLineKind::group && ! entry.dots.empty())
            return theme::trackColour (entry.dots.front().colourIndex);

        return roleColour (entry);
    }

    // The three bands of a status row - the type line, the name line and the
    // button strip - in the row's own coordinates. The 指示线 row at the bottom of
    // the panel reuses them with an offset, so the two really do look alike.
    constexpr int kRowInsetX       = 7;
    constexpr int kRowTypeTop      = 1;
    constexpr int kRowTypeHeight   = 16;
    constexpr int kRowNameTop      = 17;
    constexpr int kRowNameHeight   = 17;
    constexpr int kRowButtonTop    = 36;
    constexpr int kRowButtonHeight = 17;
}

//==============================================================================
/** One row: the type line, the name line, and the five buttons. */
class OptionsPanel::EntryRow : public juce::Component
{
public:
    EntryRow (OptionsPanel& ownerToUse, ProjectDocument& documentToUse,
              AppState& appStateToUse, juce::int64 lineIdToUse)
        : owner (ownerToUse), document (documentToUse), appState (appStateToUse), lineId (lineIdToUse)
    {
        addAndMakeVisible (lockButton);
        lockButton.setTooltip (NI_TXT ("锁定后不会被下一次新建清除，也不随选中音符改变（快捷键 n）"));
        lockButton.onClick = [this]
        {
            if (auto* entry = document.getProject().findReferenceLine (lineId))
                owner.setLocked (lineId, ! entry->locked);
        };

        addAndMakeVisible (visibleButton);
        visibleButton.setTooltip (NI_TXT ("隐藏后参考线不显示，也不能在上面写音符"));
        visibleButton.onClick = [this]
        {
            if (auto* entry = document.getProject().findReferenceLine (lineId))
                owner.setVisible (lineId, ! entry->visible);
        };

        addAndMakeVisible (soloButton);
        soloButton.setTooltip (NI_TXT ("只显示这一条参考线，其余强制隐藏；再点一次退出"));
        soloButton.onClick = [this] { owner.toggleSolo (lineId); };

        addAndMakeVisible (editButton);
        editButton.setButtonText (NI_TXT ("编辑"));
        editButton.setTooltip (NI_TXT ("打开参考线组编辑栏，改这条组的圆点 / 比率 / 颜色"));
        editButton.onClick = [this]
        {
            if (owner.onEditLineGroup != nullptr)
                owner.onEditLineGroup (lineId);
        };

        addAndMakeVisible (deleteButton);
        deleteButton.setButtonText (NI_TXT ("删除"));
        deleteButton.setTooltip (NI_TXT ("删除这条参考线状态"));
        deleteButton.onClick = [this] { owner.removeLine (lineId); };

        addChildComponent (nameEditor);
        nameEditor.setFont (theme::font (11.5f));
        nameEditor.setSelectAllWhenFocused (true);
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
    }

    juce::int64 getLineId() const noexcept { return lineId; }

    /** Diagnostic hook: 0 lock, 1 show/hide, 2 solo, 3 edit, 4 delete. */
    juce::TextButton* buttonAt (int which) const
    {
        switch (which)
        {
            case 0: return const_cast<juce::TextButton*> (&lockButton);
            case 1: return const_cast<juce::TextButton*> (&visibleButton);
            case 2: return const_cast<juce::TextButton*> (&soloButton);
            case 3: return const_cast<juce::TextButton*> (&editButton);
            case 4: return const_cast<juce::TextButton*> (&deleteButton);
            default: break;
        }

        return nullptr;
    }

    void refresh()
    {
        const auto* entry = document.getProject().findReferenceLine (lineId);

        if (entry == nullptr)
            return;

        lockButton.setButtonText (entry->locked ? NI_TXT ("解锁") : NI_TXT ("锁定"));
        lockButton.setToggleState (entry->locked, juce::dontSendNotification);
        visibleButton.setButtonText (entry->visible ? NI_TXT ("隐藏") : NI_TXT ("显示"));
        visibleButton.setToggleState (! entry->visible, juce::dontSendNotification);
        soloButton.setButtonText (NI_TXT ("独显"));
        soloButton.setToggleState (appState.soloReferenceLineId == lineId, juce::dontSendNotification);

        // Only a 参考线组 entry has anything for the editor to work on.
        editButton.setEnabled (entry->kind == ReferenceLineKind::group);

        if (! nameEditor.hasKeyboardFocus (true))
            nameEditor.setVisible (false);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto* entry = document.getProject().findReferenceLine (lineId);

        if (entry == nullptr)
            return;

        const auto selected = appState.selectedReferenceLineId == lineId;

        g.setColour (selected ? theme::panelRaised.brighter (0.10f)
                              : theme::panelRaised.withAlpha (0.45f));
        g.fillRect (getLocalBounds());

        // Left colour bar: the colour this entry's lines are drawn in.
        g.setColour (entryColour (*entry));
        g.fillRect (juce::Rectangle<int> (0, 0, 4, getHeight()));

        if (selected)
        {
            g.setColour (theme::accent);
            g.drawRect (getLocalBounds(), 1);
        }

        auto area = getLocalBounds().reduced (kRowInsetX, 1);

        g.setColour (theme::dimText);
        g.setFont (theme::font (10.0f));
        g.drawText (kindText (entry->kind)
                        + (entry->locked ? NI_TXT ("　[已锁定]") : juce::String()),
                    area.removeFromTop (kRowTypeHeight), juce::Justification::centredLeft, true);

        g.setColour (entry->visible ? theme::text : theme::dimText);
        g.setFont (theme::font (11.5f, selected));
        g.drawText (entry->name, area.removeFromTop (kRowNameHeight),
                    juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (kRowInsetX, 1);

        nameEditor.setBounds (area.withTop (kRowNameTop).withHeight (18));

        auto buttons = area.withTop (kRowButtonTop).withHeight (kRowButtonHeight);
        const auto width = juce::jmax (30, (buttons.getWidth() - 4 * 2) / 5);

        for (auto* button : { &lockButton, &visibleButton, &soloButton, &editButton, &deleteButton })
        {
            button->setBounds (buttons.removeFromLeft (width));
            buttons.removeFromLeft (2);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        appState.selectReferenceLine (lineId);
        owner.beginRowDrag (lineId);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        owner.updateRowDrag (lineId, getY() + e.getPosition().getY());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        owner.endRowDrag();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.getPosition().getY() > 34)
            return;   // only the name line renames

        const auto* entry = document.getProject().findReferenceLine (lineId);

        nameEditor.setVisible (true);
        nameEditor.setText (entry != nullptr ? entry->name : juce::String(), juce::dontSendNotification);
        nameEditor.grabKeyboardFocus();
    }

private:
    void commitName()
    {
        const auto text = nameEditor.getText().trim();
        const auto* entry = document.getProject().findReferenceLine (lineId);

        nameEditor.setVisible (false);

        if (entry == nullptr || text.isEmpty() || text == entry->name)
            return;

        document.applyEdit (NI_TXT ("重命名参考线状态"), [this, text] (Project& project)
        {
            if (auto* target = project.findReferenceLine (lineId))
                target->name = text;
        });
    }

    OptionsPanel& owner;
    ProjectDocument& document;
    AppState& appState;
    juce::int64 lineId;

    juce::TextButton lockButton, visibleButton, soloButton, editButton, deleteButton;
    juce::TextEditor nameEditor;
};

//==============================================================================
OptionsPanel::OptionsPanel (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse)
{
    titleLabel.setText (NI_TXT ("参考线控制栏"), juce::dontSendNotification);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (titleLabel);

    addButton.setButtonText ("+");
    addButton.setTooltip (NI_TXT ("输入频率，新建一条「赫兹」参考线"));
    addButton.onClick = [this] { showAddDialog(); };
    addAndMakeVisible (addButton);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);

    indicatorButton.setTooltip (NI_TXT ("鼠标停在目标频率参考线上时，是否画出比率箭头"));
    indicatorButton.onClick = [this] { toggleIndicator(); };
    addAndMakeVisible (indicatorButton);

    hint.setFont (theme::font (10.0f));
    hint.setColour (juce::Label::textColourId, theme::dimText);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setText (NI_TXT ("↑↓ 选择状态　n 锁定\n拖动可调整上下顺序\n未锁定的状态会在下一次新建参考线时清除"),
                  juce::dontSendNotification);
    addAndMakeVisible (hint);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    refresh();
}

OptionsPanel::~OptionsPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void OptionsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

int OptionsPanel::getRowCount() const
{
    return rows.size();
}

juce::Rectangle<int> OptionsPanel::getRowBounds (int index) const
{
    if (index < 0 || index >= rows.size())
        return {};

    return rows[index]->getBounds();
}

juce::int64 OptionsPanel::getRowLineId (int index) const
{
    if (index < 0 || index >= rows.size())
        return 0;

    return rows[index]->getLineId();
}

void OptionsPanel::triggerRowButton (int rowIndex, int which)
{
    if (rowIndex < 0 || rowIndex >= rows.size())
        return;

    if (auto* button = rows[rowIndex]->buttonAt (which))
        if (button->onClick != nullptr)
            button->onClick();
}

juce::String OptionsPanel::getRowButtonText (int rowIndex, int which) const
{
    if (rowIndex < 0 || rowIndex >= rows.size())
        return {};

    const auto* button = rows[rowIndex]->buttonAt (which);
    return button != nullptr ? button->getButtonText() : juce::String();
}

bool OptionsPanel::isRowEditEnabled (int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= rows.size())
        return false;

    const auto* button = rows[rowIndex]->buttonAt (3);
    return button != nullptr && button->isEnabled();
}

juce::String OptionsPanel::getRowTypeText (int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= rows.size())
        return {};

    const auto* entry = document.getProject().findReferenceLine (getRowLineId (rowIndex));

    return entry != nullptr ? kindText (entry->kind) : juce::String();
}

int OptionsPanel::indexOfLine (juce::int64 lineId) const
{
    const auto& lines = document.getProject().referenceLines;

    for (int i = 0; i < (int) lines.size(); ++i)
        if (lines[(size_t) i].id == lineId)
            return i;

    return -1;
}

void OptionsPanel::rebuildRows()
{
    const auto& lines = document.getProject().referenceLines;

    // Rebuilding on every refresh keeps the row order in step with the document,
    // which is what the drag reordering and the up/down keys both work on.
    rows.clear();

    for (const auto& entry : lines)
    {
        auto* row = rows.add (new EntryRow (*this, document, appState, entry.id));
        holder.addAndMakeVisible (row);
    }

    for (auto* row : rows)
        row->refresh();

    resized();
}

void OptionsPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    // The button says what pressing it will do, like the per-row show / hide ones.
    const auto arrowVisible = document.getProject().view.ratioArrowVisible;
    indicatorButton.setButtonText (arrowVisible ? NI_TXT ("隐藏") : NI_TXT ("显示"));
    indicatorButton.setToggleState (! arrowVisible, juce::dontSendNotification);

    rebuildRows();
}

void OptionsPanel::toggleIndicator()
{
    const auto shouldShow = ! document.getProject().view.ratioArrowVisible;

    document.applyEdit (NI_TXT ("切换指示线显示"), [shouldShow] (Project& project)
    {
        project.view.ratioArrowVisible = shouldShow;
    });

    refresh();
}

void OptionsPanel::triggerIndicator()
{
    toggleIndicator();
}

juce::String OptionsPanel::indicatorStateText() const
{
    return document.getProject().view.ratioArrowVisible ? NI_TXT ("已显示") : NI_TXT ("已隐藏");
}

void OptionsPanel::getIndicatorBands (juce::Rectangle<int>& typeLine,
                                      juce::Rectangle<int>& nameLine,
                                      juce::Rectangle<int>& buttonStrip) const
{
    // The status rows lay their three bands out from their own top-left corner;
    // this row lives inside the panel, so every band is offset by its own y.
    const auto top = indicatorBounds.getY();
    const auto x = indicatorBounds.getX() + kRowInsetX;
    const auto width = juce::jmax (0, indicatorBounds.getWidth() - 2 * kRowInsetX);

    typeLine    = { x, top + kRowTypeTop,    width, kRowTypeHeight };
    nameLine    = { x, top + kRowNameTop,    width, kRowNameHeight };
    buttonStrip = { x, top + kRowButtonTop,  width, kRowButtonHeight };
}

void OptionsPanel::paintIndicatorRow (juce::Graphics& g)
{
    if (indicatorBounds.isEmpty())
        return;

    // A hairline above it, the same divider the rest of the panel uses.
    g.setColour (theme::outline);
    g.drawHorizontalLine (indicatorBounds.getY() - 2, 8.0f, (float) getWidth() - 8.0f);

    // Unselected status-row background, with the colour bar the arrow is drawn in.
    g.setColour (theme::panelRaised.withAlpha (0.45f));
    g.fillRect (indicatorBounds);

    g.setColour (theme::accent);
    g.fillRect (juce::Rectangle<int> (indicatorBounds.getX(), indicatorBounds.getY(),
                                      4, indicatorBounds.getHeight()));

    juce::Rectangle<int> typeLine, nameLine, buttonStrip;
    getIndicatorBands (typeLine, nameLine, buttonStrip);

    const auto visible = document.getProject().view.ratioArrowVisible;

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.0f));
    g.drawText (NI_TXT ("指示线"), typeLine, juce::Justification::centredLeft, true);

    g.setColour (visible ? theme::text : theme::dimText);
    g.setFont (theme::font (11.5f));
    g.drawText (indicatorStateText(), nameLine, juce::Justification::centredLeft, true);
}

juce::Rectangle<int> OptionsPanel::getIndicatorRowBounds() const
{
    return indicatorBounds;
}

void OptionsPanel::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto titleRow = area.removeFromTop (22);
    addButton.setBounds (titleRow.removeFromRight (28));
    titleRow.removeFromRight (4);
    titleLabel.setBounds (titleRow);

    area.removeFromTop (4);

    const auto hintHeight = juce::jlimit (0, 46, area.getHeight() / 5);
    hint.setBounds (area.removeFromBottom (hintHeight));
    area.removeFromBottom (4);

    // 指示线 sits right under the status list, above the 操作提示, drawn as one
    // more status-row-looking band.
    indicatorBounds = area.removeFromBottom (indicatorRowHeight);
    area.removeFromBottom (4);

    juce::Rectangle<int> typeLine, nameLine, buttonStrip;
    getIndicatorBands (typeLine, nameLine, buttonStrip);

    // One button in the first slot, so it lines up with the rows above. Slightly
    // inset vertically so it matches the height of their buttons.
    const auto slotWidth = juce::jmax (30, (buttonStrip.getWidth() - 4 * 2) / 5);
    indicatorButton.setBounds (buttonStrip.removeFromLeft (slotWidth).reduced (0, 1));

    viewport.setBounds (area);

    const auto width = juce::jmax (80, area.getWidth()
                                          - (viewport.isVerticalScrollBarShown() ? 12 : 0));
    auto y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight + 2;
    }

    holder.setSize (width, juce::jmax (area.getHeight(), y));
}

juce::int64 OptionsPanel::addFixedFrequency (double hz, const juce::String& name)
{
    if (hz < kMinVisibleHz || hz > kMaxVisibleHz)
        return 0;

    juce::int64 newId = 0;

    document.applyEdit (NI_TXT ("添加参考线"), [hz, name, &newId] (Project& project)
    {
        auto entry = music::makeFixedHzEntry (project.nextReferenceLineId++, hz, name);
        newId = entry.id;

        // A new reference line clears whatever was still scratch.
        music::clearUnlockedEntries (project.referenceLines, entry.id);
        project.referenceLines.push_back (std::move (entry));
    });

    if (newId == 0)
        return 0;

    // Typing a frequency is an explicit request to work there.
    if (! appState.hasNoteSelection())
        document.viewState().centreHz = music::VerticalAxis::clampHz (hz);

    appState.setSoloReferenceLine (0);
    appState.selectReferenceLine (newId);
    refresh();
    return newId;
}

void OptionsPanel::showAddDialog()
{
    if (! showDialogs)
    {
        addFixedFrequency (440.0, {});
        return;
    }

    auto* window = new juce::AlertWindow (NI_TXT ("新建参考线"),
                                          NI_TXT ("输入频率（Hz）。未锁定的参考线会在下一次新建时被清除。"),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("hz", "440", NI_TXT ("频率 Hz"));
    window->addTextEditor ("name", {}, NI_TXT ("名称（可留空）"));
    window->addButton (NI_TXT ("确定"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            if (result != 1)
                return;

            const auto hz = window->getTextEditorContents ("hz").getDoubleValue();

            if (hz >= kMinVisibleHz && hz <= kMaxVisibleHz)
                addFixedFrequency (hz, window->getTextEditorContents ("name").trim());
        }), true);
}

void OptionsPanel::removeLine (juce::int64 lineId)
{
    document.applyEdit (NI_TXT ("删除参考线状态"), [lineId] (Project& project)
    {
        auto& lines = project.referenceLines;
        lines.erase (std::remove_if (lines.begin(), lines.end(),
                                     [lineId] (const ReferenceLineEntry& e) { return e.id == lineId; }),
                     lines.end());
    });

    if (appState.selectedReferenceLineId == lineId)
        appState.selectReferenceLine (0);

    if (appState.soloReferenceLineId == lineId)
        appState.setSoloReferenceLine (0);
}

void OptionsPanel::setVisible (juce::int64 lineId, bool shouldShow)
{
    document.applyEdit (NI_TXT ("切换参考线显示"), [lineId, shouldShow] (Project& project)
    {
        if (auto* entry = project.findReferenceLine (lineId))
            entry->visible = shouldShow;
    });
}

void OptionsPanel::setLocked (juce::int64 lineId, bool shouldLock)
{
    const auto selected = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                           appState.selectedNoteIds);
    const auto ratio = appState.currentRatio;

    document.applyEdit (NI_TXT ("锁定参考线状态"),
                        [lineId, shouldLock, selected, ratio] (Project& project)
    {
        auto* entry = project.findReferenceLine (lineId);

        if (entry == nullptr)
            return;

        // Sealing a derived entry freezes what it is showing right now, so it stops
        // following the note selection.
        if (shouldLock && ! entry->locked)
        {
            if (entry->kind == ReferenceLineKind::noteRatio && selected.size() == 1)
            {
                entry->frozenHz    = selected.front().frequencyHz;
                entry->frozenRatio = ratio;
                entry->name        = music::derivedEntryName (entry->kind, selected.front(),
                                                              entry->frozenRatio);
            }
            else if (entry->kind == ReferenceLineKind::multiNote && selected.size() >= 2)
            {
                entry->frozenNotes.clear();

                for (const auto& note : selected)
                    entry->frozenNotes.push_back (note.frequencyHz);

                entry->name = juce::String ((int) selected.size()) + NI_TXT (" 个音符");
            }
        }

        entry->locked = shouldLock;
    });
}

void OptionsPanel::toggleSolo (juce::int64 lineId)
{
    appState.setSoloReferenceLine (appState.soloReferenceLineId == lineId ? 0 : lineId);
}

void OptionsPanel::moveSelection (int delta)
{
    const auto& lines = document.getProject().referenceLines;

    if (lines.empty() || delta == 0)
        return;

    const auto current = indexOfLine (appState.selectedReferenceLineId);
    const auto next = current < 0 ? (delta > 0 ? 0 : (int) lines.size() - 1)
                                  : juce::jlimit (0, (int) lines.size() - 1, current + delta);

    appState.selectReferenceLine (lines[(size_t) next].id);

    if (auto* row = rows[next])
        viewport.setViewPosition (0, juce::jlimit (0, juce::jmax (0, holder.getHeight()
                                                                       - viewport.getHeight()),
                                                   row->getY() - 8));
}

void OptionsPanel::toggleSelectedLock()
{
    const auto* entry = document.getProject().findReferenceLine (appState.selectedReferenceLineId);

    if (entry == nullptr)
        return;

    setLocked (entry->id, ! entry->locked);
}

void OptionsPanel::beginRowDrag (juce::int64 lineId)
{
    dragLineId = lineId;
    dragSourceIndex = indexOfLine (lineId);
    dropTargetIndex = dragSourceIndex;
}

void OptionsPanel::updateRowDrag (juce::int64 lineId, int mouseYInHolder)
{
    if (dragLineId != lineId || dragSourceIndex < 0)
        return;

    const auto& lines = document.getProject().referenceLines;
    auto target = 0;

    for (int i = 0; i < (int) lines.size() && i < rows.size(); ++i)
        if (mouseYInHolder > rows[i]->getBounds().getCentreY())
            target = i + 1;

    dropTargetIndex = juce::jlimit (0, (int) lines.size(), target);
    repaint();
}

void OptionsPanel::endRowDrag()
{
    const auto source = dragSourceIndex;
    const auto target = dropTargetIndex;

    dragLineId = 0;
    dragSourceIndex = -1;
    dropTargetIndex = -1;

    if (source < 0 || target < 0)
        return;

    // Dropping on your own position (or the gap right after it) changes nothing.
    if (target == source || target == source + 1)
    {
        repaint();
        return;
    }

    document.applyEdit (NI_TXT ("排列参考线状态"), [source, target] (Project& project)
    {
        const auto to = target > source ? target - 1 : target;
        music::moveEntry (project.referenceLines, source, to);
    });
}

void OptionsPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    // Drawn before the empty-list hint below: the 指示线 row is always there, even
    // when there is not a single reference line state yet.
    paintIndicatorRow (g);

    if (rows.isEmpty())
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f));
        g.drawText (NI_TXT ("还没有参考线状态\n点上面的 ＋ 添加"),
                    viewport.getBounds().reduced (6), juce::Justification::centred);
        return;
    }

    // Drop indicator while a row is being dragged to a new position.
    if (dragSourceIndex >= 0 && dropTargetIndex >= 0)
    {
        const auto after = dropTargetIndex > dragSourceIndex;
        const auto index = juce::jlimit (0, rows.size() - 1, after ? dropTargetIndex - 1
                                                                   : dropTargetIndex);
        const auto bounds = rows[index]->getBounds();
        const auto y = after ? bounds.getBottom() + 1 : bounds.getY() - 1;

        g.setColour (theme::accent);
        g.fillRect (viewport.getX(), viewport.getY() + y - viewport.getViewPositionY(),
                    viewport.getWidth(), 2);
    }
}

} // namespace nidaw
