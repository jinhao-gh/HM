#include "LaneHeader.h"
#include "Theme.h"

namespace hmdaw
{

namespace
{
    constexpr int kColourBarWidth = 4;
    constexpr int kRowGap         = 4;
    constexpr int kNameRowHeight  = 24;
    constexpr int kSliderRowHeight = 22;
}

LaneHeader::LaneHeader (ProjectDocument& documentToUse, AppState& appStateToUse,
                        juce::int64 trackIdToUse)
    : document (documentToUse), appState (appStateToUse), trackId (trackIdToUse)
{
    // Clicking the header's blank parts selects the track; only the name box itself
    // starts a rename. Without this the header does not want the focus, and JUCE
    // would give it to the first focusable child - the name editor.
    setWantsKeyboardFocus (true);

    addAndMakeVisible (nameEditor);
    nameEditor.setFont (theme::font (12.5f));
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setTextToShowWhenEmpty (NI_TXT ("轨道名称"), theme::dimText);
    nameEditor.onReturnKey = [this] { applyName(); };
    nameEditor.onFocusLost = [this] { applyName(); };
    // Escape leaves the editor without changing anything; without this the only way
    // out was to click something that happens to take the focus, and clicking the
    // lane area (which does not) left the name sitting in its editing state.
    nameEditor.onEscapeKey = [this] { cancelNameEdit(); };

    addAndMakeVisible (timbreButton);
    timbreButton.setButtonText (NI_TXT ("音色"));
    timbreButton.setTooltip (NI_TXT ("打开这条轨道的音色编辑窗口"));
    timbreButton.onClick = [this]
    {
        if (onEditTimbre != nullptr)
            onEditTimbre (trackId);
    };

    addChildComponent (importButton);
    importButton.setButtonText (NI_TXT ("导入音频"));
    importButton.setTooltip (NI_TXT ("把一段音频文件放到这条轨道上"));
    importButton.onClick = [this]
    {
        if (onImportAudio != nullptr)
            onImportAudio (trackId);
    };

    // 效果: the insert chain used to sit in a column down the right of the
    // arrangement; it now opens in its own window from here.
    addAndMakeVisible (effectsButton);
    effectsButton.setButtonText (NI_TXT ("效果"));
    effectsButton.setTooltip (NI_TXT ("这一轨的效果器插入链（加 / 旁路 / 上下移 / 删 / 界面）"));
    effectsButton.onClick = [this]
    {
        if (onEditEffects != nullptr)
            onEditEffects (trackId);
    };

    addAndMakeVisible (volumeSlider);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    volumeSlider.setRange (-60.0, 6.0, 0.5);
    volumeSlider.setTooltip (NI_TXT ("音量 (dB)"));
    volumeSlider.onValueChange = [this] { applyVolume(); };
    // The wheel is how the user scrolls the lanes, and this slider sits right in the
    // path of that gesture. Letting it change the volume there meant a scroll could
    // quietly rewrite a track's level (with an undo step to go with it).
    volumeSlider.setScrollWheelEnabled (false);

    addAndMakeVisible (panSlider);
    panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setTooltip (NI_TXT ("声相 (-1 左 .. +1 右)"));
    panSlider.onValueChange = [this] { applyPan(); };
    panSlider.setScrollWheelEnabled (false);

    addAndMakeVisible (deleteButton);
    deleteButton.setButtonText (NI_TXT ("删除"));
    deleteButton.setTooltip (NI_TXT ("删除这条轨道及其全部内容"));
    deleteButton.onClick = [this]
    {
        if (onDelete != nullptr)
            onDelete (trackId);
    };

    addAndMakeVisible (muteButton);
    muteButton.setButtonText ("M");
    muteButton.setClickingTogglesState (false);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::playhead);
    muteButton.setTooltip (NI_TXT ("静音这条音轨（不发声，导出里也没有）"));
    muteButton.onClick = [this] { toggleMute(); };

    addAndMakeVisible (soloButton);
    soloButton.setButtonText ("S");
    soloButton.setClickingTogglesState (false);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::referenceLineTarget);
    soloButton.setTooltip (NI_TXT ("独奏：只听到独奏的轨道，可以多条一起独奏"));
    soloButton.onClick = [this] { toggleSolo(); };

    document.addChangeListener (this);
    appState.addChangeListener (this);

    refresh();
}

LaneHeader::~LaneHeader()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void LaneHeader::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

const InstrumentTrack* LaneHeader::track() const
{
    return document.getProject().findTrack (trackId);
}

