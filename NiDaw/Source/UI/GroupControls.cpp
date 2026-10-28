#include "GroupControls.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    /** The five preset ratio keys, in the order the buttons and the 1..5 shortcuts
        offer them. */
    constexpr double kPresetRatios[] = { 2.0, 3.0, 5.0, 7.0, 9.0 };
}

GroupControls::GroupControls (ProjectDocument& documentToUse, AppState& appStateToUse,
                              OctaveLadder& ladderToUse)
    : document (documentToUse), appState (appStateToUse), ladder (ladderToUse)
{
    for (int i = 0; i < kReferencePaletteSize; ++i)
    {
        auto* button = colourButtons.add (new juce::TextButton());
        addAndMakeVisible (button);
        button->setColour (juce::TextButton::buttonColourId, theme::trackColour (i));
        button->setColour (juce::TextButton::buttonOnColourId, theme::trackColour (i));
        button->setTooltip (NI_TXT ("把选中的圆点都改成这个颜色"));
        button->onClick = [this, i] { applyPaletteColour (i); };
    }

    const juce::StringArray ratioNames { "1:2", "1:3", "1:5", "1:7", "1:9", NI_TXT ("自定义") };

    for (int i = 0; i < ratioNames.size(); ++i)
    {
        auto* button = ratioButtons.add (new juce::TextButton (ratioNames[i]));
        addAndMakeVisible (button);
        button->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        button->setTooltip (NI_TXT ("在选中圆点的 ×比率 与 ÷比率 位置生成叉点（快捷键 1-5）"));
        button->onClick = [this, i] { triggerRatioButton (i); };
    }

    addAndMakeVisible (customRatioEditor);
    customRatioEditor.setTextToShowWhenEmpty (NI_TXT ("如 1:4"), theme::dimText);
    customRatioEditor.onReturnKey = [this] { applyRatio (music::parseRatioText (customRatioEditor.getText())); };

    addAndMakeVisible (divisionEditor);
    divisionEditor.setTextToShowWhenEmpty (NI_TXT ("n"), theme::dimText);
    divisionEditor.setJustification (juce::Justification::centred);
    divisionEditor.onReturnKey = [this] { applyDivision(); };

    addAndMakeVisible (divisionButton);
    divisionButton.setButtonText (NI_TXT ("等分"));
    divisionButton.setTooltip (NI_TXT ("选中两个圆点后，把两点之间等分为 n 份"));
    divisionButton.onClick = [this] { applyDivision(); };

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (theme::font (10.5f));
    statusLabel.setColour (juce::Label::textColourId, theme::dimText);
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    document.addChangeListener (this);
    appState.addChangeListener (this);
    ladder.addChangeListener (this);   // dot selection changes

    // The segment's 1..5 keys come back through the same path the buttons use.
    ladder.onRatioKey = [this] (int index) { triggerRatioButton (index); };

    updateControlsFromState();
}

GroupControls::~GroupControls()
{
    ladder.onRatioKey = nullptr;
    ladder.removeChangeListener (this);
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void GroupControls::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateControlsFromState();
    repaint();
}

void GroupControls::refresh()
{
    updateControlsFromState();
    repaint();
}

bool GroupControls::isPaletteEnabled() const
{
    return ! colourButtons.isEmpty() && colourButtons[0]->isEnabled();
}

bool GroupControls::isRatioEnabledForTest() const
{
    return ! ratioButtons.isEmpty() && ratioButtons[0]->isEnabled();
}

juce::Rectangle<int> GroupControls::getRatioButtonBoundsForTest (int index) const
{
    if (index < 0 || index >= ratioButtons.size())
        return {};

    return ratioButtons[index]->getBounds();
}

void GroupControls::triggerRatioButton (int index)
{
    if (updating || index < 0)
        return;

    if (index < 5)
        applyRatio (kPresetRatios[index]);
    else
        applyRatio (music::parseRatioText (customRatioEditor.getText()));
}

void GroupControls::triggerColourForTest (int index)
{
    applyPaletteColour (index);
}

