#include "SynthEditor.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    constexpr int kTitleHeight    = 22;
    constexpr int kPresetHeight   = 26;
    constexpr int kToolbarHeight  = 26;
    constexpr int kKeyboardHeight = 68;
    constexpr int kRowGap         = 6;
    constexpr int kLibraryColumns = 8;
    constexpr int kLibraryPageSize = 32;
    constexpr int kEnvelopeHeight = 96;

    /** DX7 order: triangle, saw down, saw up, square, sine, sample & hold. */
    const juce::StringArray& lfoWaveNames()
    {
        static const juce::StringArray names { NI_TXT ("三角波"), NI_TXT ("下降锯齿"),
                                               NI_TXT ("上升锯齿"), NI_TXT ("方波"),
                                               NI_TXT ("正弦"), NI_TXT ("随机保持") };
        return names;
    }

    const juce::StringArray& scaleCurveNames()
    {
        static const juce::StringArray names { "-LIN", "-EXP", "+EXP", "+LIN" };
        return names;
    }
}

SynthEditor::SynthEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                          PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    // ---- the preset row ----------------------------------------------------
    addAndMakeVisible (presetBar);
    presetBar.getEntries       = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName   = [this] { return currentPatchName(); };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset     = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name) { return userPresetExists (name); };

    // ---- the top bar -------------------------------------------------------
    addAndMakeVisible (backButton);
    backButton.setButtonText (NI_TXT ("返回"));
    backButton.onClick = [this]
    {
        switch (currentView)
        {
            case View::overview:       setView (View::library); break;
            case View::operatorDetail: setView (View::overview); break;
            case View::lfo:
            case View::output:
            case View::extras:         setView (View::overview); break;
            default: break;
        }
    };

    addAndMakeVisible (breadcrumbLabel);
    breadcrumbLabel.setFont (theme::font (11.5f));
    breadcrumbLabel.setColour (juce::Label::textColourId, theme::dimText);
    breadcrumbLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (lfoButton);
    lfoButton.setButtonText ("LFO");
    lfoButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    lfoButton.onClick = [this] { setView (currentView == View::lfo ? View::overview : View::lfo); };

    addAndMakeVisible (outputButton);
    outputButton.setButtonText (NI_TXT ("输出"));
    outputButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    outputButton.onClick = [this] { setView (currentView == View::output ? View::overview : View::output); };

    addChildComponent (extrasButton);
    extrasButton.setButtonText (NI_TXT ("细节"));
    extrasButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    extrasButton.onClick = [this] { setView (currentView == View::extras ? View::overview : View::extras); };

    addChildComponent (libraryPageButton);
    libraryPageButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    libraryPageButton.setButtonText (NI_TXT ("HM 1-32"));
    libraryPageButton.onClick = [this]
    {
        libraryPage = libraryPage == 0 ? 1 : 0;
        updateLibraryPage();
    };

    // ---- level one: two pages of 32 algorithm pictures ---------------------
    for (int i = 0; i < kLibraryPageSize; ++i)
    {
        auto cell = std::make_unique<AlgorithmDiagram>();
        cell->setThumbnail (true);
        cell->setAlgorithm (i + 1);
        cell->onDiagramClicked = [this, i] { selectAlgorithm (algorithmForCell (i)); };
        addChildComponent (*cell);
        algorithmCells.push_back (std::move (cell));
    }

    // ---- level two: the operators of the chosen algorithm ------------------
    addChildComponent (diagram);
    diagram.setThumbnail (false);
    diagram.onOperatorClicked = [this] (int index) { openOperator (index); };
    diagram.onOperatorToggled = [this] (int index)
    {
        if (auto* track = currentTrack())
            setOperatorEnabled (index, ! track->synth.patch.ops[(size_t) index].enabled);
    };

    // ---- level three: one operator in detail -------------------------------
    addChildComponent (envelopeCurve);

    addChildComponent (operatorPowerButton);
    operatorPowerButton.onClick = [this]
    {
        if (auto* track = currentTrack())
            setOperatorEnabled (selectedOperator,
                                ! track->synth.patch.ops[(size_t) selectedOperator].enabled);
    };

    auto setupOperatorSlider = [this] (juce::Slider& slider, juce::Label& label,
                                       const juce::String& text, double minimum, double maximum,
                                       double interval, const juce::String& suffix)
    {
        addChildComponent (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (10.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整运算器")); };
        slider.onValueChange = [this] { pushCurrentParamsToPreview(); };
    };

    setupOperatorSlider (ratioSlider, ratioLabel, NI_TXT ("比率"), 0.5, 32.0, 0.01, {});
    setupOperatorSlider (detuneSlider, detuneLabel, NI_TXT ("失谐"), -1.0, 1.0, 0.01, {});
    setupOperatorSlider (levelSlider, levelLabel, NI_TXT ("输出电平"), 0.0, 99.0, 1.0, {});
    setupOperatorSlider (velSensSlider, velSensLabel, NI_TXT ("力度敏感"), 0.0, 7.0, 1.0, {});
    setupOperatorSlider (rateScaleSlider, rateScaleLabel, NI_TXT ("速率缩放"), 0.0, 7.0, 1.0, {});

    addChildComponent (envelopeLabel);
    envelopeLabel.setText (NI_TXT ("包络（R1-R4 速率 / L1-L4 电平）"), juce::dontSendNotification);
    envelopeLabel.setFont (theme::font (11.0f));
    envelopeLabel.setColour (juce::Label::textColourId, theme::dimText);

    for (int i = 0; i < 4; ++i)
    {
        auto& rate = rateSliders[(size_t) i];
        addChildComponent (rate);
        rate.setSliderStyle (juce::Slider::LinearHorizontal);
        rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        rate.setRange (0.0, 99.0, 1.0);
        rate.setTooltip (NI_TXT ("速率越大越快"));
        rate.onDragStart = [this] { beginParamEdit (NI_TXT ("调整包络")); };
        rate.onValueChange = [this] { pushCurrentParamsToPreview(); };

        auto& level = envLevelSliders[(size_t) i];
        addChildComponent (level);
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        level.setRange (0.0, 99.0, 1.0);
        level.setTooltip (NI_TXT ("电平越大越响"));
        level.onDragStart = [this] { beginParamEdit (NI_TXT ("调整包络")); };
        level.onValueChange = [this] { pushCurrentParamsToPreview(); };

        addChildComponent (envelopeValueLabels[(size_t) i]);
        envelopeValueLabels[(size_t) i].setText ("R" + juce::String (i + 1), juce::dontSendNotification);
        envelopeValueLabels[(size_t) i].setFont (theme::font (10.0f));
        envelopeValueLabels[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (envelopeValueLabels[(size_t) (i + 4)]);
        envelopeValueLabels[(size_t) (i + 4)].setText ("L" + juce::String (i + 1), juce::dontSendNotification);
        envelopeValueLabels[(size_t) (i + 4)].setFont (theme::font (10.0f));
        envelopeValueLabels[(size_t) (i + 4)].setColour (juce::Label::textColourId, theme::dimText);
    }

    // ---- LFO screen --------------------------------------------------------
    auto setupLfoSlider = [this] (juce::Slider& slider, juce::Label& label,
                                  const juce::String& text, double maximum)
    {
        addChildComponent (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.0f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        slider.setRange (0.0, maximum, 1.0);
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整 LFO")); };
        slider.onValueChange = [this] { pushCurrentParamsToPreview(); };
    };

    setupLfoSlider (lfoRateSlider, lfoRateLabel, NI_TXT ("速率"), 99.0);
    setupLfoSlider (lfoDelaySlider, lfoDelayLabel, NI_TXT ("延迟"), 99.0);
    setupLfoSlider (lfoPitchSlider, lfoPitchLabel, NI_TXT ("音高深度"), 99.0);
    setupLfoSlider (lfoAmpSlider, lfoAmpLabel, NI_TXT ("幅度深度"), 99.0);
    setupLfoSlider (lfoPitchModSensSlider, lfoPitchModSensLabel, NI_TXT ("音高灵敏度"), 7.0);

    addChildComponent (lfoSyncButton);
    lfoSyncButton.setButtonText (NI_TXT ("按键同步"));
    lfoSyncButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    lfoSyncButton.onClick = [this]
    {
        if (updating)
            return;

        if (auto* track = currentTrack())
        {
            beginParamEdit (NI_TXT ("调整 LFO"));
            track->synth.patch.lfoSync = ! track->synth.patch.lfoSync;
            pushParams (track->synth);
            refresh();
        }
    };

    addChildComponent (lfoWaveLabel);
    lfoWaveLabel.setText (NI_TXT ("波形"), juce::dontSendNotification);
    lfoWaveLabel.setFont (theme::font (11.0f));
    lfoWaveLabel.setColour (juce::Label::textColourId, theme::dimText);

    for (int i = 0; i < (int) lfoWaveButtons.size(); ++i)
    {
        auto& button = lfoWaveButtons[(size_t) i];
        addChildComponent (button);
        button.setButtonText (lfoWaveNames()[i]);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        button.onClick = [this, i]
        {
            if (updating)
                return;

            if (auto* track = currentTrack())
            {
                beginParamEdit (NI_TXT ("调整 LFO"));
                track->synth.patch.lfoWaveform = i;
                pushParams (track->synth);
                refresh();
            }
        };
    }

    // ---- output screen -----------------------------------------------------
    auto setupOutSlider = [this] (juce::Slider& slider, juce::Label& label,
                                  const juce::String& text, double minimum, double maximum,
                                  double interval, bool logarithmic, const juce::String& suffix)
    {
        addChildComponent (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.0f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 18);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.setSkewFactorFromMidPoint (logarithmic ? std::sqrt (minimum * maximum)
                                                      : (minimum + maximum) * 0.5);
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整输出")); };
        slider.onValueChange = [this] { pushCurrentParamsToPreview(); };
    };

    setupOutSlider (gainSlider, gainLabel, NI_TXT ("增益"), 0.0, 1.0, 0.001, false, {});
    setupOutSlider (cutoffSlider, cutoffLabel, NI_TXT ("滤波截止"), 20.0, 20000.0, 1.0, true, " Hz");
    setupOutSlider (resonanceSlider, resonanceLabel, NI_TXT ("滤波共振"), 0.1, 20.0, 0.01, false, {});
    setupOutSlider (transposeSlider, transposeLabel, NI_TXT ("移调"), -24.0, 24.0, 1.0, false,
                    NI_TXT (" 半音"));
    setupOutSlider (feedbackSlider, feedbackLabel, NI_TXT ("反馈（算法指定运算器）"), 0.0, 7.0, 1.0,
                    false, {});

    // ---- extras screen: fixed frequency, keyboard scaling, AMS, pitch EG ----
    addChildComponent (modeLabel);
    modeLabel.setText (NI_TXT ("振荡模式"), juce::dontSendNotification);
    modeLabel.setFont (theme::font (10.5f));
    modeLabel.setColour (juce::Label::textColourId, theme::dimText);

    auto setupModeButton = [this] (juce::TextButton& button, const juce::String& text,
                                   FmOscMode mode)
    {
        addChildComponent (button);
        button.setButtonText (text);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        button.onClick = [this, mode]
        {
            if (updating)
                return;

            if (auto* track = currentTrack())
            {
                beginParamEdit (NI_TXT ("振荡模式"));
                track->synth.patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1, selectedOperator)]
                    .mode = mode;
                pushParams (track->synth);
                refresh();
            }
        };
    };

    setupModeButton (ratioModeButton, NI_TXT ("比率"), FmOscMode::ratio);
    setupModeButton (fixedModeButton, NI_TXT ("固定"), FmOscMode::fixed);

    auto setupExtraSlider = [this] (juce::Slider& slider, juce::Label& label,
                                    const juce::String& text, double minimum, double maximum,
                                    double interval, const juce::String& suffix)
    {
        addChildComponent (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (10.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整运算器细节")); };
        slider.onValueChange = [this] { pushCurrentParamsToPreview(); };
    };

    setupExtraSlider (fixedHzSlider, fixedHzLabel, NI_TXT ("固定频率"), 1.0, 10000.0, 1.0, " Hz");
    fixedHzSlider.setSkewFactorFromMidPoint (400.0);
    setupExtraSlider (amSensSlider, amSensLabel, NI_TXT ("AM 灵敏度"), 0.0, 3.0, 1.0, {});
    setupExtraSlider (breakPointSlider, breakPointLabel, NI_TXT ("断点（MIDI）"), 0.0, 127.0, 1.0, {});
    setupExtraSlider (leftDepthSlider, leftDepthLabel, NI_TXT ("左深度"), 0.0, 99.0, 1.0, {});
    setupExtraSlider (rightDepthSlider, rightDepthLabel, NI_TXT ("右深度"), 0.0, 99.0, 1.0, {});

    auto setupCurveButtons = [this] (std::array<juce::TextButton, 4>& buttons,
                                     juce::Label& label, const juce::String& text, bool leftSide)
    {
        addChildComponent (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (10.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);

        for (int i = 0; i < 4; ++i)
        {
            auto& button = buttons[(size_t) i];
            addChildComponent (button);
            button.setButtonText (scaleCurveNames()[i]);
            button.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
            button.onClick = [this, i, leftSide]
            {
                if (updating)
                    return;

                if (auto* track = currentTrack())
                {
                    beginParamEdit (NI_TXT ("调整键盘缩放"));
                    auto& op = track->synth.patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1,
                                                                            selectedOperator)];

                    if (leftSide)
                        op.leftCurve = (FmScaleCurve) i;
                    else
                        op.rightCurve = (FmScaleCurve) i;

                    pushParams (track->synth);
                    refresh();
                }
            };
        }
    };

    setupCurveButtons (leftCurveButtons, leftCurveLabel, NI_TXT ("左曲线"), true);
    setupCurveButtons (rightCurveButtons, rightCurveLabel, NI_TXT ("右曲线"), false);

    addChildComponent (pitchEgLabel);
    pitchEgLabel.setText (NI_TXT ("音高包络（50 = 原音高）"), juce::dontSendNotification);
    pitchEgLabel.setFont (theme::font (11.0f));
    pitchEgLabel.setColour (juce::Label::textColourId, theme::dimText);

    for (int i = 0; i < 4; ++i)
    {
        auto& rate = pitchRateSliders[(size_t) i];
        addChildComponent (rate);
        rate.setSliderStyle (juce::Slider::LinearHorizontal);
        rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        rate.setRange (0.0, 99.0, 1.0);
        rate.onDragStart = [this] { beginParamEdit (NI_TXT ("调整音高包络")); };
        rate.onValueChange = [this] { pushCurrentParamsToPreview(); };

        auto& level = pitchLevelSliders[(size_t) i];
        addChildComponent (level);
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        level.setRange (0.0, 99.0, 1.0);
        level.onDragStart = [this] { beginParamEdit (NI_TXT ("调整音高包络")); };
        level.onValueChange = [this] { pushCurrentParamsToPreview(); };

        addChildComponent (pitchEgValueLabels[(size_t) i]);
        pitchEgValueLabels[(size_t) i].setText ("R" + juce::String (i + 1),
                                                juce::dontSendNotification);
        pitchEgValueLabels[(size_t) i].setFont (theme::font (10.0f));
        pitchEgValueLabels[(size_t) i].setColour (juce::Label::textColourId, theme::dimText);

        addChildComponent (pitchEgValueLabels[(size_t) (i + 4)]);
        pitchEgValueLabels[(size_t) (i + 4)].setText ("L" + juce::String (i + 1),
                                                      juce::dontSendNotification);
        pitchEgValueLabels[(size_t) (i + 4)].setFont (theme::font (10.0f));
        pitchEgValueLabels[(size_t) (i + 4)].setColour (juce::Label::textColourId, theme::dimText);
    }

    // Always visible: the keyboard is the one control every screen needs.
    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe6e6ee));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::outline);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accent.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dimText);
    keyboard.setAvailableRange (60, 72);   // C4 .. C5
    keyboard.setKeyWidth (34.0f);
    keyboard.setLowestVisibleKey (60);
    keyboardState.addListener (this);

    document.addChangeListener (this);

    setView (View::library);
    refresh();
}

