#include "HybridMixerPanel.h"
#include "Theme.h"

#include "../Audio/HybridFxChain.h"

namespace nidaw
{

namespace
{
    constexpr int kHeader = 38;
    constexpr int kSourceHeight = 226;
    constexpr int kFilterHeight = 138;
    constexpr int kFxHeight = 210;
}

HybridMixerPanel::HybridMixerPanel (ProjectDocument& d)
    : document (d)
{
    addAndMakeVisible (back);
    back.setButtonText (NI_TXT ("返回混合器"));
    back.onClick = [this] { if (onBack != nullptr) onBack(); };

    addAndMakeVisible (title);
    title.setText (NI_TXT ("混音器 · 五源 / 双滤波 / FX / 总输出"),
                   juce::dontSendNotification);
    title.setFont (theme::font (13.0f, true));
    title.setColour (juce::Label::textColourId, theme::accent);

    for (int i = 0; i < 5; ++i)
        setupSource (i);

    addAndMakeVisible (filterTitle);
    filterTitle.setText (NI_TXT ("双滤波"), juce::dontSendNotification);
    filterTitle.setFont (theme::font (11.0f, true));
    filterTitle.setColour (juce::Label::textColourId, theme::accent);

    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (filterCutoff[(size_t) i]);
        configureSynthKnob (filterCutoff[(size_t) i]);
        filterCutoff[(size_t) i].setRange (20.0, 20000.0, 1.0);
        filterCutoff[(size_t) i].setSkewFactorFromMidPoint (1000.0);
        filterCutoff[(size_t) i].onDragStart = [this]
        {
            document.beginEdit (NI_TXT ("调整混音器滤波"));
        };
        filterCutoff[(size_t) i].onValueChange = [this]
        {
            if (! updating)
                push (NI_TXT ("调整混音器滤波"));
        };

        addAndMakeVisible (filterResonance[(size_t) i]);
        configureSynthKnob (filterResonance[(size_t) i]);
        filterResonance[(size_t) i].setRange (0.1, 20.0, 0.01);
        filterResonance[(size_t) i].onDragStart = [this]
        {
            document.beginEdit (NI_TXT ("调整混音器滤波"));
        };
        filterResonance[(size_t) i].onValueChange = [this]
        {
            if (! updating)
                push (NI_TXT ("调整混音器滤波"));
        };

        addAndMakeVisible (filterCutoffLabel[(size_t) i]);
        filterCutoffLabel[(size_t) i].setText (
            NI_TXT ("F") + juce::String (i + 1) + NI_TXT (" 截止"),
            juce::dontSendNotification);
        filterCutoffLabel[(size_t) i].setFont (theme::font (9.0f));
        filterCutoffLabel[(size_t) i].setJustificationType (juce::Justification::centred);
        filterCutoffLabel[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);

        addAndMakeVisible (filterResonanceLabel[(size_t) i]);
        filterResonanceLabel[(size_t) i].setText (
            NI_TXT ("F") + juce::String (i + 1) + NI_TXT (" 共振"),
            juce::dontSendNotification);
        filterResonanceLabel[(size_t) i].setFont (theme::font (9.0f));
        filterResonanceLabel[(size_t) i].setJustificationType (juce::Justification::centred);
        filterResonanceLabel[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);
    }

    addAndMakeVisible (filterRoute);
    filterRoute.addItem (NI_TXT ("串联"), 1);
    filterRoute.addItem (NI_TXT ("并联"), 2);
    filterRoute.addItem (NI_TXT ("仅滤波 1"), 3);
    filterRoute.onChange = [this]
    {
        if (! updating)
            push (NI_TXT ("改混音器滤波路由"));
    };

    addAndMakeVisible (fxTitle);
    fxTitle.setText (NI_TXT ("四个 FX 槽"), juce::dontSendNotification);
    fxTitle.setFont (theme::font (11.0f, true));
    fxTitle.setColour (juce::Label::textColourId, theme::accent);

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (fxLabel[(size_t) i]);
        fxLabel[(size_t) i].setText ("FX " + juce::String (i + 1),
                                     juce::dontSendNotification);
        fxLabel[(size_t) i].setFont (theme::font (10.0f, true));
        fxLabel[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);

