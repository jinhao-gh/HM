#include "GranularEditor.h"
#include "Theme.h"
#include "SynthControls.h"

#include <cmath>

namespace hmdaw
{

namespace
{
    constexpr int kTitleHeight    = 22;
    constexpr int kPresetHeight   = 26;
    constexpr int kFileRowHeight  = 26;
    constexpr int kCloudHeight    = 140;
    constexpr int kKeyboardHeight = 68;
    constexpr int kRowGap         = 6;
    constexpr int kRowHeight      = 23;
    constexpr int kRowSpacing     = 3;
    constexpr int kCaptionWidth   = 64;
}

//==============================================================================
void GrainCloudView::setSample (std::shared_ptr<const LoadedSample> newSample)
{
    sample = std::move (newSample);
    repaint();
}

void GrainCloudView::setGrains (std::vector<std::pair<float, float>> newGrains)
{
    grains = std::move (newGrains);
    repaint();
}

void GrainCloudView::setScanFraction (float fraction)
{
    scanFraction = fraction;
}

void GrainCloudView::paint (juce::Graphics& g)
{
    g.setColour (theme::gridBackground);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds(), 1);

    auto area = getLocalBounds().reduced (2);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (12.0f));
        g.drawText (NI_TXT ("还没有载入音频：导入一个文件，这里就会画出波形与颗粒"),
                    area, juce::Justification::centred);
        return;
    }

    const auto numSamples = sample->audio.getNumSamples();
    const auto* data = sample->audio.getReadPointer (0);
    const auto columns = juce::jmax (1, area.getWidth());
    const auto centreY = (float) area.getCentreY();
    const auto halfHeight = (float) area.getHeight() * 0.5f;

    g.setColour (theme::referenceLine.withAlpha (0.85f));

    for (int x = 0; x < columns; ++x)
    {
        const auto from = (int) ((juce::int64) x * numSamples / columns);
        const auto to = juce::jmax (from + 1, (int) ((juce::int64) (x + 1) * numSamples / columns));

        auto peak = 0.0f;

        for (int i = from; i < to && i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));

        const auto height = juce::jmax (1.0f, peak * halfHeight);
        g.fillRect (juce::Rectangle<float> ((float) (area.getX() + x), centreY - height,
                                            1.0f, height * 2.0f));
    }

    // The scan line: where the cloud is reading at this instant.
    if (scanFraction >= 0.0f)
    {
        const auto x = (float) area.getX() + juce::jlimit (0.0f, 1.0f, scanFraction)
                                                * (float) area.getWidth();
        g.setColour (theme::accent.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x, (float) area.getY(), 1.0f, (float) area.getHeight()));
    }

    // The grains themselves: one dot each, brighter when it has just started.
    for (const auto& [position, age] : grains)
    {
        const auto x = (float) area.getX() + juce::jlimit (0.0f, 1.0f, position)
                                                * (float) area.getWidth();
        const auto y = (float) area.getBottom() - juce::jlimit (0.0f, 1.0f, age)
                                                  * (float) area.getHeight();

        g.setColour (theme::accent.withAlpha (0.35f + 0.65f * (1.0f - juce::jlimit (0.0f, 1.0f, age))));
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}

