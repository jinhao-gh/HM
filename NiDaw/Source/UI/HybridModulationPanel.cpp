#include "HybridModulationPanel.h"
#include "Theme.h"

namespace nidaw
{

HybridModulationPanel::HybridModulationPanel (ProjectDocument& d)
    : document (d)
{
    addAndMakeVisible (back);
    back.setButtonText (NI_TXT ("返回混合器"));
    back.onClick = [this] { if (onBack != nullptr) onBack(); };
    addAndMakeVisible (title);
    title.setText (NI_TXT ("包络 / LFO / 调制矩阵"), juce::dontSendNotification);
    title.setFont (theme::font (13.0f, true));
    title.setColour (juce::Label::textColourId, theme::accent);

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (envTabs[(size_t) i]);
        envTabs[(size_t) i].setButtonText ("ENV " + juce::String (i + 1));
        envTabs[(size_t) i].setClickingTogglesState (true);
        envTabs[(size_t) i].setRadioGroupId (0x58a1);
        envTabs[(size_t) i].onClick = [this, i]
        {
            envelopeIndex = i;
            refreshControls();
        };

        addAndMakeVisible (macros[(size_t) i]);
        configureSynthKnob (macros[(size_t) i]);
        macros[(size_t) i].setRange (0.0, 1.0, 0.01);
        macros[(size_t) i].onDragStart = [this] { document.beginEdit (NI_TXT ("调整宏")); };
        macros[(size_t) i].onValueChange = [this] { if (! updating) push(); };
        addAndMakeVisible (macroLabels[(size_t) i]);
        macroLabels[(size_t) i].setText (NI_TXT ("宏 ") + juce::String (i + 1),
                                         juce::dontSendNotification);
        macroLabels[(size_t) i].setFont (theme::font (9.0f));
        macroLabels[(size_t) i].setJustificationType (juce::Justification::centred);
        macroLabels[(size_t) i].setEditable (true, true, false);
        macroLabels[(size_t) i].onEditorHide = [this, i]
        {
            if (updating)
                return;

            if (auto* t = track())
            {
                document.beginEdit (NI_TXT ("重命名宏"));
                t->hybrid.macroNames[(size_t) i] = macroLabels[(size_t) i].getText();
                document.notifyEdit();
            }
        };
    }

    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible (lfoTabs[(size_t) i]);
        lfoTabs[(size_t) i].setButtonText ("LFO " + juce::String (i + 1));
        lfoTabs[(size_t) i].setClickingTogglesState (true);
        lfoTabs[(size_t) i].setRadioGroupId (0x58a2);
        lfoTabs[(size_t) i].onClick = [this, i]
        {
            lfoIndex = i;
            refreshControls();
        };
    }

    addAndMakeVisible (envelope);
    envelope.onGesture = [this] (bool begin, EnvelopeEditorView::Values v)
    {
        if (updating)
            return;
        if (begin)
            document.beginEdit (NI_TXT ("编辑包络"));
        if (auto* t = track())
        {
            auto& e = t->hybrid.envelopes[(size_t) envelopeIndex];
            e.attack = v.attack;
            e.decay = v.decay;
            e.sustain = v.sustain;
            e.release = v.release;
        }
        push();
    };

    addAndMakeVisible (lfoWave);
    lfoWave.addItem (NI_TXT ("三角"), 1);
    lfoWave.addItem (NI_TXT ("正弦"), 2);
    lfoWave.addItem (NI_TXT ("方波"), 3);
    lfoWave.addItem (NI_TXT ("锯齿"), 4);
    lfoWave.addItem (NI_TXT ("采样保持"), 5);
    lfoWave.onChange = [this] { if (! updating) { document.beginEdit (NI_TXT ("调整 LFO")); push(); } };
    addAndMakeVisible (lfoRate);
    configureSynthKnob (lfoRate);
    lfoRate.setRange (1.0, 10000.0, 1.0);
    lfoRate.setSkewFactorFromMidPoint (500.0);
    lfoRate.setTextValueSuffix (" ms");
    lfoRate.onDragStart = [this] { document.beginEdit (NI_TXT ("调整 LFO")); };
    lfoRate.onValueChange = [this] { if (! updating) push(); };
    addAndMakeVisible (lfoPhase);
    configureSynthKnob (lfoPhase);
    lfoPhase.setRange (0.0, 1.0, 0.01);
    lfoPhase.onDragStart = [this] { document.beginEdit (NI_TXT ("调整 LFO")); };
    lfoPhase.onValueChange = [this] { if (! updating) push(); };
    addAndMakeVisible (lfoSync);
    lfoSync.setButtonText (NI_TXT ("同步"));
    lfoSync.setClickingTogglesState (true);
    lfoSync.onClick = [this] { document.beginEdit (NI_TXT ("调整 LFO")); push(); };
    addAndMakeVisible (lfoRetrigger);
    lfoRetrigger.setButtonText (NI_TXT ("重触发"));
    lfoRetrigger.setClickingTogglesState (true);
    lfoRetrigger.onClick = [this] { document.beginEdit (NI_TXT ("调整 LFO")); push(); };

    for (auto& row : rows)
        setupModRow (row);

    for (auto& tab : envTabs) tab.setVisible (false);
    for (auto& tab : lfoTabs) tab.setVisible (false);
    envelope.setVisible (false);
    lfoWave.setVisible (false);
    lfoRate.setVisible (false);
    lfoPhase.setVisible (false);
    lfoSync.setVisible (false);
    lfoRetrigger.setVisible (false);

    document.addChangeListener (this);
    refresh();
}

