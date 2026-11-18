#include "HybridEditor.h"
#include "Theme.h"

#include "../Core/WavetablePresets.h"
#include "../Core/WaveformPresets.h"
#include "../Core/WavetableTables.h"
#include "../Core/HybridPresets.h"

#include <algorithm>

namespace hmdaw
{

namespace
{
    constexpr int kTitleHeight = 26;
    constexpr int kCardGap = 8;
    constexpr int kCardHeight = 320;
    constexpr int kModeNone = 1;
    constexpr int kModeWavetable = 2;
    constexpr int kModeSubtractive = 3;
    constexpr int kModeSample = 4;
    constexpr int kModeGranular = 5;
    constexpr int kModeSub = 6;
    constexpr int kModeNoise = 7;

    class RightClickForwarder : public juce::MouseListener
    {
    public:
        explicit RightClickForwarder (std::function<void()> callback)
            : onRightClick (std::move (callback)) {}

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu() && onRightClick != nullptr)
                onRightClick();
        }

    private:
        std::function<void()> onRightClick;
    };

    juce::String modeName (HybridLayerMode m)
    {
        switch (m)
        {
            case HybridLayerMode::off:         return NI_TXT ("关闭");
            case HybridLayerMode::wavetable:   return NI_TXT ("波表");
            case HybridLayerMode::subtractive: return NI_TXT ("减法");
            case HybridLayerMode::sample:      return NI_TXT ("单采样");
            case HybridLayerMode::granular:    return NI_TXT ("粒子");
            case HybridLayerMode::sub:         return NI_TXT ("Sub");
            case HybridLayerMode::noise:       return NI_TXT ("噪声");
        }

        return NI_TXT ("关闭");
    }

    HybridLayerMode modeFromChoice (int id)
    {
        switch (id)
        {
            case kModeWavetable:   return HybridLayerMode::wavetable;
            case kModeSubtractive: return HybridLayerMode::subtractive;
            case kModeSample:      return HybridLayerMode::sample;
            case kModeGranular:    return HybridLayerMode::granular;
            case kModeSub:         return HybridLayerMode::sub;
            case kModeNoise:       return HybridLayerMode::noise;
            default:               return HybridLayerMode::wavetable;
        }
    }

    int choiceForMode (HybridLayerMode m)
    {
        switch (m)
        {
            case HybridLayerMode::wavetable:   return kModeWavetable;
            case HybridLayerMode::subtractive: return kModeWavetable;
            case HybridLayerMode::sample:      return kModeSample;
            case HybridLayerMode::granular:    return kModeGranular;
            case HybridLayerMode::sub:
            case HybridLayerMode::noise:
            case HybridLayerMode::off:         break;
        }

        return kModeNone;
    }
}

class HybridEditor::UiTimer : public juce::Timer
{
public:
    explicit UiTimer (HybridEditor& owner) : editor (owner) {}
    void timerCallback() override { editor.updatePlayheads(); }

private:
    HybridEditor& editor;
};

