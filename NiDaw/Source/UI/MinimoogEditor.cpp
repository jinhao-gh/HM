#include "MinimoogEditor.h"
#include "Theme.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr int kCheekWidth  = 14;
    constexpr int kStripHeight = 46;

    const juce::Colour kPanel      { 0xff121213 };
    const juce::Colour kPanelEdge  { 0xff3a3a3c };
    const juce::Colour kCream      { 0xffe9e4d4 };
    const juce::Colour kDimCream   { 0xffa8a292 };
    const juce::Colour kWoodDark   { 0xff1d0d05 };
    const juce::Colour kWoodMid    { 0xff4a2412 };
    const juce::Colour kWoodLight  { 0xff7a3f1e };
    const juce::Colour kOrange     { 0xffd2572f };
    const juce::Colour kMetal      { 0xffc8c4ba };

    const juce::StringArray& waveNames()
    {
        static const juce::StringArray names { NI_TXT ("三角"), NI_TXT ("斜三角"),
                                               NI_TXT ("锯齿"), NI_TXT ("方波"),
                                               NI_TXT ("宽脉冲"), NI_TXT ("窄脉冲") };
        return names;
    }

    const juce::StringArray& rangeLabels()
    {
        static const juce::StringArray names { "LO", "32", "16", "8", "4", "2" };
        return names;
    }

    void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> panel,
                           const juce::String& title)
    {
        g.setColour (kCream);
        g.setFont (theme::font (16.0f, true));
        g.drawText (title, panel.removeFromBottom (26), juce::Justification::centred);
    }
}

//==============================================================================
void MinimoogRotarySwitch::setLabels (juce::StringArray newLabels)
{
    labels = std::move (newLabels);
    selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), selected);
    repaint();
}

void MinimoogRotarySwitch::setShowLabels (bool shouldShow)
{
    showLabels = shouldShow;
    repaint();
}

void MinimoogRotarySwitch::setSelected (int index, bool notify)
{
    const auto next = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);

    if (next == selected)
        return;

    selected = next;
    repaint();

    if (notify && onChange != nullptr)
        onChange (selected);
}

void MinimoogRotarySwitch::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    if (showLabels && ! labels.isEmpty())
    {
        auto labelRow = area.removeFromTop (15);
        const auto width = juce::jmax (8, labelRow.getWidth() / labels.size());

        for (int i = 0; i < labels.size(); ++i)
        {
            auto slot = labelRow.withX (labelRow.getX() + i * width).withWidth (width);
            g.setColour (i == selected ? kCream : kDimCream);
            g.setFont (theme::font (9.0f, i == selected));
            g.drawText (labels[i], slot, juce::Justification::centred);
        }
    }

    auto knobArea = area.reduced (8, 4);
    const auto diameter = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    auto knob = juce::Rectangle<float> ((float) knobArea.getCentreX() - diameter * 0.5f,
                                        (float) knobArea.getCentreY() - diameter * 0.5f,
                                        (float) diameter, (float) diameter);

    g.setColour (juce::Colour (0xff080809));
    g.fillEllipse (knob);
    g.setColour (kMetal.withAlpha (0.55f));
    g.drawEllipse (knob.reduced (1.0f), 1.0f);

    const auto count = juce::jmax (1, labels.size());
    const auto angle = juce::MathConstants<float>::pi * 1.25f
                           + (float) selected / (float) juce::jmax (1, count - 1)
                                 * juce::MathConstants<float>::pi * 1.5f;
    const auto centre = knob.getCentre();
    const auto radius = knob.getWidth() * 0.42f;

    g.setColour (kCream);
    g.drawLine (centre.x, centre.y,
                centre.x + std::sin (angle) * radius,
                centre.y - std::cos (angle) * radius,
                2.4f);
}

void MinimoogRotarySwitch::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStart = e.getPosition();
    dragStartSelected = selected;
    step (e.getPosition().y < getHeight() / 2 ? 1 : -1);
}

void MinimoogRotarySwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = (e.getPosition().y - dragStart.y) / 10;
    const auto wanted = juce::jlimit (0, juce::jmax (0, labels.size() - 1),
                                      dragStartSelected - delta);

    if (wanted != selected)
    {
        selected = wanted;
        repaint();

        if (onChange != nullptr)
            onChange (selected);
    }
}

void MinimoogRotarySwitch::mouseWheelMove (const juce::MouseEvent&,
                                           const juce::MouseWheelDetails& wheel)
{
    step (wheel.deltaY > 0.0f ? -1 : 1);
}

bool MinimoogRotarySwitch::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
    {
        step (-1);
        return true;
    }

    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
    {
        step (1);
        return true;
    }

    return false;
}

void MinimoogRotarySwitch::step (int delta)
{
    const auto next = juce::jlimit (0, juce::jmax (0, labels.size() - 1), selected + delta);

    if (next == selected)
        return;

    selected = next;
    repaint();

    if (onChange != nullptr)
        onChange (selected);
}

