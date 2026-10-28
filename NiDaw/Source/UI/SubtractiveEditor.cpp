#include "SubtractiveEditor.h"
#include "Theme.h"

#include "../Core/SubtractivePresets.h"

namespace nidaw
{

namespace
{
    constexpr int kTitleHeight   = 24;
    constexpr int kPresetHeight  = 26;
    constexpr int kDisplayHeight = 92;
    constexpr int kKeyboardHeight = 84;
    constexpr int kRowHeight     = 24;
    constexpr int kHeaderHeight  = 18;
    constexpr int kRowGap        = 6;
    constexpr int kColumnGap     = 10;
    constexpr int kLabelWidth    = 58;
    constexpr int kToggleWidth   = 58;

    juce::StringArray waveNames()
    {
        return { NI_TXT ("锯齿"), NI_TXT ("方波"), NI_TXT ("脉冲"),
                 NI_TXT ("三角"), NI_TXT ("正弦") };
    }
}

//==============================================================================
void SubtractiveDisplay::setParams (const SubtractiveParams& newParams)
{
    params = newParams;
    repaint();
}

void SubtractiveDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme::panelRaised.withAlpha (0.6f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 1.0f);

    waveBounds   = area.removeFromLeft (area.getWidth() / 2).reduced (6);
    filterBounds = area.reduced (6);

    // ---- one cycle of the oscillator mix -----------------------------------
    {
        const auto& box = waveBounds;
        const auto centreY = (float) box.getCentreY();
        const auto halfHeight = (float) box.getHeight() * 0.45f;

        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) centreY, (float) box.getX(), (float) box.getRight());

        auto total = 0.0;

        for (const auto& osc : params.oscs)
            if (osc.enabled)
                total += (double) osc.level;

        total += (double) params.noiseLevel;

        if (total > 0.0)
        {
            constexpr auto baseHz = 220.0;
            constexpr auto sampleRate = 48000.0;
            constexpr auto periods = 1.0;
            const auto columns = juce::jmax (1, box.getWidth());

            juce::Path path;

            for (int x = 0; x < columns; ++x)
            {
                const auto t = (double) x / (double) columns * periods / baseHz;
                auto value = 0.0;

                for (const auto& osc : params.oscs)
                {
                    if (! osc.enabled || osc.level <= 0.0f)
                        continue;

                    const auto hz = baseHz * subtractive::oscillatorRatio (osc);
                    value += subtractive::oscillatorSample (osc.wave,
                                                            std::fmod (t * hz, 1.0),
                                                            hz / sampleRate,
                                                            osc.pulseWidth)
                                 * (double) osc.level;
                }

                const auto scaled = juce::jlimit (-1.0, 1.0, value / total);
                const auto y = centreY - (float) scaled * halfHeight;

                if (x == 0)
                    path.startNewSubPath ((float) box.getX(), y);
                else
                    path.lineTo ((float) box.getX() + (float) x, y);
            }

            g.setColour (theme::referenceLine);
            g.strokePath (path, juce::PathStrokeType (1.4f));
        }

        g.setColour (theme::dimText);
        g.setFont (theme::font (10.0f));
        g.drawText (NI_TXT ("波形"), box.reduced (2, 0), juce::Justification::topLeft);
    }

    // ---- filter magnitude response -----------------------------------------
    {
        const auto& box = filterBounds;
        const auto cutoffHz = juce::jlimit (20.0, 20000.0, (double) params.filterCutoffHz);
        const auto q = juce::jlimit (0.1, 20.0, (double) params.filterResonance);
        const auto slope = params.filterSlopeDb >= 24 ? 2 : 1;
        const auto columns = juce::jmax (1, box.getWidth());

        const auto minHz = 20.0;
        const auto maxHz = 20000.0;

        const auto magnitudeAt = [&] (double hz)
        {
            const auto r = hz / cutoffHz;
            const auto denom = std::sqrt (std::pow (1.0 - r * r, 2.0) + std::pow (r / q, 2.0));

            auto mag = 1.0 / juce::jmax (1.0e-6, denom);

            switch (params.filterMode)
            {
                case FilterMode::bandPass: mag = (r / q) / juce::jmax (1.0e-6, denom); break;
                case FilterMode::highPass: mag = (r * r) / juce::jmax (1.0e-6, denom); break;
                case FilterMode::notch:
                    mag = std::abs (1.0 - r * r) / juce::jmax (1.0e-6, denom);
                    break;
                case FilterMode::comb:
                    mag = 0.35 + 0.65 * std::abs (std::cos (juce::MathConstants<double>::pi * r));
                    break;
                case FilterMode::lowPass:  break;
            }

            return slope == 2 ? mag * mag : mag;
        };

        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine (box.getBottom() - 1, (float) box.getX(), (float) box.getRight());

        juce::Path path;

        for (int x = 0; x < columns; ++x)
        {
            const auto hz = minHz * std::pow (maxHz / minHz, (double) x / (double) columns);
            const auto mag = juce::jlimit (0.0, 1.0, magnitudeAt (hz));
            const auto y = (float) box.getBottom() - (float) mag * (float) box.getHeight();

            if (x == 0)
                path.startNewSubPath ((float) box.getX(), y);
            else
                path.lineTo ((float) box.getX() + (float) x, y);
        }

        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (1.6f));

        // Where the envelope pushes the cutoff to at its peak.
        const auto peakOctaves = juce::jlimit (-1.0f, 1.0f, params.filterEnvAmount) * 8.0f;
        const auto peakHz = juce::jlimit (minHz, maxHz,
                                          cutoffHz * std::pow (2.0, (double) peakOctaves));
        const auto peakX = (float) box.getX()
                               + (float) (std::log (peakHz / minHz) / std::log (maxHz / minHz))
                                     * (float) box.getWidth();

        g.setColour (theme::accent.withAlpha (0.55f));
        g.drawVerticalLine ((int) peakX, (float) box.getY(), (float) box.getBottom());

        g.setColour (theme::dimText);
        g.setFont (theme::font (10.0f));
        g.drawText (NI_TXT ("滤波响应"), box.reduced (2, 0), juce::Justification::topLeft);
    }
}

