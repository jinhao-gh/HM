#include "DrumEditor.h"
#include "Theme.h"

#include <set>

namespace nidaw
{

namespace
{
    constexpr int kTitleHeight    = 24;
    constexpr int kPresetHeight   = 26;
    constexpr int kKeyboardHeight = 78;
    constexpr int kRowHeight      = 24;
    constexpr int kHeaderHeight   = 17;
    constexpr int kRowGap         = 6;
    constexpr int kPadButtonHeight = 24;
    constexpr int kLabelWidth     = 64;
}

DrumEditor::DrumEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                        PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (presetBar);
    presetBar.getEntries       = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName   = [this]
    {
        const auto* track = currentTrack();
        return track != nullptr ? track->drums.name : juce::String();
    };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset     = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name) { return userPresetExists (name); };

    addAndMakeVisible (restoreButton);
    restoreButton.setButtonText (NI_TXT ("恢复默认鼓组"));
    restoreButton.setTooltip (NI_TXT ("把所有鼓的参数调回出厂值"));
    restoreButton.onClick = [this]
    {
        document.beginEdit (NI_TXT ("恢复默认鼓组"));

        if (auto* target = currentTrack())
            target->drums = DrumKitParams();

        refresh();

        if (auto* target = currentTrack())
            pushParams (target->drums);
    };

    for (int pad = 0; pad < kDrumPads; ++pad)
    {
        auto* button = padButtons.add (new juce::TextButton (drumPadName (pad)));
        addAndMakeVisible (button);
        button->setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        button->onClick = [this, pad] { selectPadForTest (pad); };
    }

    auto setupHeader = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.0f, true));
        label.setColour (juce::Label::textColourId, theme::accent);
    };

    setupHeader (padParamsHeader, NI_TXT ("鼓垫参数"));
    setupHeader (globalHeader, NI_TXT ("全局"));

    addAndMakeVisible (padNameLabel);
    padNameLabel.setFont (theme::font (12.0f, true));
    padNameLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (padToggle);
    padToggle.setButtonText (NI_TXT ("启用"));
    padToggle.setClickingTogglesState (true);
    padToggle.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    padToggle.onClick = [this] { pushCurrentParams(); };

    addAndMakeVisible (previewButton);
    previewButton.setButtonText (NI_TXT ("试听"));
    previewButton.onClick = [this] { triggerPadForTest (selectedPad); };

    auto setupSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                               double interval, const juce::String& suffix)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 17);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整鼓组")); };
        slider.onValueChange = [this] { pushCurrentParams(); };
    };

    setupSlider (padLevel, 0.0, 1.0, 0.01, {});
    setupSlider (padPitch, -12.0, 12.0, 1.0, " st");
    setupSlider (padDecay, 0.1, 4.0, 0.01, " x");
    setupSlider (padTone, 0.0, 1.0, 0.01, {});
    setupSlider (padSnap, 0.0, 1.0, 0.01, {});

    setupSlider (masterGain, 0.0, 1.0, 0.01, {});
    setupSlider (masterPitch, -12.0, 12.0, 1.0, " st");
    setupSlider (masterDecay, 0.1, 4.0, 0.01, " x");
    setupSlider (masterWidth, 0.0, 1.0, 0.01, {});

    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe6e6ee));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::outline);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accent.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dimText);
    keyboard.setAvailableRange (60, 72);
    keyboard.setKeyWidth (34.0f);
    keyboard.setLowestVisibleKey (60);
    keyboardState.addListener (this);

    document.addChangeListener (this);

    refresh();
}

DrumEditor::~DrumEditor()
{
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

void DrumEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

InstrumentTrack* DrumEditor::currentTrack()
{
    auto* track = document.findTrack (trackId);
    return track != nullptr && track->kind == TrackKind::drums ? track : nullptr;
}

const InstrumentTrack* DrumEditor::currentTrack() const
{
    return const_cast<DrumEditor*> (this)->currentTrack();
}

void DrumEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

void DrumEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void DrumEditor::refresh()
{
    updateControlsFromTrack();
    presetBar.refresh();
}

void DrumEditor::updateControlsFromTrack()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto* track = currentTrack();

    if (track == nullptr)
    {
        titleLabel.setText (NI_TXT ("没有选中乐器"), juce::dontSendNotification);
        setEnabled (false);
        return;
    }

    setEnabled (true);
    titleLabel.setText (NI_TXT ("鼓机 · ") + track->name, juce::dontSendNotification);

    const auto& kit = track->drums;
    const auto pad = juce::jlimit (0, kDrumPads - 1, selectedPad);
    const auto& params = kit.pads[(size_t) pad];

    for (int i = 0; i < padButtons.size(); ++i)
        padButtons[i]->setToggleState (i == pad, juce::dontSendNotification);

    for (int i = 0; i < padButtons.size(); ++i)
        padButtons[i]->setEnabled (kit.pads[(size_t) i].enabled
                                       || i == pad);   // a disabled pad stays selectable

    padNameLabel.setText (drumPadName (pad), juce::dontSendNotification);
    padToggle.setToggleState (params.enabled, juce::dontSendNotification);
    padLevel.setValue ((double) params.level, juce::dontSendNotification);
    padPitch.setValue ((double) params.pitch, juce::dontSendNotification);
    padDecay.setValue ((double) params.decay, juce::dontSendNotification);
    padTone.setValue ((double) params.tone, juce::dontSendNotification);
    padSnap.setValue ((double) params.snap, juce::dontSendNotification);

    masterGain.setValue ((double) kit.gain, juce::dontSendNotification);
    masterPitch.setValue ((double) kit.masterPitch, juce::dontSendNotification);
    masterDecay.setValue ((double) kit.masterDecay, juce::dontSendNotification);
    masterWidth.setValue ((double) kit.width, juce::dontSendNotification);
}

void DrumEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void DrumEditor::pushParams (const DrumKitParams& params)
{
    audio.updatePreviewDrumParams (params);
    document.notifyEdit();
}

void DrumEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& kit = track->drums;
    const auto pad = juce::jlimit (0, kDrumPads - 1, selectedPad);
    auto& params = kit.pads[(size_t) pad];

    params.enabled = padToggle.getToggleState();
    params.level   = (float) padLevel.getValue();
    params.pitch   = (float) padPitch.getValue();
    params.decay   = (float) padDecay.getValue();
    params.tone    = (float) padTone.getValue();
    params.snap    = (float) padSnap.getValue();

    kit.gain        = (float) masterGain.getValue();
    kit.masterPitch = (float) masterPitch.getValue();
    kit.masterDecay = (float) masterDecay.getValue();
    kit.width       = (float) masterWidth.getValue();

    pushParams (kit);
}

void DrumEditor::triggerPadForTest (int pad)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    audio.triggerPreviewDrumNote (track->drums, juce::jlimit (0, kDrumPads - 1, pad), 110.0f);
}

void DrumEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    // C4 (60) is the kick, so the keyboard reads like the pad list.
    const auto pad = juce::jlimit (0, kDrumPads - 1, midiNoteNumber - 60);
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    previewMidiNote = midiNoteNumber;
    audio.triggerPreviewDrumNote (track->drums, pad, velocity * 127.0f);
}

void DrumEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        audio.releasePreviewNote();
        previewMidiNote = -1;
    }
}

//==============================================================================
void DrumEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setFont (theme::font (10.5f));

    for (const auto& row : rowLabels)
    {
        g.setColour (row.header ? theme::accent : theme::dimText);
        g.drawText (row.text, row.bounds, juce::Justification::centredLeft);
    }
}

void DrumEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    titleLabel.setBounds (area.removeFromTop (kTitleHeight));

    auto presetRow = area.removeFromTop (kPresetHeight);
    restoreButton.setBounds (presetRow.removeFromRight (130).reduced (0, 2));
    presetRow.removeFromRight (6);
    presetBar.setBounds (presetRow);

    area.removeFromTop (kRowGap);

    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    area.removeFromBottom (kRowGap);

    rowLabels.clear();

    // Left column: the twelve pads.
    auto padColumn = area.removeFromLeft (150);
    rowLabels.push_back ({ padColumn.removeFromTop (kHeaderHeight), NI_TXT ("鼓垫"), true });
    padColumn.removeFromTop (2);

    for (int pad = 0; pad < kDrumPads; ++pad)
        padButtons[pad]->setBounds (padColumn.removeFromTop (kPadButtonHeight - 2));

    area.removeFromLeft (12);

    // Middle column: the selected pad's controls.
    auto padArea = area.removeFromLeft (juce::jmax (200, area.getWidth() / 2));
    rowLabels.push_back ({ padArea.removeFromTop (kHeaderHeight), NI_TXT ("选中的鼓"), true });
    padArea.removeFromTop (2);

    auto nameRow = padArea.removeFromTop (kRowHeight);
    padNameLabel.setBounds (nameRow.removeFromLeft (90));
    padToggle.setBounds (nameRow.removeFromLeft (70).reduced (0, 2));
    nameRow.removeFromLeft (6);
    previewButton.setBounds (nameRow.removeFromLeft (70).reduced (0, 2));

    auto placeRow = [this] (juce::Rectangle<int>& column, const juce::String& text,
                            juce::Component& control)
    {
        auto row = column.removeFromTop (kRowHeight);
        rowLabels.push_back ({ row.removeFromLeft (kLabelWidth), text, false });
        control.setBounds (row);
    };

    placeRow (padArea, NI_TXT ("音量"), padLevel);
    placeRow (padArea, NI_TXT ("音高"), padPitch);
    placeRow (padArea, NI_TXT ("衰减"), padDecay);
    placeRow (padArea, NI_TXT ("音色"), padTone);
    placeRow (padArea, NI_TXT ("喳音"), padSnap);

    area.removeFromLeft (12);

    // Right column: the kit wide controls.
    auto globalArea = area;
    rowLabels.push_back ({ globalArea.removeFromTop (kHeaderHeight), NI_TXT ("全局"), true });
    globalArea.removeFromTop (2);

    placeRow (globalArea, NI_TXT ("总音量"), masterGain);
    placeRow (globalArea, NI_TXT ("总音高"), masterPitch);
    placeRow (globalArea, NI_TXT ("总衰减"), masterDecay);
    placeRow (globalArea, NI_TXT ("声场宽度"), masterWidth);
}

