#include "GroupEditorPanel.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    constexpr int kHeadingHeight = 18;
    constexpr int kNameRowHeight = 24;
    constexpr int kAnchorRowHeight = 16;
    constexpr int kFooterHeight = 24;
}

GroupEditorPanel::GroupEditorPanel (ProjectDocument& documentToUse, AppState& appStateToUse,
                                    PresetStore& presetsToUse)
    : document (documentToUse), appState (appStateToUse), presets (presetsToUse),
      ladder (documentToUse, appStateToUse),
      controls (documentToUse, appStateToUse, ladder)
{
    addAndMakeVisible (headingLabel);
    headingLabel.setFont (theme::font (12.0f, true));
    headingLabel.setColour (juce::Label::textColourId, theme::accent);
    headingLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (nameEditor);
    nameEditor.setFont (theme::font (12.5f));
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.setTextToShowWhenEmpty (NI_TXT ("名字"), theme::dimText);

    addAndMakeVisible (backButton);
    backButton.setButtonText (NI_TXT ("返回"));
    backButton.setTooltip (NI_TXT ("放弃没保存的改动，回到参考组栏"));
    backButton.onClick = [this] { triggerBackForTest(); };

    addAndMakeVisible (anchorLabel);
    anchorLabel.setFont (theme::font (10.5f));
    anchorLabel.setColour (juce::Label::textColourId, theme::dimText);
    anchorLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (ladder);
    addAndMakeVisible (controls);

    addAndMakeVisible (saveButton);
    saveButton.setButtonText (NI_TXT ("保存"));
    saveButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    saveButton.setTooltip (NI_TXT ("写回原来那一条（预设就覆盖它，工程里的参考线组就改它）"));
    saveButton.onClick = [this] { triggerSaveForTest(); };

    addAndMakeVisible (saveAsButton);
    saveAsButton.setButtonText (NI_TXT ("另存为"));
    saveAsButton.setTooltip (NI_TXT ("用名字框里的名字存成一个新的预设"));
    saveAsButton.onClick = [this] { triggerSaveAsForTest(); };

    // 1..5 land on this column while it has the keyboard (the segment grabs the
    // focus on a click, exactly like the reference group column).
    setWantsKeyboardFocus (true);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    applySession();
}

GroupEditorPanel::~GroupEditorPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void GroupEditorPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void GroupEditorPanel::refresh()
{
    applySession();
    ladder.refresh();
    controls.refresh();
    repaint();
}

void GroupEditorPanel::applySession()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& session = appState.groupEditor;
    const auto lineGroup = session.isLineGroup();

    // What is being edited: a preset in the library, or one of the project's own
    // reference line groups.
    juce::String name;

    if (lineGroup)
    {
        if (session.lineId != 0)
        {
            if (const auto* entry = document.getProject().findReferenceLine (session.lineId))
                name = entry->name;
        }
        else
        {
            const auto& list = presets.getReferenceLineGroups();

            if (session.presetIndex >= 0 && session.presetIndex < (int) list.size())
                name = list[(size_t) session.presetIndex].name;
        }
    }
    else
    {
        const auto& list = presets.getReferenceGroups();

        if (session.presetIndex >= 0 && session.presetIndex < (int) list.size())
            name = list[(size_t) session.presetIndex].name;
    }

    headingLabel.setText ((lineGroup ? NI_TXT ("编辑参考线组　")
                                     : NI_TXT ("编辑参考组　")) + name,
                          juce::dontSendNotification);

    // Only rewrite the box while the user is not typing in it, so a refresh from a
    // document edit cannot eat half a name.
    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (name, juce::dontSendNotification);

    anchorLabel.setVisible (lineGroup);

    if (lineGroup)
        anchorLabel.setText (NI_TXT ("锚点 ") + formatHz (session.anchorHz)
                                 + NI_TXT ("　位置 ") + juce::String (session.anchorPosition, 3),
                             juce::dontSendNotification);

    // The two looks: a line group is one plain line with frequencies beside its
    // dots; a reference group keeps the frame-octave highlight and its ratios.
    OctaveLadder::Look look;
    look.plainLine = lineGroup;
    look.frequencyLabels = lineGroup;
    look.anchorHz = session.anchorHz;
    look.anchorPosition = session.anchorPosition;
    ladder.setLook (look);
}

void GroupEditorPanel::triggerBackForTest()
{
    if (onBack != nullptr)
        onBack();
}

void GroupEditorPanel::triggerSaveForTest()
{
    if (onSave != nullptr)
        onSave (nameEditor.getText().trim());
}

void GroupEditorPanel::triggerSaveAsForTest()
{
    if (onSaveAs != nullptr)
        onSaveAs (nameEditor.getText().trim());
}

void GroupEditorPanel::setNameForTest (const juce::String& name)
{
    nameEditor.setText (name, juce::dontSendNotification);
}

bool GroupEditorPanel::keyPressed (const juce::KeyPress& key)
{
    // The same five ratio keys the reference group column answers: they pick a
    // ratio for the single selected dot, and do nothing when there is no single dot
    // to stand the lens on.
    const auto code = key.getKeyCode();

    if (key.getModifiers().isAnyModifierKeyDown() || code < '1' || code > '5')
        return false;

    controls.triggerRatioButton (code - '1');
    return true;
}

void GroupEditorPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground.brighter (0.03f));
    g.fillRect (getLocalBounds());

    // A frame, so the editing column is never mistaken for the ordinary one.
    g.setColour (theme::accent.withAlpha (0.5f));
    g.drawRect (getLocalBounds(), 1);
}

void GroupEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 5);

    headingLabel.setBounds (area.removeFromTop (kHeadingHeight));

    auto nameRow = area.removeFromTop (kNameRowHeight);
    backButton.setBounds (nameRow.removeFromRight (56));
    nameRow.removeFromRight (4);
    nameEditor.setBounds (nameRow.reduced (0, 1));

    if (anchorLabel.isVisible())
        anchorLabel.setBounds (area.removeFromTop (kAnchorRowHeight));

    area.removeFromTop (4);

    auto footer = area.removeFromBottom (kFooterHeight);
    const auto half = footer.getWidth() / 2;
    saveButton.setBounds (footer.removeFromLeft (half - 2));
    footer.removeFromLeft (4);
    saveAsButton.setBounds (footer);

    area.removeFromBottom (3);

    controls.setBounds (area.removeFromBottom (GroupControls::preferredHeight));

    area.removeFromBottom (3);
    ladder.setBounds (area);
}

} // namespace nidaw
