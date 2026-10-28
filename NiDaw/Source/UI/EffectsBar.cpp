#include "EffectsBar.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    constexpr int kBypassWidth = 24;
    constexpr int kEditWidth   = 44;
    constexpr int kSmallWidth  = 22;
    constexpr int kGap         = 3;
}

/** One insert slot: bypass, name (with the plugin's latency), and the four
    actions. */
class EffectsBar::Row : public juce::Component
{
public:
    Row (EffectsBar& ownerToUse, int indexToUse)
        : owner (ownerToUse),
          index (indexToUse)
    {
        addAndMakeVisible (bypassButton);
        bypassButton.setTooltip (NI_TXT ("旁路这个效果器（保留它，但声音不经过它）"));
        bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::playhead);
        bypassButton.onClick = [this] { owner.toggleBypass (index); };

        addAndMakeVisible (nameLabel);
        nameLabel.setFont (theme::font (11.0f));
        nameLabel.setColour (juce::Label::textColourId, theme::text);
        nameLabel.setInterceptsMouseClicks (false, false);

        addAndMakeVisible (editButton);
        editButton.setButtonText (NI_TXT ("界面"));
        editButton.setTooltip (NI_TXT ("打开这个效果器自己的界面"));
        editButton.onClick = [this] { owner.editSlot (index); };

        addAndMakeVisible (upButton);
        upButton.setButtonText (NI_TXT ("↑"));
        upButton.setTooltip (NI_TXT ("往前移一位（更靠前处理）"));
        upButton.onClick = [this] { owner.moveSlot (index, -1); };

        addAndMakeVisible (downButton);
        downButton.setButtonText (NI_TXT ("↓"));
        downButton.setTooltip (NI_TXT ("往后移一位（更靠后处理）"));
        downButton.onClick = [this] { owner.moveSlot (index, 1); };

        addAndMakeVisible (deleteButton);
        deleteButton.setButtonText (NI_TXT ("✕"));
        deleteButton.setTooltip (NI_TXT ("从链上移除这个效果器"));
        deleteButton.onClick = [this] { owner.removeSlot (index); };
    }

    void setBypassed (bool shouldBypass)
    {
        bypassButton.setButtonText (shouldBypass ? NI_TXT ("旁") : NI_TXT ("开"));
        bypassButton.setToggleState (shouldBypass, juce::dontSendNotification);
        bypassButton.setTooltip (shouldBypass
                                     ? NI_TXT ("已旁路：点一下让它重新参与处理")
                                     : NI_TXT ("旁路这个效果器（保留它，但声音不经过它）"));
    }

    void setRowName (const juce::String& text)
    {
        nameLabel.setText (text, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (index % 2 == 0 ? theme::laneBackground : theme::gridBackground);
        g.fillRect (getLocalBounds());

        g.setColour (theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 3);

        bypassButton.setBounds (area.removeFromLeft (kBypassWidth));
        area.removeFromLeft (kGap);
        deleteButton.setBounds (area.removeFromRight (kSmallWidth));
        area.removeFromRight (kGap);
        downButton.setBounds (area.removeFromRight (kSmallWidth));
        area.removeFromRight (kGap);
        upButton.setBounds (area.removeFromRight (kSmallWidth));
        area.removeFromRight (kGap);
        editButton.setBounds (area.removeFromRight (kEditWidth));
        area.removeFromRight (kGap);

        nameLabel.setBounds (area);
    }

    juce::TextButton bypassButton, editButton, upButton, downButton, deleteButton;

private:
    EffectsBar& owner;
    int index = 0;
    juce::Label nameLabel;
};

//==============================================================================
EffectsBar::EffectsBar (ProjectDocument& documentToUse, AppState& appStateToUse,
                        PluginHostManager& pluginHost)
    : document (documentToUse),
      appState (appStateToUse),
      host (pluginHost)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);

    addAndMakeVisible (emptyLabel);
    emptyLabel.setFont (theme::font (11.0f));
    emptyLabel.setColour (juce::Label::textColourId, theme::dimText);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setText (NI_TXT ("这一轨还没有效果器"), juce::dontSendNotification);

    addAndMakeVisible (addButton);
    addButton.setButtonText (NI_TXT ("＋ 添加效果器"));
    addButton.setTooltip (NI_TXT ("给这一条轨道加一个 VST3 效果器"));
    addButton.onClick = [this]
    {
        if (const auto* track = selectedTrack())
            if (onAddEffect != nullptr)
                onAddEffect (track->id);
    };

    document.addChangeListener (this);
    appState.addChangeListener (this);

    refresh();
}