HybridEditor::HybridEditor (ProjectDocument& d, AudioEngine& a, PresetStore& p,
                            AppState* state)
    : document (d), audio (a), presets (p), appState (state)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (14.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);
    titleLabel.setText (NI_TXT ("混合合成器"), juce::dontSendNotification);

    addAndMakeVisible (presetBar);
    presetBar.getEntries = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName = [this]
    {
        const auto* track = currentTrack();
        return track != nullptr ? track->hybrid.name : juce::String();
    };
    presetBar.loadPreset = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset = [this] (const juce::String& name, bool overwrite)
    {
        return savePresetByName (name, overwrite);
    };
    presetBar.renamePreset = [this] (int id, const juce::String& name)
    {
        return renamePresetById (id, name);
    };
    presetBar.deletePreset = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name)
    {
        return userPresetExists (name);
    };

    addAndMakeVisible (gain);
    configureSynthKnob (gain);
    configureVolumeBar (gain);
    gain.setRange (0.0, 1.0, 0.01);
    gain.setValue (1.0, juce::dontSendNotification);
    gain.onDragStart = [this] { beginParamEdit (NI_TXT ("调整混合合成器")); };
    gain.onValueChange = [this] { pushCurrentParams(); };
    addModMenu (gain, HybridModDestination::gain);

    addAndMakeVisible (modButton);
    modButton.setButtonText (NI_TXT ("调制"));
    modButton.onClick = [this]
    {
        if (mixerPanel != nullptr)
            mixerPanel->setVisible (false);
        if (fxPanel != nullptr)
            fxPanel->setVisible (false);
        if (embeddedOscillator != nullptr)
            embeddedOscillator->setVisible (false);
        if (embeddedWaveEditor != nullptr)
            embeddedWaveEditor->setVisible (false);
        embedBack.setVisible (false);

        if (modulationPanel == nullptr)
        {
            modulationPanel = std::make_unique<HybridModulationPanel> (document);
            addAndMakeVisible (*modulationPanel);
            modulationPanel->onBack = [this]
            {
                if (modulationPanel != nullptr)
                    modulationPanel->setVisible (false);
            };
        }
        modulationPanel->setTrack (trackId);
        modulationPanel->setBounds (getLocalBounds());
        modulationPanel->setVisible (true);
        modulationPanel->toFront (true);
    };

    addAndMakeVisible (fxButton);
    fxButton.setButtonText (NI_TXT ("FX"));
    fxButton.onClick = [this]
    {
        if (mixerPanel != nullptr)
            mixerPanel->setVisible (false);
        if (modulationPanel != nullptr)
            modulationPanel->setVisible (false);
        if (embeddedOscillator != nullptr)
            embeddedOscillator->setVisible (false);
        if (embeddedWaveEditor != nullptr)
            embeddedWaveEditor->setVisible (false);
        embedBack.setVisible (false);

        if (fxPanel == nullptr)
        {
            fxPanel = std::make_unique<HybridFxPanel> (document);
            addAndMakeVisible (*fxPanel);
            fxPanel->onBack = [this]
            {
                if (fxPanel != nullptr)
                    fxPanel->setVisible (false);
            };
        }

        fxPanel->setTrack (trackId);
        fxPanel->setBounds (getLocalBounds());
        fxPanel->setVisible (true);
        fxPanel->toFront (true);
    };

    addAndMakeVisible (mixerButton);
    mixerButton.setButtonText (NI_TXT ("混音"));
    mixerButton.onClick = [this]
    {
        if (modulationPanel != nullptr)
            modulationPanel->setVisible (false);
        if (fxPanel != nullptr)
            fxPanel->setVisible (false);
        if (embeddedOscillator != nullptr)
            embeddedOscillator->setVisible (false);
        if (embeddedWaveEditor != nullptr)
            embeddedWaveEditor->setVisible (false);
        embedBack.setVisible (false);

        if (mixerPanel == nullptr)
        {
            mixerPanel = std::make_unique<HybridMixerPanel> (document);
            addAndMakeVisible (*mixerPanel);
            mixerPanel->onBack = [this]
            {
                if (mixerPanel != nullptr)
                    mixerPanel->setVisible (false);
            };
        }

        mixerPanel->setTrack (trackId);
        mixerPanel->setBounds (getLocalBounds());
        mixerPanel->setVisible (true);
        mixerPanel->toFront (true);
    };

    addAndMakeVisible (status);
    status.setFont (theme::font (10.0f));
    status.setColour (juce::Label::textColourId, theme::dimText);
    status.setText (NI_TXT ("选择层，然后拖动滤波曲线或音量包络"), juce::dontSendNotification);

    for (int i = 0; i < 3; ++i)
    {
        auto& c = layers[(size_t) i];
        addAndMakeVisible (c.select);
        c.select.setButtonText (NI_TXT ("层 ") + juce::String (i + 1));
        c.select.setClickingTogglesState (true);
        c.select.setRadioGroupId (0x7a31);
        c.select.onClick = [this, i] { selectLayer (i); };

        addAndMakeVisible (c.enable);
        c.enable.setButtonText (NI_TXT ("开"));
        c.enable.setClickingTogglesState (true);
        c.enable.setToggleState (true, juce::dontSendNotification);
        c.enable.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        c.enable.onClick = [this] { beginParamEdit (NI_TXT ("开关主振")); pushCurrentParams(); };
        addAndMakeVisible (c.edit);
        c.edit.setButtonText (NI_TXT ("编辑"));

        addAndMakeVisible (c.modeLabel);
        c.modeLabel.setText (NI_TXT ("模式"), juce::dontSendNotification);
        c.modeLabel.setFont (theme::font (10.0f));
        c.modeLabel.setColour (juce::Label::textColourId, theme::dimText);

        addAndMakeVisible (c.mode);
        c.mode.addItem (NI_TXT ("波表"), kModeWavetable);
        c.mode.addItem (NI_TXT ("单采样"), kModeSample);
        c.mode.addItem (NI_TXT ("粒子"), kModeGranular);
        c.mode.onChange = [this, i]
        {
            if (updating)
                return;

            selectLayer (i);
            beginParamEdit (NI_TXT ("切换混合层模式"));
            pushCurrentParams();
        };

        auto setupLayerKnob = [this] (juce::Slider& knob, juce::Label& label,
                                       const juce::String& text, double lo, double hi,
                                       double interval, const juce::String& editName)
        {
            addAndMakeVisible (label);
            label.setText (text, juce::dontSendNotification);
            label.setFont (theme::font (9.0f));
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (knob);
            configureSynthKnob (knob);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
            knob.setRange (lo, hi, interval);
            knob.onDragStart = [this, editName] { beginParamEdit (editName); };
            knob.onValueChange = [this] { pushCurrentParams(); };
        };

        setupLayerKnob (c.unison, c.unisonLabel, NI_TXT ("齐奏"), 1.0, 16.0, 1.0,
                        NI_TXT ("调整齐奏"));
        setupLayerKnob (c.detune, c.detuneLabel, NI_TXT ("失谐"), 0.0, 100.0, 0.1,
                        NI_TXT ("调整失谐"));
        setupLayerKnob (c.position, c.positionLabel, NI_TXT ("位置"), 0.0, 1.0, 0.001,
                        NI_TXT ("调整波表位置"));

        const std::array<HybridModDestination, 3> positionDests {
            HybridModDestination::layer1Position,
            HybridModDestination::layer2Position,
            HybridModDestination::layer3Position
        };
        const std::array<HybridModDestination, 3> unisonDests {
            HybridModDestination::layer1Unison,
            HybridModDestination::layer2Unison,
            HybridModDestination::layer3Unison
        };
        const std::array<HybridModDestination, 3> detuneDests {
            HybridModDestination::layer1Detune,
            HybridModDestination::layer2Detune,
            HybridModDestination::layer3Detune
        };
        const std::array<HybridModDestination, 3> levelDests {
            HybridModDestination::layer1Level,
            HybridModDestination::layer2Level,
            HybridModDestination::layer3Level
        };
        const std::array<HybridModDestination, 3> panDests {
            HybridModDestination::layer1Pan,
            HybridModDestination::layer2Pan,
            HybridModDestination::layer3Pan
        };
        addModMenu (c.position, positionDests[(size_t) i], i);
        addModMenu (c.unison, unisonDests[(size_t) i]);
        addModMenu (c.detune, detuneDests[(size_t) i]);
        addModMenu (c.level, levelDests[(size_t) i]);
        addModMenu (c.pan, panDests[(size_t) i]);

        addAndMakeVisible (c.level);
        configureSynthKnob (c.level);
        c.level.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
        c.level.setRange (0.0, 1.0, 0.01);
        c.level.onDragStart = [this] { beginParamEdit (NI_TXT ("调整混合层电平")); };
        c.level.onValueChange = [this] { pushCurrentParams(); };
        addAndMakeVisible (c.levelLabel);
        c.levelLabel.setText (NI_TXT ("电平"), juce::dontSendNotification);
        c.levelLabel.setFont (theme::font (9.0f));
        c.levelLabel.setColour (juce::Label::textColourId, theme::dimText);
        c.levelLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (c.pan);
        configureSynthKnob (c.pan);
        c.pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
        c.pan.setRange (-1.0, 1.0, 0.01);
        c.pan.onDragStart = [this] { beginParamEdit (NI_TXT ("调整混合层声相")); };
        c.pan.onValueChange = [this] { pushCurrentParams(); };
        addAndMakeVisible (c.panLabel);
        c.panLabel.setText (NI_TXT ("声相"), juce::dontSendNotification);
        c.panLabel.setFont (theme::font (9.0f));
        c.panLabel.setColour (juce::Label::textColourId, theme::dimText);
        c.panLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (c.import);
        c.import.setButtonText (NI_TXT ("导入音频…"));
        c.import.onClick = [this, i]
        {
            selectLayer (i);
            importForSelectedLayer();
        };

        c.edit.onClick = [this, i]
        {
            if (thumbnails[(size_t) i] != nullptr && thumbnails[(size_t) i]->onOpen)
                thumbnails[(size_t) i]->onOpen();
        };
    }

    addAndMakeVisible (filterView);
    addAndMakeVisible (envelopeView);
    addAndMakeVisible (keyboard);
    keyboardState.addListener (this);

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (envTabs[(size_t) i]);
        envTabs[(size_t) i].setButtonText (i == 0 ? NI_TXT ("ENV 1 音量")
                                                  : "ENV " + juce::String (i + 1));
        envTabs[(size_t) i].setClickingTogglesState (true);
        envTabs[(size_t) i].setRadioGroupId (0x71a1);
        envTabs[(size_t) i].onClick = [this, i]
        {
            selectedEnvelope = i;
            refresh();
        };
    }

    addAndMakeVisible (envMode);
    envMode.setButtonText (NI_TXT ("MS"));
    envMode.setClickingTogglesState (true);
    envMode.onClick = [this] { beginParamEdit (NI_TXT ("切换包络时值")); pushCurrentParams(); };

    addAndMakeVisible (envMs);
    envMs.setButtonText (NI_TXT ("MS"));
    envMs.setClickingTogglesState (true);
    envMs.setRadioGroupId (0x71b1);
    envMs.onClick = [this]
    {
        if (updating)
            return;
        if (auto* track = currentTrack())
        {
            beginParamEdit (NI_TXT ("切换包络时值"));
            track->hybrid.envelopes[(size_t) selectedEnvelope].timeMode
                = HybridTimeMode::milliseconds;
            document.notifyEdit();
            refresh();
        }
    };
    addAndMakeVisible (envBpm);
    envBpm.setButtonText (NI_TXT ("BPM"));
    envBpm.setClickingTogglesState (true);
    envBpm.setRadioGroupId (0x71b1);
    envBpm.onClick = [this]
    {
        if (updating)
            return;
        if (auto* track = currentTrack())
        {
            beginParamEdit (NI_TXT ("切换包络时值"));
            track->hybrid.envelopes[(size_t) selectedEnvelope].timeMode
                = HybridTimeMode::beats;
            document.notifyEdit();
            refresh();
        }
    };
    envMode.setVisible (false);
    envTabs[0].setVisible (true);

    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (envDivisions[(size_t) i]);

        for (int division = 0; division < 8; ++division)
            envDivisions[(size_t) i].addItem (
                hybridDivisionName ((HybridTimeDivision) division), division + 1);

        envDivisions[(size_t) i].onChange = [this]
        {
            if (! updating)
                pushCurrentParams();
        };
        addAndMakeVisible (envDivisionLabels[(size_t) i]);
        envDivisionLabels[(size_t) i].setFont (theme::font (9.0f));
        envDivisionLabels[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);
        envDivisionLabels[(size_t) i].setJustificationType (juce::Justification::centred);
    }
    envDivisionLabels[0].setText (NI_TXT ("起音"), juce::dontSendNotification);
    envDivisionLabels[1].setText (NI_TXT ("衰减"), juce::dontSendNotification);
    envDivisionLabels[2].setText (NI_TXT ("释放"), juce::dontSendNotification);

    auto setupEnvKnob = [this] (int index, const juce::String& text,
                                double lo, double hi, double interval)
    {
        addAndMakeVisible (envKnobs[(size_t) index]);
        configureSynthKnob (envKnobs[(size_t) index]);
        envKnobs[(size_t) index].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
        envKnobs[(size_t) index].setRange (lo, hi, interval);
        envKnobs[(size_t) index].onDragStart =
            [this] { beginParamEdit (NI_TXT ("调整包络")); };
        envKnobs[(size_t) index].onValueChange = [this] { pushCurrentParams(); };
        addAndMakeVisible (envKnobLabels[(size_t) index]);
        envKnobLabels[(size_t) index].setText (text, juce::dontSendNotification);
        envKnobLabels[(size_t) index].setFont (theme::font (9.0f));
        envKnobLabels[(size_t) index].setColour (juce::Label::textColourId, theme::dimText);
        envKnobLabels[(size_t) index].setJustificationType (juce::Justification::centred);
    };
    setupEnvKnob (0, NI_TXT ("起音"), 0.0, 20.0, 0.001);
    setupEnvKnob (1, NI_TXT ("衰减"), 0.0, 20.0, 0.001);
    setupEnvKnob (2, NI_TXT ("持续"), 0.0, 1.0, 0.001);
    setupEnvKnob (3, NI_TXT ("释放"), 0.0, 20.0, 0.001);

    addAndMakeVisible (lfoView);

    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible (lfoTabs[(size_t) i]);
        lfoTabs[(size_t) i].setButtonText ("LFO " + juce::String (i + 1));
        lfoTabs[(size_t) i].setClickingTogglesState (true);
        lfoTabs[(size_t) i].setRadioGroupId (0x71a2);
        lfoTabs[(size_t) i].onClick = [this, i]
        {
            selectedLfo = i;
            refresh();
        };
    }

    addAndMakeVisible (lfoMode);
    lfoMode.setButtonText (NI_TXT ("MS"));
    lfoMode.setClickingTogglesState (true);
    lfoMode.onClick = [this] { beginParamEdit (NI_TXT ("切换 LFO 时值")); pushCurrentParams(); };

    addAndMakeVisible (lfoMs);
    lfoMs.setButtonText (NI_TXT ("MS"));
    lfoMs.setClickingTogglesState (true);
    lfoMs.setRadioGroupId (0x71b2);
    lfoMs.onClick = [this]
    {
        if (updating)
            return;
        if (auto* track = currentTrack())
        {
            beginParamEdit (NI_TXT ("切换 LFO 时值"));
            track->hybrid.lfos[(size_t) selectedLfo].timeMode
                = HybridTimeMode::milliseconds;
            track->hybrid.lfos[(size_t) selectedLfo].sync = false;
            document.notifyEdit();
            refresh();
        }
    };
    addAndMakeVisible (lfoBpm);
    lfoBpm.setButtonText (NI_TXT ("BPM"));
    lfoBpm.setClickingTogglesState (true);
    lfoBpm.setRadioGroupId (0x71b2);
    lfoBpm.onClick = [this]
    {
        if (updating)
            return;
        if (auto* track = currentTrack())
        {
            beginParamEdit (NI_TXT ("切换 LFO 时值"));
            track->hybrid.lfos[(size_t) selectedLfo].timeMode
                = HybridTimeMode::beats;
            track->hybrid.lfos[(size_t) selectedLfo].sync = true;
            document.notifyEdit();
            refresh();
        }
    };
    lfoMode.setVisible (false);

    addAndMakeVisible (lfoWave);
    lfoWave.addItem (NI_TXT ("三角"), 1);
    lfoWave.addItem (NI_TXT ("正弦"), 2);
    lfoWave.addItem (NI_TXT ("方波"), 3);
    lfoWave.addItem (NI_TXT ("锯齿"), 4);
    lfoWave.addItem (NI_TXT ("采样保持"), 5);
    lfoWave.onChange = [this] { if (! updating) pushCurrentParams(); };
    addAndMakeVisible (lfoWaveLabel);
    lfoWaveLabel.setText (NI_TXT ("波形"), juce::dontSendNotification);
    lfoWaveLabel.setFont (theme::font (9.0f));
    lfoWaveLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoWaveLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoDivision);
    for (int division = 0; division < 8; ++division)
        lfoDivision.addItem (hybridDivisionName ((HybridTimeDivision) division),
                             division + 1);
    lfoDivision.onChange = [this] { if (! updating) pushCurrentParams(); };
    addAndMakeVisible (lfoDivisionLabel);
    lfoDivisionLabel.setText (NI_TXT ("拍分数"), juce::dontSendNotification);
    lfoDivisionLabel.setFont (theme::font (9.0f));
    lfoDivisionLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoDivisionLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoRate);
    configureSynthKnob (lfoRate);
    lfoRate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 12);
    lfoRate.setRange (1.0, 10000.0, 1.0);
    lfoRate.setSkewFactorFromMidPoint (500.0);
    lfoRate.onDragStart = [this] { beginParamEdit (NI_TXT ("调整 LFO")); };
    lfoRate.onValueChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (lfoRateLabel);
    lfoRateLabel.setText (NI_TXT ("周期 ms"), juce::dontSendNotification);
    lfoRateLabel.setFont (theme::font (9.0f));
    lfoRateLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoRateLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoPhase);
    configureSynthKnob (lfoPhase);
    lfoPhase.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
    lfoPhase.setRange (0.0, 1.0, 0.001);
    lfoPhase.onDragStart = [this] { beginParamEdit (NI_TXT ("调整 LFO")); };
    lfoPhase.onValueChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (lfoPhaseLabel);
    lfoPhaseLabel.setText (NI_TXT ("相位"), juce::dontSendNotification);
    lfoPhaseLabel.setFont (theme::font (9.0f));
    lfoPhaseLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoPhaseLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoDepth);
    configureSynthKnob (lfoDepth);
    lfoDepth.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
    lfoDepth.setRange (0.0, 1.0, 0.001);
    lfoDepth.onDragStart = [this] { beginParamEdit (NI_TXT ("调整 LFO")); };
    lfoDepth.onValueChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (lfoDepthLabel);
    lfoDepthLabel.setText (NI_TXT ("深度"), juce::dontSendNotification);
    lfoDepthLabel.setFont (theme::font (9.0f));
    lfoDepthLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoDepthLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoFade);
    configureSynthKnob (lfoFade);
    lfoFade.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 12);
    lfoFade.setRange (0.0, 5000.0, 1.0);
    lfoFade.setSkewFactorFromMidPoint (300.0);
    lfoFade.onDragStart = [this] { beginParamEdit (NI_TXT ("调整 LFO")); };
    lfoFade.onValueChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (lfoFadeLabel);
    lfoFadeLabel.setText (NI_TXT ("淡入 ms"), juce::dontSendNotification);
    lfoFadeLabel.setFont (theme::font (9.0f));
    lfoFadeLabel.setColour (juce::Label::textColourId, theme::dimText);
    lfoFadeLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (lfoRetrigger);
    lfoRetrigger.setButtonText (NI_TXT ("重触发"));
    lfoRetrigger.setClickingTogglesState (true);
    lfoRetrigger.onClick = [this] { beginParamEdit (NI_TXT ("调整 LFO")); pushCurrentParams(); };

    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (filterCutoffKnobs[(size_t) i]);
        configureSynthKnob (filterCutoffKnobs[(size_t) i]);
        filterCutoffKnobs[(size_t) i].setTextBoxStyle (
            juce::Slider::TextBoxBelow, false, 54, 12);
        filterCutoffKnobs[(size_t) i].setRange (20.0, 20000.0, 1.0);
        filterCutoffKnobs[(size_t) i].setSkewFactorFromMidPoint (1000.0);
        filterCutoffKnobs[(size_t) i].onDragStart =
            [this] { beginParamEdit (NI_TXT ("调整滤波器")); };
        filterCutoffKnobs[(size_t) i].onValueChange = [this] { pushCurrentParams(); };
        filterCutoffLabels[(size_t) i].setText (NI_TXT ("截止"), juce::dontSendNotification);
        filterCutoffLabels[(size_t) i].setFont (theme::font (9.0f));
        filterCutoffLabels[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);
        filterCutoffLabels[(size_t) i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (filterCutoffLabels[(size_t) i]);

        addAndMakeVisible (filterResonanceKnobs[(size_t) i]);
        configureSynthKnob (filterResonanceKnobs[(size_t) i]);
        filterResonanceKnobs[(size_t) i].setTextBoxStyle (
            juce::Slider::TextBoxBelow, false, 54, 12);
        filterResonanceKnobs[(size_t) i].setRange (0.1, 20.0, 0.01);
        filterResonanceKnobs[(size_t) i].onDragStart =
            [this] { beginParamEdit (NI_TXT ("调整滤波器")); };
        filterResonanceKnobs[(size_t) i].onValueChange = [this] { pushCurrentParams(); };
        filterResonanceLabels[(size_t) i].setText (NI_TXT ("共振"), juce::dontSendNotification);
        filterResonanceLabels[(size_t) i].setFont (theme::font (9.0f));
        filterResonanceLabels[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);
        filterResonanceLabels[(size_t) i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (filterResonanceLabels[(size_t) i]);

        addModMenu (filterCutoffKnobs[(size_t) i],
                    i == 0 ? HybridModDestination::filter1Cutoff
                           : HybridModDestination::filter2Cutoff);
        addModMenu (filterResonanceKnobs[(size_t) i],
                    i == 0 ? HybridModDestination::filter1Resonance
                           : HybridModDestination::filter2Resonance);
    }

    auto setupAuxEnable = [this] (juce::TextButton& b)
    {
        addAndMakeVisible (b);
        b.setButtonText (NI_TXT ("开"));
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        b.onClick = [this] { beginParamEdit (NI_TXT ("开关辅助音源")); pushCurrentParams(); };
    };
    setupAuxEnable (subEnable);
    setupAuxEnable (noiseEnable);
    addAndMakeVisible (subOpen);
    subOpen.setButtonText (NI_TXT ("编辑"));
    subOpen.onClick = [this]
    {
        if (thumbnails[3] != nullptr && thumbnails[3]->onOpen)
            thumbnails[3]->onOpen();
    };
    addAndMakeVisible (noiseOpen);
    noiseOpen.setButtonText (NI_TXT ("编辑"));
    noiseOpen.onClick = [this]
    {
        if (thumbnails[4] != nullptr && thumbnails[4]->onOpen)
            thumbnails[4]->onOpen();
    };

    auto setupAuxKnob = [this] (juce::Slider& s, double lo, double hi)
    {
        addAndMakeVisible (s);
        configureSynthKnob (s);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 12);
        s.setRange (lo, hi, 0.01);
        s.onDragStart = [this] { beginParamEdit (NI_TXT ("调整辅助音源")); };
        s.onValueChange = [this] { pushCurrentParams(); };
    };
    setupAuxKnob (subLevel, 0.0, 1.0);
    setupAuxKnob (subPan, -1.0, 1.0);
    setupAuxKnob (noiseLevel, 0.0, 1.0);
    setupAuxKnob (noisePan, -1.0, 1.0);
    setupAuxKnob (noiseTone, 20.0, 20000.0);
    noiseTone.setSkewFactorFromMidPoint (1000.0);
    auto setupAuxLabel = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (9.0f));
        label.setColour (juce::Label::textColourId, theme::dimText);
        label.setJustificationType (juce::Justification::centred);
    };
    setupAuxLabel (subLevelLabel, NI_TXT ("音量"));
    setupAuxLabel (subPanLabel, NI_TXT ("声相"));
    setupAuxLabel (noiseLevelLabel, NI_TXT ("音量"));
    setupAuxLabel (noisePanLabel, NI_TXT ("声相"));
    setupAuxLabel (noiseToneLabel, NI_TXT ("Tone"));
    addModMenu (subLevel, HybridModDestination::subLevel);
    addModMenu (subPan, HybridModDestination::subPan);
    addModMenu (noiseLevel, HybridModDestination::noiseLevel);
    addModMenu (noisePan, HybridModDestination::noisePan);

    addAndMakeVisible (subWave);
    subWave.addItem (NI_TXT ("正弦"), 1);
    subWave.addItem (NI_TXT ("三角"), 2);
    subWave.addItem (NI_TXT ("方波"), 3);
    subWave.onChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (subOctave);
    subOctave.addItem ("-1", 1);
    subOctave.addItem ("-2", 2);
    subOctave.onChange = [this] { pushCurrentParams(); };
    addAndMakeVisible (noiseType);
    noiseType.addItem (NI_TXT ("白"), 1);
    noiseType.addItem (NI_TXT ("粉"), 2);
    noiseType.addItem (NI_TXT ("棕"), 3);
    noiseType.onChange = [this] { pushCurrentParams(); };

    for (int i = 0; i < 5; ++i)
    {
        thumbnails[(size_t) i] = std::make_unique<HybridOscillatorThumbnail> (document, audio);
        addAndMakeVisible (*thumbnails[(size_t) i]);
        thumbnails[(size_t) i]->onOpen = [this, i]
        {
            auto* t = currentTrack();
            const auto isWave = i < 3 && t != nullptr
                                    && t->hybrid.layers[(size_t) i].mode == HybridLayerMode::wavetable;

            if (! isWave)
            {
                embeddedSource = i;
                if (embeddedOscillator == nullptr)
                {
                    embeddedOscillator = std::make_unique<HybridOscillatorPanel> (document, audio);
                    addAndMakeVisible (*embeddedOscillator);
                    embeddedOscillator->onBack = [this]
                    {
                        if (embeddedOscillator != nullptr)
                            embeddedOscillator->setVisible (false);
                    };
                }
                if (embeddedWaveEditor != nullptr)
                    embeddedWaveEditor->setVisible (false);
                embedBack.setVisible (false);
                embeddedOscillator->setSource (trackId, i);
                embeddedOscillator->setBounds (getLocalBounds());
                embeddedOscillator->setVisible (true);
                embeddedOscillator->toFront (true);
                return;
            }

            const auto tableId = ensureCustomTableForLayer (i);
            embeddedLayer = i;
            embeddedSource = i;

            if (tableId == 0)
                return;

            if (embeddedWaveEditor == nullptr)
            {
                embeddedWaveEditor = std::make_unique<WavetableTablePanel> (document, audio);
                addAndMakeVisible (*embeddedWaveEditor);
                embedBack.setButtonText (NI_TXT ("返回混合器"));
                addAndMakeVisible (embedBack);
                embedBack.onClick = [this]
                {
                    if (embeddedWaveEditor != nullptr)
                        embeddedWaveEditor->setVisible (false);
                    embedBack.setVisible (false);
                };

                auto& panel = *embeddedWaveEditor;
                panel.onRenameTable = [this] (const juce::String& name)
                {
                    if (embeddedWaveEditor == nullptr)
                        return;
                    const auto id = embeddedWaveEditor->getTableId();
                    document.applyEdit (NI_TXT ("重命名波表"), [id, name] (Project& project)
                    {
                        if (auto* table = project.findCustomWavetable (id))
                            table->name = name;
                    });
                };
                panel.onDeleteTable = [this]
                {
                    if (embeddedWaveEditor == nullptr)
                        return;
                    const auto id = embeddedWaveEditor->getTableId();
                    document.applyEdit (NI_TXT ("删除波表"), [id] (Project& project)
                    {
                        auto& tables = project.customWavetables;
                        tables.erase (std::remove_if (tables.begin(), tables.end(),
                                                     [id] (const CustomWavetable& t) { return t.id == id; }),
                                      tables.end());
                        for (auto& track : project.tracks)
                            for (auto& layer : track.hybrid.layers)
                                if (layer.wavetable.oscs[0].customTableId == id)
                                    layer.wavetable.oscs[0].customTableId = 0;
                    });
                    embeddedWaveEditor->setVisible (false);
                    embedBack.setVisible (false);
                };
                panel.onSampleFromClip = [this]
                {
                    if (embeddedWaveEditor == nullptr || appState == nullptr)
                    {
                        if (embeddedWaveEditor != nullptr)
                            embeddedWaveEditor->setNotice (NI_TXT ("没有可用的当前音频片段。"));
                        return;
                    }

                    const auto* clip = document.getProject().findClip (appState->selectedClipId);

                    if (clip == nullptr || clip->audioFilePath.isEmpty())
                    {
                        embeddedWaveEditor->setNotice (
                            NI_TXT ("请先在编辑栏打开一个有音频文件的片段，再使用“从片段取样”。"));
                        return;
                    }

                    juce::String error;
                    auto sample = getCachedSample (clip->audioFilePath, error);

                    if (sample == nullptr)
                    {
                        embeddedWaveEditor->setNotice (error);
                        return;
                    }

                    const auto channels = juce::jmax (1, sample->audio.getNumChannels());
                    const auto count = sample->audio.getNumSamples();
                    std::vector<float> mono ((size_t) count, 0.0f);

                    for (int c = 0; c < channels; ++c)
                        for (int i = 0; i < count; ++i)
                            mono[(size_t) i] += sample->audio.getSample (c, i) / (float) channels;

                    const auto frames = juce::jlimit (1, kMaxCustomWavetableFrames,
                                                      juce::jmax (1, count / kCustomWavetableFrameSize));
                    const auto name = clip->name.isNotEmpty() ? clip->name : clip->audioFileName;
                    auto table = wavetabletables::makeFromAudio (
                        mono.data(), count, frames, 0,
                        name.isNotEmpty() ? name : NI_TXT ("片段波表"));
                    const auto layer = embeddedLayer;
                    juce::int64 created = 0;

                    document.applyEdit (NI_TXT ("从片段取样波表"),
                                        [this, layer, table, &created] (Project& project) mutable
                    {
                        table.id = project.nextCustomWavetableId++;
                        created = table.id;
                        project.customWavetables.push_back (table);
                        if (auto* t = project.findTrack (trackId))
                            t->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId = created;
                    });

                    if (created != 0)
                    {
                        embeddedWaveEditor->setTableId (created);
                        embeddedWaveEditor->refresh();
                    }
                };
                panel.onAudition = [this]
                {
                    auto* track = currentTrack();

                    if (track == nullptr)
                        return;

                    auto wave = track->hybrid.layers[(size_t) embeddedLayer].wavetable;
                    wave.oscs[1].enabled = false;
                    auto table = wavetabletables::playableTableFor (
                        document.getProject(), wave.oscs[0].customTableId);
                    audio.updatePreviewWaveTables (table, nullptr);
                    audio.triggerPreviewWaveNote (wave, 261.6255653, 100.0f);

                    juce::Component::SafePointer<HybridEditor> safeThis (this);
                    juce::Timer::callAfterDelay (700, [safeThis]
                    {
                        if (safeThis != nullptr)
                            safeThis->audio.releasePreviewNote();
                    });
                };
                panel.onCopyBuiltIn = [this] (int builtInIndex)
                {
                    if (embeddedWaveEditor == nullptr)
                        return;
                    const auto layer = embeddedLayer;
                    juce::int64 created = 0;
                    document.applyEdit (NI_TXT ("复制内置波表"), [this, layer, builtInIndex, &created] (Project& project)
                    {
                        auto table = wavetabletables::copyBuiltIn (
                            builtInIndex, kMaxCustomWavetableFrames,
                            project.nextCustomWavetableId++,
                            wavetable::tableName (builtInIndex) + NI_TXT (" 副本"));
                        created = table.id;
                        project.customWavetables.push_back (std::move (table));
                        if (auto* t = project.findTrack (trackId))
                            t->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId = created;
                    });
                    if (created != 0)
                    {
                        embeddedWaveEditor->setTableId (created);
                        embeddedWaveEditor->refresh();
                    }
                };
                panel.onWaveformPreset = [this] (int presetIndex)
                {
                    if (embeddedWaveEditor == nullptr)
                        return;

                    const auto layer = juce::jlimit (0, 2, embeddedLayer);
                    const auto wantedTrack = trackId;
                    juce::int64 created = 0;

                    document.applyEdit (NI_TXT ("载入波形预设"),
                                        [wantedTrack, layer, presetIndex, &created] (Project& project)
                    {
                        auto table = waveformpresets::build (presetIndex,
                                                             project.nextCustomWavetableId++);
                        created = table.id;
                        project.customWavetables.push_back (std::move (table));

                        if (auto* t = project.findTrack (wantedTrack))
                            t->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId = created;
                    });

                    if (created != 0)
                    {
                        embeddedWaveEditor->setTableId (created);
                        embeddedWaveEditor->refresh();
                        embeddedWaveEditor->setNotice (NI_TXT ("已载入波形预设：")
                                                           + waveformpresets::name (presetIndex));
                        refresh();
                    }
                };
                panel.onImportWav = [this]
                {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        NI_TXT ("导入波表音频"),
                        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                        "*.wav;*.aif;*.aiff;*.flac");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode
                                              | juce::FileBrowserComponent::canSelectFiles,
                                          [safeThis = juce::Component::SafePointer<HybridEditor> (this),
                                           chooser] (const juce::FileChooser& fc)
                    {
                        if (safeThis == nullptr || safeThis->embeddedWaveEditor == nullptr)
                            return;
                        const auto file = fc.getResult();
                        if (file == juce::File())
                            return;

                        juce::String error;
                        auto sample = getCachedSample (file.getFullPathName(), error);
                        if (sample == nullptr)
                        {
                            safeThis->embeddedWaveEditor->setNotice (error);
                            return;
                        }

                        const auto channels = juce::jmax (1, sample->audio.getNumChannels());
                        const auto count = sample->audio.getNumSamples();
                        std::vector<float> mono ((size_t) count, 0.0f);
                        for (int c = 0; c < channels; ++c)
                            for (int i = 0; i < count; ++i)
                                mono[(size_t) i] += sample->audio.getSample (c, i) / (float) channels;

                        const auto frames = juce::jlimit (1, kMaxCustomWavetableFrames,
                                                          juce::jmax (1, count / kCustomWavetableFrameSize));
                        auto table = wavetabletables::makeFromAudio (
                            mono.data(), count, frames, 0, file.getFileNameWithoutExtension());
                        juce::int64 created = 0;
                        const auto layer = safeThis->embeddedLayer;
                        safeThis->document.applyEdit (NI_TXT ("导入波表音频"),
                                                      [safeThis, layer, table, &created] (Project& project) mutable
                        {
                            table.id = project.nextCustomWavetableId++;
                            created = table.id;
                            project.customWavetables.push_back (table);
                            if (auto* t = project.findTrack (safeThis->trackId))
                                t->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId = created;
                        });
                        if (created != 0)
                        {
                            safeThis->embeddedWaveEditor->setTableId (created);
                            safeThis->embeddedWaveEditor->refresh();
                        }
                    });
                };
            }

            embeddedWaveEditor->setTableId (tableId);
            embeddedWaveEditor->setBounds (getLocalBounds().withTrimmedTop (30));
            embeddedWaveEditor->setVisible (true);
            embeddedWaveEditor->toFront (true);
            if (embeddedOscillator != nullptr)
                embeddedOscillator->setVisible (false);
            embedBack.setVisible (true);
            embedBack.setBounds (8, 4, 96, 24);
            embedBack.toFront (true);
        };
    }

    filterView2.setVisible (false);
    filter2Mode.setVisible (false);
    filter2ModeLabel.setVisible (false);
    filter1ModeLabel.setVisible (false);
    addAndMakeVisible (filterView2);
    auto setupFilterMode = [this] (juce::Label& label, juce::ComboBox& box,
                                    const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (10.0f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        addAndMakeVisible (box);
        box.addItem (NI_TXT ("低通"), 1);
        box.addItem (NI_TXT ("带通"), 2);
        box.addItem (NI_TXT ("高通"), 3);
        box.addItem (NI_TXT ("陷波"), 4);
        box.addItem (NI_TXT ("梳状"), 5);
        box.onChange = [this]
        {
            if (updating)
                return;
            beginParamEdit (NI_TXT ("改滤波类型"));
            pushCurrentParams();
        };
    };
    setupFilterMode (filter1ModeLabel, filter1Mode, NI_TXT ("F1 类型"));
    setupFilterMode (filter2ModeLabel, filter2Mode, NI_TXT ("F2 类型"));

    addAndMakeVisible (filterRoute);
    filterRoute.addItem (NI_TXT ("串联"), 1);
    filterRoute.addItem (NI_TXT ("并联"), 2);
    filterRoute.addItem (NI_TXT ("仅滤波 1"), 3);
    filterRoute.onChange = [this]
    {
        if (updating)
            return;
        beginParamEdit (NI_TXT ("改滤波路由"));
        pushCurrentParams();
    };

    auto wireFilter = [this] (int index, FilterResponseView& view)
    {
        view.onRightClick = [this, index] { showFilterModMenu (index); };

        view.onGesture = [this, index] (bool begin, FilterResponseView::Values v)
        {
            if (updating)
                return;
            if (begin)
                beginParamEdit (NI_TXT ("拖动滤波器"));
            const auto which = index == 0 ? juce::jlimit (0, 1, activeFilter) : index;
            if (auto* track = currentTrack())
            {
                auto& f = track->hybrid.filters[(size_t) which];
                f.mode = v.mode;
                f.slopeDb = v.slopeDb;
                f.cutoffHz = v.cutoffHz;
                f.resonance = v.resonance;
                f.envAmount = v.envAmount;
                f.keyTrack = v.keyTrack;
                filterCutoffKnobs[(size_t) which].setValue (
                    (double) v.cutoffHz, juce::dontSendNotification);
                filterResonanceKnobs[(size_t) which].setValue (
                    (double) v.resonance, juce::dontSendNotification);
            }
            pushCurrentParams();
        };
    };
    wireFilter (0, filterView);
    wireFilter (1, filterView2);

    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (filterTabs[(size_t) i]);
        filterTabs[(size_t) i].setButtonText ("Filter " + juce::String (i + 1));
        filterTabs[(size_t) i].setClickingTogglesState (true);
        filterTabs[(size_t) i].setRadioGroupId (0x71b3);
        filterTabs[(size_t) i].onClick = [this, i]
        {
            activeFilter = i;
            refresh();
        };
    }

    envelopeView.onGesture = [this] (bool begin, EnvelopeEditorView::Values v)
    {
        if (updating)
            return;

        if (begin)
            beginParamEdit (NI_TXT ("拖动音量包络"));

        if (auto* track = currentTrack())
        {
            auto& e = track->hybrid.envelopes[(size_t) selectedEnvelope];
            e.attack = v.attack;
            e.decay = v.decay;
            e.sustain = v.sustain;
            e.release = v.release;
            envKnobs[0].setValue ((double) v.attack, juce::dontSendNotification);
            envKnobs[1].setValue ((double) v.decay, juce::dontSendNotification);
            envKnobs[2].setValue ((double) v.sustain, juce::dontSendNotification);
            envKnobs[3].setValue ((double) v.release, juce::dontSendNotification);
        }

        pushCurrentParams();
    };

    uiTimer = std::make_unique<UiTimer> (*this);
    uiTimer->startTimerHz (30);

    document.addChangeListener (this);
    selectLayer (0);
}