//==============================================================================
void DrumEditor::selectPadForTest (int pad)
{
    selectedPad = juce::jlimit (0, kDrumPads - 1, pad);
    refresh();
}

void DrumEditor::setPadParamForTest (int pad, int which, double value)
{
    selectPadForTest (pad);

    switch (which)
    {
        case 0: padLevel.setValue (value, juce::sendNotificationSync); break;
        case 1: padPitch.setValue (value, juce::sendNotificationSync); break;
        case 2: padDecay.setValue (value, juce::sendNotificationSync); break;
        case 3: padTone.setValue (value, juce::sendNotificationSync); break;
        case 4: padSnap.setValue (value, juce::sendNotificationSync); break;
        case 5: padToggle.setToggleState (value > 0.5, juce::dontSendNotification);
                pushCurrentParams(); break;
        default: break;
    }
}

bool DrumEditor::restoreDefaultsForTest()
{
    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    document.beginEdit (NI_TXT ("恢复默认鼓组"));

    if (auto* target = currentTrack())
        target->drums = DrumKitParams();

    refresh();
    return true;
}

//==============================================================================
std::vector<PresetBar::Entry> DrumEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    // A kit preset always carries the whole kit, so there is no factory bank
    // here: the list is the user's own kits.
    const auto& saved = presets.getDrumPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的鼓组 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

bool DrumEditor::userPresetExists (const juce::String& name) const
{
    return presets.findDrumPresetByName (name) >= 0;
}

bool DrumEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr || id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getDrumPresets().size())
        return false;

    const auto params = presets.getDrumPresets()[(size_t) index];

    document.beginEdit (NI_TXT ("载入鼓组预设"));

    if (auto* target = currentTrack())
        target->drums = params;

    refresh();

    if (auto* target = currentTrack())
        pushParams (target->drums);

    return true;
}

bool DrumEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->drums;
    params.name = name;

    const auto existing = presets.findDrumPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceDrumPreset (existing, params);
    }
    else
    {
        presets.addDrumPreset (params);
    }

    document.beginEdit (NI_TXT ("保存鼓组预设"));

    if (auto* target = currentTrack())
        target->drums.name = name;

    refresh();
    return true;
}

bool DrumEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getDrumPresets().size())
        return false;

    const auto oldName = presets.getDrumPresets()[(size_t) index].name;
    presets.renameDrumPreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->drums.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名鼓组预设"));

        if (auto* target = currentTrack())
            target->drums.name = newName;
    }

    refresh();
    return true;
}

bool DrumEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getDrumPresets().size())
        return false;

    presets.removeDrumPreset (index);
    refresh();
    return true;
}

bool DrumEditor::loadUserPresetForTest (int index)
{
    return loadPresetById (kUserPresetIdBase + index);
}

bool DrumEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

bool DrumEditor::renamePresetForTest (const juce::String& newName)
{
    return renamePresetById (presetBar.getSelectedIdForTest(), newName);
}

bool DrumEditor::deletePresetForTest()
{
    return deletePresetById (presetBar.getSelectedIdForTest());
}

//==============================================================================
juce::Rectangle<int> DrumEditor::getControlBoundsForTest (int which) const
{
    if (which >= 0 && which < kDrumPads)
        return padButtons[which]->getBounds();

    switch (which - kDrumPads)
    {
        case 0:  return padNameLabel.getBounds();
        case 1:  return padToggle.getBounds();
        case 2:  return padLevel.getBounds();
        case 3:  return padPitch.getBounds();
        case 4:  return padDecay.getBounds();
        case 5:  return padTone.getBounds();
        case 6:  return padSnap.getBounds();
        case 7:  return masterGain.getBounds();
        case 8:  return masterPitch.getBounds();
        case 9:  return masterDecay.getBounds();
        case 10: return masterWidth.getBounds();
        case 11: return keyboard.getBounds();
        case 12: return previewButton.getBounds();
        case 13: return restoreButton.getBounds();
        default: break;
    }

    return {};
}

} // namespace nidaw
