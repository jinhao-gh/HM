#include "PluginPicker.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"

namespace hmdaw
{

namespace
{
    constexpr int kGroupRowHeight  = 24;
    constexpr int kPluginRowHeight = 22;
    constexpr int kWindowWidth  = 600;
    constexpr int kWindowHeight = 520;

    /** The combo ids of the 分组 entries (also what the settings text maps to). */
    constexpr int kGroupByVendorId = 1;
    constexpr int kGroupByTypeId   = 2;
    constexpr int kGroupByNoneId   = 3;

    int comboIdFor (PluginGroupAxis axis)
    {
        switch (axis)
        {
            case PluginGroupAxis::vendor: return kGroupByVendorId;
            case PluginGroupAxis::type:   return kGroupByTypeId;
            case PluginGroupAxis::none:   return kGroupByNoneId;
        }

        return kGroupByVendorId;
    }

    PluginGroupAxis axisForComboId (int id)
    {
        switch (id)
        {
            case kGroupByTypeId: return PluginGroupAxis::type;
            case kGroupByNoneId: return PluginGroupAxis::none;
            default: break;
        }

        return PluginGroupAxis::vendor;
    }
}

//==============================================================================
/** Every item under `item`, headings and plugin rows alike. */
int countItemsUnder (juce::TreeViewItem& item)
{
    auto count = 0;

    for (int i = 0; i < item.getNumSubItems(); ++i)
        if (auto* child = item.getSubItem (i))
            count += 1 + countItemsUnder (*child);

    return count;
}

/** The plugin rows the tree would actually draw: a closed heading hides its own,
    and with 「不分组」 the rows sit directly under the (invisible) root. */
int countShownRowsUnder (juce::TreeViewItem& item)
{
    auto count = 0;

    for (int i = 0; i < item.getNumSubItems(); ++i)
    {
        auto* child = item.getSubItem (i);

        if (child == nullptr)
            continue;

        if (child->mightContainSubItems())
        {
            if (child->isOpen())
                count += countShownRowsUnder (*child);
        }
        else
        {
            ++count;
        }
    }

    return count;
}

/** One plugin row.

    Inside paintItem() the graphics origin is already past the item's indent, so
    x = 0 is the start of the row's content (JUCE's TreeView does that for us).
*/
class PluginPicker::PluginItem : public juce::TreeViewItem
{
public:
    PluginItem (PluginPicker& ownerToUse, int groupToUse, int pluginToUse)
        : owner (ownerToUse),
          group (groupToUse),
          plugin (pluginToUse)
    {
    }

    int getItemHeight() const override { return kPluginRowHeight; }
    bool mightContainSubItems() override { return false; }
    bool canBeSelected() const override { return true; }

    juce::String getUniqueName() const override
    {
        return "plugin-" + juce::String (group) + "-" + juce::String (plugin);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected)
            owner.setPluginSelected (group, plugin);
    }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        owner.choose (group, plugin);
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        const auto text = owner.pluginText (group, plugin);

        // The highlight is drawn by the TreeView's selected-item colour, but a
        // row that is merely hovered stays quiet.
        g.setColour (isSelected() ? theme::text : theme::dimText);
        g.setFont (theme::font (12.0f));
        g.drawText (text,
                    juce::Rectangle<int> (4, 0, juce::jmax (0, width - 8), height),
                    juce::Justification::centredLeft, true);
    }

private:
    PluginPicker& owner;
    int group = 0;
    int plugin = 0;
};

/** One heading: the vendor / type name, how many plugins are under it, and the
    plugin rows themselves. */
class PluginPicker::GroupItem : public juce::TreeViewItem
{
public:
    GroupItem (PluginPicker& ownerToUse, int indexToUse)
        : owner (ownerToUse),
          index (indexToUse)
    {
        const auto& group = owner.groups[(size_t) index];

        for (int i = 0; i < (int) group.plugins.size(); ++i)
            addSubItem (new PluginItem (owner, index, i));

        // A heading the user closed stays closed: setOpen(false) cannot be used
        // here, because an item that is not attached to a TreeView yet counts as
        // closed (isOpen() needs an owner view), so setOpen(false) would store
        // nothing and the heading would pop open again the moment it is attached.
        if (owner.closedGroups.count (group.name) != 0)
            setOpenness (juce::TreeViewItem::Openness::opennessClosed);
    }

    bool mightContainSubItems() override { return true; }
    int getItemHeight() const override { return kGroupRowHeight; }

