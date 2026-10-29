#include "PresetBar.h"
#include "../Core/Types.h"
#include "Theme.h"

#include <algorithm>

namespace nidaw
{

namespace
{
    constexpr int kButtonGap = 4;
}

PresetBar::PresetBar (const juce::String& builtInSectionName)
    : builtInName (builtInSectionName)
{
    addAndMakeVisible (label);
    label.setText (NI_TXT ("预设"), juce::dontSendNotification);
    label.setFont (theme::font (11.0f));
    label.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (categoryCombo);
    categoryCombo.setTextWhenNothingSelected (NI_TXT ("全部类别"));
    categoryCombo.onChange = [this]
    {
        if (updating)
            return;

        const auto id = categoryCombo.getSelectedId();
        selectedCategory = id <= 1 ? juce::String() : categoryCombo.getItemText (id);
        refresh();
    };

    addAndMakeVisible (combo);
    combo.onChange = [this]
    {
        if (updating)
            return;

        const auto id = combo.getSelectedId();

        if (id != 0)
            selectEntryById (id, true);
    };

    addChildComponent (menuButton);
    menuButton.setTooltip (NI_TXT ("按类别选择音色"));
    menuButton.onClick = [this] { showPresetMenu(); };

    auto setupButton = [this] (juce::TextButton& button, const juce::String& text)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
        return &button;
    };

    setupButton (prevButton, "<");
    prevButton.setTooltip (NI_TXT ("上一个预设"));
    prevButton.onClick = [this] { step (-1); };

    setupButton (nextButton, ">");
    nextButton.setTooltip (NI_TXT ("下一个预设"));
    nextButton.onClick = [this] { step (1); };

    setupButton (saveButton, NI_TXT ("保存为预设…"));
    saveButton.onClick = [this] { showSaveDialog(); };

    setupButton (renameButton, NI_TXT ("重命名"));
    renameButton.onClick = [this] { showRenameDialog(); };

    setupButton (deleteButton, NI_TXT ("删除"));
    deleteButton.onClick = [this] { confirmDelete(); };

    updateButtonStates();
}

PresetBar::~PresetBar() = default;

const PresetBar::Entry* PresetBar::findEntry (int id) const
{
    for (const auto& entry : filteredEntries)
        if (entry.id == id)
            return &entry;

    return nullptr;
}

int PresetBar::currentId() const
{
    return selection;
}

juce::String PresetBar::groupOf (const Entry& entry) const
{
    if (entry.category.isNotEmpty())
        return entry.category;

    return entry.isUser ? NI_TXT ("我的") : builtInName;
}

void PresetBar::selectEntryById (int id, bool notify)
{
    selection = id;

    if (const auto* entry = findEntry (id))
        menuButton.setButtonText (entry->name);

    combo.setSelectedId (id, juce::dontSendNotification);
    updateButtonStates();

    if (notify && loadPreset != nullptr)
        loadPreset (id);
}

void PresetBar::updateButtonStates()
{
    const auto* entry = findEntry (selection);
    const auto isUser = entry != nullptr && entry->isUser;

    renameButton.setEnabled (isUser);
    deleteButton.setEnabled (isUser);
}