EffectsBar::~EffectsBar()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void EffectsBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

const InstrumentTrack* EffectsBar::selectedTrack() const
{
    return document.getProject().findTrack (pinnedTrackId != 0 ? pinnedTrackId
                                                              : appState.selectedTrackId);
}

void EffectsBar::setTrack (juce::int64 trackId)
{
    if (pinnedTrackId == trackId)
        return;

    pinnedTrackId = trackId;
    refresh();
}

int EffectsBar::getPreferredHeight() const
{
    const auto* track = selectedTrack();
    const auto rowCount = track != nullptr ? (int) track->effects.size() : 0;

    return headerHeight + rowCount * rowHeight + addButtonHeight + 10;
}

void EffectsBar::refresh()
{
    const auto* track = selectedTrack();
    const auto count = track != nullptr ? (int) track->effects.size() : 0;

    while (rows.size() > count)
        rows.removeLast();

    while (rows.size() < count)
    {
        auto* row = new Row (*this, rows.size());
        rowsHolder.addAndMakeVisible (row);
        rows.add (row);
    }

    const auto sampleRate = host.getCurrentSampleRate();

    for (int i = 0; i < count; ++i)
    {
        const auto& slot = track->effects[(size_t) i];
        auto* row = rows[i];

        row->setBypassed (slot.bypass);

        auto text = slot.name.isNotEmpty() ? slot.name : NI_TXT ("未选择插件");

        if (auto plugin = host.effectPluginFor (track->id, i))
        {
            const auto latency = plugin->getLatencySamples();

            if (latency > 0 && sampleRate > 0.0)
                text += NI_TXT ("　·　延迟 ")
                            + juce::String (latency * 1000.0 / sampleRate, 1) + NI_TXT (" ms");
        }
        else if (host.getEffectError (track->id, i).isNotEmpty())
        {
            text += NI_TXT ("（插件缺失）");
        }

        row->setRowName (text);
    }

    emptyLabel.setVisible (count == 0);
    addButton.setEnabled (track != nullptr && count < effects::kMaxPerTrack);
    resized();
    repaint();
}

void EffectsBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setColour (theme::dimText);
    g.setFont (theme::font (11.0f, true));
    g.drawText (headerText(), getLocalBounds().removeFromTop (headerHeight).reduced (6, 0),
                juce::Justification::centredLeft, true);
}

juce::String EffectsBar::headerText() const
{
    const auto* track = selectedTrack();
    const auto count = track != nullptr ? (int) track->effects.size() : 0;

    auto text = NI_TXT ("效果器");

    if (track != nullptr)
        text += NI_TXT (" · ") + track->name;

    if (count > 0)
        text += " (" + juce::String (count) + ")";

    return text;
}

void EffectsBar::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);

    addButton.setBounds (area.removeFromBottom (addButtonHeight).reduced (4, 2));
    area.removeFromBottom (2);

    viewport.setBounds (area);

    const auto width = juce::jmax (60, viewport.getMaximumVisibleWidth());
    auto y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight;
    }

    rowsHolder.setSize (width, juce::jmax (y, viewport.getMaximumVisibleHeight()));
    emptyLabel.setBounds (area.reduced (10, 0));
}

juce::Rectangle<int> EffectsBar::getRowBoundsForTest (int index) const
{
    if (index < 0 || index >= rows.size())
        return {};

    return rows[index]->getBounds() - viewport.getViewPosition();
}

juce::String EffectsBar::getRowTextForTest (int index) const
{
    if (index < 0 || index >= rows.size())
        return {};

    const auto* track = selectedTrack();

    if (track == nullptr || index >= (int) track->effects.size())
        return {};

    const auto& slot = track->effects[(size_t) index];
    const auto sampleRate = host.getCurrentSampleRate();

    auto text = slot.name.isNotEmpty() ? slot.name : NI_TXT ("未选择插件");

    if (auto plugin = host.effectPluginFor (track->id, index))
    {
        const auto latency = plugin->getLatencySamples();

        if (latency > 0 && sampleRate > 0.0)
            text += NI_TXT ("　·　延迟 ") + juce::String (latency * 1000.0 / sampleRate, 1)
                        + NI_TXT (" ms");
    }
    else if (host.getEffectError (track->id, index).isNotEmpty())
    {
        text += NI_TXT ("（插件缺失）");
    }

    return text;
}