HybridEditor::~HybridEditor()
{
    if (uiTimer != nullptr)
        uiTimer->stopTimer();

    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

void HybridEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (trackId == 0)
        return;

    const auto hz = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    audio.noteOnTrack (trackId, liveNoteIdFor (midiNoteNumber), hz,
                       juce::jlimit (0.0f, 127.0f, velocity * 127.0f));

    if (uiTimer != nullptr && ! uiTimer->isTimerRunning())
        uiTimer->startTimerHz (30);
}

void HybridEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (trackId == 0)
        return;

    audio.noteOffTrack (trackId, liveNoteIdFor (midiNoteNumber));

    if (previewMidiNote == midiNoteNumber)
        previewMidiNote = -1;
}

void HybridEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

InstrumentTrack* HybridEditor::currentTrack()
{
    auto* track = document.findTrack (trackId);
    return track != nullptr && track->kind == TrackKind::hybrid ? track : nullptr;
}

const InstrumentTrack* HybridEditor::currentTrack() const
{
    return const_cast<HybridEditor*> (this)->currentTrack();
}

HybridLayerParams* HybridEditor::selectedParams()
{
    auto* track = currentTrack();
    return track != nullptr ? &track->hybrid.layers[(size_t) selectedLayer] : nullptr;
}

void HybridEditor::selectLayer (int layer)
{
    selectedLayer = juce::jlimit (0, 2, layer);

    for (int i = 0; i < 3; ++i)
        layers[(size_t) i].select.setToggleState (i == selectedLayer, juce::dontSendNotification);

    refresh();
}