    juce::String getUniqueName() const override
    {
        return "group-" + juce::String (index) + "-" + owner.groups[(size_t) index].name;
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        auto& name = owner.groups[(size_t) index].name;

        if (isNowOpen)
            owner.closedGroups.erase (name);
        else
            owner.closedGroups.insert (name);
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        g.setColour (theme::panelRaised);
        g.fillRect (0, 0, width, height);

        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (owner.groupText (index),
                    juce::Rectangle<int> (2, 0, juce::jmax (0, width - 6), height),
                    juce::Justification::centredLeft, true);

        g.setColour (theme::outline);
        g.drawHorizontalLine (height - 1, 0.0f, (float) width);
    }

private:
    PluginPicker& owner;
    int index = 0;
};

/** The invisible root: it holds the headings (or, with no grouping, the plugins
    themselves). */
class PluginPicker::RootItem : public juce::TreeViewItem
{
public:
    explicit RootItem (PluginPicker& ownerToUse)
        : owner (ownerToUse)
    {
        // This is what the user actually looks at: the tree is filled here, from the
        // groups the picker worked out. A heading that came out with no name is the
        // 「不分组」 case (one group holding everything), and its plugin rows then hang
        // straight off this invisible root instead of under a heading.
        for (int i = 0; i < (int) owner.groups.size(); ++i)
        {
            const auto& group = owner.groups[(size_t) i];

            if (group.name.isEmpty())
            {
                for (int j = 0; j < (int) group.plugins.size(); ++j)
                    addSubItem (new PluginItem (owner, i, j));
            }
            else
            {
                addSubItem (new GroupItem (owner, i));
            }
        }
    }

    bool mightContainSubItems() override { return true; }
    int getItemHeight() const override { return 0; }
    bool canBeSelected() const override { return false; }

    void paintItem (juce::Graphics&, int, int) override {}

private:
    PluginPicker& owner;
};

//==============================================================================
PluginPicker::PluginPicker (PluginHostManager& hostManager, const juce::String& headingText,
                           Kind kindToUse)
    : host (hostManager),
      heading (headingText),
      kind (kindToUse),
      grouping (host.getGroupingAxis())
{
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty (NI_TXT ("搜索插件名称…"), theme::dimText);
    search.onTextChange = [this] { rebuild(); };

    addAndMakeVisible (groupingLabel);
    groupingLabel.setText (NI_TXT ("分组"), juce::dontSendNotification);
    groupingLabel.setFont (theme::font (11.5f));
    groupingLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (groupingBox);
    groupingBox.addItem (NI_TXT ("按厂家"), kGroupByVendorId);
    groupingBox.addItem (NI_TXT ("按类型"), kGroupByTypeId);
    groupingBox.addItem (NI_TXT ("不分组"), kGroupByNoneId);
    groupingBox.setSelectedId (comboIdFor (grouping), juce::dontSendNotification);
    groupingBox.setTooltip (NI_TXT ("插件按厂家或类型分组；选择会被记住"));
    groupingBox.onChange = [this]
    {
        const auto axis = axisForComboId (groupingBox.getSelectedId());

        if (axis == grouping)
            return;

        grouping = axis;
        host.setGroupingAxis (axis);
        rebuild();
    };

    addAndMakeVisible (tree);
    tree.setColour (juce::TreeView::backgroundColourId, theme::gridBackground);
    tree.setColour (juce::TreeView::linesColourId, theme::outline);
    tree.setColour (juce::TreeView::selectedItemBackgroundColourId, theme::accentDim);
    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (true);
    tree.setMultiSelectEnabled (false);

    addAndMakeVisible (scanButton);
    scanButton.setButtonText (NI_TXT ("重新扫描"));
    scanButton.setTooltip (NI_TXT ("扫描标准 VST3 目录以及添加过的目录"));
    scanButton.onClick = [this] { startScan(); };

    addAndMakeVisible (folderButton);
    folderButton.setButtonText (NI_TXT ("添加目录…"));
    folderButton.setTooltip (NI_TXT ("插件装在别处时，把它所在的目录加进来"));
    folderButton.onClick = [this] { addFolder(); };

    addAndMakeVisible (okButton);
    okButton.setButtonText (NI_TXT ("确定"));
    okButton.setEnabled (false);
    okButton.onClick = [this] { choose (selectedGroup, selectedPlugin); };

    addAndMakeVisible (cancelButton);
    cancelButton.setButtonText (NI_TXT ("取消"));
    cancelButton.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (theme::font (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::dimText);

    rebuild();

    // The very first visit is the one that has to go looking: opening the picker
    // with an empty cache starts a scan in the background instead of freezing the
    // editor while every plugin on the machine is read.
    if (host.getPluginCount() == 0 && ! host.isScanning())
        startScan();

    setSize (kWindowWidth, kWindowHeight);
}

PluginPicker::~PluginPicker()
{
    // A scan can take a while (one child process per plugin): the picker going away
    // means the user is done looking, so the scan stops at the file it is on.
    host.cancelScan();

    // The tree owns its items; clearing the root here keeps the destruction order
    // obvious (the items hold a reference back to this component).
    tree.setRootItem (nullptr);
}

void PluginPicker::timerCallback()
{
    refreshStatus();

    if (! host.isScanning())
        stopTimer();
}

void PluginPicker::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (heading, getLocalBounds().reduced (12, 10).removeFromTop (18),
                juce::Justification::centredLeft, true);
}

