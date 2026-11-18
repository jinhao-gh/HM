#include "SamplerEditor.h"
#include "Theme.h"
#include "SynthControls.h"

namespace hmdaw
{

namespace
{
    constexpr int kTitleHeight    = 22;
    constexpr int kPresetHeight   = 26;
    constexpr int kFileRowHeight  = 26;
    constexpr int kWaveHeight     = 90;
    constexpr int kKeyboardHeight = 68;
    constexpr int kRowGap         = 6;
}

//==============================================================================
void WaveformView::setSample (std::shared_ptr<const LoadedSample> newSample)
{
    sample = std::move (newSample);
    repaint();
}

void WaveformView::setLoop (bool shouldLoop, double start, double end)
{
    looped = shouldLoop;
    loopStart = juce::jlimit (0.0, 1.0, start);
    loopEnd = juce::jlimit (0.0, 1.0, end);
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
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
        g.drawText (NI_TXT ("还没有载入音频"), area, juce::Justification::centred);
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

    if (looped)
    {
        g.setColour (theme::accent);

        for (const auto fraction : { loopStart, loopEnd })
        {
            const auto x = (float) area.getX() + (float) fraction * (float) area.getWidth();
            g.fillRect (juce::Rectangle<float> (x, (float) area.getY(), 1.5f, (float) area.getHeight()));
        }
    }
}

//==============================================================================
SamplerEditor::SamplerEditor (ProjectDocument& documentToUse, AudioEngine& audioToUse,
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
        return track != nullptr ? track->sampler.name : juce::String();
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
    loadButton.onClick = [this] { chooseFile(); };

    addAndMakeVisible (fileLabel);
    fileLabel.setFont (theme::font (11.0f));
    fileLabel.setColour (juce::Label::textColourId, theme::dimText);
    fileLabel.setText (NI_TXT ("还没有载入音频文件"), juce::dontSendNotification);

    addAndMakeVisible (waveform);

    auto setupSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                               double interval, bool logarithmic, const juce::String& suffix)
    {
        addAndMakeVisible (slider);
        configureSynthKnob (slider);
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);

        if (logarithmic && minimum > 0.0)
            slider.setSkewFactorFromMidPoint (std::sqrt (minimum * maximum));