void HybridEditor::showFxPageForTest()
{
    fxButton.triggerClick();
}

void HybridEditor::showMixerPageForTest()
{
    mixerButton.triggerClick();
}

void HybridEditor::showModulationPageForTest()
{
    modButton.triggerClick();
}

void HybridEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void HybridEditor::updatePlayheads()
{
    auto* track = currentTrack();

    if (track == nullptr || trackId == 0)
        return;

    if (audio.getHybridActiveVoices (trackId) <= 0)
    {
        // Draw the thumbnails once more with the parameter position, then stop
        // the 30 Hz clock: a silent instrument should not keep waking the UI.
        for (auto& thumbnail : thumbnails)
            if (thumbnail != nullptr)
                thumbnail->repaint();

        if (uiTimer != nullptr)
            uiTimer->stopTimer();

        return;
    }

    envelopeView.setPlayhead (audio.getHybridEnvelopePosition (trackId, selectedEnvelope),
                              audio.getHybridEnvelopeValue (trackId, selectedEnvelope));
    lfoView.setValues (track->hybrid.lfos[(size_t) selectedLfo].waveform,
                       audio.getHybridLfoPhase (trackId, selectedLfo),
                       audio.getHybridLfoValue (trackId, selectedLfo));

    for (auto& thumbnail : thumbnails)
        if (thumbnail != nullptr)
            thumbnail->repaint();
}

