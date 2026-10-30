#include "MinimoogEditor.h"
#include "Theme.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr int kCheekWidth   = 26;
    constexpr int kStripHeight  = 40;
    constexpr int kKeyboardHeight = 64;

    const juce::Colour kPanel      { 0xff17171a };
    const juce::Colour kPanelRaised{ 0xff212126 };
    const juce::Colour kCream      { 0xffe8e2d2 };
    const juce::Colour kDimCream   { 0xff9a9484 };
    const juce::Colour kWoodDark   { 0xff2a1408 };
    const juce::Colour kWoodLight  { 0xff6b3a1a };
    const juce::Colour kMetal      { 0xffb8b4aa };

    const juce::StringArray& waveNames()
    {
        static const juce::StringArray names { NI_TXT ("三角"), NI_TXT ("斜三角"),
                                               NI_TXT ("锯齿"), NI_TXT ("方波"),
                                               NI_TXT ("宽脉冲"), NI_TXT ("窄脉冲") };
        return names;
    }

    const juce::StringArray& rangeNames()
    {
        static const juce::StringArray names { "LO", "32'", "16'", "8'", "4'", "2'" };
        return names;
    }

    void drawPanelSection (juce::Graphics& g, juce::Rectangle<int> bounds,
                           const juce::String& title)
    {
        g.setColour (kPanelRaised.withAlpha (0.92f));
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        g.setColour (kCream.withAlpha (0.22f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 5.0f, 1.0f);

        g.setColour (kCream);
        g.setFont (theme::font (11.0f, true));
        g.drawText (title, bounds.reduced (8, 5).removeFromTop (16),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void MinimoogRotarySwitch::setLabels (juce::StringArray newLabels)
{
    labels = std::move (newLabels);
    selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), selected);
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
    const auto labelHeight = 12;

    // Labels above the knob, like the printed positions around a real switch.
    auto top = area.removeFromTop (labelHeight);

    if (labels.size() > 0)
    {
        const auto width = juce::jmax (12, top.getWidth() / juce::jmax (1, labels.size()));

        for (int i = 0; i < labels.size(); ++i)
        {
            auto slot = top.withX (top.getX() + i * width).withWidth (width);
            g.setColour (i == selected ? kCream : kDimCream);
            g.setFont (theme::font (8.0f, i == selected));
            g.drawText (labels[i], slot, juce::Justification::centred);
        }
    }

    auto knobArea = area.reduced (6, 4);
    const auto diameter = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    auto knob = juce::Rectangle<float> ((float) knobArea.getCentreX() - diameter * 0.5f,
                                        (float) knobArea.getCentreY() - diameter * 0.5f,
                                        (float) diameter, (float) diameter);

    g.setColour (juce::Colour (0xff0c0c0e));
    g.fillEllipse (knob);
    g.setColour (kMetal.withAlpha (0.6f));
    g.drawEllipse (knob.reduced (1.0f), 1.0f);

    // The pointer steps between the printed positions.
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
                2.0f);
}

void MinimoogRotarySwitch::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStart = e.getPosition();
    dragStartSelected = selected;

    // A click in the top half picks the next position, in the bottom half the
    // previous one; dragging gives the same effect with a little more travel.
    const auto stepUp = e.getPosition().y < getHeight() / 2;
    step (stepUp ? 1 : -1);
}

void MinimoogRotarySwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = (e.getPosition().y - dragStart.y) / 8;
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

    g.setColour (kDimCream);
    g.setFont (theme::font (7.5f));
    g.drawText (topLabel, area.removeFromTop (11), juce::Justification::centred);

    auto rocker = area.removeFromTop (juce::jmax (18, area.getHeight() - 12)).reduced (2, 1);

    g.setColour (juce::Colour (0xff0b0b0d));
    g.fillRoundedRectangle (rocker.toFloat(), 2.0f);
    g.setColour (kMetal.withAlpha (0.35f));
    g.drawRoundedRectangle (rocker.toFloat().reduced (0.5f), 2.0f, 0.8f);

    // The lit half is the selected one.
    const auto lit = state ? rocker.removeFromTop (rocker.getHeight() / 2)
                           : rocker.removeFromBottom (rocker.getHeight() / 2);

    g.setColour (state ? juce::Colour (0xffd24b35) : juce::Colour (0xff4a6fa5));
    g.fillRoundedRectangle (lit.toFloat(), 2.0f);
    g.setColour (kDimCream);
    g.setFont (theme::font (7.5f));
    g.drawText (bottomLabel, getLocalBounds().removeFromBottom (11),
                juce::Justification::centred);
}

