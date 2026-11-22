#include "GmEditor.h"
#include "Theme.h"

#include <cmath>

namespace hmdaw
{

GmEditor::GmEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                    PresetStore& presetsToUse)
    : document (documentToUse), audio (audioToUse), presets (presetsToUse)
{
    setOpaque (true);

    addAndMakeVisible (titleLabel);
    titleLabel.setText (NI_TXT ("GM 音色"), juce::dontSendNotification);
    titleLabel.setFont (theme::font (22.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (fontLabel);
    fontLabel.setFont (theme::font (11.0f));
    fontLabel.setColour (juce::Label::textColourId, theme::dimText);
    fontLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (categoryBox);
    for (int i = 0; i < 16; ++i)
        categoryBox.addItem (juce::String::fromUTF8 (gm::categoryName (i)), i + 1);
    categoryBox.setSelectedId (1, juce::dontSendNotification);
    categoryBox.onChange = [this]
    {
        if (! updating)
            rebuildProgramList();
    };

    addAndMakeVisible (programList);
    programList.setModel (this);
    programList.setRowHeight (26);
    programList.setColour (juce::ListBox::backgroundColourId, theme::panelRaised);
    programList.setColour (juce::ListBox::outlineColourId, theme::accentDim);

    addAndMakeVisible (programLabel);
    programLabel.setFont (theme::font (14.0f, true));
    programLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (drumButton);
    drumButton.onClick = [this]
    {
        if (updating)
            return;

        if (auto* track = currentTrack())
        {
            track->gm.drums = drumButton.getToggleState();
            applyEdit (NI_TXT ("切换 GM 鼓组"));
            refresh();
        }
    };

    addAndMakeVisible (volumeLabel);
    volumeLabel.setText (NI_TXT ("音量"), juce::dontSendNotification);
    volumeLabel.setFont (theme::font (12.0f));
    volumeLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (volumeSlider);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.onValueChange = [this]
    {
        if (! updating)
            if (auto* track = currentTrack())
            {
                track->gm.volume = (float) volumeSlider.getValue();
                applyEdit (NI_TXT ("GM 音量"));
            }
    };

    addAndMakeVisible (panLabel);
    panLabel.setText (NI_TXT ("声像"), juce::dontSendNotification);
    panLabel.setFont (theme::font (12.0f));
    panLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (panSlider);
    panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.onValueChange = [this]
    {
        if (! updating)
            if (auto* track = currentTrack())
            {
                track->gm.pan = (float) panSlider.getValue();
                applyEdit (NI_TXT ("GM 声像"));
            }
    };

    addAndMakeVisible (polyphonyLabel);
    polyphonyLabel.setText (NI_TXT ("复音"), juce::dontSendNotification);
    polyphonyLabel.setFont (theme::font (12.0f));
    polyphonyLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (polyphonyBox);
    polyphonyBox.addItem ("1", 1);
    polyphonyBox.addItem ("2", 2);
    polyphonyBox.addItem ("4", 3);
    polyphonyBox.addItem ("8", 4);
    polyphonyBox.addItem ("16", 5);
    polyphonyBox.addItem ("32", 6);
    polyphonyBox.addItem ("64", 7);
    polyphonyBox.onChange = [this]
    {
        if (! updating)
            if (auto* track = currentTrack())
            {
                track->gm.polyphony = choiceToPolyphony (polyphonyBox.getSelectedId());
                applyEdit (NI_TXT ("GM 复音"));
            }
    };

    addAndMakeVisible (bendLabel);
    bendLabel.setText (NI_TXT ("弯音范围"), juce::dontSendNotification);
    bendLabel.setFont (theme::font (12.0f));
    bendLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (bendBox);
    for (auto semitones : { 0, 1, 2, 3, 5, 7, 12 })
        bendBox.addItem (juce::String (semitones) + " st", semitones + 1);
    bendBox.onChange = [this]
    {
        if (! updating)
            if (auto* track = currentTrack())
            {
                track->gm.pitchBendRange = bendBox.getSelectedId() - 1;
                applyEdit (NI_TXT ("GM 弯音范围"));
            }
    };

    addAndMakeVisible (auditionButton);
    auditionButton.onClick = [this]
    {
        if (auto* track = currentTrack())
        {
            audio.triggerPreviewGmNote (track->gm, 440.0, 100.0f);
            startTimer (700);
        }
    };

    addAndMakeVisible (hintLabel);
    hintLabel.setText (NI_TXT ("拖动音符头/尾可以弯音；GM 音色按频率精确发声。"),
                       juce::dontSendNotification);
    hintLabel.setFont (theme::font (11.0f));
    hintLabel.setColour (juce::Label::textColourId, theme::dimText);

    document.addChangeListener (this);
    rebuildProgramList();
    refresh();
}

GmEditor::~GmEditor()
{
    stopTimer();
    document.removeChangeListener (this);
}

void GmEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

InstrumentTrack* GmEditor::currentTrack() const
{
    return document.findTrack (trackId);
}

void GmEditor::applyEdit (const juce::String& name)
{
    document.beginEdit (name);
    document.notifyEdit();
}

void GmEditor::selectProgramForTest (int program)
{
    applyProgram (program);
}

void GmEditor::setDrumModeForTest (bool drums)
{
    if (auto* track = currentTrack())
    {
        track->gm.drums = drums;
        refresh();
    }
}

void GmEditor::applyProgram (int program)
{
    program = juce::jlimit (0, 127, program);
    selectedProgram = program;

    if (auto* track = currentTrack())
    {
        track->gm.program = program;
        track->gm.drums = false;
        applyEdit (NI_TXT ("选择 GM 音色"));
    }

    refresh();
}

void GmEditor::rebuildProgramList()
{
    visiblePrograms.clear();

    const auto category = juce::jlimit (0, 15, categoryBox.getSelectedId() - 1);
    for (int i = 0; i < 128; ++i)
        if (gm::categoryOf (i) == category)
            visiblePrograms.push_back (i);

    programList.updateContent();

    int row = -1;
    for (int i = 0; i < (int) visiblePrograms.size(); ++i)
        if (visiblePrograms[(size_t) i] == selectedProgram)
            row = i;

    if (row >= 0)
        programList.selectRow (row, true, true);
    else
        programList.deselectAllRows();

    programList.repaint();
}

void GmEditor::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    auto* track = currentTrack();

    if (track == nullptr)
    {
        programLabel.setText (NI_TXT ("没有选中 GM 轨道"), juce::dontSendNotification);
        return;
    }

    const auto& params = track->gm;
    selectedProgram = juce::jlimit (0, 127, params.program);
    categoryBox.setSelectedId (gm::categoryOf (selectedProgram) + 1, juce::dontSendNotification);
    rebuildProgramList();

    programLabel.setText (gm::displayName (selectedProgram), juce::dontSendNotification);
    drumButton.setToggleState (params.drums, juce::dontSendNotification);
    volumeSlider.setValue (params.volume, juce::dontSendNotification);
    panSlider.setValue (params.pan, juce::dontSendNotification);
    polyphonyBox.setSelectedId (polyphonyToChoice (params.polyphony), juce::dontSendNotification);
    bendBox.setSelectedId (juce::jlimit (0, 12, params.pitchBendRange) + 1,
                           juce::dontSendNotification);

    const auto file = params.soundFontPath.isNotEmpty()
                          ? juce::File (params.soundFontPath)
                          : GmSoundFont::bundledFile();

    fontLabel.setText (file.existsAsFile() ? NI_TXT ("音色库：") + file.getFileName()
                                           : NI_TXT ("找不到 GeneralUserGS.sf2"),
                       juce::dontSendNotification);
    fontLabel.setColour (juce::Label::textColourId,
                         file.existsAsFile() ? theme::dimText : juce::Colours::orange);
}

int GmEditor::polyphonyToChoice (int voices) const
{
    if (voices <= 1)  return 1;
    if (voices <= 2)  return 2;
    if (voices <= 4)  return 3;
    if (voices <= 8)  return 4;
    if (voices <= 16) return 5;
    if (voices <= 32) return 6;
    return 7;
}

int GmEditor::choiceToPolyphony (int choice) const
{
    switch (choice)
    {
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        case 4: return 8;
        case 5: return 16;
        case 6: return 32;
        default: return 64;
    }
}

int GmEditor::getNumRows()
{
    return (int) visiblePrograms.size();
}

void GmEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                 bool rowIsSelected)
{
    if (row < 0 || row >= (int) visiblePrograms.size())
        return;

    if (rowIsSelected)
        g.fillAll (theme::accent.withAlpha (0.28f));

    const auto program = visiblePrograms[(size_t) row];
    g.setColour (rowIsSelected ? theme::text : theme::dimText);
    g.setFont (theme::font (12.5f));
    g.drawText (gm::displayName (program), 8, 0, width - 16, height,
                juce::Justification::centredLeft, true);
}