void HybridEditor::refresh()
{
    auto* track = currentTrack();

    if (track == nullptr)
        return;

    updating = true;
    gain.setValue ((double) track->hybrid.gain, juce::dontSendNotification);

    for (int i = 0; i < 3; ++i)
    {
        const auto& l = track->hybrid.layers[(size_t) i];
        auto& c = layers[(size_t) i];
        c.mode.setSelectedId (choiceForMode (l.mode), juce::dontSendNotification);
        c.enable.setToggleState (l.enabled, juce::dontSendNotification);
        c.level.setValue ((double) l.level, juce::dontSendNotification);
        c.pan.setValue ((double) l.pan, juce::dontSendNotification);
        c.unison.setValue ((double) l.wavetable.oscs[0].unisonVoices,
                           juce::dontSendNotification);
        c.detune.setValue ((double) l.wavetable.oscs[0].unisonDetuneCents,
                           juce::dontSendNotification);
        c.position.setValue ((double) l.wavetable.oscs[0].position,
                             juce::dontSendNotification);
        const auto isWave = l.mode == HybridLayerMode::wavetable;
        c.unison.setEnabled (isWave);
        c.detune.setEnabled (isWave);
        c.position.setEnabled (isWave);
        c.import.setVisible (l.mode == HybridLayerMode::sample || l.mode == HybridLayerMode::granular);
    }

    subEnable.setToggleState (track->hybrid.sub.enabled, juce::dontSendNotification);
    subWave.setSelectedId (juce::jlimit (0, 2, track->hybrid.sub.waveform) + 1,
                           juce::dontSendNotification);
    subOctave.setSelectedId (track->hybrid.sub.octave <= -2 ? 2 : 1, juce::dontSendNotification);
    subLevel.setValue ((double) track->hybrid.sub.level, juce::dontSendNotification);
    subPan.setValue ((double) track->hybrid.sub.pan, juce::dontSendNotification);
    noiseEnable.setToggleState (track->hybrid.noise.enabled, juce::dontSendNotification);
    noiseType.setSelectedId (juce::jlimit (0, 2, track->hybrid.noise.type) + 1,
                             juce::dontSendNotification);
    noiseLevel.setValue ((double) track->hybrid.noise.level, juce::dontSendNotification);
    noisePan.setValue ((double) track->hybrid.noise.pan, juce::dontSendNotification);
    noiseTone.setValue ((double) track->hybrid.noise.toneHz, juce::dontSendNotification);

    selectedEnvelope = juce::jlimit (0, 3, selectedEnvelope);
    const auto& envelope = track->hybrid.envelopes[(size_t) selectedEnvelope];
    EnvelopeEditorView::Values e { envelope.attack, envelope.decay,
                                   envelope.sustain, envelope.release };

    for (int i = 0; i < 4; ++i)
        envTabs[(size_t) i].setToggleState (i == selectedEnvelope,
                                            juce::dontSendNotification);

    const auto envelopeMs = envelope.timeMode == HybridTimeMode::milliseconds;
    envelopeView.setInterceptsMouseClicks (envelopeMs, false);
    envMs.setToggleState (envelopeMs, juce::dontSendNotification);
    envBpm.setToggleState (! envelopeMs, juce::dontSendNotification);
    envDivisions[0].setSelectedId ((int) envelope.attackDivision + 1,
                                   juce::dontSendNotification);
    envDivisions[1].setSelectedId ((int) envelope.decayDivision + 1,
                                   juce::dontSendNotification);
    envDivisions[2].setSelectedId ((int) envelope.releaseDivision + 1,
                                   juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
    {
        envDivisions[(size_t) i].setVisible (false);
        envDivisionLabels[(size_t) i].setVisible (false);
    }

    if (envelopeMs)
    {
        envKnobs[0].setRange (0.0, 20.0, 0.001);
        envKnobs[1].setRange (0.0, 20.0, 0.001);
        envKnobs[3].setRange (0.0, 20.0, 0.001);
        envKnobs[0].setValue ((double) envelope.attack, juce::dontSendNotification);
        envKnobs[1].setValue ((double) envelope.decay, juce::dontSendNotification);
        envKnobs[3].setValue ((double) envelope.release, juce::dontSendNotification);
        envKnobLabels[0].setText (NI_TXT ("起音"), juce::dontSendNotification);
        envKnobLabels[1].setText (NI_TXT ("衰减"), juce::dontSendNotification);
        envKnobLabels[3].setText (NI_TXT ("释放"), juce::dontSendNotification);
    }
    else
    {
        envKnobs[0].setRange (0.0, 7.0, 1.0);
        envKnobs[1].setRange (0.0, 7.0, 1.0);
        envKnobs[3].setRange (0.0, 7.0, 1.0);
        envKnobs[0].setValue ((double) envelope.attackDivision, juce::dontSendNotification);
        envKnobs[1].setValue ((double) envelope.decayDivision, juce::dontSendNotification);
        envKnobs[3].setValue ((double) envelope.releaseDivision, juce::dontSendNotification);
        envKnobLabels[0].setText (NI_TXT ("起音 ") + hybridDivisionName (envelope.attackDivision),
                                  juce::dontSendNotification);
        envKnobLabels[1].setText (NI_TXT ("衰减 ") + hybridDivisionName (envelope.decayDivision),
                                  juce::dontSendNotification);
        envKnobLabels[3].setText (NI_TXT ("释放 ") + hybridDivisionName (envelope.releaseDivision),
                                  juce::dontSendNotification);
    }

    envKnobs[2].setRange (0.0, 1.0, 0.001);
    envKnobs[2].setValue ((double) envelope.sustain, juce::dontSendNotification);
    envelopeView.setTitle (selectedEnvelope == 0 ? NI_TXT ("ENV 1 · 音量")
                                                  : "ENV " + juce::String (selectedEnvelope + 1));
    envKnobs[0].setEnabled (true);
    envKnobs[1].setEnabled (true);
    envKnobs[3].setEnabled (true);

    selectedLfo = juce::jlimit (0, 5, selectedLfo);
    const auto& lfo = track->hybrid.lfos[(size_t) selectedLfo];
    for (int i = 0; i < 6; ++i)
        lfoTabs[(size_t) i].setToggleState (i == selectedLfo, juce::dontSendNotification);
    const auto lfoMsMode = lfo.timeMode == HybridTimeMode::milliseconds;
    lfoMs.setToggleState (lfoMsMode, juce::dontSendNotification);
    lfoBpm.setToggleState (! lfoMsMode, juce::dontSendNotification);
    lfoWave.setSelectedId (juce::jlimit (0, 4, lfo.waveform) + 1,
                           juce::dontSendNotification);
    lfoDivision.setSelectedId ((int) lfo.syncDivision + 1, juce::dontSendNotification);
    lfoRate.setValue ((double) lfo.rateMs, juce::dontSendNotification);
    lfoPhase.setValue ((double) lfo.phase, juce::dontSendNotification);
    lfoDepth.setValue ((double) lfo.depth, juce::dontSendNotification);
    lfoFade.setValue ((double) lfo.fadeMs, juce::dontSendNotification);
    lfoRetrigger.setToggleState (lfo.retrigger, juce::dontSendNotification);
    lfoRate.setEnabled (lfoMsMode);
    lfoDivision.setEnabled (! lfoMsMode);
    lfoView.setValues (lfo.waveform, 0.0f, 0.0f);

    auto filterValues = [] (const HybridFilterParams& f)
    {
        return FilterResponseView::Values { f.mode, f.slopeDb, f.cutoffHz,
                                            f.resonance, f.envAmount, f.keyTrack };
    };
    activeFilter = juce::jlimit (0, 1, activeFilter);
    const auto& active = track->hybrid.filters[(size_t) activeFilter];
    filterView.setValues (filterValues (active));
    filter1Mode.setSelectedId ((int) active.mode + 1, juce::dontSendNotification);
    filterCutoffKnobs[0].setValue ((double) active.cutoffHz, juce::dontSendNotification);
    filterResonanceKnobs[0].setValue ((double) active.resonance,
                                      juce::dontSendNotification);
    filterCutoffKnobs[1].setValue ((double) active.cutoffHz, juce::dontSendNotification);
    filterResonanceKnobs[1].setValue ((double) active.resonance,
                                      juce::dontSendNotification);
    for (int i = 0; i < 2; ++i)
        filterTabs[(size_t) i].setToggleState (i == activeFilter,
                                               juce::dontSendNotification);
    filterRoute.setSelectedId ((int) track->hybrid.filterRouting + 1,
                              juce::dontSendNotification);
    envelopeView.setValues (e);

    status.setText (track->hybrid.layers[(size_t) selectedLayer].mode == HybridLayerMode::off
                        ? NI_TXT ("这一层关闭；选择模式或导入音频后开始发声")
                        : NI_TXT ("滤波器可拖动；Shift 拖动滤波包络量；包络节点可直接拖动"),
                    juce::dontSendNotification);
    updating = false;
    if (uiTimer != nullptr && audio.getHybridActiveVoices (trackId) > 0
        && ! uiTimer->isTimerRunning())
        uiTimer->startTimerHz (30);

    presetBar.refresh();
    for (int i = 0; i < 5; ++i)
        if (thumbnails[(size_t) i] != nullptr)
            thumbnails[(size_t) i]->setSource (trackId, i);
    if (modulationPanel != nullptr)
        modulationPanel->setTrack (trackId);
    if (fxPanel != nullptr && fxPanel->isVisible())
        fxPanel->setTrack (trackId);
    if (mixerPanel != nullptr && mixerPanel->isVisible())
        mixerPanel->setTrack (trackId);
    if (embeddedOscillator != nullptr && embeddedOscillator->isVisible())
        embeddedOscillator->setSource (trackId, embeddedSource);
}

void HybridEditor::showFilterModMenu (int filterIndex)
{
    juce::PopupMenu menu;
    const auto prefix = filterIndex == 0 ? NI_TXT ("滤波 1") : NI_TXT ("滤波 2");
    const auto env = filterIndex == 0 ? HybridModSource::env2 : HybridModSource::env3;
    const auto lfo = filterIndex == 0 ? HybridModSource::lfo1 : HybridModSource::lfo2;
    const auto cutoff = filterIndex == 0 ? HybridModDestination::filter1Cutoff
                                         : HybridModDestination::filter2Cutoff;
    const auto resonance = filterIndex == 0 ? HybridModDestination::filter1Resonance
                                            : HybridModDestination::filter2Resonance;

    menu.addSectionHeader (prefix + NI_TXT (" · 右键分配调制"));
    menu.addItem (1, NI_TXT ("包络 → 截止"));
    menu.addItem (2, NI_TXT ("包络 → 共振"));
    menu.addItem (3, NI_TXT ("LFO → 截止"));
    menu.addItem (4, NI_TXT ("LFO → 共振"));

    juce::Component::SafePointer<HybridEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safeThis, env, lfo, cutoff, resonance, prefix] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto* track = safeThis->currentTrack();

        if (track == nullptr)
            return;

        auto& slots = track->hybrid.modSlots;
        int target = -1;

        for (int i = 0; i < (int) slots.size(); ++i)
            if (! slots[(size_t) i].enabled)
            {
                target = i;
                break;
            }

        if (target < 0)
            target = (int) slots.size() - 1;

        safeThis->document.beginEdit (NI_TXT ("分配调制"));
        auto& slot = slots[(size_t) target];
        slot.enabled = true;
        slot.source = result <= 2 ? env : lfo;
        slot.destination = result == 1 || result == 3 ? cutoff : resonance;
        slot.amount = 0.35f;

        safeThis->document.notifyEdit();
        safeThis->status.setText (NI_TXT ("已把调制分配到 ") + prefix,
                                  juce::dontSendNotification);
        safeThis->refresh();
    });
}