bool EffectsBar::isBypassOnForTest (int index) const
{
    if (index < 0 || index >= rows.size())
        return false;

    return rows[index]->bypassButton.getToggleState();
}

void EffectsBar::pressBypassForTest (int index)
{
    if (index >= 0 && index < rows.size() && rows[index]->bypassButton.onClick != nullptr)
        rows[index]->bypassButton.onClick();
}

void EffectsBar::pressUpForTest (int index)
{
    if (index >= 0 && index < rows.size() && rows[index]->upButton.onClick != nullptr)
        rows[index]->upButton.onClick();
}

void EffectsBar::pressDownForTest (int index)
{
    if (index >= 0 && index < rows.size() && rows[index]->downButton.onClick != nullptr)
        rows[index]->downButton.onClick();
}

void EffectsBar::pressDeleteForTest (int index)
{
    if (index >= 0 && index < rows.size() && rows[index]->deleteButton.onClick != nullptr)
        rows[index]->deleteButton.onClick();
}

void EffectsBar::addEffect (const juce::PluginDescription& description)
{
    const auto trackId = appState.selectedTrackId;
    const auto* track = selectedTrack();

    if (trackId == 0 || track == nullptr)
        return;

    EffectSlot slot;
    slot.kind       = 0;
    slot.name       = description.name;
    slot.pluginPath = description.fileOrIdentifier;
    slot.pluginUid  = juce::String ((juce::int64) description.uniqueId);

    auto added = false;

    document.applyEdit (NI_TXT ("添加效果器"), [trackId, &slot, &added] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            added = effects::add (target->effects, slot);
    });

    if (! added)
        return;

    // Start loading it now rather than waiting for the next schedule rebuild.
    if (const auto* target = document.getProject().findTrack (trackId))
        host.requestEffect (trackId, (int) target->effects.size() - 1, target->effects.back());

    refresh();
}

void EffectsBar::toggleBypass (int index)
{
    const auto* track = selectedTrack();

    if (track == nullptr || index < 0 || index >= (int) track->effects.size())
        return;

    const auto trackId = track->id;
    const auto shouldBypass = ! track->effects[(size_t) index].bypass;

    document.applyEdit (shouldBypass ? NI_TXT ("旁路效果器") : NI_TXT ("启用效果器"),
                        [trackId, index, shouldBypass] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            if (index < (int) target->effects.size())
                target->effects[(size_t) index].bypass = shouldBypass;
    });

    // The audio thread reads the flag from the live instance, so set it there too
    // instead of waiting for the schedule to be rebuilt.
    host.setEffectBypassed (trackId, index, shouldBypass);
    refresh();
}

void EffectsBar::moveSlot (int index, int delta)
{
    const auto* track = selectedTrack();

    if (track == nullptr || index < 0 || index >= (int) track->effects.size())
        return;

    const auto trackId = track->id;
    auto moved = false;

    document.applyEdit (delta < 0 ? NI_TXT ("效果器前移") : NI_TXT ("效果器后移"),
                        [trackId, index, delta, &moved] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            moved = delta < 0 ? effects::moveUp (target->effects, index)
                              : effects::moveDown (target->effects, index);
    });

    if (moved)
        refresh();
}

void EffectsBar::removeSlot (int index)
{
    const auto* track = selectedTrack();

    if (track == nullptr || index < 0 || index >= (int) track->effects.size())
        return;

    const auto trackId = track->id;
    auto removed = false;

    document.applyEdit (NI_TXT ("移除效果器"), [trackId, index, &removed] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            removed = effects::remove (target->effects, index);
    });

    if (removed)
        refresh();
}

void EffectsBar::editSlot (int index)
{
    const auto* track = selectedTrack();

    if (track == nullptr)
        return;

    if (onEditEffect != nullptr)
        onEditEffect (track->id, index);
}

} // namespace nidaw