//==============================================================================
void MinimoogRocker::setLabels (juce::String top, juce::String bottom)
{
    topLabel = std::move (top);
    bottomLabel = std::move (bottom);
    repaint();
}

void MinimoogRocker::setState (bool on, bool notify)
{
    if (on == state)
        return;

    state = on;
    repaint();

    if (notify && onChange != nullptr)
        onChange (state);
}

void MinimoogRocker::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto caption = area.removeFromBottom (13);
    auto rocker = area.reduced (1, 1);

    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (rocker.toFloat(), 3.0f);

    if (state)
    {
        g.setGradientFill (juce::ColourGradient (kOrange.brighter (0.25f),
                                                 (float) rocker.getX(), (float) rocker.getY(),
                                                 kOrange.darker (0.35f),
                                                 (float) rocker.getX(),
                                                 (float) rocker.getBottom(), false));
        g.fillRoundedRectangle (rocker.toFloat().reduced (2.0f), 2.5f);
    }

    g.setColour (kMetal.withAlpha (0.35f));
    g.drawRoundedRectangle (rocker.toFloat().reduced (0.5f), 3.0f, 1.0f);

    g.setColour (state ? kCream : kDimCream);
    g.setFont (theme::font (10.5f, state));
    g.drawText (state ? topLabel : bottomLabel, caption, juce::Justification::centred);
}

void MinimoogRocker::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled())
        return;

    setState (! state, true);
}

bool MinimoogRocker::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        setState (! state, true);
        return true;
    }

    return false;
}