void HybridEditor::addModMenu (juce::Slider& slider, HybridModDestination destination,
                               int positionLayer)
{
    auto listener = std::make_unique<RightClickForwarder> (
        [this, destination, positionLayer]
        {
            showModMenu (destination, positionLayer);
        });
    slider.addMouseListener (listener.get(), false);
    modListeners.push_back (std::move (listener));
}

void HybridEditor::showModMenu (HybridModDestination destination, int positionLayer)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return;

    juce::PopupMenu sourceMenu;
    const HybridModSource sourceOrder[] = {
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
        sourceMenu.addItem ((int) source + 1, hybridModSourceName (source));

    juce::PopupMenu menu;

    if (positionLayer >= 0 && positionLayer < 3)
    {
        const auto discrete = track->hybrid.layers[(size_t) positionLayer]
                                  .wavetable.oscs[0].positionDiscrete;
        menu.addItem (1000, NI_TXT ("位置模式：离散"),
                      true, discrete);
        menu.addItem (1001, NI_TXT ("位置模式：平滑"),
                      true, ! discrete);
        menu.addSeparator();
    }

    menu.addSubMenu (NI_TXT ("分配调制…"), sourceMenu);

    juce::Component::SafePointer<HybridEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safeThis, destination, positionLayer] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto* t = safeThis->currentTrack();

        if (t == nullptr)
            return;

        if (result == 1000 || result == 1001)
        {
            if (positionLayer >= 0 && positionLayer < 3)
            {
                safeThis->document.beginEdit (NI_TXT ("切换位置模式"));
                t->hybrid.layers[(size_t) positionLayer]
                    .wavetable.oscs[0].positionDiscrete = result == 1000;
                safeThis->document.notifyEdit();
                safeThis->refresh();
            }
            return;
        }

        const auto source = (HybridModSource) juce::jlimit (
            0, hybridModSourceCount() - 1, result - 1);
        auto& slots = t->hybrid.modSlots;
        int target = -1;

        for (int i = 0; i < (int) slots.size(); ++i)
            if (! slots[(size_t) i].enabled)
            {
                target = i;
                break;
            }

        if (target < 0)
            target = (int) slots.size() - 1;

        safeThis->document.beginEdit (NI_TXT ("分配调制"));
        auto& slot = slots[(size_t) target];
        slot.enabled = true;
        slot.source = source;
        slot.destination = destination;
        slot.amount = 0.35f;
        safeThis->document.notifyEdit();
        safeThis->refresh();
    });
}

void HybridEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void HybridEditor::pushParams (const HybridParams&)
{
    document.notifyEdit();
}

void HybridEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    track->hybrid.gain = (float) gain.getValue();

    for (int i = 0; i < 3; ++i)
    {
        auto& l = track->hybrid.layers[(size_t) i];
        auto& c = layers[(size_t) i];
        l.mode = modeFromChoice (c.mode.getSelectedId());
        l.enabled = c.enable.getToggleState();
        l.level = (float) c.level.getValue();
        l.pan = (float) c.pan.getValue();
        if (l.mode == HybridLayerMode::wavetable)
        {
            l.wavetable.oscs[0].unisonVoices = juce::jlimit (
                1, kMaxWavetableUnisonVoices, (int) std::round (c.unison.getValue()));
            l.wavetable.oscs[0].unisonDetuneCents = (float) c.detune.getValue();
            l.wavetable.oscs[0].position = (float) c.position.getValue();
        }
    }

    track->hybrid.sub.enabled = subEnable.getToggleState();
    track->hybrid.sub.waveform = juce::jlimit (0, 2, subWave.getSelectedId() - 1);
    track->hybrid.sub.octave = subOctave.getSelectedId() == 2 ? -2 : -1;
    track->hybrid.sub.level = (float) subLevel.getValue();
    track->hybrid.sub.pan = (float) subPan.getValue();
    track->hybrid.noise.enabled = noiseEnable.getToggleState();
    track->hybrid.noise.type = juce::jlimit (0, 2, noiseType.getSelectedId() - 1);
    track->hybrid.noise.level = (float) noiseLevel.getValue();
    track->hybrid.noise.pan = (float) noisePan.getValue();
    track->hybrid.noise.toneHz = (float) noiseTone.getValue();
    track->hybrid.filterRouting = (HybridFilterRouting) juce::jlimit (
        0, 2, filterRoute.getSelectedId() - 1);
    activeFilter = juce::jlimit (0, 1, activeFilter);
    auto& activeFilterParams = track->hybrid.filters[(size_t) activeFilter];
    activeFilterParams.mode = (FilterMode) juce::jlimit (
        0, 4, filter1Mode.getSelectedId() - 1);
    activeFilterParams.cutoffHz = (float) filterCutoffKnobs[0].getValue();
    activeFilterParams.resonance = (float) filterResonanceKnobs[0].getValue();

    {
        auto& e = track->hybrid.envelopes[(size_t) selectedEnvelope];
        e.sustain = (float) envKnobs[2].getValue();
        e.timeMode = envBpm.getToggleState() ? HybridTimeMode::beats
                                             : HybridTimeMode::milliseconds;

        if (e.timeMode == HybridTimeMode::beats)
        {
            e.attackDivision = (HybridTimeDivision) juce::jlimit (
                0, 7, (int) std::round (envKnobs[0].getValue()));
            e.decayDivision = (HybridTimeDivision) juce::jlimit (
                0, 7, (int) std::round (envKnobs[1].getValue()));
            e.releaseDivision = (HybridTimeDivision) juce::jlimit (
                0, 7, (int) std::round (envKnobs[3].getValue()));
        }
        else
        {
            e.attack = (float) envKnobs[0].getValue();
            e.decay = (float) envKnobs[1].getValue();
            e.release = (float) envKnobs[3].getValue();
        }
    }

    {
        auto& l = track->hybrid.lfos[(size_t) selectedLfo];
        l.waveform = juce::jlimit (0, 4, lfoWave.getSelectedId() - 1);
        l.rateMs = (float) lfoRate.getValue();
        l.rateHz = 1000.0f / juce::jmax (1.0f, l.rateMs);
        l.phase = (float) lfoPhase.getValue();
        l.depth = (float) lfoDepth.getValue();
        l.fadeMs = (float) lfoFade.getValue();
        l.retrigger = lfoRetrigger.getToggleState();
        l.timeMode = lfoBpm.getToggleState() ? HybridTimeMode::beats
                                             : HybridTimeMode::milliseconds;
        l.syncDivision = (HybridTimeDivision) juce::jlimit (
            0, 7, lfoDivision.getSelectedId() - 1);
        l.sync = l.timeMode == HybridTimeMode::beats;
    }

    pushParams (track->hybrid);
    refresh();
}