//==============================================================================
SubtractiveEditor::SubtractiveEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                                      PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (presetBar);
    presetBar.getEntries       = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName   = [this]
    {
        const auto* track = currentTrack();
        return track != nullptr ? track->subtractive.name : juce::String();
    };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset     = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name) { return userPresetExists (name); };

    addAndMakeVisible (display);
    addAndMakeVisible (filterView);
    addAndMakeVisible (ampEnvelopeView);

    filterView.onGesture = [this] (bool begin, FilterResponseView::Values v)
    {
        if (updating)
            return;

        if (begin)
            beginParamEdit (NI_TXT ("拖动滤波器"));

        cutoff.setValue ((double) v.cutoffHz, juce::dontSendNotification);
        resonance.setValue ((double) v.resonance, juce::dontSendNotification);
        filterEnvAmount.setValue ((double) v.envAmount, juce::dontSendNotification);
        pushCurrentParams();
    };

    ampEnvelopeView.onGesture = [this] (bool begin, EnvelopeEditorView::Values v)
    {
        if (updating)
            return;

        if (begin)
            beginParamEdit (NI_TXT ("拖动音量包络"));

        ampAttack.setValue ((double) v.attack, juce::dontSendNotification);
        ampDecay.setValue ((double) v.decay, juce::dontSendNotification);
        ampSustain.setValue ((double) v.sustain, juce::dontSendNotification);
        ampRelease.setValue ((double) v.release, juce::dontSendNotification);
        pushCurrentParams();
    };

    auto setupSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                               double interval, const juce::String& suffix, bool logarithmic)
    {
        addAndMakeVisible (slider);
        configureSynthKnob (slider);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);

        if (logarithmic && minimum > 0.0)
            slider.setSkewFactorFromMidPoint (std::sqrt (minimum * maximum));

        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整减法音色")); };
        slider.onValueChange = [this] { pushCurrentParams(); };
    };

    auto setupCombo = [this] (juce::ComboBox& combo, const juce::StringArray& items)
    {
        addAndMakeVisible (combo);
        combo.addItemList (items, 1);
        combo.setSelectedId (1, juce::dontSendNotification);
        combo.onChange = [this] { pushCurrentParams(); };
    };

    auto setupHeader = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.0f, true));
        label.setColour (juce::Label::textColourId, theme::accent);
    };

    // ---- oscillators --------------------------------------------------------
    setupHeader (osc1Header, NI_TXT ("振荡器 1"));
    setupCombo (osc1Wave, waveNames());
    setupSlider (osc1Octave, -2.0, 2.0, 1.0, {}, false);
    setupSlider (osc1Semitone, -12.0, 12.0, 1.0, {}, false);
    setupSlider (osc1Detune, -50.0, 50.0, 1.0, " cent", false);
    setupSlider (osc1Level, 0.0, 1.0, 0.01, {}, false);
    setupSlider (osc1PulseWidth, 0.05, 0.95, 0.01, {}, false);

    setupHeader (osc2Header, NI_TXT ("振荡器 2"));
    addAndMakeVisible (osc2Enable);
    osc2Enable.setButtonText (NI_TXT ("开启"));
    osc2Enable.setClickingTogglesState (true);
    osc2Enable.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    osc2Enable.onClick = [this] { pushCurrentParams(); };

    setupCombo (osc2Wave, waveNames());
    setupSlider (osc2Octave, -2.0, 2.0, 1.0, {}, false);
    setupSlider (osc2Semitone, -12.0, 12.0, 1.0, {}, false);
    setupSlider (osc2Detune, -50.0, 50.0, 1.0, " cent", false);
    setupSlider (osc2Level, 0.0, 1.0, 0.01, {}, false);
    setupSlider (osc2PulseWidth, 0.05, 0.95, 0.01, {}, false);

    setupHeader (noiseHeader, NI_TXT ("噪声"));
    setupSlider (noiseLevel, 0.0, 1.0, 0.01, {}, false);

    // ---- filter -------------------------------------------------------------
    setupHeader (filterHeader, NI_TXT ("滤波器"));
    setupCombo (filterMode, { NI_TXT ("低通"), NI_TXT ("带通"), NI_TXT ("高通") });
    setupCombo (filterSlope, { "12 dB", "24 dB" });
    setupSlider (cutoff, 20.0, 20000.0, 1.0, " Hz", true);
    setupSlider (resonance, 0.1, 20.0, 0.01, {}, false);
    setupSlider (filterEnvAmount, -1.0, 1.0, 0.01, {}, false);
    setupSlider (filterKeyTrack, 0.0, 1.0, 0.01, {}, false);

    setupHeader (filterEnvHeader, NI_TXT ("滤波包络"));
    setupSlider (filterAttack, 0.0, 5.0, 0.001, " s", false);
    setupSlider (filterDecay, 0.0, 5.0, 0.001, " s", false);
    setupSlider (filterSustain, 0.0, 1.0, 0.01, {}, false);
    setupSlider (filterRelease, 0.0, 5.0, 0.001, " s", false);

    // ---- amplifier, LFO and output -------------------------------------------
    setupHeader (ampHeader, NI_TXT ("放大器包络"));
    setupSlider (ampAttack, 0.0, 5.0, 0.001, " s", false);
    setupSlider (ampDecay, 0.0, 5.0, 0.001, " s", false);
    setupSlider (ampSustain, 0.0, 1.0, 0.01, {}, false);
    setupSlider (ampRelease, 0.0, 5.0, 0.001, " s", false);
    ampAttack.setVisible (false);
    ampDecay.setVisible (false);
    ampSustain.setVisible (false);
    ampRelease.setVisible (false);

    setupHeader (lfoHeader, "LFO");
    setupSlider (lfoRate, 0.0, 20.0, 0.1, " Hz", false);
    setupCombo (lfoWave, { NI_TXT ("三角"), NI_TXT ("正弦"), NI_TXT ("方波"),
                           NI_TXT ("采样保持") });
    setupSlider (lfoPitch, 0.0, 100.0, 1.0, " cent", false);
    setupSlider (lfoCutoff, 0.0, 1.0, 0.01, {}, false);
    setupSlider (lfoAmp, 0.0, 1.0, 0.01, {}, false);

    setupHeader (outputHeader, NI_TXT ("输出"));
    setupSlider (gain, 0.0, 1.0, 0.01, {}, false);
    configureVolumeBar (gain);

    // ---- preview keyboard ----------------------------------------------------
    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe6e6ee));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::outline);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accent.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dimText);
    keyboard.setAvailableRange (60, 72);
    keyboard.setKeyWidth (34.0f);
    keyboard.setLowestVisibleKey (60);
    keyboardState.addListener (this);

    document.addChangeListener (this);

    refresh();
}