void GmEditor::selectedRowsChanged (int lastRowSelected)
{
    if (updating || lastRowSelected < 0
        || lastRowSelected >= (int) visiblePrograms.size())
        return;

    const auto program = visiblePrograms[(size_t) lastRowSelected];

    if (auto* track = currentTrack())
    {
        track->gm.program = program;
        track->gm.drums = false;
        applyEdit (NI_TXT ("选择 GM 音色"));
        refresh();
    }
}

void GmEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void GmEditor::timerCallback()
{
    audio.releasePreviewNote();
    stopTimer();
}

void GmEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (42);
    g.setColour (theme::accentDim);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
}

void GmEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto header = r.removeFromTop (38);
    titleLabel.setBounds (header.removeFromLeft (160));
    fontLabel.setBounds (header.removeFromRight (360));
    r.removeFromTop (8);

    auto left = r.removeFromLeft (540);
    auto right = r;

    categoryBox.setBounds (left.removeFromTop (28));
    left.removeFromTop (8);
    programList.setBounds (left.removeFromTop (left.getHeight() - 8));

    auto top = right.removeFromTop (58);
    programLabel.setBounds (top);
    drumButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (12);

    auto row = right.removeFromTop (24);
    volumeLabel.setBounds (row.removeFromLeft (50));
    volumeSlider.setBounds (row);
    right.removeFromTop (8);

    row = right.removeFromTop (24);
    panLabel.setBounds (row.removeFromLeft (50));
    panSlider.setBounds (row);
    right.removeFromTop (8);

    row = right.removeFromTop (24);
    polyphonyLabel.setBounds (row.removeFromLeft (50));
    polyphonyBox.setBounds (row.removeFromLeft (90));
    bendLabel.setBounds (row.removeFromLeft (80));
    bendBox.setBounds (row.removeFromLeft (90));
    right.removeFromTop (12);

    auditionButton.setBounds (right.removeFromTop (30).removeFromLeft (110));
    right.removeFromTop (8);
    hintLabel.setBounds (right.removeFromTop (36));
}

} // namespace hmdaw
