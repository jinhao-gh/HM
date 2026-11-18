#include "HybridFxPanel.h"
#include "Theme.h"

#include "../Audio/HybridFxChain.h"

namespace hmdaw
{

namespace
{
    constexpr int kHeaderHeight = 38;
    constexpr int kSlotHeight = 108;
    constexpr int kSlotGap = 6;
}

HybridFxPanel::HybridFxPanel (ProjectDocument& d)
    : document (d)
{
    addAndMakeVisible (back);
    back.setButtonText (NI_TXT ("返回混合器"));
    back.onClick = [this] { if (onBack != nullptr) onBack(); };

    addAndMakeVisible (title);
    title.setText (NI_TXT ("混合器 FX · 四个串联槽"), juce::dontSendNotification);
    title.setFont (theme::font (13.0f, true));
    title.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (hint);
    hint.setText (NI_TXT ("效果作用在五源混音之后、轨道插入效果器之前；旁通不改变干声。"),
                  juce::dontSendNotification);
    hint.setFont (theme::font (10.0f));
    hint.setColour (juce::Label::textColourId, theme::dimText);

    for (int i = 0; i < 4; ++i)
        setupSlot (i);

    document.addChangeListener (this);
    refresh();
}

HybridFxPanel::~HybridFxPanel()
{
    document.removeChangeListener (this);
}

void HybridFxPanel::setupSlot (int index)
{
    auto& slot = slots[(size_t) index];

    addAndMakeVisible (slot.index);
    slot.index.setText ("FX " + juce::String (index + 1), juce::dontSendNotification);
    slot.index.setFont (theme::font (11.0f, true));
    slot.index.setColour (juce::Label::textColourId, theme::accent);
    slot.index.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (slot.enabled);
    slot.enabled.setButtonText (NI_TXT ("开"));
    slot.enabled.setClickingTogglesState (true);
    slot.enabled.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    slot.enabled.onClick = [this, index]
    {
        if (updating)
            return;
        document.beginEdit (NI_TXT ("开关混合器 FX"));
        push (NI_TXT ("开关混合器 FX"));
        updateParamLabels (index);
    };

    addAndMakeVisible (slot.bypass);
    slot.bypass.setButtonText (NI_TXT ("旁通"));
    slot.bypass.setClickingTogglesState (true);
    slot.bypass.onClick = [this]
    {
        if (updating)
            return;
        document.beginEdit (NI_TXT ("旁通混合器 FX"));
        push (NI_TXT ("旁通混合器 FX"));
    };

    addAndMakeVisible (slot.type);
    for (int type = 0; type < hybridFxTypeCount(); ++type)
        slot.type.addItem (hybridFxTypeName ((HybridFxType) type), type + 1);
    slot.type.onChange = [this, index]
    {
        if (updating)
            return;
        document.beginEdit (NI_TXT ("切换混合器 FX"));
        push (NI_TXT ("切换混合器 FX"));
        updateParamLabels (index);
    };

    auto setupKnob = [this] (juce::Slider& knob, juce::Label& label,
                             const juce::String& text)
    {
        addAndMakeVisible (knob);
        configureSynthKnob (knob);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 13);
        knob.setRange (0.0, 1.0, 0.001);
        knob.onDragStart = [this] { document.beginEdit (NI_TXT ("调整混合器 FX")); };
        knob.onValueChange = [this] { if (! updating) push (NI_TXT ("调整混合器 FX")); };

        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (9.0f));
        label.setColour (juce::Label::textColourId, theme::dimText);
        label.setJustificationType (juce::Justification::centred);
    };

    setupKnob (slot.mix, slot.mixLabel, NI_TXT ("干湿"));
    setupKnob (slot.param1, slot.param1Label, NI_TXT ("参数 1"));
    setupKnob (slot.param2, slot.param2Label, NI_TXT ("参数 2"));
}

void HybridFxPanel::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

InstrumentTrack* HybridFxPanel::track()
{
    auto* instrument = document.findTrack (trackId);
    return instrument != nullptr && instrument->kind == TrackKind::hybrid ? instrument : nullptr;
}

void HybridFxPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void HybridFxPanel::refresh()
{
    refreshControls();
}

void HybridFxPanel::refreshControls()
{
    auto* instrument = track();

    if (instrument == nullptr)
        return;

    updating = true;

    for (int i = 0; i < 4; ++i)
    {
        const auto& fx = instrument->hybrid.fxSlots[(size_t) i];
        auto& slot = slots[(size_t) i];
        slot.enabled.setToggleState (fx.enabled, juce::dontSendNotification);
        slot.bypass.setToggleState (fx.bypass, juce::dontSendNotification);
        slot.type.setSelectedId ((int) fx.type + 1, juce::dontSendNotification);
        slot.mix.setValue ((double) fx.mix, juce::dontSendNotification);
        slot.param1.setValue ((double) fx.param1, juce::dontSendNotification);
        slot.param2.setValue ((double) fx.param2, juce::dontSendNotification);
        updateParamLabels (i);
    }

    updating = false;
}

