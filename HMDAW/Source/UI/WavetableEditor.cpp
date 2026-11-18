#include "WavetableEditor.h"
#include "Theme.h"
#include "SynthControls.h"

#include "../Core/WavetablePresets.h"
#include "../Core/WavetableTables.h"

namespace hmdaw
{

namespace
{
    constexpr int kTitleHeight    = 24;
    constexpr int kPresetHeight   = 26;
    /** The waterfall view: 168 px tall, with a 22 px strip at the top for the
        振荡器 1 / 2 buttons and the readout. */
    constexpr int kDisplayHeight  = 168;
    constexpr int kWaterfallTopStrip = 22;
    constexpr int kOscButtonWidth = 84;
    constexpr int kOscButtonHeight = 18;
    constexpr int kKeyboardHeight = 80;
    constexpr int kRowHeight      = 20;
    constexpr int kHeaderHeight   = 17;
    constexpr int kRowGap         = 6;
    constexpr int kColumnGap      = 10;
    constexpr int kLabelWidth     = 58;
    constexpr int kToggleWidth    = 58;

    /** Combo ids for the 波表 choices: the built-in bank first, then the project's
        own tables. (The actions that make tables live in the editor window, so the
        choice itself never doubles as a command.) */
    constexpr int kTableBuiltInIdBase = 1;
    constexpr int kTableCustomIdBase  = 1000;

    /** The 振荡器 1 / 振荡器 2 pair above the waterfall view: two states of one choice. */
    constexpr int kOscViewRadioGroup = 0x0f3a1;

    /** Two cycles of the wave, which is enough to read the shape. */
    constexpr double kDisplayCycles = 1.0;
}

//==============================================================================
void WavetableWaterfallView::setContent (std::shared_ptr<const wavetable::Wavetable> customTable,
                                        int builtInTableIndex, int oscIndex,
                                        double parameterPositionToUse, double livePositionToUse)
{
    custom = std::move (customTable);
    builtInIndex = builtInTableIndex;
    osc = juce::jlimit (0, 1, oscIndex);
    parameterPosition = juce::jlimit (0.0, 1.0, parameterPositionToUse);
    livePosition = livePositionToUse < 0.0 ? -1.0 : juce::jlimit (0.0, 1.0, livePositionToUse);
    repaint();
}

void WavetableWaterfallView::setLivePosition (double newLivePosition)
{
    const auto wanted = newLivePosition < 0.0 ? -1.0 : juce::jlimit (0.0, 1.0, newLivePosition);

    // The timer ticks far more often than the position moves: an editor with nothing
    // playing must not repaint 30 times a second for nothing.
    if (wanted < 0.0 && livePosition < 0.0)
        return;

    if (wanted >= 0.0 && livePosition >= 0.0 && std::abs (wanted - livePosition) < 1.0e-4)
        return;

    livePosition = wanted;
    repaint();
}

const wavetable::Wavetable& WavetableWaterfallView::table() const
{
    // A custom table wins over the built-in index - the same rule the engine's
    // render loop uses, so the picture is of the table that is really playing.
    return custom != nullptr ? *custom : wavetable::getTable (builtInIndex);
}

juce::String WavetableWaterfallView::getDisplayedTableName() const
{
    return custom != nullptr ? custom->name : wavetable::tableName (builtInIndex);
}

int WavetableWaterfallView::getFrameCountForTest() const
{
    return juce::jmax (1, table().frameCount);
}

double WavetableWaterfallView::sliceForPosition (double position) const
{
    return juce::jlimit (0.0, 1.0, position) * (double) (getFrameCountForTest() - 1);
}

float WavetableWaterfallView::amplitude() const
{
    return (float) plot.getHeight() * 0.30f;
}

float WavetableWaterfallView::centreYForSlice (double slice) const
{
    const auto frames = juce::jmax (1, getFrameCountForTest());
    const auto depth = frames > 1 ? juce::jlimit (0.0, 1.0, slice / (double) (frames - 1)) : 0.0;

    // The nearest frame sits low (its wave just touches the bottom edge) and every
    // frame further back is drawn one depth step higher, so eight of them stack into
    // the plot without clipping.
    return (float) plot.getBottom() - amplitude()
               - (float) depth * (float) plot.getHeight() * 0.40f;
}

float WavetableWaterfallView::shrinkForSlice (double slice) const
{
    const auto frames = juce::jmax (1, getFrameCountForTest());
    const auto depth = frames > 1 ? juce::jlimit (0.0, 1.0, slice / (double) (frames - 1)) : 0.0;
    return 1.0f - (float) depth * 0.21f;
}

double WavetableWaterfallView::positionAt (juce::Point<int> point) const
{
    const auto frames = juce::jmax (1, getFrameCountForTest());

    if (frames <= 1)
        return 0.0;

    const auto span = (float) plot.getHeight() * 0.40f;
    const auto y = (float) juce::jlimit (plot.getY(), plot.getBottom(), point.y);
    const auto slice = ((float) plot.getBottom() - amplitude() - y) / juce::jmax (1.0f, span)
                           * (float) (frames - 1);

    return juce::jlimit (0.0, 1.0, (double) slice / (double) (frames - 1));
}

int WavetableWaterfallView::frameAt (juce::Point<int> point) const
{
    const auto frames = juce::jmax (1, getFrameCountForTest());

    if (! plot.contains (point))
        return -1;

    return juce::jlimit (0, frames - 1, (int) std::lround (sliceForPosition (positionAt (point))));
}

juce::Rectangle<int> WavetableWaterfallView::getFrameRowBoundsForTest (int frame) const
{
    const auto frames = juce::jmax (1, getFrameCountForTest());

    if (frame < 0 || frame >= frames)
        return {};

    const auto step = frames > 1 ? (float) plot.getHeight() * 0.40f / (float) (frames - 1) : 0.0f;
    const auto centre = centreYForSlice ((double) frame);

    return { plot.getX(), (int) std::round (centre - step * 0.5f), plot.getWidth(),
             juce::jmax (1, (int) std::round (step)) };
}

juce::Point<int> WavetableWaterfallView::getCursorPointForTest() const
{
    return { plot.getX(), (int) std::round (centreYForSlice (sliceForPosition (getDisplayedPosition()))) };
}

void WavetableWaterfallView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme::panelRaised.withAlpha (0.6f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 1.0f);

