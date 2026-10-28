#pragma once

#include "../Core/ProjectDocument.h"
#include "SynthControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** The four Serum-style effect slots of the hybrid instrument.

    The panel edits the project model directly; the audio engine reads the same
    HybridParams::fxSlots, so every knob change is heard on the next block.
*/
class HybridFxPanel : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit HybridFxPanel (ProjectDocument& document);
    ~HybridFxPanel() override;

    void setTrack (juce::int64 trackId);
    void refresh();
    std::function<void()> onBack;

    void paint (juce::Graphics& g) override;
    void resized() override;

    juce::Rectangle<int> getSlotBoundsForTest (int index) const;
    int getTypeForTest (int index) const;
    void setTypeForTest (int index, int type);
    int getEnabledCountForTest() const;

private:
    struct SlotControls
    {
        juce::Label index;
        juce::TextButton enabled;
        juce::TextButton bypass;
        juce::ComboBox type;
        juce::Slider mix;
        juce::Slider param1;
        juce::Slider param2;
        juce::Label mixLabel;
        juce::Label param1Label;
        juce::Label param2Label;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    InstrumentTrack* track();
    void setupSlot (int index);
    void refreshControls();
    void updateParamLabels (int index);
    void push (const juce::String& editName);

    ProjectDocument& document;
    juce::int64 trackId = 0;
    bool updating = false;

    juce::TextButton back;
    juce::Label title;
    juce::Label hint;
    std::array<SlotControls, 4> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridFxPanel)
};

} // namespace nidaw