HybridModulationPanel::~HybridModulationPanel()
{
    document.removeChangeListener (this);
}

InstrumentTrack* HybridModulationPanel::track()
{
    auto* t = document.findTrack (trackId);
    return t != nullptr && t->kind == TrackKind::hybrid ? t : nullptr;
}

void HybridModulationPanel::setTrack (juce::int64 id)
{
    trackId = id;
    refresh();
}

void HybridModulationPanel::setupModRow (ModRow& row)
{
    addAndMakeVisible (row.enabled);
    row.enabled.setButtonText (NI_TXT ("开"));
    row.enabled.setClickingTogglesState (true);
    row.enabled.onClick = [this] { document.beginEdit (NI_TXT ("改调制矩阵")); push(); };

    addAndMakeVisible (row.source);
    const HybridModSource sourceOrder[] = {
        HybridModSource::none,
        HybridModSource::env1, HybridModSource::env2, HybridModSource::env3,
        HybridModSource::env4,
        HybridModSource::lfo1, HybridModSource::lfo2, HybridModSource::lfo3,
        HybridModSource::lfo4, HybridModSource::lfo5, HybridModSource::lfo6,
        HybridModSource::velocity, HybridModSource::keyTrack,
        HybridModSource::modWheel,
        HybridModSource::macro1, HybridModSource::macro2,
        HybridModSource::macro3, HybridModSource::macro4
    };
    for (auto source : sourceOrder)
        row.source.addItem (hybridModSourceName (source), (int) source + 1);
    row.source.onChange = [this] { document.beginEdit (NI_TXT ("改调制矩阵")); push(); };

    addAndMakeVisible (row.destination);
    for (int i = 0; i < hybridModDestinationCount(); ++i)
        row.destination.addItem (hybridModDestinationName ((HybridModDestination) i), i + 1);
    row.destination.onChange = [this] { document.beginEdit (NI_TXT ("改调制矩阵")); push(); };

    addAndMakeVisible (row.amount);
    configureSynthKnob (row.amount);
    row.amount.setRange (-1.0, 1.0, 0.01);
    row.amount.onDragStart = [this] { document.beginEdit (NI_TXT ("改调制量")); };
    row.amount.onValueChange = [this] { if (! updating) push(); };
}

void HybridModulationPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshControls();
}

void HybridModulationPanel::refresh()
{
    refreshControls();
}