    // The top strip belongs to the 振荡器 1 / 振荡器 2 buttons and the readout.
    plot = area.reduced (8).withTrimmedTop (kWaterfallTopStrip);

    const auto& t = table();
    const auto frames = juce::jmax (1, t.frameCount);
    const auto columns = juce::jmax (2, plot.getWidth());
    const auto xCentre = (float) plot.getCentreX();
    const auto shown = sliceForPosition (getDisplayedPosition());

    // ---- the frames, from the back forwards ---------------------------------
    for (int frame = frames - 1; frame >= 0; --frame)
    {
        const auto shrink = shrinkForSlice ((double) frame);
        const auto centre = centreYForSlice ((double) frame);
        const auto halfAmp = amplitude() * shrink;
        const auto* data = t.frameData (0, frame);

        juce::Path path;

        for (int x = 0; x < columns; ++x)
        {
            const auto index = (int) ((juce::int64) x * wavetable::kFrameSize / columns);
            const auto px = xCentre + ((float) x - xCentre) * shrink;
            const auto py = centre - juce::jlimit (-1.0f, 1.0f, data[index]) * halfAmp;

            if (x == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }

        // Filled down to its own centre line, so a stack of eight reads as a surface.
        juce::Path filled (path);
        filled.lineTo (xCentre + ((float) (columns - 1) - xCentre) * shrink, centre);
        filled.lineTo (xCentre + (0.0f - xCentre) * shrink, centre);
        filled.closeSubPath();

        g.setColour (theme::referenceLine.withAlpha (0.14f));
        g.fillPath (filled);

        const auto active = std::abs ((double) frame - shown) < 0.5;

        g.setColour (active ? theme::accentDim.brighter (0.5f)
                            : theme::referenceLine.withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (active ? 1.4f : 1.0f));

        // The frame number, on the left edge of the depth axis.
        g.setColour (theme::dimText.withAlpha (0.85f));
        g.setFont (theme::font (9.5f));
        g.drawText (juce::String (frame),
                    juce::Rectangle<int> (plot.getX() - 14, (int) centre - 6, 10, 12),
                    juce::Justification::centredRight, false);
    }

    // ---- the wave that is on right now --------------------------------------
    {
        const auto shrink = shrinkForSlice (shown);
        const auto centre = centreYForSlice (shown);
        const auto halfAmp = amplitude() * shrink;
        const auto position = getDisplayedPosition();

        juce::Path path;

        for (int x = 0; x < columns; ++x)
        {
            const auto phase = (double) x / (double) columns * kDisplayCycles;
            const auto value = WavetableSynth::sampleTable (t, 0, phase, position);
            const auto px = xCentre + ((float) x - xCentre) * shrink;
            const auto py = centre - (float) juce::jlimit (-1.0, 1.0, value) * halfAmp;

            if (x == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }

        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    // ---- the cursor line, the frame outline and the readout ------------------
    {
        const auto centre = (int) std::round (centreYForSlice (shown));

        g.setColour (theme::accent.withAlpha (0.85f));
        g.drawLine ((float) plot.getX() - 6.0f, (float) centre, (float) plot.getRight(),
                    (float) centre, 1.0f);

        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawRect (plot, 1);

        g.setColour (theme::dimText);
        g.setFont (theme::font (10.0f));
        g.drawText (getDisplayedTableName()
                        + "   位置 " + juce::String (getDisplayedPosition(), 2)
                        + (isShowingLivePosition() ? NI_TXT ("  ·  实时") : juce::String()),
                    getLocalBounds().reduced (8).removeFromTop (kWaterfallTopStrip)
                        .withTrimmedLeft (180),
                    juce::Justification::centredLeft, false);
    }
}

void WavetableWaterfallView::mouseDown (const juce::MouseEvent& e)
{
    if (! plot.contains (e.getPosition()))
        return;

    dragged = false;
}

void WavetableWaterfallView::mouseDrag (const juce::MouseEvent& e)
{
    if (onPositionGesture == nullptr || ! plot.contains (e.getPosition()))
        return;

    if (! dragged)
    {
        dragged = true;
        onPositionGesture (Gesture::begin, positionAt (e.getPosition()));
    }

    onPositionGesture (Gesture::update, positionAt (e.getPosition()));
}

void WavetableWaterfallView::mouseUp (const juce::MouseEvent& e)
{
    // A press and release without a drag means "that frame": its own undo step.
    if (! dragged && e.getNumberOfClicks() < 2 && onPositionGesture != nullptr)
    {
        const auto frames = juce::jmax (1, getFrameCountForTest());
        const auto frame = frameAt (e.getPosition());

        if (frame >= 0 && frames > 1)
        {
            const auto wanted = (double) frame / (double) (frames - 1);
            onPositionGesture (Gesture::begin, wanted);
            onPositionGesture (Gesture::update, wanted);
        }
    }

    dragged = false;
}

void WavetableWaterfallView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onPositionGesture == nullptr || ! plot.contains (e.getPosition()))
        return;

    onPositionGesture (Gesture::begin, 0.5);
    onPositionGesture (Gesture::update, 0.5);
}

void WavetableWaterfallView::mouseWheelMove (const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused (e);

    if (onPositionGesture == nullptr || wheel.deltaY == 0.0f)
        return;

    const auto step = wheel.deltaY > 0.0f ? 0.01 : -0.01;
    const auto wanted = juce::jlimit (0.0, 1.0, getDisplayedPosition() + step);

    onPositionGesture (Gesture::begin, wanted);
    onPositionGesture (Gesture::update, wanted);
}

//==============================================================================
WavetableEditor::WavetableEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                                  PresetStore& presetsToUse)
    : document (documentToUse),
      audio (audioToUse),
      presets (presetsToUse),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // Tables are a read-only library: touching it here builds it on the message
    // thread rather than inside an audio callback.
    wavetable::ensureBuilt();

    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    addAndMakeVisible (editTableButton);
    editTableButton.setButtonText (NI_TXT ("波表编辑器…"));
    editTableButton.setColour (juce::TextButton::buttonColourId, theme::accentDim);
    editTableButton.setTooltip (NI_TXT ("打开波表编辑器（会把当前这张内置表复制成一张自定义表再编辑）"));
    editTableButton.onClick = [this] { if (onEditTable != nullptr) onEditTable(); };

    addAndMakeVisible (deleteTableButton);
    deleteTableButton.setButtonText (NI_TXT ("删除波表"));
    deleteTableButton.setTooltip (NI_TXT ("把这张自定义表从工程里删掉（用到它的振荡器退回内置表）"));
    deleteTableButton.onClick = [this] { if (onDeleteTable != nullptr) onDeleteTable(); };
    deleteTableButton.setEnabled (false);

    addAndMakeVisible (presetBar);
    presetBar.getEntries       = [this] { return buildPresetEntries(); };
    presetBar.getCurrentName   = [this]
    {
        const auto* track = currentTrack();
        return track != nullptr ? track->wavetable.name : juce::String();
    };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset      = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name) { return userPresetExists (name); };

    addAndMakeVisible (display);
    display.onPositionGesture = [this] (WavetableWaterfallView::Gesture gesture, double position)
    {
        auto& slider = displayedOscillator == 0 ? osc1Position : osc2Position;

        if (gesture == WavetableWaterfallView::Gesture::begin)
            beginParamEdit (NI_TXT ("改波表位置"));

        // Setting the slider is what writes the project (its own onValueChange runs
        // pushCurrentParams), so a drag is one undo step and the sound follows it.
        slider.setValue (position, juce::sendNotificationSync);
    };

    // Which oscillator's table the waterfall shows (and which one its gestures move).
    auto setupViewButton = [this] (juce::TextButton& button, const juce::String& text, int which)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (kOscViewRadioGroup);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        button.setToggleState (which == 0, juce::dontSendNotification);
        button.onClick = [this, which] { selectDisplayedOscillator (which); };
    };

    setupViewButton (osc1ViewButton, NI_TXT ("振荡器 1"), 0);
    setupViewButton (osc2ViewButton, NI_TXT ("振荡器 2"), 1);

    // The cursor follows the sound at 30 Hz; the view repaints only when the
    // position actually moves, so an idle editor costs nothing.
    startTimerHz (30);

    auto setupSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                               double interval, const juce::String& suffix, bool logarithmic)
    {
        addAndMakeVisible (slider);
        configureSynthKnob (slider);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);

        if (logarithmic && minimum > 0.0)
            slider.setSkewFactorFromMidPoint (std::sqrt (minimum * maximum));

        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整波表音色")); };
        slider.onValueChange = [this] { pushCurrentParams(); };
    };

    auto setupCombo = [this] (juce::ComboBox& combo)
    {
        addAndMakeVisible (combo);
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

    // Both 波表 choices are filled by refreshTableChoices (built-in bank first,
    // then the project's own tables), so they can never disagree with the project.

    // ---- oscillators --------------------------------------------------------
    setupHeader (osc1Header, NI_TXT ("振荡器 1"));
    setupCombo (osc1Table);
    setupSlider (osc1Position, 0.0, 1.0, 0.001, {}, false);
    setupSlider (osc1Octave, -2.0, 2.0, 1.0, {}, false);
    setupSlider (osc1Semitone, -12.0, 12.0, 1.0, {}, false);
    setupSlider (osc1Detune, -50.0, 50.0, 1.0, " cent", false);
    setupSlider (osc1Level, 0.0, 1.0, 0.01, {}, false);

    setupHeader (osc2Header, NI_TXT ("振荡器 2"));
    addAndMakeVisible (osc2Enable);
    osc2Enable.setButtonText (NI_TXT ("开启"));
    osc2Enable.setClickingTogglesState (true);
    osc2Enable.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    osc2Enable.onClick = [this] { pushCurrentParams(); };

    setupCombo (osc2Table);
    setupSlider (osc2Position, 0.0, 1.0, 0.001, {}, false);
    setupSlider (osc2Octave, -2.0, 2.0, 1.0, {}, false);
    setupSlider (osc2Semitone, -12.0, 12.0, 1.0, {}, false);
    setupSlider (osc2Detune, -50.0, 50.0, 1.0, " cent", false);
    setupSlider (osc2Level, 0.0, 1.0, 0.01, {}, false);

    setupHeader (noiseHeader, NI_TXT ("噪声"));
    setupSlider (noiseLevel, 0.0, 1.0, 0.01, {}, false);

    // ---- filter and position envelope ---------------------------------------
    setupHeader (filterHeader, NI_TXT ("滤波器"));
    setupCombo (filterMode);
    filterMode.addItem (NI_TXT ("低通"), 1);
    filterMode.addItem (NI_TXT ("带通"), 2);
    filterMode.addItem (NI_TXT ("高通"), 3);

    setupCombo (filterSlope);
    filterSlope.addItem ("12 dB", 1);
    filterSlope.addItem ("24 dB", 2);

    setupSlider (cutoff, 20.0, 20000.0, 1.0, " Hz", true);
    setupSlider (resonance, 0.1, 20.0, 0.01, {}, false);
    setupSlider (filterKeyTrack, 0.0, 1.0, 0.01, {}, false);

    setupHeader (posEnvHeader, NI_TXT ("位置包络"));
    setupSlider (posAttack, 0.0, 5.0, 0.001, " s", false);
    setupSlider (posDecay, 0.0, 5.0, 0.001, " s", false);
    setupSlider (posSustain, 0.0, 1.0, 0.01, {}, false);
    setupSlider (posRelease, 0.0, 5.0, 0.001, " s", false);
    setupSlider (posEnvAmount, -1.0, 1.0, 0.01, {}, false);

    // ---- amplifier, LFO and output -------------------------------------------
    setupHeader (ampHeader, NI_TXT ("放大器包络"));
    setupSlider (ampAttack, 0.0, 5.0, 0.001, " s", false);
    setupSlider (ampDecay, 0.0, 5.0, 0.001, " s", false);
    setupSlider (ampSustain, 0.0, 1.0, 0.01, {}, false);
    setupSlider (ampRelease, 0.0, 5.0, 0.001, " s", false);

    setupHeader (lfoHeader, "LFO");
    setupSlider (lfoRate, 0.0, 20.0, 0.1, " Hz", false);
    setupCombo (lfoWave);
    lfoWave.addItem (NI_TXT ("三角"), 1);
    lfoWave.addItem (NI_TXT ("正弦"), 2);
    lfoWave.addItem (NI_TXT ("方波"), 3);
    lfoWave.addItem (NI_TXT ("采样保持"), 4);

    setupSlider (lfoPosition, 0.0, 1.0, 0.01, {}, false);
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

WavetableEditor::~WavetableEditor()
{
    stopTimer();
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

void WavetableEditor::timerCallback()
{
    const auto* track = currentTrack();
    auto live = -1.0;

    if (track != nullptr)
    {
        // What the user is hearing wins: the track's own voices while the transport
        // plays, otherwise the audition voice of this window's keyboard.
        if (audio.getWavetableActiveVoices (track->id) > 0)
            live = (double) audio.getWavetablePosition (track->id, displayedOscillator);
        else if (audio.getPreviewWavetableActiveVoices() > 0)
            live = (double) audio.getPreviewWavetablePosition (displayedOscillator);
    }

    display.setLivePosition (live);
}

void WavetableEditor::selectDisplayedOscillator (int which)
{
    displayedOscillator = juce::jlimit (0, 1, which);
    osc1ViewButton.setToggleState (displayedOscillator == 0, juce::dontSendNotification);
    osc2ViewButton.setToggleState (displayedOscillator == 1, juce::dontSendNotification);
    updateWaterfall();
}

void WavetableEditor::updateWaterfall()
{
    const auto* track = currentTrack();

    if (track == nullptr)
    {
        display.setContent ({}, 0, displayedOscillator, 0.5, -1.0);
        return;
    }

    const auto& project = document.getProject();
    const auto& osc = track->wavetable.oscs[(size_t) displayedOscillator];

    display.setContent (wavetabletables::playableTableFor (project, osc.customTableId),
                        osc.tableIndex, displayedOscillator, (double) osc.position, -1.0);
}

void WavetableEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

InstrumentTrack* WavetableEditor::currentTrack()
{
    auto* track = document.findTrack (trackId);
    return track != nullptr && track->kind == TrackKind::wavetable ? track : nullptr;
}

const InstrumentTrack* WavetableEditor::currentTrack() const
{
    return const_cast<WavetableEditor*> (this)->currentTrack();
}

void WavetableEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    refresh();
}

void WavetableEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void WavetableEditor::refresh()
{
    refreshTableChoices();
    updateControlsFromTrack();
    pushPreviewTables();
    presetBar.refresh();
}

void WavetableEditor::refreshTableChoices()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& project = document.getProject();
    const auto* track = currentTrack();
    const auto builtInCount = juce::jmax (1, wavetable::builtInTableCount());

    auto rebuild = [&project, builtInCount] (juce::ComboBox& box, int builtInIndex, juce::int64 customId)
    {
        box.clear (juce::dontSendNotification);

        for (int i = 0; i < wavetable::builtInTableCount(); ++i)
            box.addItem (wavetable::tableName (i), kTableBuiltInIdBase + i);

        if (! project.customWavetables.empty())
        {
            for (int i = 0; i < (int) project.customWavetables.size(); ++i)
                box.addItem (NI_TXT ("自定义 · ") + project.customWavetables[(size_t) i].name,
                             kTableCustomIdBase + i);
        }

        if (customId != 0)
            for (int i = 0; i < (int) project.customWavetables.size(); ++i)
                if (project.customWavetables[(size_t) i].id == customId)
                {
                    box.setSelectedId (kTableCustomIdBase + i, juce::dontSendNotification);
                    return;
                }

        box.setSelectedId (kTableBuiltInIdBase + juce::jlimit (0, builtInCount - 1, builtInIndex),
                           juce::dontSendNotification);
    };

    rebuild (osc1Table,
             track != nullptr ? track->wavetable.oscs[0].tableIndex : 0,
             track != nullptr ? track->wavetable.oscs[0].customTableId : 0);
    rebuild (osc2Table,
             track != nullptr ? track->wavetable.oscs[1].tableIndex : 0,
             track != nullptr ? track->wavetable.oscs[1].customTableId : 0);

    const auto custom = track != nullptr && track->wavetable.oscs[0].customTableId != 0;
    editTableButton.setButtonText (custom ? NI_TXT ("编辑这张表…") : NI_TXT ("波表编辑器…"));
    editTableButton.setTooltip (custom
                                    ? NI_TXT ("打开波表编辑器，直接改这一张自定义表")
                                    : NI_TXT ("打开波表编辑器（会把当前这张内置表复制成一张自定义表再编辑）"));

    // 删除 only makes sense for a table of the project's own: the built-in bank is
    // a read-only library.
    deleteTableButton.setEnabled (custom);
}