//==============================================================================
GranularEditor::GranularEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
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
        return track != nullptr ? track->granular.name : juce::String();
    };
    presetBar.loadPreset       = [this] (int id) { return loadPresetById (id); };
    presetBar.savePreset       = [this] (const juce::String& name, bool overwrite)
                                 { return savePresetByName (name, overwrite); };
    presetBar.renamePreset     = [this] (int id, const juce::String& newName)
                                 { return renamePresetById (id, newName); };
    presetBar.deletePreset     = [this] (int id) { return deletePresetById (id); };
    presetBar.userPresetExists = [this] (const juce::String& name) { return userPresetExists (name); };

    addAndMakeVisible (loadButton);
    loadButton.setButtonText (NI_TXT ("导入音频…"));
    loadButton.setTooltip (NI_TXT ("选一个音频文件当颗粒的来源"));
    loadButton.onClick = [this] { chooseFile(); };

    addAndMakeVisible (fileLabel);
    fileLabel.setFont (theme::font (11.0f));
    fileLabel.setColour (juce::Label::textColourId, theme::dimText);
    fileLabel.setText (NI_TXT ("还没有载入音频文件"), juce::dontSendNotification);

    addAndMakeVisible (cloud);

    auto setupSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                               double interval, bool logarithmic, const juce::String& suffix)
    {
        addAndMakeVisible (slider);
        configureSynthKnob (slider);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);

        if (logarithmic && minimum > 0.0)
            slider.setSkewFactorFromMidPoint (std::sqrt (minimum * maximum));

        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整粒子合成器")); };
        slider.onValueChange = [this] { pushCurrentParams(); };
    };

    setupSlider (positionSlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (scanSlider, -2.0, 2.0, 0.01, false, NI_TXT (" /秒"));
    setupSlider (rootSlider, 20.0, 8000.0, 0.1, true, " Hz");
    setupSlider (grainSlider, 5.0, 500.0, 1.0, true, " ms");
    setupSlider (densitySlider, 1.0, 100.0, 0.5, false, NI_TXT (" 颗/秒"));
    setupSlider (spraySlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (spacingSlider, 0.0, 0.9, 0.01, false, {});
    setupSlider (pitchJitterSlider, 0.0, 1200.0, 1.0, false, NI_TXT (" 音分"));
    setupSlider (reverseSlider, 0.0, 1.0, 0.01, false, {});
    setupSlider (posAttackSlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (posDecaySlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (posSustainSlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (posReleaseSlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (posAmountSlider, -1.0, 1.0, 0.01, false, {});
    setupSlider (ampAttackSlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (ampDecaySlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (ampSustainSlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (ampReleaseSlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (cutoffSlider, 20.0, 20000.0, 1.0, true, " Hz");
    setupSlider (resonanceSlider, 0.1, 20.0, 0.01, false, {});
    setupSlider (keyTrackSlider, 0.0, 1.0, 0.01, false, {});
    setupSlider (spreadSlider, 0.0, 1.0, 0.01, false, {});
    setupSlider (gainSlider, 0.0, 1.0, 0.001, false, {});
    configureVolumeBar (gainSlider);

    addAndMakeVisible (filterModeBox);
    filterModeBox.setTooltip (NI_TXT ("滤波器类型"));
    filterModeBox.addItem (NI_TXT ("低通"), 1);
    filterModeBox.addItem (NI_TXT ("带通"), 2);
    filterModeBox.addItem (NI_TXT ("高通"), 3);
    filterModeBox.onChange = [this] { pushCurrentParams(); };

    addAndMakeVisible (filterSlopeBox);
    filterSlopeBox.setTooltip (NI_TXT ("滤波器斜率"));
    filterSlopeBox.addItem (NI_TXT ("12 dB"), 1);
    filterSlopeBox.addItem (NI_TXT ("24 dB"), 2);
    filterSlopeBox.onChange = [this] { pushCurrentParams(); };

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

    // The cloud follows the sound at 30 Hz, exactly like the wavetable page's cursor:
    // a tick that finds nothing new does not repaint, so an idle editor costs nothing.
    startTimerHz (30);

    refresh();
}

GranularEditor::~GranularEditor()
{
    stopTimer();
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

InstrumentTrack* GranularEditor::currentTrack()
{
    return document.findTrack (trackId);
}

const InstrumentTrack* GranularEditor::currentTrack() const
{
    return const_cast<GranularEditor*> (this)->currentTrack();
}

void GranularEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void GranularEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    sample.reset();
    lastGrains.clear();
    refresh();
}

void GranularEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

void GranularEditor::refresh()
{
    updateControlsFromTrack();
    presetBar.refresh();
    updateCloud();
}

juce::String GranularEditor::reloadSample()
{
    const auto* track = currentTrack();

    if (track == nullptr || track->granular.filePath.isEmpty())
    {
        sample.reset();
        cloud.setSample (nullptr);
        return {};
    }

    if (sample != nullptr && sample->filePath == track->granular.filePath)
        return {};

    juce::String error;
    sample = getCachedSample (track->granular.filePath, error);
    cloud.setSample (sample);

    return sample == nullptr ? error : juce::String();
}

void GranularEditor::updateControlsFromTrack()
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
    titleLabel.setText (NI_TXT ("粒子合成器 · ") + track->name, juce::dontSendNotification);

    const auto& s = track->granular;

    const auto loadError = reloadSample();

    fileLabel.setText (loadError.isNotEmpty()
                           ? NI_TXT ("载入失败：") + loadError
                           : s.filePath.isEmpty()
                                 ? NI_TXT ("还没有载入音频文件")
                                 : s.fileName + "   "
                                       + juce::String (sample != nullptr ? sample->lengthSeconds() : 0.0, 2)
                                       + NI_TXT (" 秒   ")
                                       + juce::String (sample != nullptr ? sample->sourceSampleRate : 0.0, 0)
                                       + " Hz",
                       juce::dontSendNotification);

    positionSlider.setValue (s.position, juce::dontSendNotification);
    scanSlider.setValue (s.scanSpeed, juce::dontSendNotification);
    rootSlider.setValue (s.rootHz, juce::dontSendNotification);
    grainSlider.setValue (s.grainMs, juce::dontSendNotification);
    densitySlider.setValue (s.densityHz, juce::dontSendNotification);
    spraySlider.setValue (s.spray, juce::dontSendNotification);
    spacingSlider.setValue (s.spacingJitter, juce::dontSendNotification);
    pitchJitterSlider.setValue (s.pitchJitterCents, juce::dontSendNotification);
    reverseSlider.setValue (s.reverseProb, juce::dontSendNotification);
    posAttackSlider.setValue (s.posAttack, juce::dontSendNotification);
    posDecaySlider.setValue (s.posDecay, juce::dontSendNotification);
    posSustainSlider.setValue (s.posSustain, juce::dontSendNotification);
    posReleaseSlider.setValue (s.posRelease, juce::dontSendNotification);
    posAmountSlider.setValue (s.posEnvAmount, juce::dontSendNotification);
    ampAttackSlider.setValue (s.ampAttack, juce::dontSendNotification);
    ampDecaySlider.setValue (s.ampDecay, juce::dontSendNotification);
    ampSustainSlider.setValue (s.ampSustain, juce::dontSendNotification);
    ampReleaseSlider.setValue (s.ampRelease, juce::dontSendNotification);
    filterModeBox.setSelectedId ((int) s.filterMode + 1, juce::dontSendNotification);
    filterSlopeBox.setSelectedId (s.filterSlopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
    cutoffSlider.setValue (s.filterCutoffHz, juce::dontSendNotification);
    resonanceSlider.setValue (s.filterResonance, juce::dontSendNotification);
    keyTrackSlider.setValue (s.filterKeyTrack, juce::dontSendNotification);
    spreadSlider.setValue (s.stereoSpread, juce::dontSendNotification);
    gainSlider.setValue (s.gain, juce::dontSendNotification);
}

void GranularEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void GranularEditor::pushParams (const GranularParams& params)
{
    audio.updatePreviewGranularParams (params);
    document.notifyEdit();
}

void GranularEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& s = track->granular;

    s.position     = (float) juce::jlimit (0.0, 1.0, positionSlider.getValue());
    s.scanSpeed    = (float) juce::jlimit (-2.0, 2.0, scanSlider.getValue());
    s.rootHz       = juce::jlimit (20.0, 8000.0, rootSlider.getValue());
    s.grainMs      = (float) juce::jlimit (5.0, 500.0, grainSlider.getValue());
    s.densityHz    = (float) juce::jlimit (1.0, 100.0, densitySlider.getValue());
    s.spray        = (float) juce::jlimit (0.0, 1.0, spraySlider.getValue());
    s.spacingJitter = (float) juce::jlimit (0.0, 0.9, spacingSlider.getValue());
    s.pitchJitterCents = (float) juce::jlimit (0.0, 1200.0, pitchJitterSlider.getValue());
    s.reverseProb  = (float) juce::jlimit (0.0, 1.0, reverseSlider.getValue());
    s.posAttack    = (float) posAttackSlider.getValue();
    s.posDecay     = (float) posDecaySlider.getValue();
    s.posSustain   = (float) juce::jlimit (0.0f, 1.0f, (float) posSustainSlider.getValue());
    s.posRelease   = (float) posReleaseSlider.getValue();
    s.posEnvAmount = (float) juce::jlimit (-1.0, 1.0, posAmountSlider.getValue());
    s.ampAttack    = (float) ampAttackSlider.getValue();
    s.ampDecay     = (float) ampDecaySlider.getValue();
    s.ampSustain   = (float) juce::jlimit (0.0f, 1.0f, (float) ampSustainSlider.getValue());
    s.ampRelease   = (float) ampReleaseSlider.getValue();
    s.filterMode   = (FilterMode) juce::jlimit (0, 2, filterModeBox.getSelectedId() - 1);
    s.filterSlopeDb = filterSlopeBox.getSelectedId() == 1 ? 12 : 24;
    s.filterCutoffHz  = (float) cutoffSlider.getValue();
    s.filterResonance = (float) resonanceSlider.getValue();
    s.filterKeyTrack  = (float) juce::jlimit (0.0, 1.0, keyTrackSlider.getValue());
    s.stereoSpread = (float) juce::jlimit (0.0, 1.0, spreadSlider.getValue());
    s.gain         = (float) juce::jlimit (0.0, 1.0, gainSlider.getValue());

    pushParams (s);
}

juce::String GranularEditor::loadFile (const juce::File& file)
{
    auto* track = currentTrack();

    if (track == nullptr)
        return NI_TXT ("没有选中乐器");

    juce::String error;
    auto loaded = loadSampleFile (file.getFullPathName(), error);

    if (loaded == nullptr)
        return error;

    const auto path = file.getFullPathName();
    const auto name = file.getFileName();

    document.applyEdit (NI_TXT ("导入音频"), [this, path, name] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
        {
            target->granular.filePath = path;
            target->granular.fileName = name;
        }
    });

    sample = std::move (loaded);
    cloud.setSample (sample);
    refresh();
    return {};
}

void GranularEditor::chooseFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (NI_TXT ("导入音频"),
                                                        juce::File::getSpecialLocation (
                                                            juce::File::userMusicDirectory),
                                                        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<GranularEditor> (this),
                           chooser] (const juce::FileChooser& fc)
    {
        // The window can close while the system dialog is up.
        if (safeThis == nullptr)
            return;

        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        const auto error = safeThis->loadFile (file);

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    NI_TXT ("载入失败"), error);
    });
}

void GranularEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    reloadSample();

    const auto frequency = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    audio.triggerPreviewGranularNote (track->granular, sample, frequency, velocity * 127.0f);
}

void GranularEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        previewMidiNote = -1;
        audio.releasePreviewNote();
    }
}

void GranularEditor::timerCallback()
{
    updateCloud();
}

void GranularEditor::updateCloud()
{
    const auto* track = currentTrack();

    if (track == nullptr || track->id != trackId)
        return;

    std::vector<std::pair<float, float>> grains;

    // What the user is hearing wins: the track's own cloud while the transport
    // plays, otherwise the audition voice this window's keyboard started.
    const auto trackGrains = audio.getGranularGrainCount (track->id);

    if (trackGrains > 0)
    {
        for (int i = 0; i < trackGrains; ++i)
            grains.push_back ({ audio.getGranularGrainPosition (track->id, i),
                                audio.getGranularGrainAge (track->id, i) });
    }
    else
    {
        const auto previewGrains = audio.getPreviewGranularGrainCount();

        for (int i = 0; i < previewGrains; ++i)
            grains.push_back ({ audio.getPreviewGranularGrainPosition (i),
                                audio.getPreviewGranularGrainAge (i) });
    }

    if (grains == lastGrains)
        return;

    lastGrains = grains;

    auto scan = -1.0f;

    if (! grains.empty())
    {
        auto sum = 0.0f;

        for (const auto& [position, age] : grains)
        {
            juce::ignoreUnused (age);
            sum += position;
        }

        scan = sum / (float) grains.size();
    }

    cloud.setGrains (grains);
    cloud.setScanFraction (scan);
}

void GranularEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawHorizontalLine (kTitleHeight + kPresetHeight + kFileRowHeight + kCloudHeight + 3 * kRowGap,
                          8.0f, (float) getWidth() - 8.0f);

    g.setFont (theme::font (10.5f));

    for (const auto& row : rowLabels)
    {
        g.setColour (row.header ? theme::accent : theme::dimText);
        g.drawText (row.text, row.bounds, juce::Justification::centredLeft);
    }
}

void GranularEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    titleLabel.setBounds (area.removeFromTop (kTitleHeight));
    presetBar.setBounds (area.removeFromTop (kPresetHeight));
    area.removeFromTop (kRowGap);

    auto fileRow = area.removeFromTop (kFileRowHeight);
    loadButton.setBounds (fileRow.removeFromLeft (110).reduced (0, 2));
    fileRow.removeFromLeft (8);
    fileLabel.setBounds (fileRow);

    area.removeFromTop (kRowGap);
    cloud.setBounds (area.removeFromTop (kCloudHeight));
    area.removeFromTop (kRowGap);

    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    area.removeFromBottom (kRowGap);

    const auto columnWidth = juce::jmax (150, area.getWidth() / 4);
    auto c1 = area.removeFromLeft (columnWidth);
    auto c2 = area.removeFromLeft (columnWidth);
    auto c3 = area.removeFromLeft (columnWidth);
    auto c4 = area;

    rowLabels.clear();

    auto place = [this] (juce::Rectangle<int>& column, const juce::String& text,
                         juce::Component& control)
    {
        auto row = column.removeFromTop (kRowHeight);
        rowLabels.push_back ({ row.removeFromLeft (kCaptionWidth), text, false });
        control.setBounds (row);
        column.removeFromTop (kRowSpacing);
    };

    auto placeHeader = [this] (juce::Rectangle<int>& column, const juce::String& text)
    {
        auto row = column.removeFromTop (20);
        rowLabels.push_back ({ row, text, true });
        column.removeFromTop (2);
    };

    placeHeader (c1, NI_TXT ("颗粒"));
    place (c1, NI_TXT ("粒度"), grainSlider);
    place (c1, NI_TXT ("密度"), densitySlider);
    place (c1, NI_TXT ("位置抖动"), spraySlider);
    place (c1, NI_TXT ("间距抖动"), spacingSlider);
    place (c1, NI_TXT ("音高抖动"), pitchJitterSlider);
    place (c1, NI_TXT ("反向概率"), reverseSlider);

    placeHeader (c2, NI_TXT ("取样"));
    place (c2, NI_TXT ("位置"), positionSlider);
    place (c2, NI_TXT ("扫描速度"), scanSlider);
    place (c2, NI_TXT ("根音"), rootSlider);
    placeHeader (c2, NI_TXT ("输出"));
    place (c2, NI_TXT ("声像扩展"), spreadSlider);
    place (c2, NI_TXT ("增益"), gainSlider);

    placeHeader (c3, NI_TXT ("位置包络"));
    place (c3, NI_TXT ("起音"), posAttackSlider);
    place (c3, NI_TXT ("衰减"), posDecaySlider);
    place (c3, NI_TXT ("保持"), posSustainSlider);
    place (c3, NI_TXT ("释放"), posReleaseSlider);
    place (c3, NI_TXT ("包络量"), posAmountSlider);
    placeHeader (c3, NI_TXT ("音量包络"));
    place (c3, NI_TXT ("起音"), ampAttackSlider);
    place (c3, NI_TXT ("衰减"), ampDecaySlider);
    place (c3, NI_TXT ("保持"), ampSustainSlider);
    place (c3, NI_TXT ("释放"), ampReleaseSlider);

    placeHeader (c4, NI_TXT ("滤波器"));
    place (c4, NI_TXT ("类型"), filterModeBox);
    place (c4, NI_TXT ("斜率"), filterSlopeBox);
    place (c4, NI_TXT ("截止"), cutoffSlider);
    place (c4, NI_TXT ("共振"), resonanceSlider);
    place (c4, NI_TXT ("键跟踪"), keyTrackSlider);
}