void MinimoogRocker::mouseDown (const juce::MouseEvent&)
{
    setState (! state, true);
}

bool MinimoogRocker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        setState (! state, true);
        return true;
    }

    return false;
}

//==============================================================================
void MinimoogWheel::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (2, 2);

    g.setColour (juce::Colour (0xff0a0a0c));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (kMetal.withAlpha (0.35f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 0.8f);

    const auto travel = area.getHeight() - 8;
    const auto position = bipolar ? 0.5f - value * 0.5f : 1.0f - value;
    const auto y = area.getY() + 4 + (int) std::lround (position * travel);

    g.setColour (kCream.withAlpha (0.9f));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) area.getX() + 2.0f,
                                                    (float) y - 2.0f,
                                                    (float) area.getWidth() - 4.0f,
                                                    4.0f),
                            1.5f);
}

void MinimoogWheel::mouseDown (const juce::MouseEvent& e)
{
    updateFromPoint (e.getPosition());
}

void MinimoogWheel::mouseDrag (const juce::MouseEvent& e)
{
    updateFromPoint (e.getPosition());
}

void MinimoogWheel::mouseUp (const juce::MouseEvent&)
{
    if (springLoaded)
    {
        value = 0.0f;
        repaint();

        if (onChange != nullptr)
            onChange (value);
    }
}

void MinimoogWheel::updateFromPoint (juce::Point<int> point)
{
    auto area = getLocalBounds().reduced (2, 2);
    const auto travel = juce::jmax (1, area.getHeight() - 8);
    const auto position = juce::jlimit (0.0f, 1.0f,
                                        (float) (point.y - area.getY() - 4) / (float) travel);
    const auto next = bipolar ? 1.0f - position * 2.0f : 1.0f - position;

    value = juce::jlimit (-1.0f, 1.0f, next);
    repaint();

    if (onChange != nullptr)
        onChange (value);
}