//==============================================================================
MinimoogEditor::MinimoogEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                                PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse)
{
    addAndMakeVisible (presetBar);
    presetBar.getEntries       = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName   = [this] { return currentTrack() != nullptr
                                                    ? currentTrack()->minimoog.name
                                                    : juce::String(); };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset     = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name)
                                 { return userPresetExists (name); };

    addAndMakeVisible (polyphonyBox);
    polyphonyBox.addItem (NI_TXT ("单音"), 1);
    polyphonyBox.addItem (NI_TXT ("2 声部"), 2);
    polyphonyBox.addItem (NI_TXT ("4 声部"), 3);
    polyphonyBox.addItem (NI_TXT ("8 声部"), 4);
    polyphonyBox.addItem (NI_TXT ("16 声部"), 5);
    polyphonyBox.addItem (NI_TXT ("32 声部"), 6);
    polyphonyBox.addItem (NI_TXT ("不限（最多 64 声部）"), 7);
    polyphonyBox.onChange = [this]
    {
        if (updating)
            return;

        static const int values[] = { 0, 2, 4, 8, 16, 32, -1 };
        setPolyphony (values[juce::jlimit (0, 6, polyphonyBox.getSelectedId() - 1)]);
    };

    addAndMakeVisible (velocityKnob);
    configureKnob (velocityKnob, 0.0, 10.0, 0.1, 7.0);
    velocityKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    velocityKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);

    addAndMakeVisible (mainVolume);
    configureKnob (mainVolume, 0.0, 10.0, 0.1, 7.0);
    mainVolume.setSliderStyle (juce::Slider::LinearHorizontal);
    mainVolume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);

    auto commit = [this] (const char* name)
    {
        if (updating)
            return;

        beginParamEdit (NI_TXT (name));
        pushCurrentParams();
    };

    auto setupRocker = [this, commit] (MinimoogRocker& rocker, const juce::String& on,
                                       const juce::String& off, bool initial)
    {
        addAndMakeVisible (rocker);
        rocker.setLabels (on, off);
        rocker.setState (initial, false);
        rocker.onChange = [commit] (bool) { commit ("迷你穆格开关"); };
    };

    auto setupDisabledRocker = [this] (MinimoogRocker& rocker, const juce::String& on,
                                       const juce::String& off)
    {
        addAndMakeVisible (rocker);
        rocker.setLabels (on, off);
        rocker.setState (false, false);
        rocker.setEnabled (false);
        rocker.setAlpha (0.45f);
    };

    for (int i = 0; i < 3; ++i)
    {
        auto& range = rangeSwitch[i];
        addAndMakeVisible (range);
        range.setLabels (rangeLabels());
        range.setSelected (3, false);
        range.onChange = [commit] (int) { commit ("振荡器粗调"); };

        auto& wave = waveSwitch[i];
        addAndMakeVisible (wave);
        wave.setLabels ({ "1", "2", "3", "4", "5", "6" });
        wave.setShowLabels (false);
        wave.setSelected (2, false);
        wave.onChange = [commit] (int) { commit ("振荡器波形"); };

        auto& name = waveName[(size_t) i];
        addAndMakeVisible (name);
        name.setFont (theme::font (11.5f, true));
        name.setColour (juce::Label::textColourId, kCream);
        name.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (tune[i]);
        configureKnob (tune[i], -7.0, 7.0, 0.1, 0.0);
        tune[i].setEnabled (i != 0);

        addAndMakeVisible (oscLevel[i]);
        configureKnob (oscLevel[i], 0.0, 10.0, 0.1, i == 0 ? 9.0 : 5.0);

        setupRocker (oscOn[i], NI_TXT ("开"), NI_TXT ("关"), true);
    }

    setupRocker (unison, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (osc3Control, NI_TXT ("跟随"), NI_TXT ("固定"), true);
    setupRocker (distortionOn, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (noiseType, NI_TXT ("粉"), NI_TXT ("白"), false);
    setupRocker (keyTrack1, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (keyTrack2, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (releaseSwitch, NI_TXT ("开"), NI_TXT ("关"), true);
    setupDisabledRocker (osc3FilterEg, NI_TXT ("开"), NI_TXT ("关"));
    setupDisabledRocker (noiseLfo, NI_TXT ("开"), NI_TXT ("关"));

    addAndMakeVisible (masterTune);
    configureKnob (masterTune, -50.0, 50.0, 1.0, 0.0);
    addAndMakeVisible (glideKnob);
    configureKnob (glideKnob, 0.0, 10.0, 0.1, 1.0);
    addAndMakeVisible (modMix);
    configureKnob (modMix, 0.0, 10.0, 0.1, 0.0);

    addAndMakeVisible (noiseLevelSlider);
    configureKnob (noiseLevelSlider, 0.0, 10.0, 0.1, 0.0);
    addAndMakeVisible (distortion);
    configureKnob (distortion, 0.0, 10.0, 0.1, 0.0);

    addAndMakeVisible (cutoff);
    configureKnob (cutoff, -5.0, 5.0, 0.05, 3.0);
    addAndMakeVisible (emphasis);
    configureKnob (emphasis, 0.0, 10.0, 0.1, 0.0);
    addAndMakeVisible (contour);
    configureKnob (contour, 0.0, 10.0, 0.1, 5.0);

    addAndMakeVisible (filterAttack);
    configureKnob (filterAttack, 0.0, 10.0, 0.1, 4.0);
    addAndMakeVisible (filterDecay);
    configureKnob (filterDecay, 0.0, 10.0, 0.1, 6.0);
    addAndMakeVisible (filterSustainLevel);
    configureKnob (filterSustainLevel, 0.0, 10.0, 0.1, 8.0);
    filterSustainLevel.setDoubleClickReturnValue (true, 8.0);

    addAndMakeVisible (ampAttack);
    configureKnob (ampAttack, 0.0, 10.0, 0.1, 2.0);
    addAndMakeVisible (ampDecay);
    configureKnob (ampDecay, 0.0, 10.0, 0.1, 5.0);
    addAndMakeVisible (ampSustainLevel);
    configureKnob (ampSustainLevel, 0.0, 10.0, 0.1, 8.0);
    ampSustainLevel.setDoubleClickReturnValue (true, 8.0);

    auto wireSlider = [this, commit] (juce::Slider& slider)
    {
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整迷你穆格")); };
        slider.onValueChange = [commit] { commit ("调整迷你穆格"); };
    };

    wireSlider (velocityKnob);
    wireSlider (mainVolume);
    wireSlider (masterTune);
    wireSlider (glideKnob);
    wireSlider (modMix);
    wireSlider (noiseLevelSlider);
    wireSlider (distortion);
    wireSlider (cutoff);
    wireSlider (emphasis);
    wireSlider (contour);
    wireSlider (filterAttack);
    wireSlider (filterDecay);
    wireSlider (filterSustainLevel);
    wireSlider (ampAttack);
    wireSlider (ampDecay);
    wireSlider (ampSustainLevel);

    for (int i = 0; i < 3; ++i)
    {
        wireSlider (tune[i]);
        wireSlider (oscLevel[i]);
    }

    document.addChangeListener (this);
    refresh();
}

MinimoogEditor::~MinimoogEditor()
{
    document.removeChangeListener (this);
}

void MinimoogEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

void MinimoogEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

InstrumentTrack* MinimoogEditor::currentTrack()
{
    auto* track = document.findTrack (trackId);
    return track != nullptr && track->kind == TrackKind::minimoog ? track : nullptr;
}

const InstrumentTrack* MinimoogEditor::currentTrack() const
{
    return const_cast<MinimoogEditor*> (this)->currentTrack();
}

void MinimoogEditor::configureKnob (juce::Slider& slider, double minimum, double maximum,
                                    double interval, double defaultValue,
                                    const juce::String& suffix)
{
    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 15);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setRange (minimum, maximum, interval);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setMouseDragSensitivity (180);
    slider.setScrollWheelEnabled (true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, kCream);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff09090a));
    slider.setColour (juce::Slider::thumbColourId, kMetal);
    slider.setColour (juce::Slider::textBoxTextColourId, kCream);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
}

void MinimoogEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void MinimoogEditor::refresh()
{
    updateControlsFromTrack();
}

//==============================================================================
float MinimoogEditor::cutoffToKnob (float hz) noexcept
{
    const auto clamped = juce::jlimit (10.0f, 20000.0f, hz);
    return 10.0f * std::log (clamped / 10.0f) / std::log (2000.0f) - 5.0f;
}