SubtractiveEditor::~SubtractiveEditor()
{
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

void SubtractiveEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

InstrumentTrack* SubtractiveEditor::currentTrack()
{
    auto* track = document.findTrack (trackId);
    return track != nullptr && track->kind == TrackKind::subtractive ? track : nullptr;
}

const InstrumentTrack* SubtractiveEditor::currentTrack() const
{
    return const_cast<SubtractiveEditor*> (this)->currentTrack();
}

void SubtractiveEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

void SubtractiveEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SubtractiveEditor::refresh()
{
    updateControlsFromTrack();
    presetBar.refresh();
}

void SubtractiveEditor::updateControlsFromTrack()
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
    titleLabel.setText (NI_TXT ("减法合成器 · ") + track->name, juce::dontSendNotification);

    const auto& s = track->subtractive;

    osc1Wave.setSelectedId ((int) s.oscs[0].wave + 1, juce::dontSendNotification);
    osc1Octave.setValue ((double) s.oscs[0].octave, juce::dontSendNotification);
    osc1Semitone.setValue ((double) s.oscs[0].semitone, juce::dontSendNotification);
    osc1Detune.setValue ((double) s.oscs[0].detuneCents, juce::dontSendNotification);
    osc1Level.setValue ((double) s.oscs[0].level, juce::dontSendNotification);
    osc1PulseWidth.setValue ((double) s.oscs[0].pulseWidth, juce::dontSendNotification);

    osc2Enable.setToggleState (s.oscs[1].enabled, juce::dontSendNotification);
    osc2Wave.setSelectedId ((int) s.oscs[1].wave + 1, juce::dontSendNotification);
    osc2Octave.setValue ((double) s.oscs[1].octave, juce::dontSendNotification);
    osc2Semitone.setValue ((double) s.oscs[1].semitone, juce::dontSendNotification);
    osc2Detune.setValue ((double) s.oscs[1].detuneCents, juce::dontSendNotification);
    osc2Level.setValue ((double) s.oscs[1].level, juce::dontSendNotification);
    osc2PulseWidth.setValue ((double) s.oscs[1].pulseWidth, juce::dontSendNotification);

    noiseLevel.setValue ((double) s.noiseLevel, juce::dontSendNotification);

    filterMode.setSelectedId ((int) s.filterMode + 1, juce::dontSendNotification);
    filterSlope.setSelectedId (s.filterSlopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
    cutoff.setValue ((double) s.filterCutoffHz, juce::dontSendNotification);
    resonance.setValue ((double) s.filterResonance, juce::dontSendNotification);
    filterEnvAmount.setValue ((double) s.filterEnvAmount, juce::dontSendNotification);
    filterKeyTrack.setValue ((double) s.filterKeyTrack, juce::dontSendNotification);

    filterAttack.setValue ((double) s.filterAttack, juce::dontSendNotification);
    filterDecay.setValue ((double) s.filterDecay, juce::dontSendNotification);
    filterSustain.setValue ((double) s.filterSustain, juce::dontSendNotification);
    filterRelease.setValue ((double) s.filterRelease, juce::dontSendNotification);
    ampEnvelopeView.setValues ({ s.ampAttack, s.ampDecay, s.ampSustain, s.ampRelease });
    filterView.setValues ({ s.filterMode, s.filterSlopeDb, s.filterCutoffHz,
                            s.filterResonance, s.filterEnvAmount, s.filterKeyTrack });

    ampAttack.setValue ((double) s.ampAttack, juce::dontSendNotification);
    ampDecay.setValue ((double) s.ampDecay, juce::dontSendNotification);
    ampSustain.setValue ((double) s.ampSustain, juce::dontSendNotification);
    ampRelease.setValue ((double) s.ampRelease, juce::dontSendNotification);

    lfoRate.setValue ((double) s.lfoRateHz, juce::dontSendNotification);
    lfoWave.setSelectedId (s.lfoWaveform + 1, juce::dontSendNotification);
    lfoPitch.setValue ((double) s.lfoPitchCents, juce::dontSendNotification);
    lfoCutoff.setValue ((double) s.lfoCutoffDepth, juce::dontSendNotification);
    lfoAmp.setValue ((double) s.lfoAmpDepth, juce::dontSendNotification);

    gain.setValue ((double) s.gain, juce::dontSendNotification);

    display.setParams (s);
}

void SubtractiveEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void SubtractiveEditor::pushParams (const SubtractiveParams& params)
{
    audio.updatePreviewSubParams (params);
    document.notifyEdit();
}

void SubtractiveEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& s = track->subtractive;

    s.oscs[0].wave        = (OscWave) juce::jlimit (0, 4, osc1Wave.getSelectedId() - 1);
    s.oscs[0].octave      = juce::jlimit (-2, 2, (int) osc1Octave.getValue());
    s.oscs[0].semitone    = juce::jlimit (-12, 12, (int) osc1Semitone.getValue());
    s.oscs[0].detuneCents = (float) osc1Detune.getValue();
    s.oscs[0].level       = (float) osc1Level.getValue();
    s.oscs[0].pulseWidth  = (float) osc1PulseWidth.getValue();

    s.oscs[1].enabled     = osc2Enable.getToggleState();
    s.oscs[1].wave        = (OscWave) juce::jlimit (0, 4, osc2Wave.getSelectedId() - 1);
    s.oscs[1].octave      = juce::jlimit (-2, 2, (int) osc2Octave.getValue());
    s.oscs[1].semitone    = juce::jlimit (-12, 12, (int) osc2Semitone.getValue());
    s.oscs[1].detuneCents = (float) osc2Detune.getValue();
    s.oscs[1].level       = (float) osc2Level.getValue();
    s.oscs[1].pulseWidth  = (float) osc2PulseWidth.getValue();

    s.noiseLevel = (float) noiseLevel.getValue();

    s.filterMode       = (FilterMode) juce::jlimit (0, 2, filterMode.getSelectedId() - 1);
    s.filterSlopeDb    = filterSlope.getSelectedId() == 2 ? 24 : 12;
    s.filterCutoffHz   = (float) cutoff.getValue();
    s.filterResonance  = (float) resonance.getValue();
    s.filterEnvAmount  = (float) filterEnvAmount.getValue();
    s.filterKeyTrack   = (float) filterKeyTrack.getValue();

    s.filterAttack  = (float) filterAttack.getValue();
    s.filterDecay   = (float) filterDecay.getValue();
    s.filterSustain = (float) filterSustain.getValue();
    s.filterRelease = (float) filterRelease.getValue();

    s.ampAttack  = (float) ampAttack.getValue();
    s.ampDecay   = (float) ampDecay.getValue();
    s.ampSustain = (float) ampSustain.getValue();
    s.ampRelease = (float) ampRelease.getValue();

    s.lfoRateHz      = (float) lfoRate.getValue();
    s.lfoWaveform    = juce::jlimit (0, 3, lfoWave.getSelectedId() - 1);
    s.lfoPitchCents  = (float) lfoPitch.getValue();
    s.lfoCutoffDepth = (float) lfoCutoff.getValue();
    s.lfoAmpDepth    = (float) lfoAmp.getValue();

    s.gain = (float) gain.getValue();

    display.setParams (s);
    pushParams (s);
}

void SubtractiveEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    const auto hz = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    audio.triggerPreviewSubNote (track->subtractive, hz, velocity * 127.0f);
}

void SubtractiveEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        audio.releasePreviewNote();
        previewMidiNote = -1;
    }
}

//==============================================================================
void SubtractiveEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setFont (theme::font (10.5f));

    for (const auto& row : rowLabels)
    {
        g.setColour (row.header ? theme::accent : theme::dimText);
        g.drawText (row.text, row.bounds, juce::Justification::centredLeft);
    }
}

void SubtractiveEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    titleLabel.setBounds (area.removeFromTop (kTitleHeight));
    presetBar.setBounds (area.removeFromTop (kPresetHeight));
    area.removeFromTop (kRowGap);

    display.setBounds (area.removeFromTop (kDisplayHeight));
    filterView.setBounds (display.getBounds().withTrimmedLeft (display.getWidth() / 2).reduced (5));
    area.removeFromTop (kRowGap);

    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    area.removeFromBottom (kRowGap);

    rowLabels.clear();

    auto placeRow = [this] (juce::Rectangle<int>& column, const juce::String& text,
                            juce::Component& control)
    {
        auto row = column.removeFromTop (kRowHeight);
        rowLabels.push_back ({ row.removeFromLeft (kLabelWidth), text, false });
        control.setBounds (row);
    };

    auto placeHeader = [this] (juce::Rectangle<int>& column, const juce::String& text,
                               juce::Component* trailing = nullptr)
    {
        auto row = column.removeFromTop (kHeaderHeight);

        if (trailing != nullptr)
        {
            trailing->setBounds (row.removeFromRight (kToggleWidth));
            row.removeFromRight (4);
        }

        rowLabels.push_back ({ row, text, true });
    };

    const auto columnWidth = (area.getWidth() - 3 * kColumnGap) / 4;

    juce::Rectangle<int> columns[4];

    for (int i = 0; i < 4; ++i)
    {
        columns[i] = area.removeFromLeft (i < 3 ? columnWidth : area.getWidth());

        if (i < 3)
            area.removeFromLeft (kColumnGap);
    }

    // ---- column 1: oscillator 1 --------------------------------------------
    {
        auto& column = columns[0];
        placeHeader (column, NI_TXT ("振荡器 1"));
        placeRow (column, NI_TXT ("波形"), osc1Wave);
        placeRow (column, NI_TXT ("八度"), osc1Octave);
        placeRow (column, NI_TXT ("半音"), osc1Semitone);
        placeRow (column, NI_TXT ("失谐"), osc1Detune);
        placeRow (column, NI_TXT ("音量"), osc1Level);
        placeRow (column, NI_TXT ("脉宽"), osc1PulseWidth);
    }

    // ---- column 2: oscillator 2 and the noise source ------------------------
    {
        auto& column = columns[1];
        placeHeader (column, NI_TXT ("振荡器 2"), &osc2Enable);
        placeRow (column, NI_TXT ("波形"), osc2Wave);
        placeRow (column, NI_TXT ("八度"), osc2Octave);
        placeRow (column, NI_TXT ("半音"), osc2Semitone);
        placeRow (column, NI_TXT ("失谐"), osc2Detune);
        placeRow (column, NI_TXT ("音量"), osc2Level);
        placeRow (column, NI_TXT ("脉宽"), osc2PulseWidth);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("噪声"));
        placeRow (column, NI_TXT ("音量"), noiseLevel);
    }

    // ---- column 3: the filter and its envelope ------------------------------
    {
        auto& column = columns[2];
        placeHeader (column, NI_TXT ("滤波器"));
        placeRow (column, NI_TXT ("模式"), filterMode);
        placeRow (column, NI_TXT ("斜率"), filterSlope);
        placeRow (column, NI_TXT ("截止"), cutoff);
        placeRow (column, NI_TXT ("共振"), resonance);
        placeRow (column, NI_TXT ("包络量"), filterEnvAmount);
        placeRow (column, NI_TXT ("键跟踪"), filterKeyTrack);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("滤波包络"));
        placeRow (column, NI_TXT ("起音"), filterAttack);
        placeRow (column, NI_TXT ("衰减"), filterDecay);
        placeRow (column, NI_TXT ("保持"), filterSustain);
        placeRow (column, NI_TXT ("释放"), filterRelease);
    }

    // ---- column 4: amplifier, LFO and output --------------------------------
    {
        auto& column = columns[3];
        placeHeader (column, NI_TXT ("放大器包络"));
        ampEnvelopeView.setBounds (column.removeFromTop (86).reduced (2, 0));
        column.removeFromTop (kRowGap);
        placeHeader (column, "LFO");
        placeRow (column, NI_TXT ("速率"), lfoRate);
        placeRow (column, NI_TXT ("波形"), lfoWave);
        placeRow (column, NI_TXT ("→音高"), lfoPitch);
        placeRow (column, NI_TXT ("→滤波"), lfoCutoff);
        placeRow (column, NI_TXT ("→音量"), lfoAmp);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("输出"));
        placeRow (column, NI_TXT ("增益"), gain);
    }
}