//==============================================================================
MinimoogEditor::MinimoogEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                                PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
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
        const auto index = juce::jlimit (0, 6, polyphonyBox.getSelectedId() - 1);
        setPolyphony (values[index]);
    };

    addAndMakeVisible (velocityKnob);
    configureKnob (velocityKnob, 0.0, 10.0, 0.1, 7.0);
    velocityKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    velocityKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 16);

    auto commit = [this] (const char* name)
    {
        if (updating)
            return;

        beginParamEdit (NI_TXT (name));
        pushCurrentParams();
    };

    addAndMakeVisible (pitchWheel);
    pitchWheel.setSpringLoaded (true);
    pitchWheel.onChange = [commit] (float)
    {
        commit ("弯音");
    };

    addAndMakeVisible (modWheel);
    modWheel.setBipolar (false);
    modWheel.setValue (0.0f);
    modWheel.onChange = [commit] (float)
    {
        commit ("调制轮");
    };

    auto setupRocker = [this, commit] (MinimoogRocker& rocker, const juce::String& top,
                                       const juce::String& bottom, bool initial)
    {
        addAndMakeVisible (rocker);
        rocker.setLabels (top, bottom);
        rocker.setState (initial, false);
        rocker.onChange = [commit] (bool) { commit ("迷你穆格开关"); };
    };

    for (int i = 0; i < 3; ++i)
    {
        auto& range = rangeSwitch[i];
        addAndMakeVisible (range);
        range.setLabels (rangeNames());
        range.setSelected (3, false);
        range.onChange = [commit] (int) { commit ("振荡器档位"); };

        auto& wave = waveSwitch[i];
        addAndMakeVisible (wave);
        wave.setLabels ({ "1", "2", "3", "4", "5", "6" });
        wave.setSelected (2, false);
        wave.onChange = [commit] (int) { commit ("振荡器波形"); };

        auto& name = waveName[(size_t) i];
        addAndMakeVisible (name);
        name.setFont (theme::font (8.5f));
        name.setColour (juce::Label::textColourId, kCream);
        name.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (tune[i]);
        configureKnob (tune[i], -7.0, 7.0, 0.1, 0.0);
        tune[i].setEnabled (i != 0);

        addAndMakeVisible (oscLevel[i]);
        configureKnob (oscLevel[i], 0.0, 10.0, 0.1, i == 0 ? 9.0 : 5.0);

        setupRocker (oscOn[i], "ON", "OFF", true);
    }

    setupRocker (osc3Control, NI_TXT ("跟随"), NI_TXT ("固定"), true);

    addAndMakeVisible (masterTune);
    configureKnob (masterTune, -50.0, 50.0, 1.0, 0.0);
    addAndMakeVisible (glideKnob);
    configureKnob (glideKnob, 0.0, 10.0, 0.1, 1.0);
    addAndMakeVisible (modMix);
    configureKnob (modMix, 0.0, 10.0, 0.1, 0.0);

    setupRocker (glideSwitch, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (oscMod, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (filterMod, NI_TXT ("开"), NI_TXT ("关"), false);

    addAndMakeVisible (noiseLevelSlider);
    configureKnob (noiseLevelSlider, 0.0, 10.0, 0.1, 0.0);
    setupRocker (noiseType, NI_TXT ("白"), NI_TXT ("粉"), false);

    // The mixer is a set of vertical faders, not knobs.
    for (auto* fader : { &oscLevel[0], &oscLevel[1], &oscLevel[2], &noiseLevelSlider })
    {
        fader->setSliderStyle (juce::Slider::LinearVertical);
        fader->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 14);
        fader->setRotaryParameters (0.0f, 0.0f, true);
    }

    addAndMakeVisible (cutoff);
    configureKnob (cutoff, -5.0, 5.0, 0.05, 3.0);
    addAndMakeVisible (emphasis);
    configureKnob (emphasis, 0.0, 10.0, 0.1, 0.0);
    addAndMakeVisible (contour);
    configureKnob (contour, 0.0, 10.0, 0.1, 5.0);

    setupRocker (keyTrack1, NI_TXT ("1/3"), NI_TXT ("关"), false);
    setupRocker (keyTrack2, NI_TXT ("2/3"), NI_TXT ("关"), false);

    addAndMakeVisible (filterAttack);
    configureKnob (filterAttack, 0.0, 10.0, 0.1, 4.0);
    addAndMakeVisible (filterDecay);
    configureKnob (filterDecay, 0.0, 10.0, 0.1, 6.0);
    setupRocker (filterDecayShort, NI_TXT ("长"), NI_TXT ("短"), true);
    setupRocker (filterSustain, NI_TXT ("开"), NI_TXT ("关"), true);

    addAndMakeVisible (ampAttack);
    configureKnob (ampAttack, 0.0, 10.0, 0.1, 2.0);
    addAndMakeVisible (ampDecay);
    configureKnob (ampDecay, 0.0, 10.0, 0.1, 5.0);
    setupRocker (ampDecayShort, NI_TXT ("长"), NI_TXT ("短"), true);
    setupRocker (ampSustain, NI_TXT ("开"), NI_TXT ("关"), true);
    setupRocker (releaseSwitch, NI_TXT ("开"), NI_TXT ("关"), true);

    addAndMakeVisible (volume);
    configureKnob (volume, 0.0, 10.0, 0.1, 7.0);
    setupRocker (mainOut, NI_TXT ("开"), NI_TXT ("关"), true);

    // The value changes go through pushCurrentParams, which reads every control.
    auto wireSlider = [this, commit] (juce::Slider& slider)
    {
        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整迷你穆格")); };
        slider.onValueChange = [commit] { commit ("调整迷你穆格"); };
    };

    wireSlider (velocityKnob);
    wireSlider (masterTune);
    wireSlider (glideKnob);
    wireSlider (modMix);
    wireSlider (noiseLevelSlider);
    wireSlider (cutoff);
    wireSlider (emphasis);
    wireSlider (contour);
    wireSlider (filterAttack);
    wireSlider (filterDecay);
    wireSlider (ampAttack);
    wireSlider (ampDecay);
    wireSlider (volume);

    for (int i = 0; i < 3; ++i)
    {
        wireSlider (tune[i]);
        wireSlider (oscLevel[i]);
    }

    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe6e6ee));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::outline);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accent.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dimText);
    keyboard.setAvailableRange (36, 84);
    keyboard.setKeyWidth (26.0f);
    keyboard.setLowestVisibleKey (48);
    keyboardState.addListener (this);

    document.addChangeListener (this);

    refresh();
}