        addAndMakeVisible (fxEnable[(size_t) i]);
        fxEnable[(size_t) i].setButtonText (NI_TXT ("开"));
        fxEnable[(size_t) i].setClickingTogglesState (true);
        fxEnable[(size_t) i].setColour (juce::TextButton::buttonOnColourId,
                                        theme::accentDim);
        fxEnable[(size_t) i].onClick = [this]
        {
            if (! updating)
            {
                document.beginEdit (NI_TXT ("开关混音器 FX"));
                push (NI_TXT ("开关混音器 FX"));
            }
        };

        addAndMakeVisible (fxType[(size_t) i]);
        for (int type = 0; type < hybridFxTypeCount(); ++type)
            fxType[(size_t) i].addItem (hybridFxTypeName ((HybridFxType) type), type + 1);
        fxType[(size_t) i].onChange = [this]
        {
            if (! updating)
                push (NI_TXT ("切换混音器 FX"));
        };
    }

    addAndMakeVisible (masterLabel);
    masterLabel.setText (NI_TXT ("总输出"), juce::dontSendNotification);
    masterLabel.setFont (theme::font (10.0f));
    masterLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (masterGain);
    configureVolumeBar (masterGain);
    masterGain.setRange (0.0, 1.0, 0.01);
    masterGain.onDragStart = [this] { document.beginEdit (NI_TXT ("调整总输出")); };
    masterGain.onValueChange = [this]
    {
        if (! updating)
            push (NI_TXT ("调整总输出"));
    };

    document.addChangeListener (this);
    refresh();
}

HybridMixerPanel::~HybridMixerPanel()
{
    document.removeChangeListener (this);
}

void HybridMixerPanel::setupSource (int index)
{
    auto& strip = sources[(size_t) index];
    const char* names[] = { "主振 1", "主振 2", "主振 3", "Sub", "噪声" };
    addAndMakeVisible (strip.name);
    strip.name.setText (NI_TXT (names[index]), juce::dontSendNotification);
    strip.name.setFont (theme::font (11.0f, true));
    strip.name.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (strip.mode);
    strip.mode.setFont (theme::font (9.0f));
    strip.mode.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (strip.enabled);
    strip.enabled.setButtonText (NI_TXT ("启用"));
    strip.enabled.setClickingTogglesState (true);
    strip.enabled.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    strip.enabled.onClick = [this]
    {
        if (! updating)
        {
            document.beginEdit (NI_TXT ("开关混音器音源"));
            push (NI_TXT ("开关混音器音源"));
        }
    };

    addAndMakeVisible (strip.level);
    configureSynthKnob (strip.level);
    strip.level.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 12);
    strip.level.setRange (0.0, 1.0, 0.01);
    strip.level.onDragStart = [this] { document.beginEdit (NI_TXT ("调整混音器电平")); };
    strip.level.onValueChange = [this]
    {
        if (! updating)
            push (NI_TXT ("调整混音器电平"));
    };

    addAndMakeVisible (strip.pan);
    configureSynthKnob (strip.pan);
    strip.pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 12);
    strip.pan.setRange (-1.0, 1.0, 0.01);
    strip.pan.onDragStart = [this] { document.beginEdit (NI_TXT ("调整混音器声相")); };
    strip.pan.onValueChange = [this]
    {
        if (! updating)
            push (NI_TXT ("调整混音器声相"));
    };

    addAndMakeVisible (strip.levelLabel);
    strip.levelLabel.setText (NI_TXT ("电平"), juce::dontSendNotification);
    strip.levelLabel.setFont (theme::font (9.0f));
    strip.levelLabel.setJustificationType (juce::Justification::centred);
    strip.levelLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (strip.panLabel);
    strip.panLabel.setText (NI_TXT ("声相"), juce::dontSendNotification);
    strip.panLabel.setFont (theme::font (9.0f));
    strip.panLabel.setJustificationType (juce::Justification::centred);
    strip.panLabel.setColour (juce::Label::textColourId, theme::dimText);
}

void HybridMixerPanel::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

InstrumentTrack* HybridMixerPanel::track()
{
    auto* t = document.findTrack (trackId);
    return t != nullptr && t->kind == TrackKind::hybrid ? t : nullptr;
}

void HybridMixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void HybridMixerPanel::refresh()
{
    refreshControls();
}