//==============================================================================
std::vector<PresetBar::Entry> SubtractiveEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    int count = 0;
    const auto* bank = subtractive::bank (count);

    for (int i = 0; i < count; ++i)
        list.push_back ({ kBuiltInPresetIdBase + i, juce::String::fromUTF8 (bank[i].name), false });

    const auto& saved = presets.getSubtractivePresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的减法音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

bool SubtractiveEditor::userPresetExists (const juce::String& name) const
{
    return presets.findSubtractivePresetByName (name) >= 0;
}

bool SubtractiveEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    auto params = track->subtractive;

    if (id >= kUserPresetIdBase)
    {
        const auto index = id - kUserPresetIdBase;

        if (index < 0 || index >= (int) presets.getSubtractivePresets().size())
            return false;

        params = presets.getSubtractivePresets()[(size_t) index];
    }
    else
    {
        subtractive::applyPreset (params, id - kBuiltInPresetIdBase);
    }

    document.beginEdit (NI_TXT ("载入音色预设"));

    if (auto* target = currentTrack())
        target->subtractive = params;

    if (auto* target = currentTrack())
        pushParams (target->subtractive);

    refresh();
    return true;
}

bool SubtractiveEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->subtractive;
    params.name = name;

    const auto existing = presets.findSubtractivePresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceSubtractivePreset (existing, params);
    }
    else
    {
        presets.addSubtractivePreset (params);
    }

    // Saving names the loaded sound too, so the list highlights it.
    document.beginEdit (NI_TXT ("保存减法器预设"));

    if (auto* target = currentTrack())
        target->subtractive.name = name;

    refresh();
    return true;
}

