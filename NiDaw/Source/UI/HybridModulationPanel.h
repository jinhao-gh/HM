#pragma once

#include "../Core/ProjectDocument.h"
#include "SynthControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

class HybridModulationPanel : public juce::Component,
                              private juce::ChangeListener
{
public:
    HybridModulationPanel (ProjectDocument& document);
    ~HybridModulationPanel() override;

    void setTrack (juce::int64 trackId);
    void refresh();
    std::function<void()> onBack;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct ModRow
    {
        juce::TextButton enabled;
        juce::ComboBox source;
        juce::ComboBox destination;
        juce::Slider amount;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    InstrumentTrack* track();
    void push();
    void refreshControls();
    void setupModRow (ModRow& row);

    ProjectDocument& document;
    juce::int64 trackId = 0;
    bool updating = false;

    juce::TextButton back;
    juce::Label title;

    std::array<juce::TextButton, 4> envTabs;
    EnvelopeEditorView envelope;
    int envelopeIndex = 0;

    std::array<juce::TextButton, 6> lfoTabs;
    juce::ComboBox lfoWave;
    juce::Slider lfoRate, lfoPhase;
    juce::TextButton lfoSync, lfoRetrigger;
    int lfoIndex = 0;

    std::array<juce::Slider, 4> macros;
    std::array<juce::Label, 4> macroLabels;

    std::array<ModRow, 16> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridModulationPanel)
};

} // namespace nidaw