float MinimoogEditor::knobToCutoff (float knob) noexcept
{
    return juce::jlimit (10.0f, 20000.0f,
                         10.0f * std::pow (2000.0f, (juce::jlimit (-5.0f, 5.0f, knob) + 5.0f) / 10.0f));
}

float MinimoogEditor::secondsToKnob (float seconds) noexcept
{
    const auto clamped = juce::jlimit (0.001f, 20.0f, seconds);
    return 10.0f * std::log (clamped / 0.001f) / std::log (20000.0f);
}

float MinimoogEditor::knobToSeconds (float knob) noexcept
{
    return juce::jlimit (0.001f, 20.0f,
                         0.001f * std::pow (20000.0f, juce::jlimit (0.0f, 10.0f, knob) / 10.0f));
}

void MinimoogEditor::updateControlsFromTrack()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    auto* track = currentTrack();

    setEnabled (track != nullptr);

    if (track == nullptr)
    {
        presetBar.refresh();
        return;
    }

    const auto& p = track->minimoog;

    for (int i = 0; i < 3; ++i)
    {
        rangeSwitch[i].setSelected ((int) p.oscs[(size_t) i].range, false);
        waveSwitch[i].setSelected ((int) p.oscs[(size_t) i].wave, false);
        waveName[(size_t) i].setText (waveNames()[juce::jlimit (0, 5,
                                                               (int) p.oscs[(size_t) i].wave)],
                                      juce::dontSendNotification);
        tune[i].setValue ((double) p.oscs[(size_t) i].tuneSemitones, juce::dontSendNotification);
        oscLevel[i].setValue (10.0 * std::sqrt ((double) p.oscs[(size_t) i].level),
                              juce::dontSendNotification);
        oscOn[i].setState (p.oscs[(size_t) i].enabled, false);
    }

    unison.setState (p.unison, false);
    osc3Control.setState (p.osc3Control, false);
    distortionOn.setState (p.distortionOn, false);
    masterTune.setValue (p.masterTuneCents, juce::dontSendNotification);
    glideKnob.setValue (secondsToKnob (p.glideSeconds), juce::dontSendNotification);
    modMix.setValue ((double) p.modMix * 10.0, juce::dontSendNotification);

    noiseLevelSlider.setValue (10.0 * std::sqrt ((double) p.noiseLevel),
                               juce::dontSendNotification);
    noiseType.setState (p.noisePink, false);
    distortion.setValue ((double) p.distortion * 10.0, juce::dontSendNotification);

    cutoff.setValue (cutoffToKnob (p.filterCutoffHz), juce::dontSendNotification);
    emphasis.setValue ((double) p.filterEmphasis * 10.0, juce::dontSendNotification);
    contour.setValue ((double) p.filterContourOctaves / 8.0 * 10.0, juce::dontSendNotification);
    keyTrack1.setState (p.filterKeyTrack1, false);
    keyTrack2.setState (p.filterKeyTrack2, false);

    filterAttack.setValue (secondsToKnob (p.filterAttack), juce::dontSendNotification);
    filterDecay.setValue (secondsToKnob (p.filterDecay), juce::dontSendNotification);
    filterSustainLevel.setValue ((double) p.filterSustainLevel * 10.0,
                                 juce::dontSendNotification);

    ampAttack.setValue (secondsToKnob (p.ampAttack), juce::dontSendNotification);
    ampDecay.setValue (secondsToKnob (p.ampDecay), juce::dontSendNotification);
    ampSustainLevel.setValue ((double) p.ampSustainLevel * 10.0, juce::dontSendNotification);
    releaseSwitch.setState (p.releaseOn, false);

    mainVolume.setValue ((double) p.volume * 10.0, juce::dontSendNotification);
    velocityKnob.setValue ((double) p.velocitySensitivity * 10.0, juce::dontSendNotification);

    static const int polyphonyValues[] = { 0, 2, 4, 8, 16, 32, -1 };
    auto polyphonyItem = 1;

    for (int i = 0; i < 7; ++i)
        if (polyphonyValues[i] == p.polyphony)
            polyphonyItem = i + 1;

    polyphonyBox.setSelectedId (polyphonyItem, juce::dontSendNotification);

    presetBar.refresh();
    repaint();
}

void MinimoogEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void MinimoogEditor::pushParams (const MinimoogParams& params)
{
    audio.updatePreviewMinimoogParams (params);
    document.notifyEdit();
}

void MinimoogEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& p = track->minimoog;

    for (int i = 0; i < 3; ++i)
    {
        p.oscs[(size_t) i].enabled = oscOn[i].getState();
        p.oscs[(size_t) i].wave = (MinimoogWave) waveSwitch[i].getSelected();
        p.oscs[(size_t) i].range = (MinimoogRange) rangeSwitch[i].getSelected();
        p.oscs[(size_t) i].tuneSemitones = (float) tune[i].getValue();
        p.oscs[(size_t) i].level = (float) std::pow (oscLevel[i].getValue() / 10.0, 2.0);
    }

    p.unison = unison.getState();
    p.osc3Control = osc3Control.getState();
    p.distortionOn = distortionOn.getState();
    p.masterTuneCents = (float) masterTune.getValue();
    p.glideSeconds = knobToSeconds ((float) glideKnob.getValue());
    p.glideOn = p.glideSeconds > 0.001f;
    p.modMix = (float) modMix.getValue() / 10.0f;

    p.noiseLevel = (float) std::pow (noiseLevelSlider.getValue() / 10.0, 2.0);
    p.noisePink = noiseType.getState();
    p.distortion = juce::jlimit (0.0f, 1.0f, (float) distortion.getValue() / 10.0f);

    p.filterCutoffHz = knobToCutoff ((float) cutoff.getValue());
    p.filterEmphasis = (float) emphasis.getValue() / 10.0f;
    p.filterContourOctaves = (float) contour.getValue() / 10.0f * 8.0f;
    p.filterKeyTrack1 = keyTrack1.getState();
    p.filterKeyTrack2 = keyTrack2.getState();

    p.filterAttack = knobToSeconds ((float) filterAttack.getValue());
    p.filterDecay = knobToSeconds ((float) filterDecay.getValue());
    p.filterSustainLevel = juce::jlimit (0.0f, 1.0f,
                                         (float) filterSustainLevel.getValue() / 10.0f);

    p.ampAttack = knobToSeconds ((float) ampAttack.getValue());
    p.ampDecay = knobToSeconds ((float) ampDecay.getValue());
    p.ampSustainLevel = juce::jlimit (0.0f, 1.0f,
                                      (float) ampSustainLevel.getValue() / 10.0f);
    p.releaseOn = releaseSwitch.getState();

    p.volume = (float) mainVolume.getValue() / 10.0f;
    p.velocitySensitivity = juce::jlimit (0.0f, 1.0f, (float) velocityKnob.getValue() / 10.0f);

    pushParams (p);
}

void MinimoogEditor::setPolyphony (int polyphony)
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    beginParamEdit (NI_TXT ("复音数"));
    track->minimoog.polyphony = juce::jlimit (-1, kMaxMinimoogVoices, polyphony);
    pushParams (track->minimoog);
}

