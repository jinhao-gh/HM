#include "TransportBar.h"
#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace hmdaw
{

TransportBar::TransportBar (ProjectDocument& documentToUse, AppState& appStateToUse,
                            AudioEngine& audioToUse)
    : document (documentToUse), appState (appStateToUse), audio (audioToUse)
{
    addAndMakeVisible (returnButton);
    returnButton.setButtonText (NI_TXT ("|◀ 回到开头"));
    returnButton.onClick = [this] { audio.returnToZero(); refresh(); };

    addAndMakeVisible (playButton);
    playButton.setButtonText (NI_TXT ("▶ 播放"));
    playButton.onClick = [this] { audio.play(); refresh(); };

    addAndMakeVisible (stopButton);
    stopButton.setButtonText (NI_TXT ("■ 停止"));
    stopButton.onClick = [this] { audio.stop(); refresh(); };

    addAndMakeVisible (loopButton);
    loopButton.setButtonText (NI_TXT ("循环"));
    loopButton.setClickingTogglesState (false);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    loopButton.setTooltip (NI_TXT ("开着时播放走到区间末尾就跳回起点；在标尺上 Shift+拖动可以划出区间，Shift+点击清除"));
    loopButton.onClick = [this] { toggleLoop(); };

    addAndMakeVisible (metronomeButton);
    metronomeButton.setButtonText (NI_TXT ("节拍器"));
    metronomeButton.setClickingTogglesState (false);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    metronomeButton.setTooltip (NI_TXT ("播放时按拍发出提示音，小节第一拍加重；只用于监听，不会录进导出"));
    metronomeButton.onClick = [this] { toggleMetronome(); };

    addAndMakeVisible (recordButton);
    recordButton.setButtonText (NI_TXT ("录制"));
    recordButton.setClickingTogglesState (false);
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
    recordButton.setTooltip (NI_TXT ("录制 MIDI（快捷键 *）：走带跑起来以后，"
                                     "按下的琴键会写成音符"));
    recordButton.onClick = [this] { triggerRecordButtonForTest(); };

    addAndMakeVisible (recordTargetButton);
    recordTargetButton.setButtonText (NI_TXT ("片段内"));
    recordTargetButton.setClickingTogglesState (false);
    recordTargetButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    recordTargetButton.setTooltip (NI_TXT ("片段内：写进当前打开的片段（越过末尾就把片段拉长）；"
                                           "片段外：录成新片段"));
    recordTargetButton.onClick = [this] { triggerRecordTargetForTest(); };

    auto setupCaption = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (10.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);
        label.setJustificationType (juce::Justification::centredLeft);
    };

    auto setupReadout = [this] (juce::Label& label)
    {
        addAndMakeVisible (label);
        label.setFont (theme::font (19.0f, true));
        label.setColour (juce::Label::textColourId, theme::text);
        label.setJustificationType (juce::Justification::centredLeft);
    };

    setupCaption (positionCaption, NI_TXT ("位置  小节:拍:tick"));
    setupReadout (positionLabel);
    setupCaption (tempoCaption, NI_TXT ("速度"));
    setupReadout (tempoLabel);
    setupCaption (loopCaption, NI_TXT ("循环"));
    setupReadout (loopLabel);
    loopLabel.setFont (theme::font (15.5f, true));

    addAndMakeVisible (settingsButton);
    settingsButton.setButtonText (NI_TXT ("音频设置"));
    settingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible (midiInputButton);
    midiInputButton.setButtonText (NI_TXT ("MIDI 输入"));
    midiInputButton.onClick = [this] { if (onOpenMidiInput) onOpenMidiInput(); };

    addAndMakeVisible (virtualKeyboardButton);
    virtualKeyboardButton.setButtonText (NI_TXT ("虚拟键盘"));
    virtualKeyboardButton.onClick = [this]
    {
        if (onOpenVirtualKeyboard) onOpenVirtualKeyboard();
    };

    addAndMakeVisible (deviceLabel);
    deviceLabel.setFont (theme::font (10.5f));
    deviceLabel.setColour (juce::Label::textColourId, theme::dimText);
    deviceLabel.setJustificationType (juce::Justification::centredRight);

    refresh();
}

void TransportBar::triggerRecordButtonForTest()
{
    appState.setMidiRecordArmed (! appState.midiRecordArmed);
    refresh();
}

void TransportBar::triggerRecordTargetForTest()
{
    appState.setMidiRecordIntoClip (! appState.midiRecordIntoClip);
    refresh();
}

