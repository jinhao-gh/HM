#include "MinimoogEditor.h"
#include "Theme.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr int kCheekWidth    = 14;
    constexpr int kStripHeight   = 38;
    constexpr int kKeyboardHeight = 160;

    const juce::Colour kPanel      { 0xff121213 };
    const juce::Colour kPanelEdge  { 0xff3a3a3c };
    const juce::Colour kCream      { 0xffe9e4d4 };
    const juce::Colour kDimCream   { 0xff9c9686 };
    const juce::Colour kWoodDark   { 0xff1d0d05 };
    const juce::Colour kWoodMid    { 0xff4a2412 };
    const juce::Colour kWoodLight  { 0xff7a3f1e };
    const juce::Colour kOrange     { 0xffd2572f };
    const juce::Colour kBlue       { 0xff4fa8c9 };
    const juce::Colour kMetal      { 0xffc8c4ba };

    const juce::StringArray& waveNames()
    {
        static const juce::StringArray names { NI_TXT ("三角"), NI_TXT ("斜三角"),
                                               NI_TXT ("锯齿"), NI_TXT ("方波"),
                                               NI_TXT ("宽脉冲"), NI_TXT ("窄脉冲") };
        return names;
    }

    const juce::StringArray& waveLabels()
    {
        static const juce::StringArray names { NI_TXT ("△"), NI_TXT ("◺"),
                                               NI_TXT ("锯"), NI_TXT ("□"),
                                               NI_TXT ("宽"), NI_TXT ("窄") };
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
        g.setFont (theme::font (12.5f, true));
        g.drawText (title, panel.removeFromBottom (22), juce::Justification::centred);
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
    auto labelRow = area.removeFromTop (12);

    if (! labels.isEmpty())
    {
        const auto width = juce::jmax (8, labelRow.getWidth() / labels.size());

        for (int i = 0; i < labels.size(); ++i)
        {
            auto slot = labelRow.withX (labelRow.getX() + i * width).withWidth (width);
            g.setColour (i == selected ? kCream : kDimCream);
            g.setFont (theme::font (7.5f, i == selected));
            g.drawText (labels[i], slot, juce::Justification::centred);
        }
    }

    auto knobArea = area.reduced (6, 3);
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
                2.2f);
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
    auto caption = area.removeFromBottom (10);
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
    g.setFont (theme::font (7.5f, state));
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

    g.setColour (juce::Colour (0xffcfc9b8));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) area.getX() + 3.0f,
                                                    (float) y - 3.0f,
                                                    (float) area.getWidth() - 6.0f,
                                                    6.0f),
                            2.0f);
}

void MinimoogWheel::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    updateFromPoint (e.getPosition());
}

void MinimoogWheel::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    updateFromPoint (e.getPosition());
}