void PresetBar::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    entries = getEntries != nullptr ? getEntries() : std::vector<Entry> {};

    categories.clear();

    for (const auto& entry : entries)
        if (entry.category.isNotEmpty() && ! categories.contains (entry.category))
            categories.add (entry.category);

    // Two or more categories are shown as a two-level menu; a single-category
    // instrument (sampler, drums, granular, ...) keeps the flat combo it had.
    menuMode = categories.size() > 1;

    const auto categoryWasVisible = categoryCombo.isVisible();
    categoryCombo.setVisible (! menuMode && ! categories.isEmpty());
    combo.setVisible (! menuMode);
    menuButton.setVisible (menuMode);

    if (! menuMode)
    {
        categoryCombo.clear (juce::dontSendNotification);

        if (! categories.isEmpty())
        {
            categoryCombo.addItem (NI_TXT ("全部"), 1);

            for (int i = 0; i < categories.size(); ++i)
                categoryCombo.addItem (categories[i], i + 2);

            if (selectedCategory.isNotEmpty() && ! categories.contains (selectedCategory))
                selectedCategory.clear();

            auto categoryId = 1;

            for (int i = 0; i < categories.size(); ++i)
                if (categories[i] == selectedCategory)
                    categoryId = i + 2;

            categoryCombo.setSelectedId (categoryId, juce::dontSendNotification);
        }
        else
        {
            selectedCategory.clear();
        }
    }
    else
    {
        selectedCategory.clear();
    }

    if (categoryWasVisible != categoryCombo.isVisible())
        resized();

    rebuildVisibleEntries();

    const auto hasVisible = ! filteredEntries.empty();

    if (! menuMode)
    {
        combo.clear (juce::dontSendNotification);
        combo.setTextWhenNothingSelected (hasVisible ? juce::String() : NI_TXT ("（没有预设）"));

        bool builtInHeadingAdded = false;
        bool userHeadingAdded = false;

        for (const auto& entry : filteredEntries)
        {
            if (entry.isUser)
            {
                if (! userHeadingAdded)
                {
                    combo.addSectionHeading (NI_TXT ("我的"));
                    userHeadingAdded = true;
                }
            }
            else if (! builtInHeadingAdded)
            {
                combo.addSectionHeading (builtInName);
                builtInHeadingAdded = true;
            }

            combo.addItem (entry.name, entry.id);
        }
    }

    // A sound that was saved by the user is highlighted in preference to a
    // built-in that happens to share its name, otherwise the user's own patch
    // would always look like it came from the factory bank.
    const auto currentName = getCurrentName != nullptr ? getCurrentName() : juce::String();
    int chosen = 0;

    for (const auto& entry : filteredEntries)
        if (entry.isUser && entry.name == currentName)
        {
            chosen = entry.id;
            break;
        }

    if (chosen == 0)
        for (const auto& entry : filteredEntries)
            if (! entry.isUser && entry.name == currentName)
            {
                chosen = entry.id;
                break;
            }

    selection = chosen;

    if (! menuMode)
        combo.setSelectedId (chosen, juce::dontSendNotification);

    menuButton.setButtonText (chosen != 0 && findEntry (chosen) != nullptr
                                  ? findEntry (chosen)->name
                                  : NI_TXT ("（没有预设）"));

    updateButtonStates();
    resized();
}

void PresetBar::rebuildVisibleEntries()
{
    filteredEntries.clear();

    for (const auto& entry : entries)
        if (menuMode || selectedCategory.isEmpty() || entry.category == selectedCategory)
            filteredEntries.push_back (entry);
}

int PresetBar::getVisibleEntryIdForTest (int index) const
{
    return index >= 0 && index < (int) filteredEntries.size()
               ? filteredEntries[(size_t) index].id
               : -1;
}

bool PresetBar::selectCategoryForTest (const juce::String& category)
{
    if (menuMode)
        return false;   // the menu has no separate category filter

    if (category.isNotEmpty() && ! categories.contains (category))
        return false;

    selectedCategory = category;
    refresh();
    return true;
}

juce::String PresetBar::describeMenuForTest() const
{
    juce::StringArray order;
    std::vector<int> counts;

    for (const auto& entry : entries)
    {
        const auto group = groupOf (entry);
        const auto index = order.indexOf (group);

        if (index < 0)
        {
            order.add (group);
            counts.push_back (1);
        }
        else
        {
            ++counts[(size_t) index];
        }
    }

    juce::String text;

    for (int i = 0; i < order.size(); ++i)
        text << order[i] << "=" << counts[(size_t) i] << "\n";

    return text;
}

void PresetBar::showPresetMenu()
{
    juce::StringArray order;

    for (const auto& entry : entries)
    {
        const auto group = groupOf (entry);

        if (! order.contains (group))
            order.add (group);
    }

    juce::PopupMenu menu;

    for (const auto& group : order)
    {
        juce::PopupMenu sub;

        for (const auto& entry : entries)
            if (groupOf (entry) == group)
                sub.addItem (entry.id, entry.name, true, entry.id == currentId());

        menu.addSubMenu (group, sub, true);
    }

    juce::Component::SafePointer<PresetBar> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->selectEntryById (result, true);
                        });
}

void PresetBar::step (int delta)
{
    if (filteredEntries.empty())
        return;

    auto index = -1;

    for (int i = 0; i < (int) filteredEntries.size(); ++i)
        if (filteredEntries[(size_t) i].id == currentId())
            index = i;

    index = index < 0 ? 0 : index;
    const auto count = (int) filteredEntries.size();
    const auto next = ((index + delta) % count + count) % count;

    selectEntryById (filteredEntries[(size_t) next].id, true);
}