void WavetableEditor::triggerDeleteTableForTest()
{
    deleteTableButton.onClick();
}

void WavetableEditor::updateControlsFromTrack()
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
    titleLabel.setText (NI_TXT ("波表合成器 · ") + track->name, juce::dontSendNotification);

    const auto& s = track->wavetable;

    // The choices were just rebuilt, so the ids are in step with the project.
    osc1Position.setValue ((double) s.oscs[0].position, juce::dontSendNotification);
    osc1Octave.setValue ((double) s.oscs[0].octave, juce::dontSendNotification);
    osc1Semitone.setValue ((double) s.oscs[0].semitone, juce::dontSendNotification);
    osc1Detune.setValue ((double) s.oscs[0].detuneCents, juce::dontSendNotification);
    osc1Level.setValue ((double) s.oscs[0].level, juce::dontSendNotification);

    osc2Enable.setToggleState (s.oscs[1].enabled, juce::dontSendNotification);
    osc2Position.setValue ((double) s.oscs[1].position, juce::dontSendNotification);
    osc2Octave.setValue ((double) s.oscs[1].octave, juce::dontSendNotification);
    osc2Semitone.setValue ((double) s.oscs[1].semitone, juce::dontSendNotification);
    osc2Detune.setValue ((double) s.oscs[1].detuneCents, juce::dontSendNotification);
    osc2Level.setValue ((double) s.oscs[1].level, juce::dontSendNotification);

    noiseLevel.setValue ((double) s.noiseLevel, juce::dontSendNotification);

    filterMode.setSelectedId ((int) s.filterMode + 1, juce::dontSendNotification);
    filterSlope.setSelectedId (s.filterSlopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
    cutoff.setValue ((double) s.filterCutoffHz, juce::dontSendNotification);
    resonance.setValue ((double) s.filterResonance, juce::dontSendNotification);
    filterKeyTrack.setValue ((double) s.filterKeyTrack, juce::dontSendNotification);

    posAttack.setValue ((double) s.posAttack, juce::dontSendNotification);
    posDecay.setValue ((double) s.posDecay, juce::dontSendNotification);
    posSustain.setValue ((double) s.posSustain, juce::dontSendNotification);
    posRelease.setValue ((double) s.posRelease, juce::dontSendNotification);
    posEnvAmount.setValue ((double) s.posEnvAmount, juce::dontSendNotification);

    ampAttack.setValue ((double) s.ampAttack, juce::dontSendNotification);
    ampDecay.setValue ((double) s.ampDecay, juce::dontSendNotification);
    ampSustain.setValue ((double) s.ampSustain, juce::dontSendNotification);
    ampRelease.setValue ((double) s.ampRelease, juce::dontSendNotification);

    lfoRate.setValue ((double) s.lfoRateHz, juce::dontSendNotification);
    lfoWave.setSelectedId (s.lfoWaveform + 1, juce::dontSendNotification);
    lfoPosition.setValue ((double) s.lfoPositionDepth, juce::dontSendNotification);
    lfoPitch.setValue ((double) s.lfoPitchCents, juce::dontSendNotification);
    lfoCutoff.setValue ((double) s.lfoCutoffDepth, juce::dontSendNotification);
    lfoAmp.setValue ((double) s.lfoAmpDepth, juce::dontSendNotification);

    gain.setValue ((double) s.gain, juce::dontSendNotification);

    updateWaterfall();
}

void WavetableEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void WavetableEditor::pushParams (const WavetableParams& params)
{
    audio.updatePreviewWaveParams (params);
    pushPreviewTables();
    document.notifyEdit();
}

void WavetableEditor::pushPreviewTables()
{
    const auto* track = currentTrack();

    if (track == nullptr)
    {
        audio.updatePreviewWaveTables ({}, {});
        return;
    }

    const auto& project = document.getProject();

    audio.updatePreviewWaveTables (wavetabletables::playableTableFor (project,
                                                                     track->wavetable.oscs[0].customTableId),
                                   wavetabletables::playableTableFor (project,
                                                                     track->wavetable.oscs[1].customTableId));
}

int WavetableEditor::getCustomTableChoiceCountForTest() const
{
    return (int) document.getProject().customWavetables.size();
}

bool WavetableEditor::selectTableChoiceForTest (int which, int itemIndex)
{
    auto& box = which == 1 ? osc2Table : osc1Table;
    const auto id = box.getItemId (itemIndex);

    if (id <= 0)
        return false;

    box.setSelectedId (id, juce::sendNotificationSync);
    return true;
}

void WavetableEditor::triggerEditTableForTest()
{
    editTableButton.onClick();
}

void WavetableEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& s = track->wavetable;
    const auto& project = document.getProject();
    const auto builtInCount = juce::jmax (1, wavetable::builtInTableCount());

    // A choice is either a built-in table (its id) or one of the project's own
    // (id - kTableCustomIdBase = the index in the project's list).
    auto readTableChoice = [&project, builtInCount] (const juce::ComboBox& box,
                                                     int& builtInIndex, juce::int64& customId)
    {
        const auto id = box.getSelectedId();
        builtInIndex = 0;
        customId = 0;

        if (id >= kTableCustomIdBase)
        {
            const auto index = id - kTableCustomIdBase;

            if (index >= 0 && index < (int) project.customWavetables.size())
                customId = project.customWavetables[(size_t) index].id;

            return;
        }

        builtInIndex = juce::jlimit (0, builtInCount - 1, id - kTableBuiltInIdBase);
    };

    int builtIn0 = 0, builtIn1 = 0;
    juce::int64 custom0 = 0, custom1 = 0;
    readTableChoice (osc1Table, builtIn0, custom0);
    readTableChoice (osc2Table, builtIn1, custom1);

    s.oscs[0].tableIndex    = builtIn0;
    s.oscs[0].customTableId = custom0;
    s.oscs[0].position    = (float) osc1Position.getValue();
    s.oscs[0].octave      = juce::jlimit (-2, 2, (int) osc1Octave.getValue());
    s.oscs[0].semitone    = juce::jlimit (-12, 12, (int) osc1Semitone.getValue());
    s.oscs[0].detuneCents = (float) osc1Detune.getValue();
    s.oscs[0].level       = (float) osc1Level.getValue();

    s.oscs[1].enabled     = osc2Enable.getToggleState();
    s.oscs[1].tableIndex    = builtIn1;
    s.oscs[1].customTableId = custom1;
    s.oscs[1].position    = (float) osc2Position.getValue();
    s.oscs[1].octave      = juce::jlimit (-2, 2, (int) osc2Octave.getValue());
    s.oscs[1].semitone    = juce::jlimit (-12, 12, (int) osc2Semitone.getValue());
    s.oscs[1].detuneCents = (float) osc2Detune.getValue();
    s.oscs[1].level       = (float) osc2Level.getValue();

    s.noiseLevel = (float) noiseLevel.getValue();

    s.filterMode      = (FilterMode) juce::jlimit (0, 2, filterMode.getSelectedId() - 1);
    s.filterSlopeDb   = filterSlope.getSelectedId() == 2 ? 24 : 12;
    s.filterCutoffHz  = (float) cutoff.getValue();
    s.filterResonance = (float) resonance.getValue();
    s.filterKeyTrack  = (float) filterKeyTrack.getValue();

    s.posAttack    = (float) posAttack.getValue();
    s.posDecay     = (float) posDecay.getValue();
    s.posSustain   = (float) posSustain.getValue();
    s.posRelease   = (float) posRelease.getValue();
    s.posEnvAmount = (float) posEnvAmount.getValue();

    s.ampAttack  = (float) ampAttack.getValue();
    s.ampDecay   = (float) ampDecay.getValue();
    s.ampSustain = (float) ampSustain.getValue();
    s.ampRelease = (float) ampRelease.getValue();

    s.lfoRateHz        = (float) lfoRate.getValue();
    s.lfoWaveform      = juce::jlimit (0, 3, lfoWave.getSelectedId() - 1);
    s.lfoPositionDepth = (float) lfoPosition.getValue();
    s.lfoPitchCents    = (float) lfoPitch.getValue();
    s.lfoCutoffDepth   = (float) lfoCutoff.getValue();
    s.lfoAmpDepth      = (float) lfoAmp.getValue();

    s.gain = (float) gain.getValue();

    updateWaterfall();
    pushParams (s);
}

void WavetableEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    const auto hz = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    pushPreviewTables();
    audio.triggerPreviewWaveNote (track->wavetable, hz, velocity * 127.0f);
}

void WavetableEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        audio.releasePreviewNote();
        previewMidiNote = -1;
    }
}

//==============================================================================
void WavetableEditor::paint (juce::Graphics& g)
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

void WavetableEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    {
        auto titleRow = area.removeFromTop (kTitleHeight);
        deleteTableButton.setBounds (titleRow.removeFromRight (92).reduced (0, 1));
        titleRow.removeFromRight (4);
        editTableButton.setBounds (titleRow.removeFromRight (132).reduced (0, 1));
        titleRow.removeFromRight (4);
        titleLabel.setBounds (titleRow);
    }
    presetBar.setBounds (area.removeFromTop (kPresetHeight));
    area.removeFromTop (kRowGap);

    display.setBounds (area.removeFromTop (kDisplayHeight));

    {
        // The two buttons live in the view's own top strip, which the view leaves free.
        auto strip = display.getBounds().reduced (8).removeFromTop (kWaterfallTopStrip);
        osc1ViewButton.setBounds (strip.removeFromLeft (kOscButtonWidth)
                                      .withHeight (kOscButtonHeight));
        strip.removeFromLeft (4);
        osc2ViewButton.setBounds (strip.removeFromLeft (kOscButtonWidth)
                                      .withHeight (kOscButtonHeight));
    }

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
        placeRow (column, NI_TXT ("波表"), osc1Table);
        placeRow (column, NI_TXT ("位置"), osc1Position);
        placeRow (column, NI_TXT ("八度"), osc1Octave);
        placeRow (column, NI_TXT ("半音"), osc1Semitone);
        placeRow (column, NI_TXT ("失谐"), osc1Detune);
        placeRow (column, NI_TXT ("音量"), osc1Level);
    }

    // ---- column 2: oscillator 2 and the noise source ------------------------
    {
        auto& column = columns[1];
        placeHeader (column, NI_TXT ("振荡器 2"), &osc2Enable);
        placeRow (column, NI_TXT ("波表"), osc2Table);
        placeRow (column, NI_TXT ("位置"), osc2Position);
        placeRow (column, NI_TXT ("八度"), osc2Octave);
        placeRow (column, NI_TXT ("半音"), osc2Semitone);
        placeRow (column, NI_TXT ("失谐"), osc2Detune);
        placeRow (column, NI_TXT ("音量"), osc2Level);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("噪声"));
        placeRow (column, NI_TXT ("音量"), noiseLevel);
    }

    // ---- column 3: the filter and the position envelope ---------------------
    {
        auto& column = columns[2];
        placeHeader (column, NI_TXT ("滤波器"));
        placeRow (column, NI_TXT ("模式"), filterMode);
        placeRow (column, NI_TXT ("斜率"), filterSlope);
        placeRow (column, NI_TXT ("截止"), cutoff);
        placeRow (column, NI_TXT ("共振"), resonance);
        placeRow (column, NI_TXT ("键跟踪"), filterKeyTrack);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("位置包络"));
        placeRow (column, NI_TXT ("起音"), posAttack);
        placeRow (column, NI_TXT ("衰减"), posDecay);
        placeRow (column, NI_TXT ("保持"), posSustain);
        placeRow (column, NI_TXT ("释放"), posRelease);
        placeRow (column, NI_TXT ("包络量"), posEnvAmount);
    }

    // ---- column 4: amplifier, LFO and output --------------------------------
    {
        auto& column = columns[3];
        placeHeader (column, NI_TXT ("放大器包络"));
        placeRow (column, NI_TXT ("起音"), ampAttack);
        placeRow (column, NI_TXT ("衰减"), ampDecay);
        placeRow (column, NI_TXT ("保持"), ampSustain);
        placeRow (column, NI_TXT ("释放"), ampRelease);
        column.removeFromTop (kRowGap);
        placeHeader (column, "LFO");
        placeRow (column, NI_TXT ("速率"), lfoRate);
        placeRow (column, NI_TXT ("波形"), lfoWave);
        placeRow (column, NI_TXT ("→位置"), lfoPosition);
        placeRow (column, NI_TXT ("→音高"), lfoPitch);
        placeRow (column, NI_TXT ("→滤波"), lfoCutoff);
        placeRow (column, NI_TXT ("→音量"), lfoAmp);
        column.removeFromTop (kRowGap);
        placeHeader (column, NI_TXT ("输出"));
        placeRow (column, NI_TXT ("增益"), gain);
    }
}

