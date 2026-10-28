#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace nidaw
{

/** Entry ids: the built-in bank uses 1..1000, the user's own presets start at
    1001 so a single ComboBox can hold both groups without collisions. */
constexpr int kBuiltInPresetIdBase = 1;
constexpr int kUserPresetIdBase    = 1001;

/** The "预设" row shared by the FM and the sampler editors.

    It only draws and drives the row; the instrument behind it decides what a
    preset is. Entries come in two groups - the built-in bank and the user's own -
    and every entry carries the id the ComboBox uses.
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
    juce::Rectangle<int> getComboBoxBoundsForTest() const { return combo.getBounds(); }
    int  getSelectedIdForTest() const { return combo.getSelectedId(); }
    bool canRenameForTest() const { return renameButton.isEnabled(); }
    bool canDeleteForTest() const { return deleteButton.isEnabled(); }
    juce::Rectangle<int> getCategoryBoundsForTest() const { return categoryCombo.getBounds(); }
    int  getCategoryCountForTest() const noexcept { return categories.size(); }
    int  getVisibleEntryCountForTest() const noexcept { return (int) filteredEntries.size(); }
    int  getVisibleEntryIdForTest (int index) const;
    juce::String getSelectedCategoryForTest() const { return selectedCategory; }
    /** Empty selects every category again. */
    bool selectCategoryForTest (const juce::String& category);

private:
    const Entry* findEntry (int id) const;
    void selectEntryById (int id, bool notify);
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
    juce::TextButton prevButton, nextButton, saveButton, renameButton, deleteButton;

    /** Set while the list is being rebuilt, so refilling it cannot fire a load. */
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace nidaw