void HybridEditor::importForSelectedLayer()
{
    auto* p = selectedParams();

    if (p == nullptr)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        NI_TXT ("导入混合器音频"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<HybridEditor> (this), chooser,
                           layer = selectedLayer] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        juce::String error;
        auto sample = getCachedSample (file.getFullPathName(), error);

        if (sample == nullptr)
        {
            safeThis->status.setText (error, juce::dontSendNotification);
            return;
        }

        safeThis->document.beginEdit (NI_TXT ("导入混合器音频"));
        auto* track = safeThis->currentTrack();

        if (track != nullptr)
        {
            auto& l = track->hybrid.layers[(size_t) juce::jlimit (0, 2, layer)];
            l.sample.filePath = file.getFullPathName();
            l.sample.fileName = file.getFileName();
            l.granular.filePath = file.getFullPathName();
            l.granular.fileName = file.getFileName();
        }

        safeThis->document.notifyEdit();
        safeThis->refresh();
    });
}

juce::int64 HybridEditor::ensureCustomTableForLayer (int layer)
{
    auto* track = currentTrack();

    if (track == nullptr || layer < 0 || layer >= 3)
        return 0;

    const auto id = track->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId;

    if (id != 0 && document.getProject().findCustomWavetable (id) != nullptr)
        return id;

    const auto seed = track->hybrid.layers[(size_t) layer].wavetable.oscs[0].tableIndex;
    const auto wantedTrack = trackId;
    juce::int64 created = 0;

    document.applyEdit (NI_TXT ("创建波表副本"), [wantedTrack, layer, seed, &created] (Project& project)
    {
        auto* target = project.findTrack (wantedTrack);

        if (target == nullptr || target->kind != TrackKind::hybrid)
            return;

        auto table = wavetabletables::copyBuiltIn (
            seed, kMaxCustomWavetableFrames, project.nextCustomWavetableId++,
            wavetable::tableName (seed) + NI_TXT (" 副本"));
        created = table.id;
        project.customWavetables.push_back (std::move (table));
        target->hybrid.layers[(size_t) layer].wavetable.oscs[0].customTableId = created;
    });

    return created;
}

std::vector<PresetBar::Entry> HybridEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    for (int i = 0; i < hybridpresets::factoryPresetCount(); ++i)
        list.push_back ({ i + 1, hybridpresets::name (i), false,
                          hybridpresets::category (i) });

    const auto& saved = presets.getHybridPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的混合音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true, NI_TXT ("我的") });
    }

    return list;
}

bool HybridEditor::loadPresetById (int id)
{
    if (id >= 1 && id <= hybridpresets::factoryPresetCount())
    {
        auto* track = currentTrack();

        if (track == nullptr)
            return false;

        document.beginEdit (NI_TXT ("载入混合器工厂预设"));
        hybridpresets::apply (track->hybrid, id - 1);
        document.notifyEdit();
        refresh();
        return true;
    }

    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getHybridPresets().size())
        return false;

    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    document.beginEdit (NI_TXT ("载入混合音色"));
    track->hybrid = presets.getHybridPresets()[(size_t) index];
    document.notifyEdit();
    refresh();
    return true;
}

bool HybridEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto preset = track->hybrid;
    preset.name = name;

    const auto existing = presets.findHybridPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceHybridPreset (existing, preset);
    }
    else
    {
        presets.addHybridPreset (preset);
    }

    document.beginEdit (NI_TXT ("保存混合音色"));
    track = currentTrack();

    if (track != nullptr)
        track->hybrid.name = name;

    document.notifyEdit();
    refresh();
    return true;
}

bool HybridEditor::renamePresetById (int id, const juce::String& newName)
{
    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getHybridPresets().size() || newName.isEmpty())
        return false;

    const auto oldName = presets.getHybridPresets()[(size_t) index].name;
    presets.renameHybridPreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->hybrid.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名混合音色"));
        track->hybrid.name = newName;
        document.notifyEdit();
    }

    refresh();
    return true;
}

bool HybridEditor::deletePresetById (int id)
{
    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getHybridPresets().size())
        return false;

    presets.removeHybridPreset (index);
    refresh();
    return true;
}

bool HybridEditor::userPresetExists (const juce::String& name) const
{
    return presets.findHybridPresetByName (name) >= 0;
}

void HybridEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);
    g.setColour (theme::outline.withAlpha (0.5f));

    for (int i = 0; i < 3; ++i)
    {
        const auto b = getLayerCardBoundsForTest (i);
        g.drawRoundedRectangle (b.toFloat(), 6.0f, 1.0f);
    }
}

void HybridEditor::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (kTitleHeight);
    titleLabel.setBounds (header.removeFromLeft (260));
    gain.setBounds (header.removeFromRight (90));
    modButton.setBounds (header.removeFromRight (72).reduced (4, 2));
    fxButton.setBounds (header.removeFromRight (54).reduced (4, 2));
    mixerButton.setBounds (header.removeFromRight (60).reduced (4, 2));
    presetBar.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    status.setBounds (area.removeFromTop (20));

    auto topRow = area.removeFromTop (kCardHeight);
    auto filterPanel = topRow.removeFromRight (260);
    auto cards = topRow;
    const auto gap = 6;
    const auto narrow = 80;
    const auto mainWidth = (cards.getWidth() - 2 * narrow - 4 * gap) / 3;

    for (int i = 0; i < 5; ++i)
    {
        const auto width = i < 3 ? mainWidth : narrow;
        auto c = cards.removeFromLeft (i < 4 ? width : cards.getWidth());

        if (i < 3)
        {
            auto& layer = layers[(size_t) i];
            auto top = c.removeFromTop (24);
            layer.edit.setBounds (top.removeFromRight (48).reduced (2, 1));
            layer.enable.setBounds (top.removeFromRight (34).reduced (2, 1));
            layer.select.setBounds (top);
            thumbnails[(size_t) i]->setBounds (c.removeFromTop (84).reduced (2, 1));
            auto row = c.removeFromTop (26);
            layer.modeLabel.setBounds (row.removeFromLeft (36));
            layer.mode.setBounds (row);
            auto knobRow = c.removeFromTop (78);
            auto third = knobRow.getWidth() / 3;
            auto unisonCell = knobRow.removeFromLeft (third).reduced (2);
            layer.unisonLabel.setBounds (unisonCell.removeFromTop (14));
            layer.unison.setBounds (unisonCell);
            auto detuneCell = knobRow.removeFromLeft (third).reduced (2);
            layer.detuneLabel.setBounds (detuneCell.removeFromTop (14));
            layer.detune.setBounds (detuneCell);
            auto positionCell = knobRow.reduced (2);
            layer.positionLabel.setBounds (positionCell.removeFromTop (14));
            layer.position.setBounds (positionCell);
            auto levelRow = c.removeFromTop (82);
            auto levelCell = levelRow.removeFromLeft (levelRow.getWidth() / 2).reduced (4, 1);
            layer.levelLabel.setBounds (levelCell.removeFromTop (11));
            layer.level.setBounds (levelCell);
            auto panCell = levelRow.reduced (4, 1);
            layer.panLabel.setBounds (panCell.removeFromTop (11));
            layer.pan.setBounds (panCell);
            layer.import.setBounds (c.removeFromTop (22).reduced (8, 1));
        }
        else if (i == 3)
        {
            auto row = c.removeFromTop (24);
            subOpen.setBounds (row.removeFromRight (50).reduced (2, 0));
            subEnable.setBounds (row.reduced (2, 0));
            thumbnails[3]->setBounds (c.removeFromTop (72).reduced (2, 1));
            subWave.setBounds (c.removeFromTop (24).reduced (2, 0));
            subOctave.setBounds (c.removeFromTop (24).reduced (2, 0));
            auto subLevelCell = c.removeFromTop (70).reduced (2, 1);
            subLevelLabel.setBounds (subLevelCell.removeFromTop (11));
            subLevel.setBounds (subLevelCell);
            auto subPanCell = c.removeFromTop (70).reduced (2, 1);
            subPanLabel.setBounds (subPanCell.removeFromTop (11));
            subPan.setBounds (subPanCell);
        }
        else
        {
            auto row = c.removeFromTop (24);
            noiseOpen.setBounds (row.removeFromRight (50).reduced (2, 0));
            noiseEnable.setBounds (row.reduced (2, 0));
            thumbnails[4]->setBounds (c.removeFromTop (72).reduced (2, 1));
            noiseType.setBounds (c.removeFromTop (24).reduced (2, 0));
            auto toneCell = c.removeFromTop (44).reduced (2, 0);
            noiseToneLabel.setBounds (toneCell.removeFromTop (11));
            noiseTone.setBounds (toneCell);
            auto noiseLevelCell = c.removeFromTop (66).reduced (2, 1);
            noiseLevelLabel.setBounds (noiseLevelCell.removeFromTop (11));
            noiseLevel.setBounds (noiseLevelCell);
            auto noisePanCell = c.removeFromTop (66).reduced (2, 1);
            noisePanLabel.setBounds (noisePanCell.removeFromTop (11));
            noisePan.setBounds (noisePanCell);
        }

        if (i < 4)
            cards.removeFromLeft (gap);
    }

    auto bottom = area.removeFromTop (300);
    auto envPanel = bottom.removeFromLeft (480);
    auto lfoPanel = bottom;

    // envelope panel: bottom-left
    auto envTabRow = envPanel.removeFromTop (26);
    const auto envTabWidth = envTabRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        envTabs[(size_t) i].setBounds (envTabRow.removeFromLeft (envTabWidth).reduced (2, 1));
    auto envModeRow = envPanel.removeFromTop (26);
    envMs.setBounds (envModeRow.removeFromLeft (62).reduced (2, 1));
    envBpm.setBounds (envModeRow.removeFromLeft (62).reduced (2, 1));
    auto envKnobArea = envPanel.removeFromBottom (78);
    const auto envKnobWidth = envKnobArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = envKnobArea.removeFromLeft (envKnobWidth).reduced (3, 1);
        envKnobLabels[(size_t) i].setBounds (cell.removeFromTop (12));
        envKnobs[(size_t) i].setBounds (cell);
    }
    envelopeView.setBounds (envPanel.reduced (4, 2));

    // filter panel: top-right, one display with F1/F2 tabs
    auto filterTabsRow = filterPanel.removeFromTop (28);
    filterTabs[0].setBounds (filterTabsRow.removeFromLeft (84).reduced (2, 1));
    filterTabs[1].setBounds (filterTabsRow.removeFromLeft (84).reduced (2, 1));
    auto filterModeRow = filterPanel.removeFromTop (26);
    filter1Mode.setBounds (filterModeRow.removeFromLeft (112).reduced (2, 1));
    filterRoute.setBounds (filterModeRow.removeFromLeft (112).reduced (2, 1));
    filterView.setBounds (filterPanel.removeFromTop (150).reduced (4, 2));
    auto filterKnobRow = filterPanel.removeFromBottom (78).reduced (4, 2);
    auto cutoffCell = filterKnobRow.removeFromLeft (filterKnobRow.getWidth() / 2).reduced (4, 2);
    filterCutoffLabels[0].setBounds (cutoffCell.removeFromTop (12));
    filterCutoffKnobs[0].setBounds (cutoffCell);
    auto resonanceCell = filterKnobRow.reduced (4, 2);
    filterResonanceLabels[0].setBounds (resonanceCell.removeFromTop (12));
    filterResonanceKnobs[0].setBounds (resonanceCell);
    filterView2.setVisible (false);
    filter2Mode.setVisible (false);
    filter2ModeLabel.setVisible (false);
    filterCutoffKnobs[1].setVisible (false);
    filterResonanceKnobs[1].setVisible (false);
    filterCutoffLabels[1].setVisible (false);
    filterResonanceLabels[1].setVisible (false);

    // LFO panel
    auto lfoTabsRow = lfoPanel.removeFromTop (26);
    const auto lfoTabWidth = lfoTabsRow.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
        lfoTabs[(size_t) i].setBounds (lfoTabsRow.removeFromLeft (lfoTabWidth).reduced (1, 1));
    auto lfoModeRow = lfoPanel.removeFromTop (28);
    lfoMs.setBounds (lfoModeRow.removeFromLeft (54).reduced (2, 1));
    lfoBpm.setBounds (lfoModeRow.removeFromLeft (54).reduced (2, 1));
    lfoWaveLabel.setBounds (lfoModeRow.removeFromLeft (30).reduced (2, 1));
    lfoWave.setBounds (lfoModeRow.removeFromLeft (82).reduced (2, 1));
    lfoDivisionLabel.setBounds (lfoModeRow.removeFromLeft (40).reduced (2, 1));
    lfoDivision.setBounds (lfoModeRow.removeFromLeft (72).reduced (2, 1));
    lfoRetrigger.setBounds (lfoModeRow.removeFromLeft (66).reduced (2, 1));
    auto lfoKnobs = lfoPanel.removeFromBottom (96);
    const auto lfoKnobWidth = lfoKnobs.getWidth() / 4;
    auto layoutLfoKnob = [] (juce::Rectangle<int> cell, juce::Label& label,
                             juce::Slider& knob)
    {
        cell.reduce (3, 2);
        label.setBounds (cell.removeFromTop (12));
        knob.setBounds (cell);
    };
    layoutLfoKnob (lfoKnobs.removeFromLeft (lfoKnobWidth), lfoRateLabel, lfoRate);
    layoutLfoKnob (lfoKnobs.removeFromLeft (lfoKnobWidth), lfoDepthLabel, lfoDepth);
    layoutLfoKnob (lfoKnobs.removeFromLeft (lfoKnobWidth), lfoFadeLabel, lfoFade);
    layoutLfoKnob (lfoKnobs, lfoPhaseLabel, lfoPhase);
    lfoView.setBounds (lfoPanel.reduced (4, 2));

    keyboard.setBounds (area.removeFromBottom (86).reduced (4));

    if (embeddedWaveEditor != nullptr && embeddedWaveEditor->isVisible())
    {
        embeddedWaveEditor->setBounds (getLocalBounds().withTrimmedTop (30));
        embedBack.setBounds (8, 4, 96, 24);
        embedBack.toFront (true);
    }
    if (modulationPanel != nullptr && modulationPanel->isVisible())
        modulationPanel->setBounds (getLocalBounds());
    if (fxPanel != nullptr && fxPanel->isVisible())
        fxPanel->setBounds (getLocalBounds());
    if (mixerPanel != nullptr && mixerPanel->isVisible())
        mixerPanel->setBounds (getLocalBounds());
    if (embeddedOscillator != nullptr && embeddedOscillator->isVisible())
        embeddedOscillator->setBounds (getLocalBounds());
}