SynthEditor::~SynthEditor()
{
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

InstrumentTrack* SynthEditor::currentTrack()
{
    return document.findTrack (trackId);
}

const InstrumentTrack* SynthEditor::currentTrack() const
{
    return const_cast<SynthEditor*> (this)->currentTrack();
}

void SynthEditor::setShowDialogs (bool shouldShow)
{
    showDialogs = shouldShow;
    presetBar.showDialogs = shouldShow;
}

void SynthEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SynthEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

void SynthEditor::refresh()
{
    updateControlsFromTrack();
}

void SynthEditor::updateControlsFromTrack()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto* track = currentTrack();

    if (track == nullptr)
    {
        titleLabel.setText (NI_TXT ("没有选中乐器"), juce::dontSendNotification);
        setEnabled (false);
        return;
    }

    setEnabled (true);
    titleLabel.setText (NI_TXT ("音色 · ") + track->name, juce::dontSendNotification);

    const auto& patch = track->synth.patch;

    diagram.setAlgorithm (patch.algorithm);
    diagram.setPatch (patch);
    diagram.setHighlightedOperator (selectedOperator);

    for (int i = 0; i < (int) algorithmCells.size(); ++i)
    {
        algorithmCells[(size_t) i]->setPatch (patch);
        algorithmCells[(size_t) i]->setSelected (patch.algorithm == algorithmForCell (i));
    }

    const auto& op = patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1, selectedOperator)];

    envelopeCurve.setOperator (op);
    operatorPowerButton.setToggleState (! op.enabled, juce::dontSendNotification);
    operatorPowerButton.setButtonText (op.enabled ? NI_TXT ("关闭运算器") : NI_TXT ("开启运算器"));

    ratioSlider.setValue ((double) op.ratio, juce::dontSendNotification);
    detuneSlider.setValue ((double) op.detune, juce::dontSendNotification);
    levelSlider.setValue ((double) op.outputLevel, juce::dontSendNotification);
    velSensSlider.setValue ((double) op.velSens, juce::dontSendNotification);
    rateScaleSlider.setValue ((double) op.rateScale, juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
    {
        rateSliders[(size_t) i].setValue ((double) op.rate[i], juce::dontSendNotification);
        envLevelSliders[(size_t) i].setValue ((double) op.level[i], juce::dontSendNotification);
    }

    lfoRateSlider.setValue ((double) patch.lfoRate, juce::dontSendNotification);
    lfoDelaySlider.setValue ((double) patch.lfoDelay, juce::dontSendNotification);
    lfoPitchSlider.setValue ((double) patch.lfoPitchDepth, juce::dontSendNotification);
    lfoAmpSlider.setValue ((double) patch.lfoAmpDepth, juce::dontSendNotification);
    lfoPitchModSensSlider.setValue ((double) patch.lfoPitchModSens, juce::dontSendNotification);
    lfoSyncButton.setToggleState (patch.lfoSync, juce::dontSendNotification);

    ratioModeButton.setToggleState (op.mode == FmOscMode::ratio, juce::dontSendNotification);
    fixedModeButton.setToggleState (op.mode == FmOscMode::fixed, juce::dontSendNotification);
    fixedHzSlider.setValue ((double) op.fixedHz, juce::dontSendNotification);
    amSensSlider.setValue ((double) op.amSens, juce::dontSendNotification);
    breakPointSlider.setValue ((double) op.breakPoint, juce::dontSendNotification);
    leftDepthSlider.setValue ((double) op.leftDepth, juce::dontSendNotification);
    rightDepthSlider.setValue ((double) op.rightDepth, juce::dontSendNotification);
    fixedHzSlider.setEnabled (op.mode == FmOscMode::fixed);

    for (int i = 0; i < 4; ++i)
    {
        leftCurveButtons[(size_t) i].setToggleState ((int) op.leftCurve == i,
                                                     juce::dontSendNotification);
        rightCurveButtons[(size_t) i].setToggleState ((int) op.rightCurve == i,
                                                      juce::dontSendNotification);
        pitchRateSliders[(size_t) i].setValue ((double) patch.pitchEg.rate[i],
                                               juce::dontSendNotification);
        pitchLevelSliders[(size_t) i].setValue ((double) patch.pitchEg.level[i],
                                                juce::dontSendNotification);
    }

    for (int i = 0; i < (int) lfoWaveButtons.size(); ++i)
    {
        const auto selected = patch.lfoWaveform == i;
        lfoWaveButtons[(size_t) i].setToggleState (selected, juce::dontSendNotification);
        lfoWaveButtons[(size_t) i].setColour (juce::TextButton::buttonColourId,
                                              selected ? theme::accentDim : theme::panelRaised);
    }

    gainSlider.setValue ((double) track->synth.gain, juce::dontSendNotification);
    cutoffSlider.setValue ((double) track->synth.filterCutoffHz, juce::dontSendNotification);
    resonanceSlider.setValue ((double) track->synth.filterResonance, juce::dontSendNotification);
    transposeSlider.setValue ((double) patch.transpose, juce::dontSendNotification);
    feedbackSlider.setValue ((double) patch.feedback, juce::dontSendNotification);

    presetBar.refresh();
    updateLibraryPage();
    updateBreadcrumb();
}