void GroupControls::updateControlsFromState()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto dotCount = ladder.getSelectedDotCount();
    const auto hasOne = dotCount == 1;
    const auto hasTwo = dotCount == 2;

    // A ratio lens needs exactly one dot to stand on (two dots means "divide these
    // two" instead), which is the same rule the note edit strip uses for its lens.
    for (auto* button : ratioButtons)
        button->setEnabled (hasOne);

    customRatioEditor.setEnabled (hasOne);
    divisionButton.setEnabled (hasTwo);
    divisionEditor.setEnabled (hasTwo);

    // The colour lands on every selected dot (the frame dots carry a colour too),
    // so one shared value for all of them.
    for (auto* button : colourButtons)
        button->setEnabled (dotCount >= 1);

    if (appState.referencePick == AppState::ReferencePick::reference)
    {
        statusLabel.setText (appState.armedDotValid
                                 ? NI_TXT ("基准音：已选圆点，请点一个音符")
                                 : NI_TXT ("基准音：请先点线段上的一个圆点"),
                             juce::dontSendNotification);
    }
    else if (appState.referencePick == AppState::ReferencePick::apply)
    {
        statusLabel.setText (appState.armedDotValid
                                 ? NI_TXT ("应用：已选圆点，请点一个音符")
                                 : NI_TXT ("应用：请先点线段上的一个圆点"),
                             juce::dontSendNotification);
    }
    else if (hasTwo)
        statusLabel.setText (NI_TXT ("已选两个圆点：可以等分；频率比只支持一个圆点"),
                             juce::dontSendNotification);
    else if (hasOne)
        statusLabel.setText (NI_TXT ("已选一个圆点，可选比率或颜色"),
                             juce::dontSendNotification);
    else if (dotCount > 2)
        statusLabel.setText (NI_TXT ("已选多个圆点：只能改颜色；频率比只支持一个圆点"),
                             juce::dontSendNotification);
    else
        statusLabel.setText (NI_TXT ("点圆点选中；拖动可框选；点叉点变圆点；右键圆点删除"),
                             juce::dontSendNotification);
}

void GroupControls::applyRatio (double ratio)
{
    if (updating || ratio <= 0.0)
        return;

    if (ladder.getSelectedDotCount() != 1)
        return;

    ladder.showRatioCrosses (ratio);
    updateControlsFromState();
}

void GroupControls::applyPaletteColour (int index)
{
    if (updating)
        return;

    const auto selected = ladder.getSelectedDotPositions();

    if (selected.empty())
        return;

    document.applyEdit (NI_TXT ("修改圆点颜色"), [selected, index] (Project& project)
    {
        for (const auto position : selected)
        {
            // The two frame dots are the same pitch class, so they share one colour.
            if (position <= 1.0e-9 || position >= 1.0 - 1.0e-9)
            {
                project.referenceGroup.frameColourIndex = index;
                continue;
            }

            const auto found = music::findDotNear (project.referenceGroup,
                                                   music::foldPosition (position), 1.0e-6);

            if (found >= 0)
                project.referenceGroup.dots[(size_t) found].colourIndex = index;
        }
    });
}

void GroupControls::applyDivision()
{
    if (updating)
        return;

    const auto parts = divisionEditor.getText().getIntValue();

    if (parts > 1 && ladder.getSelectedDotCount() == 2)
        ladder.showDivisionCrosses (parts);

    updateControlsFromState();
}

void GroupControls::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void GroupControls::resized()
{
    auto area = getLocalBounds();

    auto divisionRow = area.removeFromBottom (22);
    divisionEditor.setBounds (divisionRow.removeFromLeft (34));
    divisionRow.removeFromLeft (4);
    divisionButton.setBounds (divisionRow.removeFromLeft (48));
    divisionRow.removeFromLeft (6);
    customRatioEditor.setBounds (divisionRow);

    area.removeFromBottom (3);

    auto ratioRow = area.removeFromBottom (22);
    const auto ratioWidth = juce::jmax (26, ratioRow.getWidth() / 6);

    for (auto* button : ratioButtons)
        button->setBounds (ratioRow.removeFromLeft (ratioWidth));

    area.removeFromBottom (3);

    auto colourRow = area.removeFromBottom (20);
    const auto colourWidth = juce::jmax (16, colourRow.getWidth() / kReferencePaletteSize);

    for (auto* button : colourButtons)
        button->setBounds (colourRow.removeFromLeft (colourWidth).reduced (1));

    area.removeFromBottom (3);
    statusLabel.setBounds (area.removeFromBottom (16));
}

} // namespace nidaw