//==============================================================================
std::vector<PresetBar::Entry> GranularEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    // A granular preset always carries the file it was made from, so there is no
    // factory bank here: the list is the user's own sounds - exactly like the sampler.
    const auto& saved = presets.getGranularPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的粒子音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

bool GranularEditor::userPresetExists (const juce::String& name) const
{
    return presets.findGranularPresetByName (name) >= 0;
}

bool GranularEditor::loadPresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getGranularPresets().size())
        return false;

    const auto params = presets.getGranularPresets()[(size_t) index];

    document.beginEdit (NI_TXT ("载入粒子预设"));

    if (auto* target = currentTrack())
        target->granular = params;

    sample.reset();   // the preset may point at another file
    refresh();

    if (auto* target = currentTrack())
        pushParams (target->granular);

    return true;
}

bool GranularEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->granular;
    params.name = name;

    const auto existing = presets.findGranularPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceGranularPreset (existing, params);
    }
    else
    {
        presets.addGranularPreset (params);
    }

    document.beginEdit (NI_TXT ("保存粒子预设"));

    if (auto* target = currentTrack())
        target->granular.name = name;

    refresh();
    return true;
}

bool GranularEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getGranularPresets().size())
        return false;

    const auto oldName = presets.getGranularPresets()[(size_t) index].name;
    presets.renameGranularPreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->granular.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名粒子预设"));

        if (auto* target = currentTrack())
            target->granular.name = newName;
    }

    refresh();
    return true;
}