void TransportBar::refresh()
{
    const auto playing = audio.isPlaying();

    playButton.setEnabled (! playing);
    stopButton.setEnabled (playing);

    // ---- MIDI 录制 ----------------------------------------------------------
    const auto armed = appState.midiRecordArmed;
    recordButton.setToggleState (armed, juce::dontSendNotification);
    // 录制 / 停止录制 - the second click is what stops it, so the button says so.
    recordButton.setButtonText (armed ? NI_TXT ("停止录制") : NI_TXT ("录制"));
    recordButton.setTooltip (armed ? NI_TXT ("正在等琴键：按一下停止录制（快捷键 *）")
                                   : NI_TXT ("录制 MIDI（快捷键 *）：走带跑起来以后，"
                                             "按下的琴键会写成音符"));

    const auto inside = appState.midiRecordIntoClip;
    recordTargetButton.setToggleState (inside, juce::dontSendNotification);
    recordTargetButton.setButtonText (inside ? NI_TXT ("片段内") : NI_TXT ("片段外"));

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto seconds = audio.getPositionSeconds();
    const auto tick = tempoMap.tickAtSeconds (seconds);

    int bar = 1, beat = 1, tickInBeat = 0;
    tempoMap.tickToBarBeat (tick, bar, beat, tickInBeat);

    positionLabel.setText (music::TempoMap::formatPosition (bar, beat, tickInBeat),
                           juce::dontSendNotification);
    tempoLabel.setText (juce::String (tempoMap.bpmAt (tick), 2) + " BPM",
                        juce::dontSendNotification);

    // ---- loop (cycle) -------------------------------------------------------
    const auto& loop = document.getProject().loop;

    loopButton.setToggleState (loop.enabled && loop.isValid(), juce::dontSendNotification);
    loopButton.setButtonText (loop.enabled && loop.isValid() ? NI_TXT ("循环 ●")
                                                             : NI_TXT ("循环"));

    if (loop.isValid())
    {
        int startBar = 1, startBeat = 1, startTickInBeat = 0;
        int endBar = 1, endBeat = 1, endTickInBeat = 0;
        tempoMap.tickToBarBeat (loop.startTick, startBar, startBeat, startTickInBeat);
        tempoMap.tickToBarBeat (loop.endTick, endBar, endBeat, endTickInBeat);

        loopLabel.setText (juce::String (startBar) + "." + juce::String (startBeat)
                               + NI_TXT (" → ") + juce::String (endBar) + "."
                               + juce::String (endBeat),
                           juce::dontSendNotification);
        loopLabel.setColour (juce::Label::textColourId,
                             loop.enabled ? theme::text : theme::dimText);
    }
    else
    {
        loopLabel.setText (NI_TXT ("未设置"), juce::dontSendNotification);
        loopLabel.setColour (juce::Label::textColourId, theme::dimText);
    }

    // ---- metronome ----------------------------------------------------------
    metronomeButton.setToggleState (document.getProject().view.metronomeEnabled,
                                    juce::dontSendNotification);

    deviceLabel.setText (audio.getDeviceDescription(), juce::dontSendNotification);
}

void TransportBar::toggleLoop()
{
    const auto& current = document.getProject().loop;

    if (current.isValid())
    {
        const auto shouldEnable = ! current.enabled;

        document.applyEdit (shouldEnable ? NI_TXT ("开启循环") : NI_TXT ("关闭循环"),
                            [shouldEnable] (Project& project)
        {
            project.loop.enabled = shouldEnable;
        });
    }
    else
    {
        // Nothing to loop yet: cycle the bar the playhead is in, and turn it on.
        const music::TempoMap tempoMap (document.getProject().tempoMarkers);
        const auto tick = tempoMap.tickAtSeconds (audio.getPositionSeconds());
        const auto bar  = juce::jmax (1, tempoMap.barAtTick (tick));
        const auto start = tempoMap.barStartTick (bar);
        const auto end   = tempoMap.barStartTick (bar + 1);

        document.applyEdit (NI_TXT ("设置循环区间"), [start, end] (Project& project)
        {
            project.loop.startTick = start;
            project.loop.endTick   = end;
            project.loop.enabled   = true;
        });
    }

    audio.refreshTransportAids();
    refresh();
}

void TransportBar::toggleMetronome()
{
    const auto shouldClick = ! document.getProject().view.metronomeEnabled;

    document.applyEdit (shouldClick ? NI_TXT ("开启节拍器") : NI_TXT ("关闭节拍器"),
                        [shouldClick] (Project& project)
    {
        project.view.metronomeEnabled = shouldClick;
    });

    audio.refreshTransportAids();
    refresh();
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    const auto buttonWidth = 96;

    returnButton.setBounds (area.removeFromLeft (116).reduced (0, 10));
    area.removeFromLeft (6);
    playButton.setBounds (area.removeFromLeft (buttonWidth).reduced (0, 10));
    area.removeFromLeft (6);
    stopButton.setBounds (area.removeFromLeft (buttonWidth).reduced (0, 10));

    area.removeFromLeft (12);
    loopButton.setBounds (area.removeFromLeft (84).reduced (0, 10));
    area.removeFromLeft (6);
    metronomeButton.setBounds (area.removeFromLeft (84).reduced (0, 10));

    area.removeFromLeft (12);
    recordButton.setBounds (area.removeFromLeft (92).reduced (0, 10));
    area.removeFromLeft (4);
    recordTargetButton.setBounds (area.removeFromLeft (84).reduced (0, 10));

    area.removeFromLeft (18);

    auto positionArea = area.removeFromLeft (170);
    positionCaption.setBounds (positionArea.removeFromTop (14));
    positionLabel.setBounds (positionArea);

    area.removeFromLeft (10);

    auto tempoArea = area.removeFromLeft (130);
    tempoCaption.setBounds (tempoArea.removeFromTop (14));
    tempoLabel.setBounds (tempoArea);

    area.removeFromLeft (10);

    auto loopArea = area.removeFromLeft (150);
    loopCaption.setBounds (loopArea.removeFromTop (14));
    loopLabel.setBounds (loopArea);

    auto right = area;
    settingsButton.setBounds (right.removeFromRight (92).reduced (0, 10));
    right.removeFromRight (6);
    virtualKeyboardButton.setBounds (right.removeFromRight (92).reduced (0, 10));
    right.removeFromRight (6);
    midiInputButton.setBounds (right.removeFromRight (88).reduced (0, 10));
    right.removeFromRight (8);
    deviceLabel.setBounds (right);
}

void TransportBar::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        audio.getDeviceManager(), 0, 0, 1, 2, false, false, true, false);

    selector->setSize (480, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = NI_TXT ("音频设置（WASAPI）");
    options.dialogBackgroundColour = theme::panelBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

} // namespace hmdaw