        slider.onDragStart = [this] { beginParamEdit (NI_TXT ("调整采样器")); };
        slider.onValueChange = [this] { pushCurrentParams(); };
    };

    setupSlider (rootSlider, 20.0, 8000.0, 0.1, true, " Hz");
    setupSlider (loopStartSlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (loopEndSlider, 0.0, 1.0, 0.001, false, {});

    addAndMakeVisible (loopButton);
    loopButton.setButtonText (NI_TXT ("循环"));
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    loopButton.setTooltip (NI_TXT ("按住音符时在循环区里反复播放"));
    loopButton.onClick = [this] { pushCurrentParams(); };

    setupSlider (attackSlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (decaySlider, 0.0, 5.0, 0.001, false, " s");
    setupSlider (sustainSlider, 0.0, 1.0, 0.001, false, {});
    setupSlider (releaseSlider, 0.0, 5.0, 0.001, false, " s");

    setupSlider (cutoffSlider, 20.0, 20000.0, 1.0, true, " Hz");
    setupSlider (resonanceSlider, 0.1, 20.0, 0.01, false, {});
    setupSlider (lfoRateSlider, 0.0, 20.0, 0.01, false, " Hz");
    setupSlider (lfoPitchSlider, 0.0, 1200.0, 1.0, false, NI_TXT (" 音分"));
    setupSlider (lfoAmpSlider, 0.0, 1.0, 0.01, false, {});
    setupSlider (gainSlider, 0.0, 1.0, 0.001, false, {});
    configureVolumeBar (gainSlider);

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

SamplerEditor::~SamplerEditor()
{
    keyboardState.removeListener (this);
    document.removeChangeListener (this);
}

InstrumentTrack* SamplerEditor::currentTrack()
{
    return document.findTrack (trackId);
}

const InstrumentTrack* SamplerEditor::currentTrack() const
{
    return const_cast<SamplerEditor*> (this)->currentTrack();
}

void SamplerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SamplerEditor::setTrack (juce::int64 newTrackId)
{
    trackId = newTrackId;
    sample.reset();
    refresh();
}

void SamplerEditor::setShowDialogs (bool shouldShow)
{
    presetBar.showDialogs = shouldShow;
}

void SamplerEditor::refresh()
{
    updateControlsFromTrack();
}

juce::String SamplerEditor::reloadSample()
{
    const auto* track = currentTrack();

    if (track == nullptr || track->sampler.filePath.isEmpty())
    {
        sample.reset();
        waveform.setSample (nullptr);
        return {};
    }

    if (sample != nullptr && sample->filePath == track->sampler.filePath)
        return {};

    juce::String error;
    sample = getCachedSample (track->sampler.filePath, error);
    waveform.setSample (sample);

    return sample == nullptr ? error : juce::String();
}

void SamplerEditor::updateControlsFromTrack()
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
    titleLabel.setText (NI_TXT ("采样器 · ") + track->name, juce::dontSendNotification);

    const auto& s = track->sampler;

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

    rootSlider.setValue (s.rootHz, juce::dontSendNotification);
    loopButton.setToggleState (s.looped, juce::dontSendNotification);
    loopStartSlider.setValue (s.loopStart, juce::dontSendNotification);
    loopEndSlider.setValue (s.loopEnd, juce::dontSendNotification);
    attackSlider.setValue ((double) s.attack, juce::dontSendNotification);
    decaySlider.setValue ((double) s.decay, juce::dontSendNotification);
    sustainSlider.setValue ((double) s.sustain, juce::dontSendNotification);
    releaseSlider.setValue ((double) s.release, juce::dontSendNotification);
    cutoffSlider.setValue ((double) s.filterCutoffHz, juce::dontSendNotification);
    resonanceSlider.setValue ((double) s.filterResonance, juce::dontSendNotification);
    lfoRateSlider.setValue ((double) s.lfoRateHz, juce::dontSendNotification);
    lfoPitchSlider.setValue ((double) s.lfoPitchCents, juce::dontSendNotification);
    lfoAmpSlider.setValue ((double) s.lfoAmpDepth, juce::dontSendNotification);
    gainSlider.setValue ((double) s.gain, juce::dontSendNotification);

    waveform.setLoop (s.looped, s.loopStart, s.loopEnd);
    presetBar.refresh();
}

void SamplerEditor::beginParamEdit (const juce::String& name)
{
    document.beginEdit (name);
}

void SamplerEditor::pushParams (const SamplerParams& params)
{
    audio.updatePreviewSampleParams (params);
    document.notifyEdit();
}

void SamplerEditor::pushCurrentParams()
{
    if (updating)
        return;

    auto* track = currentTrack();

    if (track == nullptr)
        return;

    auto& s = track->sampler;

    s.rootHz      = juce::jlimit (20.0, 8000.0, rootSlider.getValue());
    s.looped      = loopButton.getToggleState();
    s.loopStart   = juce::jlimit (0.0, 0.99, loopStartSlider.getValue());
    s.loopEnd     = juce::jlimit (s.loopStart + 0.01, 1.0, loopEndSlider.getValue());
    s.attack      = (float) attackSlider.getValue();
    s.decay       = (float) decaySlider.getValue();
    s.sustain     = (float) sustainSlider.getValue();
    s.release     = (float) releaseSlider.getValue();
    s.filterCutoffHz  = (float) cutoffSlider.getValue();
    s.filterResonance = (float) resonanceSlider.getValue();
    s.lfoRateHz     = (float) lfoRateSlider.getValue();
    s.lfoPitchCents = (float) lfoPitchSlider.getValue();
    s.lfoAmpDepth   = (float) lfoAmpSlider.getValue();
    s.gain        = (float) gainSlider.getValue();

    waveform.setLoop (s.looped, s.loopStart, s.loopEnd);
    pushParams (s);
}

juce::String SamplerEditor::loadFile (const juce::File& file)
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
            target->sampler.filePath = path;
            target->sampler.fileName = name;
        }
    });

    sample = std::move (loaded);
    waveform.setSample (sample);
    refresh();
    return {};
}