//==============================================================================
std::vector<PresetBar::Entry> WavetableEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    int count = 0;
    const auto* bank = wavetable::bank (count);

    for (int i = 0; i < count; ++i)
        list.push_back ({ kBuiltInPresetIdBase + i, juce::String::fromUTF8 (bank[i].name), false });

    const auto& saved = presets.getWavetablePatches();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].params.name;

        if (name.isEmpty())
            name = NI_TXT ("我的波表音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

bool WavetableEditor::userPresetExists (const juce::String& name) const
{
    return presets.findWavetablePresetByName (name) >= 0;
}

bool WavetableEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return false;

    auto params = track->wavetable;

    if (id >= kUserPresetIdBase)
    {
        const auto index = id - kUserPresetIdBase;

        if (index < 0 || index >= (int) presets.getWavetablePatches().size())
            return false;

        // A patch carries the custom tables it uses: bring them into the project
        // (re-using one that is already there) and point the oscillators at them,
        // so the sound that was saved is the sound that is heard.
        const auto& patch = presets.getWavetablePatches()[(size_t) index];

        params = patch.params;

        document.beginEdit (NI_TXT ("载入波表音色"));

        document.applyDerivedEdit ([&patch, &params] (Project& project)
        {
            for (const auto& table : patch.tables)
            {
                // Already here under the same name and data? Then reuse it.
                juce::int64 id = 0;

                for (const auto& existing : project.customWavetables)
                    if (existing.name == table.name && wavetabletables::sameData (existing, table))
                        id = existing.id;

                if (id == 0)
                {
                    auto copy = table;
                    copy.id = project.nextCustomWavetableId++;
                    id = copy.id;
                    project.customWavetables.push_back (std::move (copy));
                }

                for (auto& osc : params.oscs)
                    if (osc.customTableId == table.id)
                        osc.customTableId = id;
            }
        });
    }
    else
    {
        wavetable::applyPreset (params, id - kBuiltInPresetIdBase);
    }

    if (auto* target = currentTrack())
        target->wavetable = params;

    if (auto* target = currentTrack())
        pushParams (target->wavetable);

    refresh();
    return true;
}