void SynthEditor::updateBreadcrumb()
{
    const auto* track = currentTrack();
    const auto algorithm = track != nullptr ? track->synth.patch.algorithm : 1;

    switch (currentView)
    {
        case View::library:
            breadcrumbLabel.setText (libraryPage == 0
                                         ? NI_TXT ("算法图库 · HM 1-32（点击一张图选择算法）")
                                         : NI_TXT ("算法图库 · DX7 33-64（点击一张图选择算法）"),
                                     juce::dontSendNotification);
            break;

        case View::overview:
            breadcrumbLabel.setText (NI_TXT ("算法 ") + juce::String (algorithm)
                                         + NI_TXT (" · 运算器总览（点击方块进入详情）"),
                                     juce::dontSendNotification);
            break;

        case View::operatorDetail:
            breadcrumbLabel.setText (NI_TXT ("算法 ") + juce::String (algorithm)
                                         + " > " + NI_TXT ("运算器") + " > OP"
                                         + juce::String (selectedOperator + 1),
                                     juce::dontSendNotification);
            break;

        case View::lfo:
            breadcrumbLabel.setText (NI_TXT ("LFO 调制"), juce::dontSendNotification);
            break;

        case View::output:
            breadcrumbLabel.setText (NI_TXT ("输出与滤波"), juce::dontSendNotification);
            break;

        case View::extras:
            breadcrumbLabel.setText (NI_TXT ("算法 ") + juce::String (algorithm)
                                         + " > " + NI_TXT ("运算器") + " > OP"
                                         + juce::String (selectedOperator + 1)
                                         + NI_TXT (" · 固定频率 / 键盘缩放 / 音高包络"),
                                     juce::dontSendNotification);
            break;

        default:
            break;
    }
}