void MinimoogWheel::mouseUp (const juce::MouseEvent&)
{
    if (springLoaded && isEnabled())
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
        setPolyphony (values[juce::jlimit (0, 6, polyphonyBox.getSelectedId() - 1)]);
    };

    addAndMakeVisible (velocityKnob);
    configureKnob (velocityKnob, 0.0, 10.0, 0.1, 7.0);

    auto commit = [this] (const char* name)
    {
        if (updating)
            return;

        beginParamEdit (NI_TXT (name));
        pushCurrentParams();
    };

    addAndMakeVisible (pitchWheel);
    pitchWheel.setSpringLoaded (true);
    pitchWheel.onChange = [commit] (float) { commit ("弯音"); };

    addAndMakeVisible (modWheel);
    modWheel.setBipolar (false);
    modWheel.onChange = [commit] (float) { commit ("调制轮"); };

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
        range.onChange = [commit] (int) { commit ("振荡器档位"); };

        auto& wave = waveSwitch[i];
        addAndMakeVisible (wave);
        wave.setLabels (waveLabels());
        wave.setSelected (2, false);
        wave.onChange = [commit] (int) { commit ("振荡器波形"); };

        auto& name = waveName[(size_t) i];
        addAndMakeVisible (name);
        name.setFont (theme::font (8.0f));
        name.setColour (juce::Label::textColourId, kCream);
        name.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (tune[i]);
        configureKnob (tune[i], -7.0, 7.0, 0.1, 0.0);
        tune[i].setEnabled (i != 0);

        addAndMakeVisible (oscLevel[i]);
        configureKnob (oscLevel[i], 0.0, 10.0, 0.1, i == 0 ? 9.0 : 5.0);

        setupRocker (oscOn[i], NI_TXT ("开"), NI_TXT ("关"), true);
    }

    setupRocker (osc3Control, NI_TXT ("开"), NI_TXT ("关"), true);
    setupRocker (glideSwitch, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (oscMod, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (filterMod, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (noiseType, NI_TXT ("粉"), NI_TXT ("白"), false);
    setupRocker (keyTrack1, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (keyTrack2, NI_TXT ("开"), NI_TXT ("关"), false);
    setupRocker (releaseSwitch, NI_TXT ("开"), NI_TXT ("关"), true);
    setupRocker (mainOut, NI_TXT ("开"), NI_TXT ("关"), true);

    setupDisabledRocker (osc3FilterEg, NI_TXT ("开"), NI_TXT ("关"));
    setupDisabledRocker (noiseLfo, NI_TXT ("开"), NI_TXT ("关"));
    setupDisabledRocker (externalInputOn, NI_TXT ("开"), NI_TXT ("关"));
    setupDisabledRocker (a440, NI_TXT ("开"), NI_TXT ("关"));
    setupDisabledRocker (power, NI_TXT ("开"), NI_TXT ("关"));

    addAndMakeVisible (masterTune);
    configureKnob (masterTune, -50.0, 50.0, 1.0, 0.0);
    addAndMakeVisible (glideKnob);
    configureKnob (glideKnob, 0.0, 10.0, 0.1, 1.0);
    addAndMakeVisible (modMix);
    configureKnob (modMix, 0.0, 10.0, 0.1, 0.0);

    addAndMakeVisible (noiseLevelSlider);
    configureKnob (noiseLevelSlider, 0.0, 10.0, 0.1, 0.0);

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

    addAndMakeVisible (volume);
    configureKnob (volume, 0.0, 10.0, 0.1, 7.0);

    addAndMakeVisible (externalInputVolume);
    configureKnob (externalInputVolume, 0.0, 10.0, 0.1, 0.0);
    externalInputVolume.setEnabled (false);
    externalInputVolume.setAlpha (0.4f);

    addAndMakeVisible (lfoRate);
    configureKnob (lfoRate, 0.0, 10.0, 0.1, 4.0);
    lfoRate.setEnabled (false);
    lfoRate.setAlpha (0.4f);

    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9e6da));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3830));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, kOrange);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        kOrange.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, kDimCream);
    keyboard.setAvailableRange (36, 84);
    keyboard.setKeyWidth (26.0f);
    keyboard.setLowestVisibleKey (48);
    keyboardState.addListener (this);

    // Every knob writes its value into the patch when it moves.
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
    wireSlider (filterSustainLevel);
    wireSlider (ampAttack);
    wireSlider (ampDecay);
    wireSlider (ampSustainLevel);
    wireSlider (volume);

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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 13);
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
    filterSustainLevel.setValue ((double) p.filterSustainLevel * 10.0,
                                 juce::dontSendNotification);

    ampAttack.setValue (secondsToKnob (p.ampAttack), juce::dontSendNotification);
    ampDecay.setValue (secondsToKnob (p.ampDecay), juce::dontSendNotification);
    ampSustainLevel.setValue ((double) p.ampSustainLevel * 10.0, juce::dontSendNotification);
    releaseSwitch.setState (p.releaseOn, false);

    volume.setValue ((double) p.volume * 10.0, juce::dontSendNotification);
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
    p.filterSustainLevel = juce::jlimit (0.0f, 1.0f,
                                         (float) filterSustainLevel.getValue() / 10.0f);

    p.ampAttack = knobToSeconds ((float) ampAttack.getValue());
    p.ampDecay = knobToSeconds ((float) ampDecay.getValue());
    p.ampSustainLevel = juce::jlimit (0.0f, 1.0f,
                                      (float) ampSustainLevel.getValue() / 10.0f);
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

    strip.removeFromRight (6);
    presetBar.setBounds (strip.removeFromRight (588).reduced (0, 6));
    strip.removeFromRight (8);
    velocityKnob.setBounds (strip.removeFromRight (84).reduced (2, 4));
    velocityKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 15);
    strip.removeFromRight (6);
    polyphonyBox.setBounds (strip.removeFromRight (118).reduced (2, 8));

    printedLabels.push_back ({ polyphonyBox.getBounds().translated (-58, 0), NI_TXT ("复音数"),
                               false, juce::Justification::centredRight });
    printedLabels.push_back ({ velocityKnob.getBounds().translated (-30, 0), NI_TXT ("力度"),
                               false, juce::Justification::centredRight });

    keyboardArea = area.removeFromBottom (kKeyboardHeight);
    panelArea = area.reduced (kCheekWidth, 8);

    auto inner = panelArea.reduced (6, 6);
    controllersArea = inner.removeFromLeft (143);
    inner.removeFromLeft (8);
    oscillatorArea = inner.removeFromLeft (325);
    inner.removeFromLeft (8);
    mixerArea = inner.removeFromLeft (232);
    inner.removeFromLeft (8);
    outputArea = inner.removeFromRight (78);
    inner.removeFromRight (8);
    modifiersArea = inner;

    auto placeKnob = [this] (juce::Rectangle<int> bounds, juce::Slider& slider,
                             const juce::String& caption, int knobHeight = 0)
    {
        auto label = bounds.removeFromTop (11);
        printedLabels.push_back ({ label, caption, false });
        slider.setBounds (bounds.removeFromTop (knobHeight > 0 ? knobHeight : bounds.getHeight()));
    };

    // ---- 控制器 ------------------------------------------------------------
    {
        auto col = controllersArea.reduced (4, 2);

        auto top = col.removeFromTop (66);
        auto tuneArea = top.removeFromLeft (66);
        placeKnob (tuneArea, masterTune, NI_TXT ("调音"));
        top.removeFromLeft (4);
        auto modSwitch = top.removeFromTop (30).removeFromRight (52);
        oscMod.setBounds (modSwitch);
        printedLabels.push_back ({ top.removeFromTop (12), NI_TXT ("振荡调制"), false });

        col.removeFromTop (4);
        auto knobs = col.removeFromTop (70);
        auto glideArea = knobs.removeFromLeft (66);
        placeKnob (glideArea, glideKnob, NI_TXT ("滑音"));
        knobs.removeFromLeft (2);
        auto mixArea = knobs.removeFromLeft (66);
        placeKnob (mixArea, modMix, NI_TXT ("调制混合"));

        col.removeFromTop (6);
        auto smallSwitches = col.removeFromTop (40);
        auto egArea = smallSwitches.removeFromLeft (60);
        osc3FilterEg.setBounds (egArea.removeFromTop (26));
        printedLabels.push_back ({ egArea.removeFromTop (18), NI_TXT ("OSC3"), false });
        smallSwitches.removeFromLeft (6);
        auto noiseArea = smallSwitches.removeFromLeft (60);
        noiseLfo.setBounds (noiseArea.removeFromTop (26));
        printedLabels.push_back ({ noiseArea.removeFromTop (18), NI_TXT ("噪声"), false });
    }

    // ---- 振荡器组 ----------------------------------------------------------
    {
        auto col = oscillatorArea.reduced (4, 2);

        for (int i = 0; i < 3; ++i)
        {
            auto row = col.removeFromTop (150);
            auto title = row.removeFromTop (16);
            printedLabels.push_back ({ title, NI_TXT ("振荡器 ") + juce::String (i + 1),
                                       true, juce::Justification::centredLeft });
            auto content = row.removeFromTop (104);

            auto rangeArea = content.removeFromLeft (66);
            printedLabels.push_back ({ rangeArea.removeFromTop (10), "RANGE", false });
            rangeSwitch[i].setBounds (rangeArea.removeFromTop (58));

            content.removeFromLeft (2);
            auto freqArea = content.removeFromLeft (60);
            placeKnob (freqArea, tune[i], NI_TXT ("频率"), 78);

            content.removeFromLeft (2);
            auto waveArea = content.removeFromLeft (66);
            printedLabels.push_back ({ waveArea.removeFromTop (10), "WAVEFORM", false });
            waveSwitch[i].setBounds (waveArea.removeFromTop (58));
            waveName[(size_t) i].setBounds (waveArea.removeFromTop (12));

            if (i == 2)
            {
                content.removeFromLeft (2);
                auto controlArea = content.removeFromLeft (58);
                osc3Control.setBounds (controlArea.removeFromTop (26));
                printedLabels.push_back ({ controlArea.removeFromTop (12),
                                           NI_TXT ("OSC.3 控制"), false });
            }
        }
    }

    // ---- 混音器 ------------------------------------------------------------
    {
        auto col = mixerArea.reduced (4, 2);
        col.removeFromTop (4);

        juce::Slider* levels[] = { &oscLevel[0], &oscLevel[1], &oscLevel[2] };

        for (int i = 0; i < 3; ++i)
        {
            auto row = col.removeFromTop (86);
            printedLabels.push_back ({ row.removeFromLeft (42), NI_TXT ("振荡器 ")
                                                         + juce::String (i + 1), false });
            auto knobArea = row.removeFromLeft (52);
            placeKnob (knobArea, *levels[i], "VOLUME");
            row.removeFromLeft (4);
            oscOn[i].setBounds (row.removeFromTop (28).removeFromLeft (44));
        }

        col.removeFromTop (2);
        auto noiseRow = col.removeFromTop (86);
        printedLabels.push_back ({ noiseRow.removeFromLeft (42), NI_TXT ("噪声"), false });
        auto noiseKnob = noiseRow.removeFromLeft (52);
        placeKnob (noiseKnob, noiseLevelSlider, "VOLUME");
        noiseRow.removeFromLeft (4);
        noiseType.setBounds (noiseRow.removeFromTop (28).removeFromLeft (44));

        auto externalRow = col.removeFromTop (78);
        printedLabels.push_back ({ externalRow.removeFromLeft (42), NI_TXT ("外部输入"), false });
        auto extKnob = externalRow.removeFromLeft (52);
        placeKnob (extKnob, externalInputVolume, "VOLUME");
        externalRow.removeFromLeft (4);
        externalInputOn.setBounds (externalRow.removeFromTop (28).removeFromLeft (44));

        auto lamp = mixerArea.removeFromTop (0);
        juce::ignoreUnused (lamp);
    }

    // ---- 修饰器 ------------------------------------------------------------
    {
        auto col = modifiersArea.reduced (4, 2);

        auto row = col.removeFromTop (72);
        auto c1 = row.removeFromLeft (78);
        placeKnob (c1, cutoff, NI_TXT ("截止频率"));
        row.removeFromLeft (2);
        auto c2 = row.removeFromLeft (78);
        placeKnob (c2, emphasis, NI_TXT ("滤波共振"));
        auto c3 = row.removeFromLeft (78);
        placeKnob (c3, contour, NI_TXT ("包络量"));

        col.removeFromTop (2);
        auto switches = col.removeFromTop (40);
        auto fm = switches.removeFromLeft (64);
        filterMod.setBounds (fm.removeFromTop (26));
        printedLabels.push_back ({ fm.removeFromTop (12), NI_TXT ("滤波调制"), false });
        switches.removeFromLeft (4);
        auto k1 = switches.removeFromLeft (52);
        keyTrack1.setBounds (k1.removeFromTop (26));
        printedLabels.push_back ({ k1.removeFromTop (12), NI_TXT ("键盘 1"), false });
        switches.removeFromLeft (4);
        auto k2 = switches.removeFromLeft (52);
        keyTrack2.setBounds (k2.removeFromTop (26));
        printedLabels.push_back ({ k2.removeFromTop (12), NI_TXT ("键盘 2"), false });

        col.removeFromTop (4);
        printedLabels.push_back ({ col.removeFromTop (14), NI_TXT ("滤波包络"), true });
        auto fRow = col.removeFromTop (72);
        auto fa = fRow.removeFromLeft (78);
        placeKnob (fa, filterAttack, NI_TXT ("起音"));
        fRow.removeFromLeft (2);
        auto fd = fRow.removeFromLeft (78);
        placeKnob (fd, filterDecay, NI_TXT ("衰减"));
        auto fs = fRow.removeFromLeft (78);
        placeKnob (fs, filterSustainLevel, NI_TXT ("延音"));

        col.removeFromTop (4);
        printedLabels.push_back ({ col.removeFromTop (14), NI_TXT ("响度包络"), true });
        auto aRow = col.removeFromTop (72);
        auto aa = aRow.removeFromLeft (78);
        placeKnob (aa, ampAttack, NI_TXT ("起音"));
        aRow.removeFromLeft (2);
        auto ad = aRow.removeFromLeft (78);
        placeKnob (ad, ampDecay, NI_TXT ("衰减"));
        auto as = aRow.removeFromLeft (78);
        placeKnob (as, ampSustainLevel, NI_TXT ("延音"));
    }

    // ---- 输出 --------------------------------------------------------------
    {
        auto col = outputArea.reduced (4, 6);
        auto knobArea = col.removeFromTop (78);
        placeKnob (knobArea, volume, NI_TXT ("主音量"));

        col.removeFromTop (14);
        auto mainRow = col.removeFromTop (44);
        mainOut.setBounds (mainRow.removeFromTop (28).removeFromLeft (44));
        printedLabels.push_back ({ mainRow.removeFromTop (14), NI_TXT ("主输出"), false });

        col.removeFromTop (10);
        auto a440Row = col.removeFromTop (44);
        a440.setBounds (a440Row.removeFromTop (28).removeFromLeft (44));
        printedLabels.push_back ({ a440Row.removeFromTop (14), "A-440", false });

        col.removeFromTop (18);
        printedLabels.push_back ({ col.removeFromTop (18), NI_TXT ("耳机"), false });

        col.removeFromTop (24);
        auto powerRow = col.removeFromTop (44);
        power.setBounds (powerRow.removeFromTop (28).removeFromLeft (44));
        printedLabels.push_back ({ powerRow.removeFromTop (14), NI_TXT ("电源"), false });
    }

    // ---- 键盘区 ------------------------------------------------------------
    {
        auto leftBlock = keyboardArea.removeFromLeft (232);
        auto left = leftBlock.reduced (8, 8);

        auto lfoArea = left.removeFromTop (46).removeFromLeft (60);
        placeKnob (lfoArea, lfoRate, NI_TXT ("LFO 速率"), 34);

        auto switchRow = left.removeFromTop (40);
        auto glideArea = switchRow.removeFromLeft (54);
        glideSwitch.setBounds (glideArea.removeFromTop (24));
        printedLabels.push_back ({ glideArea.removeFromTop (11), NI_TXT ("滑音"), false });
        switchRow.removeFromLeft (8);
        auto decayArea = switchRow.removeFromLeft (54);
        releaseSwitch.setBounds (decayArea.removeFromTop (24));
        printedLabels.push_back ({ decayArea.removeFromTop (11), NI_TXT ("衰减"), false });

        auto wheels = leftBlock.withTrimmedTop (86).reduced (16, 0);
        auto pitchArea = wheels.removeFromLeft (34);
        pitchWheel.setBounds (pitchArea.removeFromTop (52));
        printedLabels.push_back ({ pitchArea.removeFromTop (12), NI_TXT ("弯音"), false });
        wheels.removeFromLeft (12);
        auto modArea = wheels.removeFromLeft (34);
        modWheel.setBounds (modArea.removeFromTop (52));
        printedLabels.push_back ({ modArea.removeFromTop (12), NI_TXT ("调制"), false });

        keyboard.setBounds (keyboardArea.reduced (12, 8));
    }
}