bool GranularEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getGranularPresets().size())
        return false;

    presets.removeGranularPreset (index);
    refresh();
    return true;
}

int GranularEditor::getSelectedPresetIndex() const
{
    const auto id = presetBar.getSelectedIdForTest();
    return id >= kUserPresetIdBase ? id - kUserPresetIdBase : -1;
}

bool GranularEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

//==============================================================================
juce::String GranularEditor::getCaptionsForTest() const
{
    juce::StringArray captions;

    for (const auto& row : rowLabels)
        captions.add (row.text);

    return captions.joinIntoString (" · ");
}

juce::Rectangle<int> GranularEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return loadButton.getBounds();
        case 1: return cloud.getBounds();
        case 2: return positionSlider.getBounds();
        case 3: return grainSlider.getBounds();
        case 4: return densitySlider.getBounds();
        case 5: return keyboard.getBounds();
        case 6: return filterModeBox.getBounds();
        case 7: return scanSlider.getBounds();
        default: break;
    }

    return {};
}

void GranularEditor::setParamForTest (int which, double value)
{
    switch (which)
    {
        case 0: positionSlider.setValue (value, juce::dontSendNotification); break;
        case 1: scanSlider.setValue (value, juce::dontSendNotification); break;
        case 2: grainSlider.setValue (value, juce::dontSendNotification); break;
        case 3: densitySlider.setValue (value, juce::dontSendNotification); break;
        case 4: spraySlider.setValue (value, juce::dontSendNotification); break;
        case 5: rootSlider.setValue (value, juce::dontSendNotification); break;
        default: return;
    }

    pushCurrentParams();
}

} // namespace hmdaw
