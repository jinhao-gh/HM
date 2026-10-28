#pragma once

#include "../Audio/PluginHost.h"
#include "../Core/PluginGrouping.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>

namespace nidaw
{

/** Picking an external plugin: a search box, a two level tree (heading -> plugin)
    and the buttons that look for more.

    The headings are the plugins' vendors, their types, or nothing at all - the
    分组 combo decides, and the choice is remembered next to the extra plugin
    folders. Headings start open; closing one sticks until the picker is rebuilt.

    The component is usable on its own - the offscreen self test lays it out and
    snapshots it without ever showing a window.
*/
class PluginPicker : public juce::Component,
                     private juce::Timer
{
public:
    /** Which half of the plugin world the picker is offering: an instrument for a
        track, or an effect for one of its insert slots. */
    enum class Kind { instrument, effect };

    PluginPicker (PluginHostManager& hostManager, const juce::String& heading,
                  Kind kindToUse = Kind::instrument);
    ~PluginPicker() override;

    void paint (juce::Graphics& g) override;
    /** The "nothing here yet" note has to go over the tree's own background. */
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Called with the plugin the user picked. */
    std::function<void (const juce::PluginDescription&)> onChosen;

    Kind getKind() const noexcept { return kind; }

    /** Shows the picker in its own window. The picker owns itself from here on. */
    void launch (juce::Component* parentToCentreAround);

    // ---- self test hooks ----
    /** What the tree is showing right now (search applied, group axis applied). */
    int getVisibleCountForTest() const { return (int) visible.size(); }
    int getGroupCountForTest() const { return (int) groups.size(); }
    /** "Xfer Records (2)" - the heading as it is drawn. */
    juce::String getGroupTextForTest (int groupIndex) const;
    int getGroupPluginCountForTest (int groupIndex) const;
    juce::String getPluginTextForTest (int groupIndex, int pluginIndex) const;
    bool isGroupOpenForTest (int groupIndex) const;
    void setGroupOpenForTest (int groupIndex, bool shouldBeOpen);
    /** How many plugin rows are actually on screen (headings that are closed hide
        theirs). */
    int getShownPluginCountForTest() const;
    /** How many rows the tree widget really holds - counted by walking the
        TreeView, not the data the picker keeps beside it. */
    int getTreeItemCountForTest() const;
    /** The rows the tree would draw right now (a closed heading hides its own). */
    int getTreeVisibleRowCountForTest() const;
    PluginGroupAxis getGroupingForTest() const { return grouping; }
    void setGroupingForTest (PluginGroupAxis axis);
    void setSearchTextForTest (const juce::String& text);
    /** Pretends the tree holds these plugins, so a snapshot and the checks around
        it do not depend on what happens to be installed. */
    void setPluginsForTest (const juce::Array<juce::PluginDescription>& plugins);
    /** The same path a double click takes. */
    void chooseForTest (int groupIndex, int pluginIndex) { choose (groupIndex, pluginIndex); }

    juce::Rectangle<int> getTreeBoundsForTest() const { return tree.getBounds(); }
    juce::Rectangle<int> getSearchBoundsForTest() const { return search.getBounds(); }
    juce::Rectangle<int> getGroupingBoundsForTest() const { return groupingBox.getBounds(); }
    juce::Rectangle<int> getScanButtonBoundsForTest() const { return scanButton.getBounds(); }
    juce::Rectangle<int> getOkButtonBoundsForTest() const { return okButton.getBounds(); }

private:
    /** Keeps the status line moving while a scan walks the plugin folders. */
    void timerCallback() override;

    class GroupItem;
    class PluginItem;
    class RootItem;

    friend class GroupItem;
    friend class PluginItem;
    friend class RootItem;

    void rebuild();
    void refreshStatus();
    void startScan();
    void addFolder();
    void choose (int groupIndex, int pluginIndex);
    void setPluginSelected (int groupIndex, int pluginIndex);
    /** How a heading reads: "Xfer Records (2)". Shared by the tree's painting and
        the self test, so the two can never disagree. */
    juce::String groupText (int groupIndex) const;
    /** How a plugin row reads: "Serum 2 - Xfer Records". */
    juce::String pluginText (int groupIndex, int pluginIndex) const;

    PluginHostManager& host;
    juce::String heading;
    Kind kind = Kind::instrument;

    /** The plugins the search left (before grouping). */
    std::vector<juce::PluginDescription> visible;
    std::vector<PluginGroup> groups;
    PluginGroupAxis grouping = PluginGroupAxis::vendor;
    /** Headings the user closed. Kept across a rebuild, so searching does not
        reopen what was deliberately closed. */
    std::set<juce::String> closedGroups;
    /** Which plugin row is highlighted, as group / plugin indices. */
    int selectedGroup = -1;
    int selectedPlugin = -1;

    /** The list the picker is showing: normally what the host has found, or the
        list a test handed it. */
    bool useOverrideList = false;
    juce::Array<juce::PluginDescription> overrideList;

    juce::TextEditor search;
    juce::Label groupingLabel;
    juce::ComboBox groupingBox;
    juce::TreeView tree;
    juce::TextButton scanButton;
    juce::TextButton folderButton;
    juce::TextButton okButton;
    juce::TextButton cancelButton;
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPicker)
};

} // namespace nidaw