//==============================================================================
void MinimoogEditor::paint (juce::Graphics& g)
{
    // Wooden body.
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

    // The HM strip sits on top like a little digital overlay.
    g.setColour (juce::Colour (0xff1b1b1e));
    g.fillRect (0, 0, getWidth(), kStripHeight);
    g.setColour (juce::Colour (0xff323238));
    g.drawHorizontalLine (kStripHeight - 1, 0.0f, (float) getWidth());

    g.setColour (kCream);
    g.setFont (theme::font (13.0f, true));
    g.drawText (NI_TXT ("迷你穆格 · MODEL D"),
                juce::Rectangle<int> (kCheekWidth + 8, 3, 180, 17),
                juce::Justification::centredLeft);
    g.setColour (kDimCream);
    g.setFont (theme::font (8.5f));
    g.drawText (NI_TXT ("MODEL D · 三段振荡器"), 
                juce::Rectangle<int> (kCheekWidth + 8, 20, 180, 12),
                juce::Justification::centredLeft);

    // Black front panel.
    g.setColour (kPanel);
    g.fillRect (panelArea);
    g.setColour (kPanelEdge);
    g.drawRect (panelArea, 1);

    // Separators between the sections.
    g.setColour (kDimCream.withAlpha (0.35f));

    for (const auto& section : { controllersArea, oscillatorArea, mixerArea, modifiersArea })
        if (section.getWidth() > 0)
            g.drawVerticalLine (section.getRight() + 3, (float) panelArea.getY() + 6.0f,
                                (float) panelArea.getBottom() - 6.0f);

    if (controllersArea.getWidth() > 0)
    {
        drawSectionTitle (g, controllersArea, NI_TXT ("控制器"));
        drawSectionTitle (g, oscillatorArea, NI_TXT ("振荡器组"));
        drawSectionTitle (g, mixerArea, NI_TXT ("混音器"));
        drawSectionTitle (g, modifiersArea, NI_TXT ("修饰器"));
        drawSectionTitle (g, outputArea, NI_TXT ("输出"));
    }

    for (const auto& label : printedLabels)
    {
        g.setColour (label.title ? kCream : kDimCream);
        g.setFont (theme::font (label.title ? 9.5f : 8.0f, label.title));
        g.drawFittedText (label.text, label.bounds, label.justification, 2);
    }

    // The overload lamp watches the sum of the mixer, as on the original.
    if (const auto* track = currentTrack())
    {
        const auto& p = track->minimoog;
        const auto sum = p.oscs[0].level + p.oscs[1].level + p.oscs[2].level + p.noiseLevel;
        const auto lamp = juce::Rectangle<int> (mixerArea.getRight() - 20, mixerArea.getY() + 8, 8, 8);
        g.setColour (sum > 1.0f ? kOrange : juce::Colour (0xff31402f));
        g.fillEllipse (lamp.toFloat());
        g.setColour (kDimCream);
        g.setFont (theme::font (7.0f));
        g.drawText (NI_TXT ("过载"), lamp.translated (-24, 12).withSize (56, 10),
                    juce::Justification::centred);
    }

    // The power LED.
    const auto led = juce::Rectangle<int> (outputArea.getX() + 8, outputArea.getBottom() - 46, 7, 7);
    g.setColour (power.getState() ? juce::Colour (0xffd94a2c) : juce::Colour (0xff4a1c12));
    g.fillEllipse (led.toFloat());

    // Screws on the wooden frame.
    g.setColour (juce::Colours::black.withAlpha (0.45f));

    for (const auto& p : { juce::Point<int> (kCheekWidth / 2, kStripHeight + 8),
                           juce::Point<int> (getWidth() - kCheekWidth / 2, kStripHeight + 8),
                           juce::Point<int> (kCheekWidth / 2, panelArea.getBottom() + 4),
                           juce::Point<int> (getWidth() - kCheekWidth / 2,
                                             panelArea.getBottom() + 4) })
        g.fillEllipse (juce::Rectangle<float> ((float) p.x - 2.0f, (float) p.y - 2.0f,
                                               4.0f, 4.0f));
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