void SamplerEditor::chooseFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (NI_TXT ("导入音频"),
                                                        juce::File::getSpecialLocation (
                                                            juce::File::userMusicDirectory),
                                                        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<SamplerEditor> (this),
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

void SamplerEditor::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    const auto* track = currentTrack();

    if (track == nullptr)
        return;

    reloadSample();

    const auto frequency = 440.0 * std::pow (2.0, (midiNoteNumber - 69) / 12.0);
    previewMidiNote = midiNoteNumber;
    audio.triggerPreviewSampleNote (track->sampler, sample, frequency, velocity * 127.0f);
}

void SamplerEditor::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (midiNoteNumber == previewMidiNote)
    {
        previewMidiNote = -1;
        audio.releasePreviewNote();
    }
}

void SamplerEditor::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    g.setColour (theme::outline);
    g.drawHorizontalLine (kTitleHeight + kPresetHeight + kFileRowHeight + kWaveHeight + 3 * kRowGap,
                          8.0f, (float) getWidth() - 8.0f);

    g.setFont (theme::font (10.5f));

    for (const auto& row : rowLabels)
    {
        g.setColour (row.header ? theme::accent : theme::dimText);
        g.drawText (row.text, row.bounds, juce::Justification::centredLeft);
    }
}

void SamplerEditor::resized()
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
    waveform.setBounds (area.removeFromTop (kWaveHeight));
    area.removeFromTop (kRowGap);

    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    area.removeFromBottom (kRowGap);

    auto left = area.removeFromLeft (juce::jmax (220, area.getWidth() / 3));
    auto middle = area.removeFromLeft (juce::jmax (220, area.getWidth() / 2));
    auto right = area;

    rowLabels.clear();

    // Every single parameter gets its own caption: a row of four sliders under
    // one "包络" label left the user guessing which one was the decay.
    auto place = [this] (juce::Rectangle<int>& column, const juce::String& text,
                         juce::Component& control)
    {
        auto row = column.removeFromTop (24);
        rowLabels.push_back ({ row.removeFromLeft (76), text, false });
        control.setBounds (row);
        column.removeFromTop (3);
    };

    auto placeHeader = [this] (juce::Rectangle<int>& column, const juce::String& text)
    {
        auto row = column.removeFromTop (20);
        rowLabels.push_back ({ row, text, true });
        column.removeFromTop (2);
    };

    placeHeader (left, NI_TXT ("素材"));
    place (left, NI_TXT ("基准音高"), rootSlider);
    place (left, NI_TXT ("循环起点"), loopStartSlider);
    place (left, NI_TXT ("循环终点"), loopEndSlider);
    place (left, NI_TXT ("循环播放"), loopButton);

    middle.removeFromLeft (10);
    placeHeader (middle, NI_TXT ("包络"));
    place (middle, NI_TXT ("起音"), attackSlider);
    place (middle, NI_TXT ("衰减"), decaySlider);
    place (middle, NI_TXT ("保持"), sustainSlider);
    place (middle, NI_TXT ("释放"), releaseSlider);

    right.removeFromLeft (10);
    placeHeader (right, NI_TXT ("滤波器"));
    place (right, NI_TXT ("截止"), cutoffSlider);
    place (right, NI_TXT ("共振"), resonanceSlider);
    placeHeader (right, NI_TXT ("调制"));
    place (right, NI_TXT ("速率"), lfoRateSlider);
    place (right, NI_TXT ("音高"), lfoPitchSlider);
    place (right, NI_TXT ("幅度"), lfoAmpSlider);
    placeHeader (right, NI_TXT ("输出"));
    place (right, NI_TXT ("增益"), gainSlider);
}