void HybridMixerPanel::refreshControls()
{
    auto* t = track();

    if (t == nullptr)
        return;

    updating = true;

    for (int i = 0; i < 3; ++i)
    {
        const auto& layer = t->hybrid.layers[(size_t) i];
        auto& strip = sources[(size_t) i];
        strip.enabled.setToggleState (layer.enabled, juce::dontSendNotification);
        strip.level.setValue ((double) layer.level, juce::dontSendNotification);
        strip.pan.setValue ((double) layer.pan, juce::dontSendNotification);
        strip.mode.setText (layer.mode == HybridLayerMode::wavetable ? NI_TXT ("波表")
                            : layer.mode == HybridLayerMode::sample ? NI_TXT ("单采样")
                            : layer.mode == HybridLayerMode::granular ? NI_TXT ("粒子")
                                                                     : NI_TXT ("关闭"),
                            juce::dontSendNotification);
    }

    sources[3].enabled.setToggleState (t->hybrid.sub.enabled, juce::dontSendNotification);
    sources[3].level.setValue ((double) t->hybrid.sub.level, juce::dontSendNotification);
    sources[3].pan.setValue ((double) t->hybrid.sub.pan, juce::dontSendNotification);
    sources[3].mode.setText (t->hybrid.sub.waveform == 0 ? NI_TXT ("正弦")
                                                        : NI_TXT ("辅助"), juce::dontSendNotification);

    sources[4].enabled.setToggleState (t->hybrid.noise.enabled, juce::dontSendNotification);
    sources[4].level.setValue ((double) t->hybrid.noise.level, juce::dontSendNotification);
    sources[4].pan.setValue ((double) t->hybrid.noise.pan, juce::dontSendNotification);
    sources[4].mode.setText (NI_TXT ("噪声"), juce::dontSendNotification);

    for (int i = 0; i < 2; ++i)
    {
        filterCutoff[(size_t) i].setValue ((double) t->hybrid.filters[(size_t) i].cutoffHz,
                                           juce::dontSendNotification);
        filterResonance[(size_t) i].setValue (
            (double) t->hybrid.filters[(size_t) i].resonance, juce::dontSendNotification);
    }

    filterRoute.setSelectedId ((int) t->hybrid.filterRouting + 1,
                               juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
    {
        fxEnable[(size_t) i].setToggleState (t->hybrid.fxSlots[(size_t) i].enabled,
                                             juce::dontSendNotification);
        fxType[(size_t) i].setSelectedId ((int) t->hybrid.fxSlots[(size_t) i].type + 1,
                                          juce::dontSendNotification);
    }

    masterGain.setValue ((double) t->hybrid.gain, juce::dontSendNotification);
    updating = false;
}

void HybridMixerPanel::push (const juce::String&)
{
    auto* t = track();

    if (t == nullptr || updating)
        return;

    for (int i = 0; i < 3; ++i)
    {
        auto& layer = t->hybrid.layers[(size_t) i];
        auto& strip = sources[(size_t) i];
        layer.enabled = strip.enabled.getToggleState();
        layer.level = (float) strip.level.getValue();
        layer.pan = (float) strip.pan.getValue();
    }

    t->hybrid.sub.enabled = sources[3].enabled.getToggleState();
    t->hybrid.sub.level = (float) sources[3].level.getValue();
    t->hybrid.sub.pan = (float) sources[3].pan.getValue();
    t->hybrid.noise.enabled = sources[4].enabled.getToggleState();
    t->hybrid.noise.level = (float) sources[4].level.getValue();
    t->hybrid.noise.pan = (float) sources[4].pan.getValue();

    for (int i = 0; i < 2; ++i)
    {
        t->hybrid.filters[(size_t) i].cutoffHz = (float) filterCutoff[(size_t) i].getValue();
        t->hybrid.filters[(size_t) i].resonance =
            (float) filterResonance[(size_t) i].getValue();
    }

    t->hybrid.filterRouting = (HybridFilterRouting) juce::jlimit (
        0, 2, filterRoute.getSelectedId() - 1);

    for (int i = 0; i < 4; ++i)
    {
        t->hybrid.fxSlots[(size_t) i].enabled = fxEnable[(size_t) i].getToggleState();
        t->hybrid.fxSlots[(size_t) i].type = (HybridFxType) juce::jlimit (
            0, hybridFxTypeCount() - 1, fxType[(size_t) i].getSelectedId() - 1);
    }

    t->hybrid.gain = (float) masterGain.getValue();
    document.notifyEdit();
    refreshControls();
}

void HybridMixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    for (int i = 0; i < 5; ++i)
    {
        const auto bounds = getSourceBoundsForTest (i).toFloat();
        g.setColour (theme::panelBackground.brighter (0.05f));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (theme::outline.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    }

    const auto filterBounds = getFilterBoundsForTest().toFloat();
    g.setColour (theme::panelBackground.brighter (0.05f));
    g.fillRoundedRectangle (filterBounds, 7.0f);
    g.setColour (theme::outline.withAlpha (0.7f));
    g.drawRoundedRectangle (filterBounds, 7.0f, 1.0f);
}

void HybridMixerPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (kHeader);
    back.setBounds (header.removeFromLeft (104).reduced (0, 4));
    header.removeFromLeft (8);
    title.setBounds (header);
    area.removeFromTop (4);

    auto stripArea = area.removeFromTop (kSourceHeight);
    const auto gap = 6;
    const auto width = (stripArea.getWidth() - 4 * gap) / 5;

    for (int i = 0; i < 5; ++i)
    {
        auto strip = stripArea.removeFromLeft (i < 4 ? width : stripArea.getWidth());
        auto& s = sources[(size_t) i];
        auto top = strip.removeFromTop (28);
        s.name.setBounds (top.removeFromLeft (80).reduced (4, 2));
        s.enabled.setBounds (top.removeFromRight (60).reduced (2, 2));
        s.mode.setBounds (strip.removeFromTop (18).reduced (4, 0));
        auto knobs = strip.removeFromTop (112);
        auto cell = knobs.removeFromLeft (knobs.getWidth() / 2).reduced (2);
        s.levelLabel.setBounds (cell.removeFromTop (16));
        s.level.setBounds (cell);
        cell = knobs.reduced (2);
        s.panLabel.setBounds (cell.removeFromTop (16));
        s.pan.setBounds (cell);

        if (i < 4)
            stripArea.removeFromLeft (gap);
    }

    area.removeFromTop (6);
    auto filterArea = area.removeFromTop (kFilterHeight);
    filterTitle.setBounds (filterArea.removeFromTop (20).reduced (6, 0));
    auto route = filterArea.removeFromRight (120).reduced (4, 30);
    filterRoute.setBounds (route);
    const auto filterWidth = filterArea.getWidth() / 2;

    for (int i = 0; i < 2; ++i)
    {
        auto cell = filterArea.removeFromLeft (i == 0 ? filterWidth : filterArea.getWidth())
                        .reduced (8, 2);
        auto knobRow = cell.removeFromTop (86);
        auto half = knobRow.removeFromLeft (knobRow.getWidth() / 2).reduced (2);
        filterCutoffLabel[(size_t) i].setBounds (half.removeFromTop (14));
        filterCutoff[(size_t) i].setBounds (half);
        half = knobRow.reduced (2);
        filterResonanceLabel[(size_t) i].setBounds (half.removeFromTop (14));
        filterResonance[(size_t) i].setBounds (half);
    }

    area.removeFromTop (6);
    auto fxArea = area.removeFromTop (kFxHeight);
    fxTitle.setBounds (fxArea.removeFromTop (20).reduced (6, 0));

    for (int i = 0; i < 4; ++i)
    {
        auto row = fxArea.removeFromTop (42).reduced (4, 2);
        fxLabel[(size_t) i].setBounds (row.removeFromLeft (58));
        fxEnable[(size_t) i].setBounds (row.removeFromLeft (50).reduced (2));
        fxType[(size_t) i].setBounds (row.removeFromLeft (180).reduced (2));
    }

    auto master = area.removeFromTop (36);
    masterLabel.setBounds (master.removeFromLeft (70).reduced (6, 4));
    masterGain.setBounds (master.reduced (4, 4));
}

juce::Rectangle<int> HybridMixerPanel::getSourceBoundsForTest (int source) const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kHeader + 4);
    auto stripArea = area.removeFromTop (kSourceHeight);
    const auto gap = 6;
    const auto width = (stripArea.getWidth() - 4 * gap) / 5;

    for (int i = 0; i < 5; ++i)
    {
        auto strip = stripArea.removeFromLeft (i < 4 ? width : stripArea.getWidth());

        if (i == source)
            return strip;

        if (i < 4)
            stripArea.removeFromLeft (gap);
    }

    return {};
}

juce::Rectangle<int> HybridMixerPanel::getFilterBoundsForTest() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kHeader + 4 + kSourceHeight + 6);
    return area.removeFromTop (kFilterHeight);
}

} // namespace nidaw