juce::Rectangle<int> HybridEditor::getLayerCardBoundsForTest (int layer) const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kTitleHeight + 26 + 4 + 20);
    auto top = area.removeFromTop (kCardHeight);
    top.removeFromRight (260);
    auto cards = top;
    const auto gap = 6;
    const auto narrow = 80;
    const auto mainWidth = (cards.getWidth() - 2 * narrow - 4 * gap) / 3;

    for (int i = 0; i < 5; ++i)
    {
        const auto width = i < 3 ? mainWidth : narrow;
        auto c = cards.removeFromLeft (i < 4 ? width : cards.getWidth());

        if (i == layer)
            return c;

        if (i < 4)
            cards.removeFromLeft (gap);
    }

    return {};
}

juce::Rectangle<int> HybridEditor::getEnvelopePanelBoundsForTest() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kTitleHeight + 26 + 4 + 20 + kCardHeight);
    auto bottom = area.removeFromTop (300);
    return bottom.removeFromLeft (480);
}

juce::Rectangle<int> HybridEditor::getFilterPanelBoundsForTest() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kTitleHeight + 26 + 4 + 20);
    auto top = area.removeFromTop (kCardHeight);
    return top.removeFromRight (260);
}

juce::Rectangle<int> HybridEditor::getLfoPanelBoundsForTest() const
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (kTitleHeight + 26 + 4 + 20 + kCardHeight);
    auto bottom = area.removeFromTop (300);
    bottom.removeFromLeft (480);
    return bottom;
}

void HybridEditor::toggleThumbnailForTest (int source)
{
    if (source >= 0 && source < (int) thumbnails.size()
        && thumbnails[(size_t) source] != nullptr)
        thumbnails[(size_t) source]->toggleForTest();
}

void HybridEditor::openWaveEditorForTest (int layer)
{
    if (layer >= 0 && layer < 3)
        layers[(size_t) layer].edit.triggerClick();
}

void HybridEditor::closeWaveEditorForTest()
{
    if (embeddedWaveEditor != nullptr)
        embeddedWaveEditor->setVisible (false);

    embedBack.setVisible (false);
}

HybridOscillatorThumbnail* HybridEditor::getThumbnailForTest (int source) noexcept
{
    return source >= 0 && source < (int) thumbnails.size()
               ? thumbnails[(size_t) source].get()
               : nullptr;
}

juce::Rectangle<int> HybridEditor::getEditButtonBoundsForTest (int layer) const
{
    return layer >= 0 && layer < 3 ? layers[(size_t) layer].edit.getBounds()
                                   : juce::Rectangle<int>();
}

juce::Rectangle<int> HybridEditor::getLayerKnobBoundsForTest (int layer, int which) const
{
    if (layer < 0 || layer >= 3)
        return {};

    const auto& c = layers[(size_t) layer];

    switch (which)
    {
        case 0: return c.unison.getBounds();
        case 1: return c.detune.getBounds();
        case 2: return c.position.getBounds();
        case 3: return c.level.getBounds();
        case 4: return c.pan.getBounds();
        default: break;
    }

    return {};
}

juce::Rectangle<int> HybridEditor::getLayerKnobLabelBoundsForTest (int layer, int which) const
{
    if (layer < 0 || layer >= 3)
        return {};

    const auto& c = layers[(size_t) layer];
    return which == 0 ? c.levelLabel.getBounds()
                      : which == 1 ? c.panLabel.getBounds()
                                   : juce::Rectangle<int>();
}

void HybridEditor::assignModulationForTest (int source, int destination)
{
    auto* t = currentTrack();

    if (t == nullptr)
        return;

    auto& slots = t->hybrid.modSlots;
    int target = -1;

    for (int i = 0; i < (int) slots.size(); ++i)
        if (! slots[(size_t) i].enabled)
        {
            target = i;
            break;
        }

    if (target < 0)
        target = (int) slots.size() - 1;

    document.beginEdit (NI_TXT ("分配调制"));
    auto& slot = slots[(size_t) target];
    slot.enabled = true;
    slot.source = (HybridModSource) juce::jlimit (0, hybridModSourceCount() - 1, source);
    slot.destination = (HybridModDestination) juce::jlimit (
        0, hybridModDestinationCount() - 1, destination);
    slot.amount = 0.35f;
    document.notifyEdit();
    refresh();
}

bool HybridEditor::getPositionDiscreteForTest (int layer) const
{
    auto* t = const_cast<HybridEditor*> (this)->currentTrack();
    return t != nullptr && layer >= 0 && layer < 3
               && t->hybrid.layers[(size_t) layer].wavetable.oscs[0].positionDiscrete;
}

juce::Rectangle<int> HybridEditor::getThumbnailBoundsForTest (int source) const
{
    return source >= 0 && source < (int) thumbnails.size()
               && thumbnails[(size_t) source] != nullptr
               ? thumbnails[(size_t) source]->getBounds()
               : juce::Rectangle<int>();
}

void HybridEditor::selectFilterForTest (int index)
{
    activeFilter = juce::jlimit (0, 1, index);
    refresh();
}

float HybridEditor::getActiveFilterCutoffForTest() const
{
    auto* track = const_cast<HybridEditor*> (this)->currentTrack();
    return track != nullptr ? track->hybrid.filters[(size_t) activeFilter].cutoffHz : 0.0f;
}

bool HybridEditor::getThumbnailThreeDForTest (int source) const
{
    return source >= 0 && source < (int) thumbnails.size()
               && thumbnails[(size_t) source] != nullptr
               && thumbnails[(size_t) source]->isThreeDForTest();
}

int HybridEditor::getLayerModeForTest (int layer) const
{
    if (layer < 0 || layer >= 3)
        return -1;

    return layers[(size_t) layer].mode.getSelectedId();
}

bool HybridEditor::setLayerModeForTest (int layer, int mode)
{
    if (layer < 0 || layer >= 3)
        return false;

    layers[(size_t) layer].mode.setSelectedId (mode, juce::sendNotificationSync);
    return true;
}

} // namespace hmdaw