//==============================================================================
std::vector<PresetBar::Entry> SamplerEditor::buildPresetEntries() const
{
    std::vector<PresetBar::Entry> list;

    // A sampler preset always carries the file it was made from, so there is no
    // factory bank here: the list is the user's own sounds.
    const auto& saved = presets.getSamplerPresets();

    for (int i = 0; i < (int) saved.size(); ++i)
    {
        auto name = saved[(size_t) i].name;

        if (name.isEmpty())
            name = NI_TXT ("我的采样器音色 ") + juce::String (i + 1);

        list.push_back ({ kUserPresetIdBase + i, name, true });
    }

    return list;
}

bool SamplerEditor::userPresetExists (const juce::String& name) const
{
    return presets.findSamplerPresetByName (name) >= 0;
}

bool SamplerEditor::loadPresetById (int id)
{
    auto* track = currentTrack();

    if (track == nullptr || id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getSamplerPresets().size())
        return false;

    const auto params = presets.getSamplerPresets()[(size_t) index];

    document.beginEdit (NI_TXT ("载入采样器预设"));

    if (auto* target = currentTrack())
        target->sampler = params;

    sample.reset();   // the preset may point at another file
    refresh();

    if (auto* target = currentTrack())
        pushParams (target->sampler);

    return true;
}

bool SamplerEditor::savePresetByName (const juce::String& name, bool overwrite)
{
    auto* track = currentTrack();

    if (track == nullptr || name.isEmpty())
        return false;

    auto params = track->sampler;
    params.name = name;

    const auto existing = presets.findSamplerPresetByName (name);

    if (existing >= 0)
    {
        if (! overwrite)
            return false;

        presets.replaceSamplerPreset (existing, params);
    }
    else
    {
        presets.addSamplerPreset (params);
    }

    // Saving names the loaded sound too, so the list highlights it.
    document.beginEdit (NI_TXT ("保存采样器预设"));

    if (auto* target = currentTrack())
        target->sampler.name = name;

    refresh();
    return true;
}

bool SamplerEditor::renamePresetById (int id, const juce::String& newName)
{
    if (id < kUserPresetIdBase || newName.isEmpty())
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getSamplerPresets().size())
        return false;

    const auto oldName = presets.getSamplerPresets()[(size_t) index].name;
    presets.renameSamplerPreset (index, newName);

    if (auto* track = currentTrack(); track != nullptr && track->sampler.name == oldName)
    {
        document.beginEdit (NI_TXT ("重命名采样器预设"));

        if (auto* target = currentTrack())
            target->sampler.name = newName;
    }

    refresh();
    return true;
}

bool SamplerEditor::deletePresetById (int id)
{
    if (id < kUserPresetIdBase)
        return false;

    const auto index = id - kUserPresetIdBase;

    if (index < 0 || index >= (int) presets.getSamplerPresets().size())
        return false;

    presets.removeSamplerPreset (index);
    refresh();
    return true;
}

int SamplerEditor::getSelectedPresetIndex() const
{
    const auto id = presetBar.getSelectedIdForTest();
    return id >= kUserPresetIdBase ? id - kUserPresetIdBase : -1;
}

bool SamplerEditor::savePresetForTest (const juce::String& name, bool overwrite)
{
    return savePresetByName (name, overwrite);
}

//==============================================================================
juce::String SamplerEditor::getCaptionsForTest() const
{
    juce::StringArray captions;

    for (const auto& row : rowLabels)
        captions.add (row.text);

    return captions.joinIntoString (" · ");
}

juce::Rectangle<int> SamplerEditor::getControlBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return loadButton.getBounds();
        case 1: return waveform.getBounds();
        case 2: return rootSlider.getBounds();
        case 3: return keyboard.getBounds();
        case 4: return loopButton.getBounds();
        default: break;
    }

    return {};
}

void SamplerEditor::setRootHzForTest (double hz)
{
    rootSlider.setValue (hz, juce::dontSendNotification);
    pushCurrentParams();
}

void SamplerEditor::setLoopForTest (bool shouldLoop, double start, double end)
{
    loopButton.setToggleState (shouldLoop, juce::dontSendNotification);
    loopStartSlider.setValue (start, juce::dontSendNotification);
    loopEndSlider.setValue (end, juce::dontSendNotification);
    pushCurrentParams();
}

} // namespace hmdaw