MinimoogEditor::~MinimoogEditor()
{
    keyboardState.removeListener (this);
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setRange (minimum, maximum, interval);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setMouseDragSensitivity (180);
    slider.setScrollWheelEnabled (true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, kCream);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff0c0c0e));
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

    osc3Control.setState (p.osc3Control, false);
    masterTune.setValue (p.masterTuneCents, juce::dontSendNotification);
    glideKnob.setValue (secondsToKnob (p.glideSeconds), juce::dontSendNotification);
    glideSwitch.setState (p.glideOn, false);
    modMix.setValue ((double) p.modMix * 10.0, juce::dontSendNotification);
    modWheel.setValue (p.modDepth);
    oscMod.setState (p.oscModOn, false);
    filterMod.setState (p.filterModOn, false);

    noiseLevelSlider.setValue (10.0 * std::sqrt ((double) p.noiseLevel),
                               juce::dontSendNotification);
    noiseType.setState (p.noisePink, false);

    cutoff.setValue (cutoffToKnob (p.filterCutoffHz), juce::dontSendNotification);
    emphasis.setValue ((double) p.filterEmphasis * 10.0, juce::dontSendNotification);
    contour.setValue ((double) p.filterContourOctaves / 8.0 * 10.0, juce::dontSendNotification);
    keyTrack1.setState (p.filterKeyTrack1, false);
    keyTrack2.setState (p.filterKeyTrack2, false);

    filterAttack.setValue (secondsToKnob (p.filterAttack), juce::dontSendNotification);
    filterDecay.setValue (secondsToKnob (p.filterDecay), juce::dontSendNotification);
    filterDecayShort.setState (! p.filterDecayShort, false);
    filterSustain.setState (p.filterSustainOn, false);

    ampAttack.setValue (secondsToKnob (p.ampAttack), juce::dontSendNotification);
    ampDecay.setValue (secondsToKnob (p.ampDecay), juce::dontSendNotification);
    ampDecayShort.setState (! p.ampDecayShort, false);
    ampSustain.setState (p.ampSustainOn, false);
    releaseSwitch.setState (p.releaseOn, false);

    volume.setValue ((double) p.volume * 10.0, juce::dontSendNotification);
    velocityKnob.setValue ((double) p.velocitySensitivity * 10.0, juce::dontSendNotification);
    mainOut.setState (true, false);

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

    p.osc3Control = osc3Control.getState();
    p.masterTuneCents = (float) masterTune.getValue();
    p.glideSeconds = knobToSeconds ((float) glideKnob.getValue());
    p.glideOn = glideSwitch.getState();
    p.modMix = (float) modMix.getValue() / 10.0f;
    p.modDepth = juce::jlimit (0.0f, 1.0f, modWheel.getValue());
    p.oscModOn = oscMod.getState();
    p.filterModOn = filterMod.getState();

    p.noiseLevel = (float) std::pow (noiseLevelSlider.getValue() / 10.0, 2.0);
    p.noisePink = noiseType.getState();

    p.filterCutoffHz = knobToCutoff ((float) cutoff.getValue());
    p.filterEmphasis = (float) emphasis.getValue() / 10.0f;
    p.filterContourOctaves = (float) contour.getValue() / 10.0f * 8.0f;
    p.filterKeyTrack1 = keyTrack1.getState();
    p.filterKeyTrack2 = keyTrack2.getState();

    p.filterAttack = knobToSeconds ((float) filterAttack.getValue());
    p.filterDecay = knobToSeconds ((float) filterDecay.getValue());
    p.filterDecayShort = ! filterDecayShort.getState();
    p.filterSustainOn = filterSustain.getState();

    p.ampAttack = knobToSeconds ((float) ampAttack.getValue());
    p.ampDecay = knobToSeconds ((float) ampDecay.getValue());
    p.ampDecayShort = ! ampDecayShort.getState();
    p.ampSustainOn = ampSustain.getState();
    p.releaseOn = releaseSwitch.getState();

    p.volume = (float) volume.getValue() / 10.0f;
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
    auto strip = area.removeFromTop (kStripHeight);

    // HM strip: title on the left, the two extensions, then the preset row.
    strip.removeFromRight (6);
    presetBar.setBounds (strip.removeFromRight (590).reduced (0, 7));
    strip.removeFromRight (8);
    velocityKnob.setBounds (strip.removeFromRight (84).reduced (2, 6));
    velocityKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 16);
    strip.removeFromRight (8);
    polyphonyBox.setBounds (strip.removeFromRight (116).reduced (2, 9));

    printedLabels.push_back ({ polyphonyBox.getBounds().translated (-58, 0), NI_TXT ("复音数"),
                               false, juce::Justification::centredRight });
    printedLabels.push_back ({ velocityKnob.getBounds().translated (-28, 0), NI_TXT ("力度"),
                               false, juce::Justification::centredRight });

    auto keyboardArea = area.removeFromBottom (kKeyboardHeight);
    keyboard.setBounds (keyboardArea.reduced (kCheekWidth + 10, 6));

    panelArea = area.reduced (kCheekWidth + 6, 4);

    auto inner = panelArea.reduced (6, 6);
    controllersArea = inner.removeFromLeft (160);
    inner.removeFromLeft (8);
    oscillatorArea = inner.removeFromLeft (360);
    inner.removeFromLeft (8);
    mixerArea = inner.removeFromLeft (150);
    inner.removeFromLeft (8);
    outputArea = inner.removeFromRight (84);
    inner.removeFromRight (8);
    modifiersArea = inner;

    // ---- controllers -------------------------------------------------------
    {
        auto col = controllersArea;
        col.removeFromTop (22);     // the section title is painted by paint()

        auto wheels = col.removeFromTop (158).reduced (6, 8);
        auto pitchArea = wheels.removeFromLeft (34);
        pitchWheel.setBounds (pitchArea.removeFromTop (112));
        printedLabels.push_back ({ pitchArea.removeFromTop (14), "PITCH", false });
        wheels.removeFromLeft (4);
        auto modArea = wheels.removeFromLeft (34);
        modWheel.setBounds (modArea.removeFromTop (112));
        printedLabels.push_back ({ modArea.removeFromTop (14), "MOD", false });

        auto knob = [] (juce::Rectangle<int>& column, juce::Slider& slider,
                        std::vector<PrintedLabel>& labels, const juce::String& text,
                        int height)
        {
            labels.push_back ({ column.removeFromTop (12), text, false });
            slider.setBounds (column.removeFromTop (height));
        };

        auto right = col.reduced (6, 0);
        knob (right, masterTune, printedLabels, "TUNE", 56);
        knob (right, glideKnob, printedLabels, "GLIDE", 56);

        auto glideRow = right.removeFromTop (38);
        glideSwitch.setBounds (glideRow.removeFromLeft (42).reduced (2, 2));
        printedLabels.push_back ({ glideRow.reduced (4, 0), NI_TXT ("滑音开关"), false,
                                   juce::Justification::centredLeft });
        right.removeFromTop (2);

        knob (right, modMix, printedLabels, "MOD.MIX", 56);

        auto modRow = right.removeFromTop (42);
        oscMod.setBounds (modRow.removeFromLeft (42).reduced (2, 2));
        modRow.removeFromLeft (4);
        filterMod.setBounds (modRow.removeFromLeft (42).reduced (2, 2));
        printedLabels.push_back ({ modRow.reduced (2, 0), NI_TXT ("→音高 →滤波"), false,
                                   juce::Justification::centredLeft });
    }

    // ---- oscillator bank ---------------------------------------------------
    {
        auto columns = oscillatorArea.reduced (6, 2);
        columns.removeFromTop (20);
        const auto columnWidth = (columns.getWidth() - 12) / 3;

        for (int i = 0; i < 3; ++i)
        {
            auto c = columns.removeFromLeft (columnWidth);
            columns.removeFromLeft (6);

            printedLabels.push_back ({ c.removeFromTop (18), "OSC " + juce::String (i + 1),
                                       true });

            auto rangeLabel = c.removeFromTop (10);
            printedLabels.push_back ({ rangeLabel, "RANGE", false });
            rangeSwitch[i].setBounds (c.removeFromTop (56));

            c.removeFromTop (2);
            auto waveLabel = c.removeFromTop (10);
            printedLabels.push_back ({ waveLabel, "WAVEFORM", false });
            waveSwitch[i].setBounds (c.removeFromTop (56));
            waveName[(size_t) i].setBounds (c.removeFromTop (13));

            auto freqLabel = c.removeFromTop (12);
            printedLabels.push_back ({ freqLabel, NI_TXT ("频率"), false });
            tune[i].setBounds (c.removeFromTop (58));

            if (i == 2)
            {
                c.removeFromTop (4);
                osc3Control.setBounds (c.removeFromTop (40).removeFromLeft (44).reduced (2, 1));
                printedLabels.push_back ({ c.removeFromTop (12), NI_TXT ("OSC.3 跟随键盘"),
                                           false, juce::Justification::centredLeft });
            }
        }
    }

    // ---- mixer -------------------------------------------------------------
    {
        auto col = mixerArea;
        col.removeFromTop (22);

        col.reduce (4, 2);
        auto faders = col.removeFromTop (216);
        const auto faderWidth = (faders.getWidth() - 9) / 4;
        juce::Slider* levels[] = { &oscLevel[0], &oscLevel[1], &oscLevel[2], &noiseLevelSlider };

        for (int i = 0; i < 4; ++i)
        {
            auto f = faders.removeFromLeft (faderWidth);
            faders.removeFromLeft (3);
            printedLabels.push_back ({ f.removeFromTop (14), i < 3 ? juce::String (i + 1) : "N",
                                       false });
            levels[i]->setBounds (f.removeFromTop (150));

            if (i < 3)
                oscOn[i].setBounds (f.removeFromTop (34).reduced (3, 2));
        }

        col.removeFromTop (6);
        noiseType.setBounds (col.removeFromTop (40).reduced (48, 2));
        printedLabels.push_back ({ col.removeFromTop (14), "NOISE", false });
    }

    // ---- modifiers ---------------------------------------------------------
    {
        auto columns = modifiersArea.reduced (5, 2);
        columns.removeFromTop (20);
        const auto partWidth = (columns.getWidth() - 10) / 3;
        auto filterCol = columns.removeFromLeft (partWidth);
        columns.removeFromLeft (5);
        auto filterEnvCol = columns.removeFromLeft (partWidth);
        columns.removeFromLeft (5);
        auto ampEnvCol = columns;

        printedLabels.push_back ({ filterCol.removeFromTop (15), "FILTER", true });

        auto knob = [] (juce::Rectangle<int>& column, juce::Slider& slider,
                        std::vector<PrintedLabel>& labels, const juce::String& text,
                        int height)
        {
            labels.push_back ({ column.removeFromTop (11), text, false });
            slider.setBounds (column.removeFromTop (height));
        };

        knob (filterCol, cutoff, printedLabels, "CUTOFF", 50);
        knob (filterCol, emphasis, printedLabels, "EMPHASIS", 50);
        knob (filterCol, contour, printedLabels, "CONTOUR", 50);
        filterCol.removeFromTop (2);
        auto keyRow = filterCol.removeFromTop (40);
        keyTrack1.setBounds (keyRow.removeFromLeft (42).reduced (2, 2));
        keyRow.removeFromLeft (4);
        keyTrack2.setBounds (keyRow.removeFromLeft (42).reduced (2, 2));
        printedLabels.push_back ({ filterCol.removeFromTop (12), NI_TXT ("键盘跟随"), false });

        printedLabels.push_back ({ filterEnvCol.removeFromTop (15),
                                   NI_TXT ("滤波包络"), true });
        knob (filterEnvCol, filterAttack, printedLabels, "ATTACK", 50);
        knob (filterEnvCol, filterDecay, printedLabels, "DECAY", 50);
        auto fdecayRow = filterEnvCol.removeFromTop (40);
        filterDecayShort.setBounds (fdecayRow.removeFromLeft (42).reduced (2, 2));
        fdecayRow.removeFromLeft (4);
        filterSustain.setBounds (fdecayRow.removeFromLeft (42).reduced (2, 2));
        printedLabels.push_back ({ filterEnvCol.removeFromTop (12), NI_TXT ("衰减   维持"), false });

        printedLabels.push_back ({ ampEnvCol.removeFromTop (15),
                                   NI_TXT ("响度包络"), true });
        knob (ampEnvCol, ampAttack, printedLabels, "ATTACK", 50);
        knob (ampEnvCol, ampDecay, printedLabels, "DECAY", 50);
        auto ampRow = ampEnvCol.removeFromTop (40);
        ampDecayShort.setBounds (ampRow.removeFromLeft (29).reduced (1, 2));
        ampSustain.setBounds (ampRow.removeFromLeft (29).reduced (1, 2));
        releaseSwitch.setBounds (ampRow.removeFromLeft (29).reduced (1, 2));
        printedLabels.push_back ({ ampEnvCol.removeFromTop (12), NI_TXT ("衰减 维持 释放"),
                                   false });
    }

    // ---- output ------------------------------------------------------------
    {
        auto content = outputArea.reduced (6, 22);
        printedLabels.push_back ({ content.removeFromTop (12), "VOLUME", false });
        volume.setBounds (content.removeFromTop (60));
        content.removeFromTop (8);
        mainOut.setBounds (content.removeFromTop (46).reduced (8, 2));

        content.removeFromTop (14);
        printedLabels.push_back ({ content.removeFromTop (14), "A-440", false });
        printedLabels.push_back ({ content.removeFromTop (14), "PHONES", false });
    }
}
//==============================================================================
void MinimoogEditor::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);

    // Wood cheeks.
    for (int side = 0; side < 2; ++side)
    {
        const auto x = side == 0 ? 0 : getWidth() - kCheekWidth;
        auto cheek = juce::Rectangle<int> (x, 0, kCheekWidth, getHeight());

        g.setGradientFill (juce::ColourGradient (kWoodLight, (float) cheek.getX(), 0.0f,
                                                 kWoodDark, (float) cheek.getRight(), 0.0f,
                                                 false));
        g.fillRect (cheek);

        g.setColour (juce::Colours::black.withAlpha (0.18f));

        for (int y = 4; y < getHeight(); y += 7)
            g.drawHorizontalLine (y, (float) cheek.getX() + 2.0f,
                                  (float) cheek.getRight() - 2.0f);
    }

    // Panel and printed sections.
    auto panel = getLocalBounds().withTrimmedLeft (kCheekWidth)
                                 .withTrimmedRight (kCheekWidth);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222227), 0.0f,
                                             (float) panel.getY(),
                                             juce::Colour (0xff101013), 0.0f,
                                             (float) panel.getBottom(), false));
    g.fillRect (panel);

    g.setColour (juce::Colour (0xff2c2c33));
    g.fillRect (0, 0, getWidth(), kStripHeight);
    g.setColour (kCream.withAlpha (0.25f));
    g.drawHorizontalLine (kStripHeight - 1, 0.0f, (float) getWidth());

    g.setColour (kCream);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("MINI · MODEL D", juce::Rectangle<int> (kCheekWidth + 12, 3, 260, 16),
                juce::Justification::centredLeft);
    g.setColour (kDimCream);
    g.setFont (theme::font (8.5f));
    g.drawText (NI_TXT ("三段振荡器 · 梯形滤波 · 单音/复音"), 
                juce::Rectangle<int> (kCheekWidth + 12, 20, 300, 13),
                juce::Justification::centredLeft);

    if (controllersArea.getWidth() > 0)
    {
        drawPanelSection (g, controllersArea, "CONTROLLERS");
        drawPanelSection (g, oscillatorArea, "OSCILLATOR BANK");
        drawPanelSection (g, mixerArea, "MIXER");
        drawPanelSection (g, modifiersArea, "MODIFIERS");
        drawPanelSection (g, outputArea, "OUTPUT");
    }

    for (const auto& label : printedLabels)
    {
        g.setColour (label.title ? kCream : kDimCream);
        g.setFont (theme::font (label.title ? 10.5f : 8.5f, label.title));
        g.drawFittedText (label.text, label.bounds, label.justification, 2);
    }

    // Overload lamp in the mixer: the sum of the sources is what the real
    // overload indicator watches.
    if (const auto* track = currentTrack())
    {
        const auto& p = track->minimoog;
        const auto sum = p.oscs[0].level + p.oscs[1].level + p.oscs[2].level + p.noiseLevel;
        const auto lamp = juce::Rectangle<int> (mixerArea.getRight() - 26,
                                                mixerArea.getBottom() - 26, 12, 12);
        g.setColour (sum > 1.0f ? juce::Colour (0xffe0402c) : juce::Colour (0xff2c5a33));
        g.fillEllipse (lamp.toFloat());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (lamp.toFloat(), 1.0f);
    }

    // Screws.
    g.setColour (kMetal.withAlpha (0.5f));

    for (const auto& point : { juce::Point<int> (kCheekWidth + 8, 8),
                               juce::Point<int> (getWidth() - kCheekWidth - 8, 8),
                               juce::Point<int> (kCheekWidth + 8, getHeight() - 8),
                               juce::Point<int> (getWidth() - kCheekWidth - 8, getHeight() - 8) })
    {
        g.fillEllipse (juce::Rectangle<float> ((float) point.x - 2.5f, (float) point.y - 2.5f,
                                               5.0f, 5.0f));
    }
}