//==============================================================================
void MinimoogEditor::resized()
{
    printedLabels.clear();

    auto area = getLocalBounds();
    auto strip = area.removeFromTop (kStripHeight).reduced (6, 4);

    strip.removeFromRight (4);
    presetBar.setBounds (strip.removeFromRight (500).reduced (0, 3));
    strip.removeFromRight (10);
    auto volumeArea = strip.removeFromRight (176);
    auto volumeLabel = volumeArea.removeFromLeft (58);
    printedLabels.push_back ({ volumeLabel, NI_TXT ("主音量"), false,
                               juce::Justification::centredRight });
    mainVolume.setBounds (volumeArea.reduced (0, 3));
    strip.removeFromRight (12);
    auto velocityArea = strip.removeFromRight (104);
    auto velocityLabel = velocityArea.removeFromLeft (36);
    printedLabels.push_back ({ velocityLabel, NI_TXT ("力度"), false,
                               juce::Justification::centredRight });
    velocityKnob.setBounds (velocityArea.reduced (0, 3));
    strip.removeFromRight (12);
    auto polyphonyArea = strip.removeFromRight (112);
    auto polyphonyLabel = polyphonyArea.removeFromLeft (52);
    printedLabels.push_back ({ polyphonyLabel, NI_TXT ("复音数"), false,
                               juce::Justification::centredRight });
    polyphonyBox.setBounds (polyphonyArea.reduced (0, 3));

    area.removeFromTop (4);
    area.removeFromBottom (8);
    panelArea = area.reduced (kCheekWidth, 0);

    auto inner = panelArea.reduced (8, 6);
    controllersArea = inner.removeFromLeft (226);
    inner.removeFromLeft (8);
    oscillatorArea = inner.removeFromLeft (356);
    inner.removeFromLeft (8);
    mixerArea = inner.removeFromLeft (206);
    inner.removeFromLeft (8);
    modifiersArea = inner;

    auto placeKnob = [this] (juce::Rectangle<int> bounds, juce::Slider& slider,
                             const juce::String& caption, int knobHeight = 0)
    {
        auto label = bounds.removeFromTop (14);
        printedLabels.push_back ({ label, caption, false });
        slider.setBounds (bounds.removeFromTop (knobHeight > 0 ? knobHeight : bounds.getHeight()));
    };

    auto placeSwitch = [this] (juce::Rectangle<int> bounds, MinimoogRocker& rocker,
                               const juce::String& caption)
    {
        auto label = bounds.removeFromTop (13);
        printedLabels.push_back ({ label, caption, false });
        rocker.setBounds (bounds.removeFromTop (30).removeFromLeft (50));
    };

    // ---- 控制器 ------------------------------------------------------------
    {
        auto col = controllersArea.reduced (6, 2);
        auto top = col.removeFromTop (126);

        auto tuneArea = top.removeFromLeft (90);
        placeKnob (tuneArea, masterTune, NI_TXT ("调音"), 100);

        top.removeFromLeft (12);
        placeSwitch (top.removeFromTop (54), unison, NI_TXT ("齐奏"));

        col.removeFromTop (36);
        auto row = col.removeFromTop (126);
        auto glideArea = row.removeFromLeft (90);
        placeKnob (glideArea, glideKnob, NI_TXT ("滑音"), 100);
        row.removeFromLeft (12);
        auto mixArea = row.removeFromLeft (90);
        placeKnob (mixArea, modMix, NI_TXT ("调制混合"), 100);

        col.removeFromTop (76);
        auto switches = col.removeFromTop (58);
        auto egArea = switches.removeFromLeft (66);
        placeSwitch (egArea, osc3FilterEg, NI_TXT ("OSC3 包络"));
        switches.removeFromLeft (10);
        auto noiseArea = switches.removeFromLeft (66);
        placeSwitch (noiseArea, noiseLfo, NI_TXT ("噪声"));
    }

    // ---- 振荡器组 ----------------------------------------------------------
    {
        auto col = oscillatorArea.reduced (6, 2);

        for (int i = 0; i < 3; ++i)
        {
            auto row = col.removeFromTop (160);
            auto title = row.removeFromTop (20);
            printedLabels.push_back ({ title, NI_TXT ("振荡器 ") + juce::String (i + 1),
                                       true, juce::Justification::centredLeft });

            auto content = row.removeFromTop (136);
            auto coarseArea = content.removeFromLeft (98);
            printedLabels.push_back ({ coarseArea.removeFromTop (16), NI_TXT ("粗调"), false });
            rangeSwitch[i].setBounds (coarseArea.removeFromTop (100));

            content.removeFromLeft (6);
            auto fineArea = content.removeFromLeft (84);
            placeKnob (fineArea, tune[i], NI_TXT ("细调"), 90);

            content.removeFromLeft (6);
            auto waveArea = content.removeFromLeft (98);
            printedLabels.push_back ({ waveArea.removeFromTop (16), NI_TXT ("波形"), false });
            waveName[(size_t) i].setBounds (waveArea.removeFromTop (20));
            waveSwitch[i].setBounds (waveArea.removeFromTop (90));
        }

        auto controlRow = col.removeFromTop (38);
        placeSwitch (controlRow, osc3Control, NI_TXT ("振荡器 3 控制"));
    }

    // ---- 混音器 ------------------------------------------------------------
    {
        auto col = mixerArea.reduced (6, 2);

        juce::Slider* levels[] = { &oscLevel[0], &oscLevel[1], &oscLevel[2] };

        for (int i = 0; i < 3; ++i)
        {
            auto row = col.removeFromTop (103);
            printedLabels.push_back ({ row.removeFromLeft (58), NI_TXT ("振荡器 ")
                                                         + juce::String (i + 1), false });
            auto knobArea = row.removeFromLeft (72);
            placeKnob (knobArea, *levels[i], NI_TXT ("音量"), 84);
            row.removeFromLeft (6);
            oscOn[i].setBounds (row.withSizeKeepingCentre (54, 30));
        }

        auto noiseRow = col.removeFromTop (103);
        printedLabels.push_back ({ noiseRow.removeFromLeft (58), NI_TXT ("噪声"), false });
        auto noiseKnob = noiseRow.removeFromLeft (72);
        placeKnob (noiseKnob, noiseLevelSlider, NI_TXT ("音量"), 84);
        noiseRow.removeFromLeft (6);
        noiseType.setBounds (noiseRow.withSizeKeepingCentre (54, 30));

        auto distRow = col.removeFromTop (103);
        printedLabels.push_back ({ distRow.removeFromLeft (58), NI_TXT ("失真"), false });
        auto distKnob = distRow.removeFromLeft (72);
        placeKnob (distKnob, distortion, NI_TXT ("失真量"), 84);
        distRow.removeFromLeft (6);
        distortionOn.setBounds (distRow.withSizeKeepingCentre (54, 30));
    }

    // ---- 修饰器 ------------------------------------------------------------
    {
        auto col = modifiersArea.reduced (6, 2);

        printedLabels.push_back ({ col.removeFromTop (20), NI_TXT ("滤波器"), true });

        auto row1 = col.removeFromTop (100);
        auto c1 = row1.removeFromLeft (82);
        placeKnob (c1, cutoff, NI_TXT ("截止"), 90);
        row1.removeFromLeft (2);
        auto c2 = row1.removeFromLeft (82);
        placeKnob (c2, emphasis, NI_TXT ("共振"), 90);
        auto c3 = row1.removeFromLeft (82);
        placeKnob (c3, contour, NI_TXT ("包络量"), 90);

        auto row2 = col.removeFromTop (100);
        auto f1 = row2.removeFromLeft (82);
        placeKnob (f1, filterAttack, NI_TXT ("起音"), 90);
        row2.removeFromLeft (2);
        auto f2 = row2.removeFromLeft (82);
        placeKnob (f2, filterDecay, NI_TXT ("衰减"), 90);
        auto f3 = row2.removeFromLeft (82);
        placeKnob (f3, filterSustainLevel, NI_TXT ("延音"), 90);

        col.removeFromTop (20);
        auto switches = col.removeFromTop (58);
        auto k1 = switches.removeFromLeft (70);
        placeSwitch (k1, keyTrack1, NI_TXT ("跟随 1/3"));
        switches.removeFromLeft (4);
        auto k2 = switches.removeFromLeft (70);
        placeSwitch (k2, keyTrack2, NI_TXT ("跟随 2/3"));
        switches.removeFromLeft (4);
        auto rel = switches.removeFromLeft (70);
        placeSwitch (rel, releaseSwitch, NI_TXT ("释键"));

        col.removeFromTop (24);
        printedLabels.push_back ({ col.removeFromTop (20), NI_TXT ("响度包络"), true });
        auto ampRow = col.removeFromTop (100);
        auto a1 = ampRow.removeFromLeft (82);
        placeKnob (a1, ampAttack, NI_TXT ("起音"), 90);
        ampRow.removeFromLeft (2);
        auto a2 = ampRow.removeFromLeft (82);
        placeKnob (a2, ampDecay, NI_TXT ("衰减"), 90);
        auto a3 = ampRow.removeFromLeft (82);
        placeKnob (a3, ampSustainLevel, NI_TXT ("延音"), 90);
    }
}