void HybridFxPanel::updateParamLabels (int index)
{
    const auto type = (HybridFxType) juce::jlimit (0, hybridFxTypeCount() - 1,
                                                   slots[(size_t) index].type.getSelectedId() - 1);
    slots[(size_t) index].param1Label.setText (hybridFxParam1Name (type),
                                              juce::dontSendNotification);
    slots[(size_t) index].param2Label.setText (hybridFxParam2Name (type),
                                              juce::dontSendNotification);
}

void HybridFxPanel::push (const juce::String&)
{
    auto* instrument = track();

    if (instrument == nullptr || updating)
        return;

    for (int i = 0; i < 4; ++i)
    {
        auto& fx = instrument->hybrid.fxSlots[(size_t) i];
        auto& slot = slots[(size_t) i];
        fx.enabled = slot.enabled.getToggleState();
        fx.bypass = slot.bypass.getToggleState();
        fx.type = (HybridFxType) juce::jlimit (0, hybridFxTypeCount() - 1,
                                               slot.type.getSelectedId() - 1);
        fx.mix = (float) slot.mix.getValue();
        fx.param1 = (float) slot.param1.getValue();
        fx.param2 = (float) slot.param2.getValue();
    }

    document.notifyEdit();
    refreshControls();
}

void HybridFxPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    for (int i = 0; i < 4; ++i)
    {
        const auto bounds = getSlotBoundsForTest (i).toFloat();
        g.setColour (theme::panelBackground.brighter (0.06f));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (theme::outline.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

        if (i < 3)
        {
            const auto x = bounds.getCentreX();
            const auto y1 = bounds.getBottom() + 1.0f;
            const auto y2 = y1 + (float) kSlotGap - 2.0f;
            g.setColour (theme::accent.withAlpha (0.55f));
            g.drawLine (x, y1, x, y2, 1.5f);
        }
    }
}

void HybridFxPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (kHeaderHeight);
    back.setBounds (header.removeFromLeft (104).reduced (0, 4));
    header.removeFromLeft (8);
    title.setBounds (header.removeFromTop (22));
    hint.setBounds (header);
    area.removeFromTop (6);

    for (int i = 0; i < 4; ++i)
    {
        auto row = area.removeFromTop (kSlotHeight);
        auto& slot = slots[(size_t) i];
        auto left = row.removeFromLeft (310);
        slot.index.setBounds (left.removeFromTop (24).removeFromLeft (62));
        slot.enabled.setBounds (left.removeFromTop (30).removeFromLeft (54).reduced (1));
        slot.bypass.setBounds (left.removeFromTop (30).removeFromLeft (64).reduced (1));

        auto typeArea = row.removeFromLeft (170).reduced (4, 18);
        slot.type.setBounds (typeArea);

        auto knobArea = row;
        const auto knobWidth = knobArea.getWidth() / 3;

        auto layoutKnob = [] (juce::Rectangle<int> cell, juce::Slider& knob,
                              juce::Label& label)
        {
            cell.reduce (3, 0);
            knob.setBounds (cell.removeFromTop (76));
            label.setBounds (cell.removeFromTop (16));
        };

        layoutKnob (knobArea.removeFromLeft (knobWidth), slot.mix, slot.mixLabel);
        layoutKnob (knobArea.removeFromLeft (knobWidth), slot.param1, slot.param1Label);
        layoutKnob (knobArea, slot.param2, slot.param2Label);

        if (i < 3)
            area.removeFromTop (kSlotGap);
    }
}

juce::Rectangle<int> HybridFxPanel::getSlotBoundsForTest (int index) const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kHeaderHeight + 6);

    for (int i = 0; i < 4; ++i)
    {
        auto row = area.removeFromTop (kSlotHeight);

        if (i == index)
            return row;

        if (i < 3)
            area.removeFromTop (kSlotGap);
    }

    return {};
}

int HybridFxPanel::getTypeForTest (int index) const
{
    return slots[(size_t) juce::jlimit (0, 3, index)].type.getSelectedId() - 1;
}

void HybridFxPanel::setTypeForTest (int index, int type)
{
    slots[(size_t) juce::jlimit (0, 3, index)].type.setSelectedId (
        juce::jlimit (0, hybridFxTypeCount() - 1, type) + 1, juce::sendNotificationSync);
}

int HybridFxPanel::getEnabledCountForTest() const
{
    int count = 0;

    for (const auto& slot : slots)
        if (slot.enabled.getToggleState())
            ++count;

    return count;
}

} // namespace hmdaw
