#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hmdaw
{

/** Entry ids: the built-in bank uses 1..1000, the user's own presets start at
    1001 so a single ComboBox can hold both groups without collisions. */
constexpr int kBuiltInPresetIdBase = 1;
constexpr int kUserPresetIdBase    = 1001;

/** The "预设" row shared by the instrument editors.

    With one category (or none) it is the flat list it has always been. With two
    or more categories it becomes a two-level menu: clicking the name opens the
    categories, and hovering or clicking one of them opens that category's sounds.
*/
class PresetBar : public juce::Component
{
public:
    struct Entry
    {
        int          id = 0;         // 1..N built-ins, 1001+ user presets
        juce::String name;
        bool         isUser = false;
        juce::String category;
    };

    explicit PresetBar (const juce::String& builtInSectionName);
    ~PresetBar() override;

    /** Where the merged list comes from, asked for on every refresh. */
    std::function<std::vector<Entry>()> getEntries;
    /** The name of the sound currently loaded, used for the highlight. */
    std::function<juce::String()> getCurrentName;

    std::function<bool (int id)> loadPreset;
    std::function<bool (const juce::String& name, bool overwrite)> savePreset;
    std::function<bool (int id, const juce::String& newName)> renamePreset;
    std::function<bool (int id)> deletePreset;
    std::function<bool (const juce::String& name)> userPresetExists;

    /** False in the offscreen self test, where a modal window would block. */
    bool showDialogs = true;

    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Diagnostic hooks. */
    juce::Rectangle<int> getComboBoxBoundsForTest() const
    {
        return menuMode ? menuButton.getBounds() : combo.getBounds();
    }
    int  getSelectedIdForTest() const { return currentId(); }
    bool canRenameForTest() const { return renameButton.isEnabled(); }
    bool canDeleteForTest() const { return deleteButton.isEnabled(); }
    juce::Rectangle<int> getCategoryBoundsForTest() const { return categoryCombo.getBounds(); }
    int  getCategoryCountForTest() const noexcept { return categories.size(); }
    int  getVisibleEntryCountForTest() const noexcept { return (int) filteredEntries.size(); }
    int  getVisibleEntryIdForTest (int index) const;    juce::String getSelectedCategoryForTest() const { return selectedCategory; }
    /** Empty selects every category again (flat mode only). */
    bool selectCategoryForTest (const juce::String& category);
    /** True when the bank is shown as the two-level category menu. */
    bool isMenuModeForTest() const noexcept { return menuMode; }
    /** "类别=条目数" per line, without opening a native menu. */
    juce::String describeMenuForTest() const;

private:
    const Entry* findEntry (int id) const;
    int  currentId() const;
    void selectEntryById (int id, bool notify);
    void showPresetMenu();
    juce::String groupOf (const Entry& entry) const;
    void updateButtonStates();
    void step (int delta);
    void rebuildVisibleEntries();
    void showSaveDialog();
    void requestSave (const juce::String& name);
    void showRenameDialog();
    void confirmDelete();

    juce::String builtInName;
    std::vector<Entry> entries;
    std::vector<Entry> filteredEntries;
    juce::StringArray categories;
    juce::String selectedCategory;

    juce::Label label;
    juce::ComboBox categoryCombo;
    juce::ComboBox combo;
    juce::TextButton menuButton;
    juce::TextButton prevButton, nextButton, saveButton, renameButton, deleteButton;

    /** Multi-category banks use the hierarchical menu instead of the flat combo. */
    bool menuMode = false;
    /** The id the bar is standing on (both modes keep it here). */
    int selection = 0;

    /** Set while the list is being rebuilt, so refilling it cannot fire a load. */
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace hmdaw