//==============================================================================
void MinimoogEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kWoodMid, 0.0f, 0.0f,
                                             kWoodDark, 0.0f, (float) getHeight(), false));
    g.fillAll();

    for (int y = 0; y < getHeight(); y += 9)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.drawHorizontalLine (y + (y % 5), 0.0f, (float) getWidth());
        g.setColour (kWoodLight.withAlpha (0.06f));
        g.drawHorizontalLine (y + 3, 0.0f, (float) getWidth());
    }

    g.setColour (juce::Colour (0xff1b1b1e));
    g.fillRect (0, 0, getWidth(), kStripHeight);
    g.setColour (juce::Colour (0xff323238));
    g.drawHorizontalLine (kStripHeight - 1, 0.0f, (float) getWidth());

    g.setColour (kCream);
    g.setFont (theme::font (16.0f, true));
    g.drawText (NI_TXT ("迷你穆格 · MODEL D"),
                juce::Rectangle<int> (kCheekWidth + 6, 4, 200, 20),
                juce::Justification::centredLeft);
    g.setColour (kDimCream);
    g.setFont (theme::font (10.5f));
    g.drawText (NI_TXT ("三段振荡器 · 梯形滤波 · 中文面板"),
                juce::Rectangle<int> (kCheekWidth + 6, 24, 220, 14),
                juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRect (panelArea);
    g.setColour (kPanelEdge);
    g.drawRect (panelArea, 1);

    g.setColour (kDimCream.withAlpha (0.35f));

    for (const auto& section : { controllersArea, oscillatorArea, mixerArea })
        if (section.getWidth() > 0)
            g.drawVerticalLine (section.getRight() + 4, (float) panelArea.getY() + 8.0f,
                                (float) panelArea.getBottom() - 8.0f);

    if (controllersArea.getWidth() > 0)
    {
        drawSectionTitle (g, controllersArea, NI_TXT ("控制器"));
        drawSectionTitle (g, oscillatorArea, NI_TXT ("振荡器组"));
        drawSectionTitle (g, mixerArea, NI_TXT ("混音器"));
        drawSectionTitle (g, modifiersArea, NI_TXT ("修饰器"));
    }

    for (const auto& label : printedLabels)
    {
        g.setColour (label.title ? kCream : kDimCream);
        g.setFont (theme::font (label.title ? 13.5f : 11.5f, label.title));
        g.drawFittedText (label.text, label.bounds, label.justification, 2);
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));

    for (const auto& p : { juce::Point<int> (kCheekWidth / 2, panelArea.getY() + 10),
                           juce::Point<int> (getWidth() - kCheekWidth / 2, panelArea.getY() + 10),
                           juce::Point<int> (kCheekWidth / 2, panelArea.getBottom() - 10),
                           juce::Point<int> (getWidth() - kCheekWidth / 2,
                                             panelArea.getBottom() - 10) })
        g.fillEllipse (juce::Rectangle<float> ((float) p.x - 2.0f, (float) p.y - 2.0f,
                                               4.0f, 4.0f));
}

//==============================================================================
std::vector<PresetBar::Entry> MinimoogEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    int count = 0;
    const auto* bank = minimoog::bank (count);

    for (int i = 0; i < count; ++i)
        list.push_back ({ kBuiltInPresetIdBase + i, juce::String::fromUTF8 (bank[i].name), false,
                          juce::String::fromUTF8 (bank[i].category) });

    const auto& saved = presets.getMinimoogPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的迷你穆格音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true, NI_TXT ("我的") });
    }

    return list;
}

bool MinimoogEditor::userPresetExists (const juce::String& name) const
{
    return presets.findMinimoogPresetByName (name) >= 0;
}