void HybridModulationPanel::refreshControls()
{
    auto* t = track();
    updating = true;
    for (int i = 0; i < 4; ++i)
        envTabs[(size_t) i].setToggleState (i == envelopeIndex, juce::dontSendNotification);

    for (int i = 0; i < 6; ++i)
        lfoTabs[(size_t) i].setToggleState (i == lfoIndex, juce::dontSendNotification);
    if (t == nullptr)
    {
        updating = false;
        return;
    }

    const auto& e = t->hybrid.envelopes[(size_t) envelopeIndex];
    envelope.setValues ({ e.attack, e.decay, e.sustain, e.release });
    const auto& l = t->hybrid.lfos[(size_t) lfoIndex];
    lfoWave.setSelectedId (juce::jlimit (0, 4, l.waveform) + 1, juce::dontSendNotification);
    lfoRate.setValue ((double) l.rateMs, juce::dontSendNotification);
    lfoPhase.setValue ((double) l.phase, juce::dontSendNotification);
    lfoSync.setToggleState (l.timeMode == HybridTimeMode::beats,
                            juce::dontSendNotification);
    lfoRetrigger.setToggleState (l.retrigger, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        macros[(size_t) i].setValue ((double) t->hybrid.macros[(size_t) i],
                                     juce::dontSendNotification);
        const auto name = t->hybrid.macroNames[(size_t) i];
        macroLabels[(size_t) i].setText (name.isEmpty()
                                             ? NI_TXT ("宏 ") + juce::String (i + 1) : name,
                                         juce::dontSendNotification);
    }

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& m = t->hybrid.modSlots[(size_t) i];
        auto& r = rows[(size_t) i];
        r.enabled.setToggleState (m.enabled, juce::dontSendNotification);
        r.source.setSelectedId ((int) m.source + 1, juce::dontSendNotification);
        r.destination.setSelectedId ((int) m.destination + 1, juce::dontSendNotification);
        r.amount.setValue ((double) m.amount, juce::dontSendNotification);
    }
    updating = false;
}

void HybridModulationPanel::push()
{
    if (updating)
        return;
    auto* t = track();
    if (t == nullptr)
        return;

    auto& e = t->hybrid.envelopes[(size_t) envelopeIndex];
    const auto ev = envelope.getValues();
    e.attack = ev.attack;
    e.decay = ev.decay;
    e.sustain = ev.sustain;
    e.release = ev.release;
    auto& l = t->hybrid.lfos[(size_t) lfoIndex];
    l.waveform = juce::jlimit (0, 4, lfoWave.getSelectedId() - 1);
    l.rateMs = (float) lfoRate.getValue();
    l.rateHz = 1000.0f / juce::jmax (1.0f, l.rateMs);
    l.phase = (float) lfoPhase.getValue();
    l.timeMode = lfoSync.getToggleState() ? HybridTimeMode::beats
                                          : HybridTimeMode::milliseconds;
    l.sync = l.timeMode == HybridTimeMode::beats;
    l.retrigger = lfoRetrigger.getToggleState();
    for (int i = 0; i < 4; ++i)
        t->hybrid.macros[(size_t) i] = (float) macros[(size_t) i].getValue();
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& m = t->hybrid.modSlots[(size_t) i];
        auto& r = rows[(size_t) i];
        m.enabled = r.enabled.getToggleState();
        m.source = (HybridModSource) juce::jlimit (
            0, hybridModSourceCount() - 1, r.source.getSelectedId() - 1);
        m.destination = (HybridModDestination) juce::jlimit (
            0, hybridModDestinationCount() - 1, r.destination.getSelectedId() - 1);
        m.amount = (float) r.amount.getValue();
    }
    document.notifyEdit();
}

void HybridModulationPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);
}

void HybridModulationPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto header = area.removeFromTop (28);
    back.setBounds (header.removeFromLeft (96));
    title.setBounds (header.removeFromLeft (280));

    auto macroArea = area.removeFromTop (80);
    const auto mw = macroArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = macroArea.removeFromLeft (mw).reduced (3);
        macroLabels[(size_t) i].setBounds (cell.removeFromTop (14));
        macros[(size_t) i].setBounds (cell);
    }

    auto matrix = area.reduced (0, 8);
    const auto colWidth = matrix.getWidth() / 2;
    auto leftColumn = matrix.removeFromLeft (colWidth);
    auto rightColumn = matrix;
    for (int i = 0; i < 16; ++i)
    {
        auto& column = i < 8 ? leftColumn : rightColumn;
        auto row = column.removeFromTop (34).reduced (2, 2);
        auto& r = rows[(size_t) i];
        r.enabled.setBounds (row.removeFromLeft (34));
        r.source.setBounds (row.removeFromLeft (100));
        r.destination.setBounds (row.removeFromLeft (130));
        r.amount.setBounds (row);
    }
}

} // namespace nidaw