bool WavetableEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    WavetablePatch patch;
    patch.params = track->wavetable;
    patch.params.name = name;

    // The tables this sound needs travel with it, so the preset is complete
    // wherever it is opened.
    const auto& project = document.getProject();

    for (const auto& osc : patch.params.oscs)
        if (const auto* table = project.findCustomWavetable (osc.customTableId))
            patch.tables.push_back (*table);

    const auto existing = presets.findWavetablePresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceWavetablePreset (existing, std::move (patch));
    }
    else
    {
        presets.addWavetablePreset (std::move (patch));
    }

    document.beginEdit (NI_TXT ("保存波表预设"));

    if (auto* target = currentTrack())
        target->wavetable.name = name;

    refresh();
    return true;
}

bool WavetableEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getWavetablePatches().size())
        return false;

    const auto oldName = presets.getWavetablePatches()[(size_t) index].params.name;
    presets.renameWavetablePreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->wavetable.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名波表预设"));

        if (auto* target = currentTrack())
            target->wavetable.name = newName;
    }

    refresh();
    return true;
}

bool WavetableEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getWavetablePatches().size())
        return false;

    presets.removeWavetablePreset (index);
    refresh();
    return true;
}

//==============================================================================
juce::String WavetableEditor::getChoiceTextForTest (int which, int index) const
{
    const juce::ComboBox* combo = nullptr;

    switch (which)
    {
        case 0: combo = &osc1Table; break;
        case 1: combo = &osc2Table; break;
        case 2: combo = &filterMode; break;
        case 3: combo = &filterSlope; break;
        case 4: combo = &lfoWave; break;
        default: return {};
    }

    if (index < 0 || index >= combo->getNumItems())
        return {};

    return combo->getItemText (index);
}