void SynthEditor::setView (View view)
{
    currentView = view;

    const auto isLibrary  = view == View::library;
    const auto isOverview = view == View::overview;
    const auto isDetail   = view == View::operatorDetail;
    const auto isLfo      = view == View::lfo;
    const auto isOutput   = view == View::output;
    const auto isExtras   = view == View::extras;

    for (auto& cell : algorithmCells)
        cell->setVisible (isLibrary);

    libraryPageButton.setVisible (isLibrary);

    diagram.setVisible (isOverview);

    envelopeCurve.setVisible (isDetail);
    operatorPowerButton.setVisible (isDetail);

    juce::Component* operatorSliders[] = { &ratioSlider, &detuneSlider, &levelSlider,
                                           &velSensSlider, &rateScaleSlider };
    juce::Component* operatorLabels[] = { &ratioLabel, &detuneLabel, &levelLabel,
                                          &velSensLabel, &rateScaleLabel };

    for (auto* c : operatorSliders)
        c->setVisible (isDetail);

    for (auto* c : operatorLabels)
        c->setVisible (isDetail);

    envelopeLabel.setVisible (isDetail);

    for (auto& slider : rateSliders)
        slider.setVisible (isDetail);

    for (auto& slider : envLevelSliders)
        slider.setVisible (isDetail);

    for (auto& label : envelopeValueLabels)
        label.setVisible (isDetail);

    juce::Component* lfoControls[] = { &lfoRateSlider, &lfoDelaySlider, &lfoPitchSlider,
                                       &lfoAmpSlider, &lfoWaveLabel, &lfoRateLabel,
                                       &lfoDelayLabel, &lfoPitchLabel, &lfoAmpLabel,
                                       &lfoPitchModSensSlider, &lfoPitchModSensLabel,
                                       &lfoSyncButton };

    for (auto* c : lfoControls)
        c->setVisible (isLfo);

    for (auto& button : lfoWaveButtons)
        button.setVisible (isLfo);

    juce::Component* extrasControls[] = {
        &modeLabel, &fixedHzLabel, &fixedHzSlider, &amSensLabel, &amSensSlider,
        &breakPointLabel, &breakPointSlider, &leftCurveLabel, &rightCurveLabel,
        &leftDepthLabel, &leftDepthSlider, &rightDepthLabel, &rightDepthSlider,
        &ratioModeButton, &fixedModeButton, &pitchEgLabel
    };

    for (auto* c : extrasControls)
        c->setVisible (isExtras);

    for (auto& button : leftCurveButtons)
        button.setVisible (isExtras);

    for (auto& button : rightCurveButtons)
        button.setVisible (isExtras);

    for (auto& slider : pitchRateSliders)
        slider.setVisible (isExtras);

    for (auto& slider : pitchLevelSliders)
        slider.setVisible (isExtras);

    for (auto& label : pitchEgValueLabels)
        label.setVisible (isExtras);

    juce::Component* outputControls[] = { &gainSlider, &cutoffSlider, &resonanceSlider,
                                          &transposeSlider, &gainLabel, &cutoffLabel,
                                          &resonanceLabel, &transposeLabel };

    for (auto* c : outputControls)
        c->setVisible (isOutput);

    backButton.setVisible (! isLibrary);
    lfoButton.setToggleState (isLfo, juce::dontSendNotification);
    outputButton.setToggleState (isOutput, juce::dontSendNotification);
    extrasButton.setToggleState (isExtras, juce::dontSendNotification);

    // The feedback knob only belongs to operator 6's own page; resized() turns it
    // back on when that page really is showing OP6.
    if (! isDetail)
    {
        feedbackLabel.setVisible (false);
        feedbackSlider.setVisible (false);
    }

    updateBreadcrumb();
    resized();
    repaint();
}