void PluginPicker::paintOverChildren (juce::Graphics& g)
{
    // An empty list must not look like a broken window: when there is nothing to
    // show, say why and what to do about it. This sits on top of the tree (which
    // paints its own background under it).
    if (! groups.empty())
        return;

    const auto empty = search.getText().trim().isEmpty();

    g.setColour (theme::dimText);
    g.setFont (theme::font (12.0f));
    g.drawFittedText (empty ? NI_TXT ("还没有找到插件：点「重新扫描」扫一遍标准 VST3 目录，"
                                      "或者用「添加目录…」把插件所在的目录加进来。")
                            : NI_TXT ("没有名字里带「") + search.getText().trim()
                                  + NI_TXT ("」的插件。"),
                      tree.getBounds().reduced (14, 10), juce::Justification::centred, 4);
}

void PluginPicker::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (22);              // the heading, painted above

    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    auto groupingRow = area.removeFromTop (24);
    groupingLabel.setBounds (groupingRow.removeFromLeft (32));
    groupingBox.setBounds (groupingRow.removeFromLeft (110));

    area.removeFromTop (6);

    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (78));
    buttons.removeFromRight (6);
    okButton.setBounds (buttons.removeFromRight (78));
    buttons.removeFromRight (10);
    scanButton.setBounds (buttons.removeFromLeft (88));
    buttons.removeFromLeft (6);
    folderButton.setBounds (buttons.removeFromLeft (100));

    statusLabel.setBounds (buttons);

    area.removeFromBottom (8);
    tree.setBounds (area);
}

bool PluginPicker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::returnKey))
    {
        choose (selectedGroup, selectedPlugin);
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::escapeKey))
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);

        return true;
    }

    return false;
}

void PluginPicker::rebuild()
{
    const auto filter = search.getText().trim();
    const auto source = useOverrideList ? overrideList : host.getPlugins();

    visible.clear();

    for (const auto& plugin : source)
    {
        // An instrument track needs an instrument (an effect would sit there
        // silently) and an insert slot needs an effect.
        if (plugin.isInstrument != (kind == Kind::instrument))
            continue;

        if (filter.isNotEmpty()
            && ! plugin.name.containsIgnoreCase (filter)
            && ! plugin.manufacturerName.containsIgnoreCase (filter))
            continue;

        visible.push_back (plugin);
    }

    groups = groupPlugins (visible, grouping);

    selectedGroup = -1;
    selectedPlugin = -1;
    okButton.setEnabled (false);

    // With headings on, the search opens whichever heading holds a hit, so the
    // result is visible without another click.
    if (filter.isNotEmpty())
        closedGroups.clear();

    tree.setRootItem (new RootItem (*this));   // deletes the previous root
    refreshStatus();
    repaint();
}

void PluginPicker::refreshStatus()
{
    const auto status = host.getStatusText();

    auto text = status.isNotEmpty() ? status : NI_TXT ("还没有扫描过，点「重新扫描」");

    if (groups.size() > 1)
        text += NI_TXT ("　·　") + juce::String ((int) visible.size()) + NI_TXT (" 个插件，")
                    + juce::String ((int) groups.size()) + NI_TXT (" 组");

    statusLabel.setText (text, juce::dontSendNotification);
}

juce::String PluginPicker::groupText (int groupIndex) const
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return {};

    const auto& group = groups[(size_t) groupIndex];

    if (group.name.isEmpty())
        return NI_TXT ("全部插件") + " (" + juce::String ((int) group.plugins.size()) + ")";

    return group.name + " (" + juce::String ((int) group.plugins.size()) + ")";
}

juce::String PluginPicker::getGroupTextForTest (int groupIndex) const
{
    return groupText (groupIndex);
}

int PluginPicker::getGroupPluginCountForTest (int groupIndex) const
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return 0;

    return (int) groups[(size_t) groupIndex].plugins.size();
}

juce::String PluginPicker::pluginText (int groupIndex, int pluginIndex) const
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return {};

    const auto& plugins = groups[(size_t) groupIndex].plugins;

    if (pluginIndex < 0 || pluginIndex >= (int) plugins.size())
        return {};

    const auto& plugin = plugins[(size_t) pluginIndex];

    return plugin.manufacturerName.isNotEmpty() ? plugin.name + " - " + plugin.manufacturerName
                                                : plugin.name;
}

