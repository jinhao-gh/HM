#include "SnapControl.h"
#include "Theme.h"

namespace nidaw
{

SnapControl::SnapControl (ProjectDocument& d, AppState& state, SnapArea which)
    : document (d), appState (state), area (which)
{
    addAndMakeVisible (combo);
    combo.addItem (NI_TXT ("1 拍"), 1);
    combo.addItem (NI_TXT ("1/2 拍"), 2);
    combo.addItem (NI_TXT ("1/4 拍"), 4);
    combo.addItem (NI_TXT ("1/n…"), 6);
    combo.addItem (NI_TXT ("不吸附"), 5);
    combo.onChange = [this]
    {
        if (updating)
            return;

        const auto id = combo.getSelectedId();

        if (id == 5)
        {
            settings().enabled = false;
            notifyChanged();
            return;
        }

        settings().enabled = true;

        if (id == 6)
        {
            denominatorEditor.setVisible (true);
            slashLabel.setVisible (true);
            resized();
            denominatorEditor.grabKeyboardFocus();
            denominatorEditor.selectAll();
            notifyChanged();
            return;
        }

        settings().denominator = juce::jmax (1, id);
        refresh();
        notifyChanged();
    };

    addAndMakeVisible (slashLabel);
    slashLabel.setText ("1/", juce::dontSendNotification);
    slashLabel.setFont (theme::font (10.5f));
    slashLabel.setColour (juce::Label::textColourId, theme::dimText);
    slashLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (denominatorEditor);
    denominatorEditor.setJustification (juce::Justification::centred);
    denominatorEditor.setInputRestrictions (3, "0123456789");
    denominatorEditor.setSelectAllWhenFocused (true);
    denominatorEditor.onReturnKey = [this] { commitDenominator(); };
    denominatorEditor.onFocusLost = [this] { commitDenominator(); };

    refresh();
}

SnapSettings& SnapControl::settings()
{
    auto& view = document.viewState();

    switch (area)
    {
        case SnapArea::track:      return view.trackSnap;
        case SnapArea::editor:     return view.editorSnap;
        case SnapArea::controller: return view.controllerSnap;
    }

    return view.editorSnap;
}

const SnapSettings& SnapControl::settings() const
{
    return const_cast<SnapControl*> (this)->settings();
}

void SnapControl::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& snap = settings();
    syncMenuFromSettings();
    denominatorEditor.setText (juce::String (snap.clampedDenominator()),
                               juce::dontSendNotification);
}

void SnapControl::syncMenuFromSettings()
{
    const auto& snap = settings();
    const auto n = snap.clampedDenominator();
    const auto custom = snap.enabled && n != 1 && n != 2 && n != 4;
    const auto id = ! snap.enabled ? 5
                    : custom ? 6
                             : n;
    combo.setSelectedId (id, juce::dontSendNotification);
    denominatorEditor.setVisible (custom);
    slashLabel.setVisible (custom);
    resized();
}

void SnapControl::commitDenominator()
{
    const auto n = juce::jlimit (1, 512, denominatorEditor.getText().getIntValue());
    denominatorEditor.setText (juce::String (n), juce::dontSendNotification);

    if (settings().denominator == n)
        return;

    settings().enabled = true;
    settings().denominator = n;
    refresh();
    notifyChanged();
}

void SnapControl::setDenominatorForTest (int n)
{
    denominatorEditor.setText (juce::String (juce::jlimit (1, 512, n)),
                               juce::dontSendNotification);
    settings().enabled = true;
    settings().denominator = juce::jlimit (1, 512, n);
    refresh();
    notifyChanged();
}

void SnapControl::setEnabledForTest (bool shouldBeEnabled)
{
    settings().enabled = shouldBeEnabled;
    refresh();
    notifyChanged();
}

void SnapControl::notifyChanged()
{
    appState.sendChangeMessage();

    if (onChanged != nullptr)
        onChanged();
}

void SnapControl::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
}

void SnapControl::resized()
{
    auto row = getLocalBounds().reduced (1, 1);

    if (denominatorEditor.isVisible())
    {
        combo.setBounds (row.removeFromLeft (juce::jmax (54, row.getWidth() - 46)));
        slashLabel.setBounds (row.removeFromLeft (14));
        denominatorEditor.setBounds (row);
    }
    else
    {
        combo.setBounds (row);
    }
}

} // namespace nidaw