void SynthEditor::selectAlgorithm (int algorithm)
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    document.beginEdit (NI_TXT ("切换算法"));

    if (auto* target = currentTrack())
    {
        target->synth.patch.algorithm = juce::jlimit (1, kFmAlgorithmCount, algorithm);
        libraryPage = target->synth.patch.algorithm > kFmDx7AlgorithmBase ? 1 : 0;
    }

    pushParams (track->synth);
    setView (View::overview);
    refresh();
}

int SynthEditor::algorithmForCell (int cellIndex) const noexcept
{
    return 1 + libraryPage * kLibraryPageSize + juce::jlimit (0, kLibraryPageSize - 1, cellIndex);
}

void SynthEditor::updateLibraryPage()
{
    libraryPageButton.setButtonText (libraryPage == 0 ? NI_TXT ("HM 1-32") : NI_TXT ("DX7 33-64"));
    libraryPageButton.setVisible (currentView == View::library);

    for (int i = 0; i < (int) algorithmCells.size(); ++i)
        algorithmCells[(size_t) i]->setAlgorithm (algorithmForCell (i));

    if (currentView == View::library)
        updateBreadcrumb();

    resized();
    repaint();
}

void SynthEditor::openOperator (int index)
{
    selectedOperator = juce::jlimit (0, kFmOperators - 1, index);
    setView (View::operatorDetail);
    refresh();
}

void SynthEditor::setOperatorEnabled (int index, bool enabled)
{
    if (updating)
        return;

    if (currentTrack() == nullptr || index < 0 || index >= kFmOperators)
        return;

    document.beginEdit (NI_TXT ("开关运算器"));

    if (auto* track = currentTrack())
        track->synth.patch.ops[(size_t) index].enabled = enabled;

    if (auto* track = currentTrack())
        pushParams (track->synth);

    refresh();
}

//==============================================================================
std::vector<PresetBar::Entry> SynthEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    int count = 0;
    const auto* bank = fm::bank (count);

    for (int i = 0; i < count; ++i)
        list.push_back ({ kBuiltInPresetIdBase + i, juce::String::fromUTF8 (bank[i].name), false,
                          juce::String::fromUTF8 (bank[i].category) });

    const auto& saved = presets.getFmPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].patch.name;

        if (name.isEmpty())
            name = NI_TXT ("我的音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

juce::String SynthEditor::currentPatchName() const
{
    const auto* track = currentTrack();
    return track != nullptr ? track->synth.patch.name : juce::String();
}

bool SynthEditor::userPresetExists (const juce::String& name) const
{
    return presets.findFmPresetByName (name) >= 0;
}

bool SynthEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    SynthParams params = track->synth;

    if (id >= kUserPresetIdBase)
    {
        const auto index = id - kUserPresetIdBase;

        if (index < 0 || index >= (int) presets.getFmPresets().size())
            return false;

        params = presets.getFmPresets()[(size_t) index];
    }
    else
    {
        // A built-in carries the whole patch; the track keeps its own gain and
        // filter settings.
        fm::applyPreset (params.patch, id - kBuiltInPresetIdBase);
    }

    document.beginEdit (NI_TXT ("载入音色预设"));

    if (auto* target = currentTrack())
        target->synth = params;

    if (auto* target = currentTrack())
        pushParams (target->synth);

    if (auto* target = currentTrack())
        libraryPage = target->synth.patch.algorithm > kFmDx7AlgorithmBase ? 1 : 0;

    refresh();
    return true;
}