bool SubtractiveEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getSubtractivePresets().size())
        return false;

    const auto oldName = presets.getSubtractivePresets()[(size_t) index].name;
    presets.renameSubtractivePreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->subtractive.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名减法器预设"));

        if (auto* target = currentTrack())
            target->subtractive.name = newName;
    }

    refresh();
    return true;
}

bool SubtractiveEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getSubtractivePresets().size())
        return false;

    presets.removeSubtractivePreset (index);
    refresh();
    return true;
}

//==============================================================================
juce::Rectangle<int> SubtractiveEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0:  return osc1Wave.getBounds();
        case 1:  return osc1Level.getBounds();
        case 2:  return osc2Wave.getBounds();
        case 3:  return osc2Level.getBounds();
        case 4:  return osc2Enable.getBounds();
        case 5:  return noiseLevel.getBounds();
        case 6:  return filterMode.getBounds();
        case 7:  return cutoff.getBounds();
        case 8:  return resonance.getBounds();
        case 9:  return filterEnvAmount.getBounds();
        case 10: return ampAttack.getBounds();
        case 11: return ampRelease.getBounds();
        case 12: return lfoRate.getBounds();
        case 13: return gain.getBounds();
        case 14: return keyboard.getBounds();
        case 15: return display.getBounds();
        default: break;
    }

    return {};
}