void LaneHeader::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto* t = track();

    if (t == nullptr)
        return;

    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (t->name, juce::dontSendNotification);

    volumeSlider.setValue ((double) t->volumeDb, juce::dontSendNotification);
    panSlider.setValue ((double) t->pan, juce::dontSendNotification);

    muteButton.setToggleState (t->muted, juce::dontSendNotification);
    soloButton.setToggleState (t->soloed, juce::dontSendNotification);

    // An audio track has no instrument to edit; it takes a file instead.
    const auto isAudio = t->kind == TrackKind::audio;
    const auto isVst   = t->kind == TrackKind::vst;
    timbreButton.setVisible (! isAudio);
    importButton.setVisible (isAudio);

    // An external instrument is opened in its own interface, which is what the
    // button means on such a track.
    timbreButton.setButtonText (isVst ? NI_TXT ("插件界面") : NI_TXT ("音色"));
    timbreButton.setTooltip (isVst ? NI_TXT ("打开插件自己的界面")
                                   : NI_TXT ("打开这条轨道的音色编辑窗口"));

    repaint();
}

void LaneHeader::applyName()
{
    if (updating)
        return;

    const auto* t = track();
    const auto text = nameEditor.getText().trim();

    // Whatever happens, the editor is done: leaving the focus in it means the name
    // stays in its editing state (the user's "can't get out of renaming").
    if (t == nullptr || text.isEmpty() || text == t->name)
    {
        if (t != nullptr)
            nameEditor.setText (t->name, juce::dontSendNotification);

        nameEditor.giveAwayKeyboardFocus();
        return;
    }

    document.applyEdit (NI_TXT ("重命名轨道"), [this, text] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->name = text;
    });

    nameEditor.giveAwayKeyboardFocus();
}

void LaneHeader::cancelNameEdit()
{
    const auto* t = track();

    if (t != nullptr)
        nameEditor.setText (t->name, juce::dontSendNotification);

    nameEditor.giveAwayKeyboardFocus();
}

void LaneHeader::applyVolume()
{
    if (updating)
        return;

    const auto* t = track();
    const auto value = (float) volumeSlider.getValue();

    if (t == nullptr || std::abs (value - t->volumeDb) < 1.0e-4f)
        return;

    document.applyEdit (NI_TXT ("调整音量"), [this, value] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->volumeDb = value;
    });
}

void LaneHeader::applyPan()
{
    if (updating)
        return;

    const auto* t = track();
    const auto value = juce::jlimit (-1.0f, 1.0f, (float) panSlider.getValue());

    if (t == nullptr || std::abs (value - t->pan) < 1.0e-4f)
        return;

    document.applyEdit (NI_TXT ("调整声相"), [this, value] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->pan = value;
    });
}

void LaneHeader::toggleMute()
{
    const auto* t = track();

    if (t == nullptr)
        return;

    const auto shouldMute = ! t->muted;

    document.applyEdit (shouldMute ? NI_TXT ("静音音轨") : NI_TXT ("取消静音音轨"),
                        [this, shouldMute] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->muted = shouldMute;
    });
}

void LaneHeader::toggleSolo()
{
    const auto* t = track();

    if (t == nullptr)
        return;

    const auto shouldSolo = ! t->soloed;

    document.applyEdit (shouldSolo ? NI_TXT ("独奏音轨") : NI_TXT ("取消独奏音轨"),
                        [this, shouldSolo] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->soloed = shouldSolo;
    });
}

void LaneHeader::mouseDown (const juce::MouseEvent& e)
{
    // Right click: the track's own settings (see `onMenu`). It does not steal the
    // selection click, which is what a left click on the header is for.
    if (e.mods.isPopupMenu())
    {
        if (onSelect != nullptr)
            onSelect (trackId);

        if (onMenu != nullptr)
            onMenu (trackId);

        return;
    }

    if (onSelect != nullptr)
        onSelect (trackId);
}

juce::Rectangle<int> LaneHeader::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return nameEditor.getBounds();
        case 1: return timbreButton.getBounds();
        case 2: return volumeSlider.getBounds();
        case 3: return panSlider.getBounds();
        case 4: return deleteButton.getBounds();
        case 5: return muteButton.getBounds();
        case 6: return soloButton.getBounds();
        default: break;
    }

    return {};
}

void LaneHeader::setNameForTest (const juce::String& name)
{
    nameEditor.setText (name, juce::dontSendNotification);
    applyName();
}

void LaneHeader::setVolumeForTest (double db)
{
    volumeSlider.setValue (db, juce::dontSendNotification);
    applyVolume();
}

