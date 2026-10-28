#include "PresetPanel.h"
#include "Theme.h"

#include <functional>

namespace nidaw
{

class PresetPanel::Row : public juce::Component
{
public:
    Row (juce::String name, juce::String subtitle,
         juce::String primaryText, std::function<void()> primaryAction,
         juce::String secondaryText, std::function<void()> secondaryAction,
         std::function<void (const juce::String&)> renameAction,
         juce::String editText = {}, std::function<void()> editAction = {})
        : renameCallback (std::move (renameAction)), editCallback (std::move (editAction))
    {
        addAndMakeVisible (nameEditor);
        nameEditor.setText (name, juce::dontSendNotification);
        nameEditor.setFont (theme::font (12.0f));
        nameEditor.setSelectAllWhenFocused (true);
        nameEditor.setReadOnly (renameCallback == nullptr);

        if (renameCallback != nullptr)
        {
            nameEditor.onReturnKey = [this] { commitRename(); };
            nameEditor.onFocusLost = [this] { commitRename(); };
        }

        addAndMakeVisible (subtitleLabel);
        subtitleLabel.setText (subtitle, juce::dontSendNotification);
        subtitleLabel.setFont (theme::font (9.5f));
        subtitleLabel.setColour (juce::Label::textColourId, theme::dimText);

        if (primaryAction != nullptr)
        {
            addAndMakeVisible (primaryButton);
            primaryButton.setButtonText (primaryText);
            primaryButton.onClick = std::move (primaryAction);
        }

        if (secondaryAction != nullptr)
        {
            addAndMakeVisible (secondaryButton);
            secondaryButton.setButtonText (secondaryText);
            secondaryButton.onClick = std::move (secondaryAction);
        }

        if (editCallback != nullptr)
        {
            addAndMakeVisible (editButton);
            editButton.setButtonText (editText);
            editButton.setTooltip (NI_TXT ("打开编辑栏改这个组"));
            editButton.onClick = [this] { editCallback(); };
        }
    }

    /** Diagnostic hook: the row's title, as the panel draws it. */
    juce::String getTitleForTest() const { return nameEditor.getText(); }
    /** Diagnostic hook: presses 编辑 the way a click would. */
    void triggerEditForTest() { if (editCallback != nullptr) editCallback(); }
    /** Diagnostic hook: the 编辑 button's text (empty when the row has none). */
    juce::String getEditTextForTest() const
    {
        return editButton.isVisible() ? editButton.getButtonText() : juce::String();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panelRaised.withAlpha (0.5f));
        g.fillRect (getLocalBounds().reduced (2, 1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 3);

        // The three buttons sit in one block on the right: 编辑 / 使用 / 删除. The
        // name shares its line with them (so it gives up room), while the subtitle
        // runs the full width underneath.
        const int widths[]  = { 44, 52, 52 };   // edit / primary / secondary
        const bool shown[]  = { editButton.isVisible(), primaryButton.isVisible(),
                                secondaryButton.isVisible() };
        int blockWidth = 0;
        int visibleCount = 0;

        for (int i = 0; i < 3; ++i)
            if (shown[i])
            {
                blockWidth += widths[i];
                ++visibleCount;
            }

        blockWidth += juce::jmax (0, visibleCount - 1) * 3;

        auto right = area.removeFromRight (blockWidth);

        for (auto* button : { &secondaryButton, &primaryButton, &editButton })
            if (button->isVisible())
            {
                button->setBounds (right.removeFromRight (button == &editButton ? 44 : 52));
                right.removeFromRight (3);
            }

        // The subtitle keeps the full width: it is the line that carries the
        // frequency / dot count, which is the part worth reading in full.
        auto lines = getLocalBounds().reduced (4, 3);

        nameEditor.setBounds (lines.removeFromTop (14).withTrimmedRight (blockWidth));
        subtitleLabel.setBounds (lines);
    }

private:
    void commitRename()
    {
        if (renameCallback == nullptr)
            return;

        const auto text = nameEditor.getText().trim();

        if (text.isNotEmpty())
            renameCallback (text);
    }