bool SynthEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->synth;
    params.patch.name = name;

    const auto existing = presets.findFmPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceFmPreset (existing, params);
    }
    else
    {
        presets.addFmPreset (params);
    }

    // Saving also names the sound that is playing, so the list highlights it.
    document.beginEdit (NI_TXT ("保存音色预设"));

    if (auto* target = currentTrack())
        target->synth.patch.name = name;

    if (auto* target = currentTrack())
        pushParams (target->synth);

    refresh();
    return true;
}

bool SynthEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getFmPresets().size())
        return false;

    const auto oldName = presets.getFmPresets()[(size_t) index].patch.name;
    presets.renameFmPreset (index, newName);

    // The loaded sound follows its preset's name, otherwise the highlight would
    // jump back to the built-in list the moment the user renames a patch.
    if (currentPatchName() == oldName)
    {
        document.beginEdit (NI_TXT ("重命名音色预设"));

        if (auto* target = currentTrack())
            target->synth.patch.name = newName;
    }

    refresh();
    return true;
}

bool SynthEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getFmPresets().size())
        return false;

    presets.removeFmPreset (index);
    refresh();
    return true;
}

//==============================================================================
void SynthEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void SynthEditor::pushParams (const SynthParams& params)
{
    audio.updatePreviewParams (params);
    document.notifyEdit();
}

void SynthEditor::pushCurrentParamsToPreview()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& patch = track->synth.patch;

    patch.feedback = (int) std::lround (feedbackSlider.getValue());

    if (ratioSlider.isVisible() || levelSlider.isVisible())
    {
        auto& op = patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1, selectedOperator)];
        op.ratio       = (float) ratioSlider.getValue();
        op.detune      = (float) detuneSlider.getValue();
        op.outputLevel = (int) std::lround (levelSlider.getValue());
        op.velSens     = (int) std::lround (velSensSlider.getValue());
        op.rateScale   = (int) std::lround (rateScaleSlider.getValue());

        for (int i = 0; i < 4; ++i)
        {
            op.rate[i]  = (int) std::lround (rateSliders[(size_t) i].getValue());
            op.level[i] = (int) std::lround (envLevelSliders[(size_t) i].getValue());
        }

        envelopeCurve.setOperator (op);
    }

    patch.lfoRate       = (int) std::lround (lfoRateSlider.getValue());
    patch.lfoDelay      = (int) std::lround (lfoDelaySlider.getValue());
    patch.lfoPitchDepth = (int) std::lround (lfoPitchSlider.getValue());
    patch.lfoAmpDepth   = (int) std::lround (lfoAmpSlider.getValue());
    patch.lfoPitchModSens = (int) std::lround (lfoPitchModSensSlider.getValue());
    patch.transpose     = (int) std::lround (transposeSlider.getValue());

    if (fixedHzSlider.isVisible() || amSensSlider.isVisible())
    {
        auto& op = patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1, selectedOperator)];
        op.fixedHz    = (float) fixedHzSlider.getValue();
        op.amSens     = (int) std::lround (amSensSlider.getValue());
        op.breakPoint = (int) std::lround (breakPointSlider.getValue());
        op.leftDepth  = (int) std::lround (leftDepthSlider.getValue());
        op.rightDepth = (int) std::lround (rightDepthSlider.getValue());

        for (int i = 0; i < 4; ++i)
        {
            patch.pitchEg.rate[i]  = (int) std::lround (pitchRateSliders[(size_t) i].getValue());
            patch.pitchEg.level[i] = (int) std::lround (pitchLevelSliders[(size_t) i].getValue());
        }
    }

    track->synth.gain = (float) gainSlider.getValue();
    track->synth.filterCutoffHz = (float) cutoffSlider.getValue();
    track->synth.filterResonance = (float) resonanceSlider.getValue();

    pushParams (track->synth);
}

void SynthEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    // The keyboard is a twelve-tone convenience for auditioning: A4 = 440 Hz.
    const auto frequency = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    audio.triggerPreviewNote (track->synth, frequency, velocity * 127.0f);
}

void SynthEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        previewMidiNote = -1;
        audio.releasePreviewNote();
    }
}

void SynthEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawHorizontalLine (kTitleHeight + kPresetHeight + kToolbarHeight + 2 * kRowGap,
                          8.0f, (float) getWidth() - 8.0f);
}

void SynthEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    titleLabel.setBounds (area.removeFromTop (kTitleHeight));
    presetBar.setBounds (area.removeFromTop (kPresetHeight));
    area.removeFromTop (kRowGap);

    auto toolbar = area.removeFromTop (kToolbarHeight);
    backButton.setBounds (toolbar.removeFromLeft (58).reduced (1, 0));
    toolbar.removeFromLeft (6);
    outputButton.setBounds (toolbar.removeFromRight (66).reduced (1, 0));
    toolbar.removeFromRight (4);
    lfoButton.setBounds (toolbar.removeFromRight (66).reduced (1, 0));
    toolbar.removeFromRight (4);
    extrasButton.setBounds (toolbar.removeFromRight (66).reduced (1, 0));
    toolbar.removeFromRight (6);

    if (currentView == View::library)
    {
        libraryPageButton.setBounds (toolbar.removeFromLeft (86).reduced (1, 0));
        toolbar.removeFromLeft (6);
    }

    breadcrumbLabel.setBounds (toolbar);

    area.removeFromTop (kRowGap);

    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    area.removeFromBottom (kRowGap);

    if (currentView == View::library)
    {
        const auto rows = kLibraryPageSize / kLibraryColumns;
        const auto cellWidth = juce::jmax (20, area.getWidth() / kLibraryColumns);
        const auto cellHeight = juce::jmax (18, area.getHeight() / rows);

        for (int i = 0; i < (int) algorithmCells.size(); ++i)
            algorithmCells[(size_t) i]->setBounds (area.getX() + (i % kLibraryColumns) * cellWidth,
                                                   area.getY() + (i / kLibraryColumns) * cellHeight,
                                                   cellWidth - 4, cellHeight - 4);

        return;
    }

    if (currentView == View::extras)
    {
        auto left = area.removeFromLeft (juce::jmax (250, area.getWidth() / 2));
        auto right = area;
        right.removeFromLeft (10);

        auto placeExtra = [] (juce::Rectangle<int>& column, juce::Label& label,
                              juce::Component& control)
        {
            auto row = column.removeFromTop (24);
            label.setBounds (row.removeFromLeft (88));
            control.setBounds (row);
            column.removeFromTop (4);
        };

        auto modeRow = left.removeFromTop (26);
        modeLabel.setBounds (modeRow.removeFromLeft (88));
        ratioModeButton.setBounds (modeRow.removeFromLeft (74).reduced (1, 1));
        modeRow.removeFromLeft (4);
        fixedModeButton.setBounds (modeRow.removeFromLeft (74).reduced (1, 1));
        left.removeFromTop (4);

        placeExtra (left, fixedHzLabel, fixedHzSlider);
        placeExtra (left, amSensLabel, amSensSlider);
        placeExtra (left, breakPointLabel, breakPointSlider);

        auto leftCurveRow = left.removeFromTop (24);
        leftCurveLabel.setBounds (leftCurveRow.removeFromLeft (88));

        for (auto& button : leftCurveButtons)
            button.setBounds (leftCurveRow.removeFromLeft (58).reduced (1, 1));

        left.removeFromTop (4);

        auto rightCurveRow = left.removeFromTop (24);
        rightCurveLabel.setBounds (rightCurveRow.removeFromLeft (88));

        for (auto& button : rightCurveButtons)
            button.setBounds (rightCurveRow.removeFromLeft (58).reduced (1, 1));

        left.removeFromTop (4);

        placeExtra (left, leftDepthLabel, leftDepthSlider);
        placeExtra (left, rightDepthLabel, rightDepthSlider);

        pitchEgLabel.setBounds (right.removeFromTop (18));
        right.removeFromTop (2);

        for (int i = 0; i < 4; ++i)
        {
            auto row = right.removeFromTop (24);
            pitchEgValueLabels[(size_t) i].setBounds (row.removeFromLeft (22));
            pitchRateSliders[(size_t) i].setBounds (row);
            right.removeFromTop (2);
        }

        right.removeFromTop (4);

        for (int i = 0; i < 4; ++i)
        {
            auto row = right.removeFromTop (24);
            pitchEgValueLabels[(size_t) (i + 4)].setBounds (row.removeFromLeft (22));
            pitchLevelSliders[(size_t) i].setBounds (row);
            right.removeFromTop (2);
        }

        return;
    }

    if (currentView == View::overview)
    {
        diagram.setBounds (area);
        return;
    }

    if (currentView == View::operatorDetail)
    {
        envelopeCurve.setBounds (area.removeFromTop (kEnvelopeHeight));
        area.removeFromTop (kRowGap);

        auto left = area.removeFromLeft (juce::jmax (200, area.getWidth() / 2));
        auto right = area;
        right.removeFromLeft (10);

        auto placeSlider = [] (juce::Rectangle<int>& column, juce::Label& label,
                               juce::Slider& slider)
        {
            auto row = column.removeFromTop (24);
            label.setBounds (row.removeFromLeft (62));
            slider.setBounds (row);
            column.removeFromTop (3);
        };

        placeSlider (left, ratioLabel, ratioSlider);
        placeSlider (left, detuneLabel, detuneSlider);
        placeSlider (left, levelLabel, levelSlider);
        placeSlider (left, velSensLabel, velSensSlider);
        placeSlider (left, rateScaleLabel, rateScaleSlider);

        left.removeFromTop (4);
        operatorPowerButton.setBounds (left.removeFromTop (24).removeFromLeft (140));

        // Operator 6 is the one that can feed back into itself, so its feedback
        // knob lives on its own page.
        if (selectedOperator == kFmOperators - 1)
        {
            left.removeFromTop (6);
            auto row = left.removeFromTop (24);
            feedbackLabel.setBounds (row.removeFromLeft (100));
            feedbackSlider.setBounds (row);
            feedbackLabel.setVisible (true);
            feedbackSlider.setVisible (true);
        }
        else
        {
            feedbackLabel.setVisible (false);
            feedbackSlider.setVisible (false);
        }

        envelopeLabel.setBounds (right.removeFromTop (18));
        right.removeFromTop (2);

        for (int i = 0; i < 4; ++i)
        {
            auto row = right.removeFromTop (24);
            envelopeValueLabels[(size_t) i].setBounds (row.removeFromLeft (22));
            rateSliders[(size_t) i].setBounds (row);
            right.removeFromTop (2);
        }

        right.removeFromTop (4);

        for (int i = 0; i < 4; ++i)
        {
            auto row = right.removeFromTop (24);
            envelopeValueLabels[(size_t) (i + 4)].setBounds (row.removeFromLeft (22));
            envLevelSliders[(size_t) i].setBounds (row);
            right.removeFromTop (2);
        }

        return;
    }

    if (currentView == View::lfo)
    {
        auto left = area.removeFromLeft (juce::jmax (200, area.getWidth() / 2));
        auto right = area;

        auto placeSlider = [] (juce::Rectangle<int>& column, juce::Label& label,
                               juce::Slider& slider)
        {
            auto row = column.removeFromTop (28);
            label.setBounds (row.removeFromLeft (70));
            slider.setBounds (row);
            column.removeFromTop (6);
        };

        placeSlider (left, lfoRateLabel, lfoRateSlider);
        placeSlider (left, lfoDelayLabel, lfoDelaySlider);
        placeSlider (left, lfoPitchLabel, lfoPitchSlider);
        placeSlider (left, lfoAmpLabel, lfoAmpSlider);
        placeSlider (left, lfoPitchModSensLabel, lfoPitchModSensSlider);

        left.removeFromTop (4);
        lfoSyncButton.setBounds (left.removeFromTop (26).removeFromLeft (140));

        right.removeFromLeft (10);
        lfoWaveLabel.setBounds (right.removeFromTop (20));
        right.removeFromTop (4);

        auto waveRow = right.removeFromTop (26);

        for (auto& button : lfoWaveButtons)
            button.setBounds (waveRow.removeFromLeft (58).reduced (1, 1));

        return;
    }

    // View::output
    auto column = area.removeFromLeft (juce::jmax (240, area.getWidth() / 2));

    auto placeRow = [] (juce::Rectangle<int>& areaToUse, juce::Label& label, juce::Slider& slider)
    {
        auto row = areaToUse.removeFromTop (28);
        label.setBounds (row.removeFromLeft (96));
        slider.setBounds (row);
        areaToUse.removeFromTop (6);
    };

    placeRow (column, gainLabel, gainSlider);
    placeRow (column, cutoffLabel, cutoffSlider);
    placeRow (column, resonanceLabel, resonanceSlider);
    placeRow (column, transposeLabel, transposeSlider);
}