juce::Rectangle<int> WavetableEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0:  return osc1Table.getBounds();
        case 1:  return osc1Position.getBounds();
        case 2:  return osc1Level.getBounds();
        case 3:  return osc2Table.getBounds();
        case 4:  return osc2Position.getBounds();
        case 5:  return osc2Enable.getBounds();
        case 6:  return noiseLevel.getBounds();
        case 7:  return filterMode.getBounds();
        case 8:  return cutoff.getBounds();
        case 9:  return resonance.getBounds();
        case 10: return filterKeyTrack.getBounds();
        case 11: return posEnvAmount.getBounds();
        case 12: return ampAttack.getBounds();
        case 13: return ampRelease.getBounds();
        case 14: return lfoRate.getBounds();
        case 15: return lfoPosition.getBounds();
        case 16: return gain.getBounds();
        case 17: return keyboard.getBounds();
        case 18: return display.getBounds();
        case 19: return display.getPlotBoundsForTest();
        case 20: return osc1ViewButton.getBounds();
        case 21: return osc2ViewButton.getBounds();
        default: break;
    }

    return {};
}

juce::Rectangle<int> WavetableEditor::getOscillatorButtonBoundsForTest (int which) const
{
    return which == 0 ? osc1ViewButton.getBounds() : osc2ViewButton.getBounds();
}

std::pair<int, int> WavetableEditor::getPresetCountsForTest() const
{
    return { wavetable::builtInPresetCount(), (int) presets.getWavetablePatches().size() };
}

bool WavetableEditor::selectPresetForTest (int id)
{
    return loadPresetById (id);
}

void WavetableEditor::setParamForTest (int which, double value)
{
    switch (which)
    {
        case 0:  osc1Position.setValue (value, juce::sendNotificationSync); break;
        case 1:  osc1Table.setSelectedId ((int) value, juce::sendNotificationSync); break;
        case 2:  osc2Enable.setToggleState (value > 0.5, juce::dontSendNotification);
                 pushCurrentParams(); break;
        case 3:  cutoff.setValue (value, juce::sendNotificationSync); break;
        case 4:  resonance.setValue (value, juce::sendNotificationSync); break;
        case 5:  posEnvAmount.setValue (value, juce::sendNotificationSync); break;
        case 6:  ampAttack.setValue (value, juce::sendNotificationSync); break;
        case 7:  ampRelease.setValue (value, juce::sendNotificationSync); break;
        case 8:  lfoPosition.setValue (value, juce::sendNotificationSync); break;
        case 9:  gain.setValue (value, juce::sendNotificationSync); break;
        default: break;
    }
}

bool WavetableEditor::loadUserPresetForTest (int index)
{
    return loadPresetById (kUserPresetIdBase + index);
}

bool WavetableEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

bool WavetableEditor::renamePresetForTest (const juce::String& newName)
{
    return renamePresetById (presetBar.getSelectedIdForTest(), newName);
}

bool WavetableEditor::deletePresetForTest()
{
    return deletePresetById (presetBar.getSelectedIdForTest());
}

} // namespace hmdaw