    juce::TextEditor nameEditor;
    juce::Label subtitleLabel;
    juce::TextButton primaryButton, secondaryButton, editButton;
    std::function<void (const juce::String&)> renameCallback;
    std::function<void()> editCallback;
};

//==============================================================================
PresetPanel::PresetPanel (ProjectDocument& documentToUse, AppState& appStateToUse,
                          PresetStore& presetsToUse)
    : document (documentToUse), appState (appStateToUse), presets (presetsToUse)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (referenceTab);
    referenceTab.setButtonText (NI_TXT ("参考组预设"));
    referenceTab.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    referenceTab.onClick = [this] { appState.setPresetTab (false); };

    addAndMakeVisible (lineGroupTab);
    lineGroupTab.setButtonText (NI_TXT ("参考线组预设"));
    lineGroupTab.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    lineGroupTab.onClick = [this] { appState.setPresetTab (true); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);

    addAndMakeVisible (emptyLabel);
    emptyLabel.setFont (theme::font (11.0f));
    emptyLabel.setColour (juce::Label::textColourId, theme::dimText);
    emptyLabel.setJustificationType (juce::Justification::centredTop);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    rebuildRows();
}

PresetPanel::~PresetPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void PresetPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PresetPanel::refresh()
{
    rebuildRows();
    resized();
}

juce::String PresetPanel::getRowTitleForTest (int index) const
{
    if (index < 0 || index >= rows.size())
        return {};

    return rows[index]->getTitleForTest();
}

void PresetPanel::triggerEditForTest (int index)
{
    if (index < 0 || index >= rows.size())
        return;

    rows[index]->triggerEditForTest();
}

juce::String PresetPanel::getRowEditTextForTest (int index) const
{
    if (index < 0 || index >= rows.size())
        return {};

    return rows[index]->getEditTextForTest();
}

void PresetPanel::rebuildRows()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    rows.clear();

    const auto history = showsHistory();

    titleLabel.setVisible (history);
    titleLabel.setText (NI_TXT ("历史（本次会话）"), juce::dontSendNotification);

    referenceTab.setVisible (! history);
    lineGroupTab.setVisible (! history);
    referenceTab.setToggleState (! showsLineGroups(), juce::dontSendNotification);
    lineGroupTab.setToggleState (showsLineGroups(), juce::dontSendNotification);

    const auto addRow = [this] (std::unique_ptr<Row> row)
    {
        auto* raw = rows.add (row.release());
        rowHolder.addAndMakeVisible (raw);
    };

    if (history)
    {
        for (int i = 0; i < (int) appState.referenceLineHistory.size(); ++i)
        {
            const auto& group = appState.referenceLineHistory[(size_t) i];
            const auto index = i;

            addRow (std::make_unique<Row> (
                group.name,
                formatHz (group.anchorHz) + NI_TXT (" · ") + juce::String ((int) group.dots.size())
                    + NI_TXT (" 个圆点"),
                NI_TXT ("启用"), [this, index] { enableHistoryEntry (index); },
                NI_TXT ("存为预设"), [this, index] { saveHistoryEntryAsPreset (index); },
                nullptr));
        }
    }
    else if (showsLineGroups())
    {
        const auto count = (int) presets.getReferenceLineGroups().size();

        for (int i = 0; i < count; ++i)
        {
            const auto& group = presets.getReferenceLineGroups()[(size_t) i];
            const auto index = i;

            addRow (std::make_unique<Row> (
                group.name,
                formatHz (group.anchorHz) + NI_TXT (" · ") + juce::String ((int) group.dots.size())
                    + NI_TXT (" 个圆点"),
                NI_TXT ("使用"), [this, index] { useReferenceLineGroupPreset (index); },
                NI_TXT ("删除"), [this, index] { removePreset (true, index); },
                [this, index] (const juce::String& name) { renamePreset (true, index, name); },
                NI_TXT ("编辑"), [this, index]
                {
                    if (onEditPreset != nullptr)
                        onEditPreset (true, index);
                }));
        }
    }
    else
    {
        const auto count = (int) presets.getReferenceGroups().size();

        for (int i = 0; i < count; ++i)
        {
            const auto& group = presets.getReferenceGroups()[(size_t) i];
            const auto index = i;

            addRow (std::make_unique<Row> (
                group.name,
                juce::String ((int) group.dots.size()) + NI_TXT (" 个圆点"),
                NI_TXT ("使用"), [this, index] { useReferenceGroupPreset (index); },
                NI_TXT ("删除"), [this, index] { removePreset (false, index); },
                [this, index] (const juce::String& name) { renamePreset (false, index, name); },
                NI_TXT ("编辑"), [this, index]
                {
                    if (onEditPreset != nullptr)
                        onEditPreset (false, index);
                }));
        }
    }

    const auto rowCount = rows.size();
    emptyLabel.setVisible (rowCount == 0);
    emptyLabel.setText (history ? NI_TXT ("还没有建立过参考线组")
                                : NI_TXT ("这里还没有预设"),
                        juce::dontSendNotification);

    resized();
}

void PresetPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PresetPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 5);

    if (titleLabel.isVisible())
        titleLabel.setBounds (area.removeFromTop (20));
    else
    {
        auto tabs = area.removeFromTop (22);
        referenceTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2 - 2));
        tabs.removeFromLeft (4);
        lineGroupTab.setBounds (tabs);
    }

    area.removeFromTop (4);

    emptyLabel.setBounds (area.removeFromTop (40));
    viewport.setBounds (area);

    const auto width = juce::jmax (60, viewport.getWidth()
                                        - (viewport.isVerticalScrollBarShown() ? 12 : 0));
    rowHolder.setSize (width, juce::jmax (area.getHeight(), rows.size() * rowHeight));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}

void PresetPanel::useReferenceGroupPreset (int index)
{
    const auto& list = presets.getReferenceGroups();

    if (index < 0 || index >= (int) list.size())
        return;

    const auto group = list[(size_t) index];

    document.applyEdit (NI_TXT ("使用参考组预设"), [group] (Project& project)
    {
        // Using a preset replaces whatever is on the segment.
        project.referenceGroup = group;
    });
}

void PresetPanel::useReferenceLineGroupPreset (int index)
{
    const auto& list = presets.getReferenceLineGroups();

    if (index < 0 || index >= (int) list.size())
        return;

    const auto group = list[(size_t) index];

    document.applyEdit (NI_TXT ("启用参考线组预设"), [group, this] (Project& project)
    {
        auto entry = music::makeGroupEntry (project.nextReferenceLineId++, group);
        music::clearUnlockedEntries (project.referenceLines, entry.id);
        const auto id = entry.id;
        project.referenceLines.push_back (std::move (entry));
        appState.selectReferenceLine (id);
    });

    appState.pushReferenceLineHistory (group);
}

void PresetPanel::enableHistoryEntry (int index)
{
    if (index < 0 || index >= (int) appState.referenceLineHistory.size())
        return;

    const auto group = appState.referenceLineHistory[(size_t) index];

    document.applyEdit (NI_TXT ("启用参考线组"), [group, this] (Project& project)
    {
        auto entry = music::makeGroupEntry (project.nextReferenceLineId++, group);
        music::clearUnlockedEntries (project.referenceLines, entry.id);
        const auto id = entry.id;
        project.referenceLines.push_back (std::move (entry));
        appState.selectReferenceLine (id);
    });
}

void PresetPanel::saveHistoryEntryAsPreset (int index)
{
    if (index < 0 || index >= (int) appState.referenceLineHistory.size())
        return;

    presets.addReferenceLineGroup (appState.referenceLineHistory[(size_t) index]);
    refresh();
}

void PresetPanel::renamePreset (bool lineGroup, int index, const juce::String& newName)
{
    if (updating)
        return;

    if (lineGroup)
        presets.renameReferenceLineGroup (index, newName);
    else
        presets.renameReferenceGroup (index, newName);
}

void PresetPanel::removePreset (bool lineGroup, int index)
{
    if (lineGroup)
        presets.removeReferenceLineGroup (index);
    else
        presets.removeReferenceGroup (index);

    refresh();
}

} // namespace nidaw