juce::String PluginPicker::getPluginTextForTest (int groupIndex, int pluginIndex) const
{
    return pluginText (groupIndex, pluginIndex);
}

bool PluginPicker::isGroupOpenForTest (int groupIndex) const
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return false;

    return closedGroups.count (groups[(size_t) groupIndex].name) == 0;
}

void PluginPicker::setGroupOpenForTest (int groupIndex, bool shouldBeOpen)
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return;

    const auto name = groups[(size_t) groupIndex].name;

    if (shouldBeOpen)
        closedGroups.erase (name);
    else
        closedGroups.insert (name);

    // Rebuild so the tree itself (not just the test's view of it) reflects it.
    auto* root = new RootItem (*this);
    tree.setRootItem (root);
}

int PluginPicker::getShownPluginCountForTest() const
{
    auto count = 0;

    for (const auto& group : groups)
        if (group.name.isEmpty() || closedGroups.count (group.name) == 0)
            count += (int) group.plugins.size();

    return count;
}

int PluginPicker::getTreeItemCountForTest() const
{
    if (auto* root = tree.getRootItem())
        return countItemsUnder (*root);

    return 0;
}

int PluginPicker::getTreeVisibleRowCountForTest() const
{
    if (auto* root = tree.getRootItem())
        return countShownRowsUnder (*root);

    return 0;
}

void PluginPicker::setGroupingForTest (PluginGroupAxis axis)
{
    grouping = axis;
    groupingBox.setSelectedId (comboIdFor (axis), juce::dontSendNotification);
    rebuild();
}

void PluginPicker::setSearchTextForTest (const juce::String& text)
{
    search.setText (text, juce::dontSendNotification);
    rebuild();
}

void PluginPicker::setPluginsForTest (const juce::Array<juce::PluginDescription>& plugins)
{
    useOverrideList = true;
    overrideList = plugins;
    closedGroups.clear();
    rebuild();
}

void PluginPicker::setPluginSelected (int groupIndex, int pluginIndex)
{
    selectedGroup = groupIndex;
    selectedPlugin = pluginIndex;
    okButton.setEnabled (pluginIndex >= 0);
}

void PluginPicker::startScan()
{
    // The scan runs on a background thread and reports back later; the dialog owns
    // this picker and deletes it when the user closes the window, so the callback
    // has to be able to notice that it is too late.
    juce::Component::SafePointer<PluginPicker> safeThis (this);

    host.startScan ([safeThis]
    {
        if (auto* picker = safeThis.getComponent())
        {
            picker->stopTimer();
            picker->rebuild();
        }
    });

    // The scan reports progress through the host's status text; a tick now and then
    // is what puts it on screen while the children are working.
    startTimer (400);
    rebuild();
}

void PluginPicker::addFolder()
{
    juce::Component::SafePointer<PluginPicker> safeThis (this);

    chooser = std::make_unique<juce::FileChooser> (
        NI_TXT ("选择 VST3 插件目录"),
        juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon),
        juce::String(), true);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              auto* picker = safeThis.getComponent();

                              if (picker == nullptr)
                                  return;

                              const auto folder = fc.getResult();

                              if (! folder.isDirectory())
                                  return;

                              picker->host.addExtraFolder (folder);

                              juce::String error;
                              picker->host.scanFolderNow (folder, error);
                              picker->rebuild();
                          });
}

void PluginPicker::choose (int groupIndex, int pluginIndex)
{
    if (groupIndex < 0 || groupIndex >= (int) groups.size())
        return;

    const auto& plugins = groups[(size_t) groupIndex].plugins;

    if (pluginIndex < 0 || pluginIndex >= (int) plugins.size())
        return;

    const auto chosen = plugins[(size_t) pluginIndex];

    if (onChosen)
        onChosen (chosen);

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

void PluginPicker::launch (juce::Component* parentToCentreAround)
{
    // The 「确定」/「取消」 buttons sit at the bottom of this window, so it must fit
    // the screen it opens on: on a small or scaled display the window opens at the
    // size the work area can give and the tree shares what is left.
    const auto bounds = placement::placeWindow ({ getWidth(), getHeight() },
                                                placement::displayAreaFor (parentToCentreAround));

    setSize (bounds.getWidth(), bounds.getHeight());

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = heading;
    options.dialogBackgroundColour = theme::panelBackground;
    options.content.setOwned (this);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = parentToCentreAround;
    options.launchAsync();
}

} // namespace hmdaw