//==============================================================================
void SynthEditor::setViewForTest (int view)
{
    setView ((View) juce::jlimit (0, 5, view));
    refresh();
}

void SynthEditor::setLibraryPageForTest (int page)
{
    libraryPage = juce::jlimit (0, 1, page);
    updateLibraryPage();
}

void SynthEditor::setOscModeForTest (bool fixed)
{
    if (auto* track = currentTrack())
    {
        track->synth.patch.ops[(size_t) juce::jlimit (0, kFmOperators - 1, selectedOperator)].mode
            = fixed ? FmOscMode::fixed : FmOscMode::ratio;
        pushParams (track->synth);
        refresh();
    }
}

void SynthEditor::selectAlgorithmForTest (int algorithm)
{
    selectAlgorithm (algorithm);
}

void SynthEditor::setOperatorForTest (int index)
{
    openOperator (index);
}

void SynthEditor::toggleOperatorForTest (int index)
{
    if (auto* track = currentTrack())
        setOperatorEnabled (index, ! track->synth.patch.ops[(size_t) index].enabled);
}

void SynthEditor::setRateForTest (int index, int value)
{
    if (index < 0 || index >= 4)
        return;

    rateSliders[(size_t) index].setValue ((double) value, juce::dontSendNotification);
    pushCurrentParamsToPreview();
    envelopeCurve.repaint();
}

void SynthEditor::selectPresetForTest (int index)
{
    if (index < 0)
        return;

    loadPresetById (kBuiltInPresetIdBase + index);

    // Mirror the ComboBox, so the diagnostic getter reports what a user would see.
    presetBar.refresh();
}

int SynthEditor::getSelectedPresetIndex() const
{
    return presetBar.getSelectedIdForTest() - 1;
}

bool SynthEditor::loadUserPresetForTest (int index)
{
    return loadPresetById (kUserPresetIdBase + index);
}

bool SynthEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

bool SynthEditor::renamePresetForTest (const juce::String& newName)
{
    return renamePresetById (presetBar.getSelectedIdForTest(), newName);
}

bool SynthEditor::deletePresetForTest()
{
    return deletePresetById (presetBar.getSelectedIdForTest());
}

juce::Rectangle<int> SynthEditor::getAlgorithmCellBoundsForTest (int index) const
{
    if (index < 0 || index >= (int) algorithmCells.size())
        return {};

    return algorithmCells[(size_t) index]->getBounds();
}

juce::Rectangle<int> SynthEditor::getOperatorBoxBoundsForTest (int index) const
{
    return diagram.getOperatorBox (index);
}

juce::Rectangle<int> SynthEditor::getAlgorithmCellBoxForTest (int cellIndex,
                                                              int operatorIndex) const
{
    if (cellIndex < 0 || cellIndex >= (int) algorithmCells.size())
        return {};

    return algorithmCells[(size_t) cellIndex]->getOperatorBox (operatorIndex);
}

juce::Rectangle<int> SynthEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return presetBar.getComboBoxBoundsForTest();
        case 1: return getAlgorithmCellBoundsForTest (0);
        case 2: return diagram.getOperatorBox (0);
        case 3: return rateSliders[0].getBounds();
        case 4: return envLevelSliders[0].getBounds();
        case 5: return envelopeCurve.getBounds();
        case 6: return keyboard.getBounds();
        case 7: return backButton.getBounds();
        case 8: return diagram.getBounds();
        case 9: return ratioModeButton.getBounds();
        case 10: return fixedHzSlider.getBounds();
        case 11: return amSensSlider.getBounds();
        case 12: return breakPointSlider.getBounds();
        case 13: return leftDepthSlider.getBounds();
        case 14: return rightDepthSlider.getBounds();
        case 15: return pitchRateSliders[0].getBounds();
        case 16: return pitchLevelSliders[0].getBounds();
        case 17: return lfoSyncButton.getBounds();
        case 18: return lfoPitchModSensSlider.getBounds();
        case 19: return leftCurveButtons[0].getBounds();
        case 20: return rightCurveButtons[0].getBounds();
        case 21: return libraryPageButton.getBounds();
        default: break;
    }

    return {};
}

} // namespace nidaw