void PresetBar::requestSave (const juce::String& name)
{
    const auto trimmed = name.trim();

    if (trimmed.isEmpty())
        return;

    const auto exists = userPresetExists != nullptr && userPresetExists (trimmed);

    if (exists)
    {
        if (! showDialogs)
            return;

        const auto confirmed = juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            NI_TXT ("覆盖预设"),
            NI_TXT ("已存在同名预设「") + trimmed + NI_TXT ("」，是否覆盖？"),
            NI_TXT ("覆盖"), NI_TXT ("取消"), this, nullptr);

        if (! confirmed)
        {
            // Cancelling sends the user back to the name prompt rather than
            // silently dropping the save.
            showSaveDialog();
            return;
        }
    }

    if (savePreset != nullptr)
        savePreset (trimmed, exists);

    refresh();
}

void PresetBar::showSaveDialog()
{
    if (! showDialogs)
        return;

    if (getCurrentName == nullptr)
        return;

    const auto suggested = getCurrentName();

    auto* window = new juce::AlertWindow (NI_TXT ("保存为预设"),
                                          NI_TXT ("为当前音色取一个名字："),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", suggested, NI_TXT ("预设名称"));
    window->addButton (NI_TXT ("保存"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            if (result == 1)
                requestSave (window->getTextEditorContents ("name"));
        }), true);
}

void PresetBar::showRenameDialog()
{
    const auto* entry = findEntry (currentId());

    if (entry == nullptr || ! entry->isUser || ! showDialogs)
        return;

    const auto id = entry->id;

    auto* window = new juce::AlertWindow (NI_TXT ("重命名预设"),
                                          NI_TXT ("新的名称："),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", entry->name, NI_TXT ("预设名称"));
    window->addButton (NI_TXT ("确定"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, id] (int result)
        {
            if (result != 1)
                return;

            const auto newName = window->getTextEditorContents ("name").trim();

            if (newName.isEmpty())
                return;

            if (renamePreset != nullptr)
                renamePreset (id, newName);

            refresh();
        }), true);
}

void PresetBar::confirmDelete()
{
    const auto* entry = findEntry (currentId());

    if (entry == nullptr || ! entry->isUser)
        return;

    const auto id = entry->id;
    const auto name = entry->name;
    int visibleIndex = -1;

    for (int i = 0; i < (int) filteredEntries.size(); ++i)
        if (filteredEntries[(size_t) i].id == id)
            visibleIndex = i;

    if (showDialogs)
    {
        const auto confirmed = juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon,
            NI_TXT ("删除预设"),
            NI_TXT ("确定删除预设「") + name + NI_TXT ("」吗？"),
            NI_TXT ("删除"), NI_TXT ("取消"), this, nullptr);

        if (! confirmed)
            return;
    }

    if (deletePreset != nullptr)
        deletePreset (id);

    refresh();

    // Land on the entry before the one that was removed *inside the visible
    // category*. Using the merged list here used to jump from a user preset to
    // the last factory sound, because the factory bank is in front of it.
    if (! filteredEntries.empty())
    {
        const auto next = juce::jlimit (0, (int) filteredEntries.size() - 1,
                                        visibleIndex < 0 ? 0 : visibleIndex - 1);
        selectEntryById (filteredEntries[(size_t) next].id, true);
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
}

void PresetBar::resized()
{
    auto row = getLocalBounds();

    label.setBounds (row.removeFromLeft (34));

    if (! menuMode && categoryCombo.isVisible())
    {
        categoryCombo.setBounds (row.removeFromLeft (116).reduced (2, 1));
        row.removeFromLeft (kButtonGap);
    }

    deleteButton.setBounds (row.removeFromRight (54).reduced (kButtonGap / 2, 0));
    row.removeFromRight (kButtonGap);
    renameButton.setBounds (row.removeFromRight (62).reduced (kButtonGap / 2, 0));
    row.removeFromRight (kButtonGap);
    saveButton.setBounds (row.removeFromRight (108).reduced (kButtonGap / 2, 0));
    row.removeFromRight (kButtonGap);
    nextButton.setBounds (row.removeFromRight (26).reduced (kButtonGap / 2, 0));
    row.removeFromRight (kButtonGap);
    prevButton.setBounds (row.removeFromRight (26).reduced (kButtonGap / 2, 0));
    row.removeFromRight (kButtonGap);

    if (menuMode)
        menuButton.setBounds (row.reduced (0, 1));
    else
        combo.setBounds (row.reduced (0, 1));
}

} // namespace nidaw