juce::String SubtractiveEditor::getChoiceTextForTest (int which, int index) const
{
    const juce::ComboBox* combo = nullptr;

    switch (which)
    {
        case 0: combo = &osc1Wave; break;
        case 1: combo = &filterMode; break;
        case 2: combo = &filterSlope; break;
        case 3: combo = &lfoWave; break;
        default: return {};
    }

    if (index < 0 || index >= combo->getNumItems())
        return {};

    return combo->getItemText (index);
}

std::pair<int, int> SubtractiveEditor::getPresetCountsForTest() const
{
    return { subtractive::builtInCount(), (int) presets.getSubtractivePresets().size() };
}

bool SubtractiveEditor::selectPresetForTest (int id)
{
    return loadPresetById (id);
}

void SubtractiveEditor::setParamForTest (int which, double value)
{
    switch (which)
    {
        case 0:  osc1Level.setValue (value, juce::sendNotificationSync); break;
        case 1:  osc2Enable.setToggleState (value > 0.5, juce::dontSendNotification);
                 pushCurrentParams(); break;
        case 2:  cutoff.setValue (value, juce::sendNotificationSync); break;
        case 3:  resonance.setValue (value, juce::sendNotificationSync); break;
        case 4:  filterEnvAmount.setValue (value, juce::sendNotificationSync); break;
        case 5:  ampAttack.setValue (value, juce::sendNotificationSync); break;
        case 6:  ampRelease.setValue (value, juce::sendNotificationSync); break;
        case 7:  lfoPitch.setValue (value, juce::sendNotificationSync); break;
        case 8:  gain.setValue (value, juce::sendNotificationSync); break;
        case 9:  osc1Wave.setSelectedId ((int) value, juce::sendNotificationSync); break;
        case 10: filterMode.setSelectedId ((int) value, juce::sendNotificationSync); break;
        default: break;
    }
}

bool SubtractiveEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

bool SubtractiveEditor::loadUserPresetForTest (int index)
{
    return loadPresetById (kUserPresetIdBase + index);
}

bool SubtractiveEditor::renamePresetForTest (const juce::String& newName)
{
    return renamePresetById (presetBar.getSelectedIdForTest(), newName);
}

bool SubtractiveEditor::deletePresetForTest()
{
    return deletePresetById (presetBar.getSelectedIdForTest());
}

} // namespace nidaw