bool MinimoogEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    auto params = track->minimoog;

    if (id >= kUserPresetIdBase)
    {
        const auto index = id - kUserPresetIdBase;

        if (index < 0 || index >= (int) presets.getMinimoogPresets().size())
            return false;

        params = presets.getMinimoogPresets()[(size_t) index];
    }
    else
    {
        minimoog::applyPreset (params, id - kBuiltInPresetIdBase);
    }

    document.beginEdit (NI_TXT ("载入音色预设"));

    if (auto* target = currentTrack())
        target->minimoog = params;

    if (auto* target = currentTrack())
        pushParams (target->minimoog);

    refresh();
    return true;
}

bool MinimoogEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->minimoog;
    params.name = name;

    const auto existing = presets.findMinimoogPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceMinimoogPreset (existing, params);
    }
    else
    {
        presets.addMinimoogPreset (params);
    }

    document.beginEdit (NI_TXT ("保存迷你穆格预设"));

    if (auto* target = currentTrack())
        target->minimoog.name = name;

    refresh();
    return true;
}

bool MinimoogEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getMinimoogPresets().size())
        return false;

    presets.renameMinimoogPreset (index, newName);

    document.beginEdit (NI_TXT ("重命名迷你穆格预设"));

    if (auto* target = currentTrack())
        target->minimoog.name = newName;

    refresh();
    return true;
}

bool MinimoogEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getMinimoogPresets().size())
        return false;

    presets.removeMinimoogPreset (index);
    refresh();
    return true;
}

//==============================================================================
std::pair<int, int> MinimoogEditor::getPresetCountsForTest() const
{
    return { minimoog::builtInCount(), (int) presets.getMinimoogPresets().size() };
}

bool MinimoogEditor::selectPresetForTest (int id)
{
    return loadPresetById (id);
}

void MinimoogEditor::setParamForTest (int which, double value)
{
    juce::Slider* target = nullptr;

    switch (which)
    {
        case 0: target = &velocityKnob; break;
        case 1: target = &glideKnob; break;
        case 2: target = &modMix; break;
        case 3: target = &mainVolume; break;
        case 4: target = &cutoff; break;
        case 5: target = &emphasis; break;
        case 6: target = &contour; break;
        case 7: target = &filterAttack; break;
        case 8: target = &filterDecay; break;
        case 9: target = &filterSustainLevel; break;
        case 10: target = &ampAttack; break;
        case 11: target = &ampDecay; break;
        case 12: target = &ampSustainLevel; break;
        case 13: target = &masterTune; break;
        case 14: target = &oscLevel[0]; break;
        case 15: target = &noiseLevelSlider; break;
        case 16: target = &distortion; break;
        default: break;
    }

    if (target != nullptr)
        target->setValue (value, juce::sendNotificationSync);
}

void MinimoogEditor::setPolyphonyForTest (int polyphony)
{
    updating = false;
    setPolyphony (polyphony);

    static const int values[] = { 0, 2, 4, 8, 16, 32, -1 };

    for (int i = 0; i < 7; ++i)
        if (values[i] == polyphony)
            polyphonyBox.setSelectedId (i + 1, juce::dontSendNotification);
}

int MinimoogEditor::getPolyphonyForTest() const
{
    const auto* track = currentTrack();
    return track != nullptr ? track->minimoog.polyphony : 0;
}

int MinimoogEditor::getActiveVoiceCountForTest() const
{
    return audio.getActiveVoiceCount();
}

bool MinimoogEditor::loadUserPresetForTest (int index)
{
    return loadPresetById (kUserPresetIdBase + index);
}

bool MinimoogEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

bool MinimoogEditor::renamePresetForTest (const juce::String& newName)
{
    return renamePresetById (presetBar.getSelectedIdForTest(), newName);
}

bool MinimoogEditor::deletePresetForTest()
{
    return deletePresetById (presetBar.getSelectedIdForTest());
}

juce::Rectangle<int> MinimoogEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return presetBar.getComboBoxBoundsForTest();
        case 1: return polyphonyBox.getBounds();
        case 2: return velocityKnob.getBounds();
        case 3: return mainVolume.getBounds();
        case 4: return rangeSwitch[0].getBounds();
        case 5: return waveSwitch[0].getBounds();
        case 6: return tune[1].getBounds();
        case 7: return oscLevel[0].getBounds();
        case 8: return noiseLevelSlider.getBounds();
        case 9: return distortion.getBounds();
        case 10: return cutoff.getBounds();
        case 11: return emphasis.getBounds();
        case 12: return contour.getBounds();
        case 13: return filterAttack.getBounds();
        case 14: return filterDecay.getBounds();
        case 15: return filterSustainLevel.getBounds();
        case 16: return ampAttack.getBounds();
        case 17: return ampDecay.getBounds();
        case 18: return ampSustainLevel.getBounds();
        case 19: return keyTrack1.getBounds();
        case 20: return releaseSwitch.getBounds();
        case 21: return glideKnob.getBounds();
        default: break;
    }

    return {};
}

} // namespace nidaw
