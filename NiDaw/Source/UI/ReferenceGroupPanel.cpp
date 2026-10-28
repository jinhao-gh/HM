#include "ReferenceGroupPanel.h"
#include "Theme.h"

namespace nidaw
{

ReferenceGroupPanel::ReferenceGroupPanel (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse), ladder (documentToUse, appStateToUse),
      controls (documentToUse, appStateToUse, ladder)
{
    addAndMakeVisible (historyButton);
    historyButton.setButtonText (NI_TXT ("历史"));
    historyButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    historyButton.onClick = [this] { appState.toggleSidePanel (AppState::SidePanel::history); };

    addAndMakeVisible (presetButton);
    presetButton.setButtonText (NI_TXT ("预设"));
    presetButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    presetButton.onClick = [this] { appState.toggleSidePanel (AppState::SidePanel::presets); };

    addAndMakeVisible (ladder);
    addAndMakeVisible (controls);

    addAndMakeVisible (referenceButton);
    referenceButton.setButtonText (NI_TXT ("选择基准音"));
    referenceButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    referenceButton.setTooltip (NI_TXT ("点它，再点一个圆点，再点一个音符"));
    referenceButton.onClick = [this]
    {
        appState.setReferencePick (appState.referencePick == AppState::ReferencePick::reference
                                       ? AppState::ReferencePick::none
                                       : AppState::ReferencePick::reference);
    };

    addAndMakeVisible (savePresetButton);
    savePresetButton.setButtonText (NI_TXT ("保存预设"));
    savePresetButton.onClick = [this] { if (onSavePreset) onSavePreset(); };

    addAndMakeVisible (applyButton);
    applyButton.setButtonText (NI_TXT ("应用"));
    applyButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    applyButton.setTooltip (NI_TXT ("点它，再点一个圆点，再点一个音符"));
    applyButton.onClick = [this]
    {
        appState.setReferencePick (appState.referencePick == AppState::ReferencePick::apply
                                       ? AppState::ReferencePick::none
                                       : AppState::ReferencePick::apply);
    };

    document.addChangeListener (this);
    appState.addChangeListener (this);

    updateControlsFromState();
}

ReferenceGroupPanel::~ReferenceGroupPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void ReferenceGroupPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateControlsFromState();
    repaint();
}

void ReferenceGroupPanel::refresh()
{
    updateControlsFromState();
    ladder.refresh();
    controls.refresh();
    repaint();
}

void ReferenceGroupPanel::updateControlsFromState()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    historyButton.setToggleState (appState.sidePanel == AppState::SidePanel::history,
                                  juce::dontSendNotification);
    presetButton.setToggleState (appState.sidePanel == AppState::SidePanel::presets,
                                 juce::dontSendNotification);

    referenceButton.setToggleState (appState.referencePick == AppState::ReferencePick::reference,
                                    juce::dontSendNotification);
    applyButton.setToggleState (appState.referencePick == AppState::ReferencePick::apply,
                                juce::dontSendNotification);

    controls.refresh();
}

bool ReferenceGroupPanel::isPaletteEnabled() const
{
    return controls.isPaletteEnabled();
}

void ReferenceGroupPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void ReferenceGroupPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 5);

    auto topRow = area.removeFromTop (24);
    historyButton.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2 - 2));
    topRow.removeFromLeft (4);
    presetButton.setBounds (topRow);

    area.removeFromTop (5);

    auto bottom = area.removeFromBottom (24);
    const auto buttonWidth = bottom.getWidth() / 3;
    referenceButton.setBounds (bottom.removeFromLeft (buttonWidth - 3));
    bottom.removeFromLeft (4);
    savePresetButton.setBounds (bottom.removeFromLeft (buttonWidth - 3));
    bottom.removeFromLeft (4);
    applyButton.setBounds (bottom);

    area.removeFromBottom (3);

    // The status / colour / ratio / division block is shared with the group editor,
    // so it is laid out in one place (GroupControls) instead of twice.
    controls.setBounds (area.removeFromBottom (GroupControls::preferredHeight));

    area.removeFromBottom (3);
    ladder.setBounds (area);
}

} // namespace nidaw
