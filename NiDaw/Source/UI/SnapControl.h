#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

enum class SnapArea { track, editor, controller };

/** Compact on/off + editable 1/n control shared by the three editing areas. */
class SnapControl : public juce::Component
{
public:
    SnapControl (ProjectDocument& document, AppState& appState, SnapArea area);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();

    std::function<void()> onChanged;

    juce::Rectangle<int> getToggleBoundsForTest() const { return combo.getBounds(); }
    juce::Rectangle<int> getComboBoundsForTest() const { return combo.getBounds(); }
    juce::Rectangle<int> getDenominatorBoundsForTest() const { return denominatorEditor.getBounds(); }
    int getComboItemCountForTest() const { return combo.getNumItems(); }
    juce::String getComboItemTextForTest (int itemId) const
    {
        return combo.getItemText (combo.indexOfItemId (itemId));
    }
    bool isCustomEditorVisibleForTest() const { return denominatorEditor.isVisible(); }
    int getDenominatorForTest() const { return settings().clampedDenominator(); }
    bool isEnabledForTest() const { return settings().enabled; }
    void setDenominatorForTest (int n);
    void setEnabledForTest (bool shouldBeEnabled);

private:
    SnapSettings& settings();
    const SnapSettings& settings() const;
    void commitDenominator();
    void notifyChanged();
    void syncMenuFromSettings();

    ProjectDocument& document;
    AppState& appState;
    SnapArea area;
    juce::ComboBox combo;
    juce::Label slashLabel;
    juce::TextEditor denominatorEditor;
    bool updating = false;
};

} // namespace nidaw
