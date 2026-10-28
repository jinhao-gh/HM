#pragma once

#include "../Core/ProjectDocument.h"
#include "SynthControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** One page that shows the whole hybrid voice at a glance: five sources, the
    two filters, the four FX slots and the master gain. */
class HybridMixerPanel : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit HybridMixerPanel (ProjectDocument& document);
    ~HybridMixerPanel() override;

    void setTrack (juce::int64 trackId);
    void refresh();
    std::function<void()> onBack;

    void paint (juce::Graphics& g) override;
    void resized() override;

    juce::Rectangle<int> getSourceBoundsForTest (int source) const;
    juce::Rectangle<int> getFilterBoundsForTest() const;

private:
    struct SourceStrip
    {
        juce::Label name;
        juce::Label mode;
        juce::TextButton enabled;
        juce::Slider level;
        juce::Slider pan;
        juce::Label levelLabel;
        juce::Label panLabel;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    InstrumentTrack* track();
    void setupSource (int index);
    void refreshControls();
    void push (const juce::String& editName);

    ProjectDocument& document;
    juce::int64 trackId = 0;
    bool updating = false;

    juce::TextButton back;
    juce::Label title;
    std::array<SourceStrip, 5> sources;

    juce::Label filterTitle;
    std::array<juce::Slider, 2> filterCutoff;
    std::array<juce::Slider, 2> filterResonance;
    std::array<juce::Label, 2> filterCutoffLabel;
    std::array<juce::Label, 2> filterResonanceLabel;
    juce::ComboBox filterRoute;

    juce::Label fxTitle;
    std::array<juce::TextButton, 4> fxEnable;
    std::array<juce::ComboBox, 4> fxType;
    std::array<juce::Label, 4> fxLabel;

    juce::Slider masterGain;
    juce::Label masterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridMixerPanel)
};

} // namespace nidaw