//==============================================================================
void MinimoogEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber,
                                   float velocity)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return;

    previewMidiNote = midiNoteNumber;
    const auto frequency = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    audio.triggerPreviewMinimoogNote (track->minimoog, frequency, velocity * 127.0f);
}

void MinimoogEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        previewMidiNote = -1;
        audio.releasePreviewNote();
    }
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
        case 3: target = &cutoff; break;
        case 4: target = &emphasis; break;
        case 5: target = &contour; break;
        case 6: target = &filterAttack; break;
        case 7: target = &filterDecay; break;
        case 8: target = &ampAttack; break;
        case 9: target = &ampDecay; break;
        case 10: target = &volume; break;
        case 11: target = &oscLevel[0]; break;
        case 12: target = &noiseLevelSlider; break;
        case 13: target = &masterTune; break;
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
        case 3: return rangeSwitch[0].getBounds();
        case 4: return waveSwitch[0].getBounds();
        case 5: return tune[1].getBounds();
        case 6: return oscLevel[0].getBounds();
        case 7: return noiseLevelSlider.getBounds();
        case 8: return cutoff.getBounds();
        case 9: return emphasis.getBounds();
        case 10: return contour.getBounds();
        case 11: return filterAttack.getBounds();
        case 12: return filterDecay.getBounds();
        case 13: return ampAttack.getBounds();
        case 14: return ampDecay.getBounds();
        case 15: return volume.getBounds();
        case 16: return modWheel.getBounds();
        case 17: return keyboard.getBounds();
        case 18: return keyTrack1.getBounds();
        case 19: return mainOut.getBounds();
        case 20: return glideKnob.getBounds();
        default: break;
    }

    return {};
}

} // namespace nidaw