void LaneHeader::setPanForTest (double pan)
{
    panSlider.setValue (pan, juce::dontSendNotification);
    applyPan();
}

void LaneHeader::pressTimbreForTest()
{
    if (timbreButton.onClick != nullptr)
        timbreButton.onClick();
}

void LaneHeader::pressDeleteForTest()
{
    if (deleteButton.onClick != nullptr)
        deleteButton.onClick();
}

void LaneHeader::pressMuteForTest()
{
    if (muteButton.onClick != nullptr)
        muteButton.onClick();
}

void LaneHeader::pressSoloForTest()
{
    if (soloButton.onClick != nullptr)
        soloButton.onClick();
}

void LaneHeader::pressRenameReturnForTest()
{
    if (nameEditor.onReturnKey != nullptr)
        nameEditor.onReturnKey();
}

void LaneHeader::pressRenameEscapeForTest()
{
    if (nameEditor.onEscapeKey != nullptr)
        nameEditor.onEscapeKey();
}

void LaneHeader::paint (juce::Graphics& g)
{
    const auto* t = track();

    if (t == nullptr)
        return;

    const auto selected = appState.selectedTrackId == trackId;
    const auto colour = theme::trackColour (t->colourIndex);

    g.setColour (selected ? theme::panelRaised : theme::laneHeader);
    g.fillRect (getLocalBounds());

    // The colour bar identifies the track everywhere else too (clips, notes).
    g.setColour (colour.withAlpha (selected ? 1.0f : 0.6f));
    g.fillRect (juce::Rectangle<int> (0, 0, kColourBarWidth, getHeight()));

    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    // Third row: the kind of instrument plus how much is on the lane.
    auto row = getLocalBounds().reduced (6 + kColourBarWidth, 4);
    row.removeFromTop (kNameRowHeight + kRowGap + kSliderRowHeight + kRowGap);

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.5f));
    g.drawText (typeLineText() + "   " + juce::String ((int) t->clips.size())
                    + NI_TXT (" 个片段"),
                // Room for the delete button and the two M / S buttons to its left.
                row.withTrimmedRight (105), juce::Justification::centredLeft, true);
}

juce::String LaneHeader::typeLineText() const
{
    const auto* t = track();

    if (t == nullptr)
        return {};

    // An external instrument names itself: which plugin a track holds matters more
    // than the fact that it is a VST track, and a missing plugin has to be visible
    // rather than sounding like a broken track.
    auto text = trackKindLabel (t->kind);

    if (t->kind == TrackKind::vst)
    {
        text += NI_TXT (" · ")
                    + (t->vst.name.isNotEmpty() ? t->vst.name : NI_TXT ("未选择插件"));

        if (! t->vst.loadedOk)
            text += NI_TXT ("（插件缺失）");
    }

    return text;
}

juce::String LaneHeader::getTypeTextForTest() const
{
    const auto* t = track();
    return t != nullptr ? trackKindLabel (t->kind) : juce::String();
}

void LaneHeader::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    area.removeFromLeft (kColourBarWidth);

    auto nameRow = area.removeFromTop (kNameRowHeight);
    timbreButton.setBounds (nameRow.removeFromRight (56).reduced (0, 2));
    importButton.setBounds (timbreButton.getBounds());
    nameRow.removeFromRight (kRowGap);
    nameEditor.setBounds (nameRow.reduced (0, 2));

    area.removeFromTop (kRowGap);

    auto sliderRow = area.removeFromTop (kSliderRowHeight);
    const auto sliderWidth = juce::jmax (60, (sliderRow.getWidth() - kRowGap) / 2);
    volumeSlider.setBounds (sliderRow.removeFromLeft (sliderWidth));
    sliderRow.removeFromLeft (kRowGap);
    panSlider.setBounds (sliderRow);

    area.removeFromTop (kRowGap);

    auto bottomRow = area;
    deleteButton.setBounds (bottomRow.removeFromRight (50).reduced (0, 2));
    bottomRow.removeFromRight (4);
    soloButton.setBounds (bottomRow.removeFromRight (20).reduced (0, 2));
    bottomRow.removeFromRight (3);
    muteButton.setBounds (bottomRow.removeFromRight (20).reduced (0, 2));
    // 效果 sits at the far left of the same row: that end of the row was empty.
    bottomRow.removeFromRight (8);
    effectsButton.setBounds (bottomRow.removeFromLeft (56).reduced (0, 2));
}

} // namespace hmdaw
