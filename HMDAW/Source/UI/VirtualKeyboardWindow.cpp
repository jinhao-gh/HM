#include "VirtualKeyboardWindow.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"

#include <algorithm>

namespace hmdaw
{

namespace
{
    constexpr int kMargin = 8;
    constexpr int kRowHeight = 24;
    constexpr int kRowGap = 4;
    constexpr int kKeyboardHeight = 90;
    constexpr float kKeyWidth = 18.0f;   // 52 white keys fit the design width

    juce::File& settingsFileOverride()
    {
        static juce::File file;
        return file;
    }

    juce::String noteNameOf (int midiNote)
    {
        return midiNote >= 0 ? midimap::noteName (midiNote) : juce::String ("—");
    }
}

//==============================================================================
LetteredKeyboard::LetteredKeyboard (juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
}

void LetteredKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                      juce::Rectangle<float> area, bool isDown, bool isOver,
                                      juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver,
                                                lineColour, textColour);
    drawLetters (g, midiNoteNumber, area, textColour);
}

void LetteredKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                                      juce::Rectangle<float> area, bool isDown, bool isOver,
                                      juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (midiNoteNumber, g, area, isDown, isOver,
                                                noteFillColour);
    drawLetters (g, midiNoteNumber, area, juce::Colours::white.withAlpha (0.75f));
}

void LetteredKeyboard::drawLetters (juce::Graphics& g, int midiNote, juce::Rectangle<float> area,
                                    juce::Colour colour)
{
    if (lettersForNote == nullptr)
        return;

    const auto text = lettersForNote (midiNote);

    if (text.isEmpty())
        return;

    // The white keys already carry the note's own name at the bottom (JUCE draws
    // it), so the computer keys go just above that strip; on a black key there is
    // no such label and they can use the lower part of the key.
    auto strip = area.removeFromBottom (area.getHeight() * 0.42f);

    if (midiNote >= 0 && ! music::piano::isBlackKey (midiNote))
        strip = strip.removeFromTop (strip.getHeight() * 0.45f);

    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (juce::jlimit (7.0f, 10.5f, area.getWidth() * 0.45f))));
    g.drawFittedText (text, strip.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
void ComputerKeyboardView::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    if (getCaps == nullptr)
        return;

    const auto caps = getCaps();

    if (caps.empty())
        return;

    const auto area = getLocalBounds().reduced (4, 2).toFloat();
    const auto unit = area.getWidth() / kBoardUnits;
    const auto rowHeight = area.getHeight() / kBoardRows;

    for (const auto& cap : caps)
    {
        const auto rect = juce::Rectangle<float> (area.getX() + cap.x * unit,
                                                  area.getY() + (float) cap.row * rowHeight,
                                                  cap.width * unit, rowHeight).reduced (1.0f, 1.5f);
        const auto note = noteForCap != nullptr ? noteForCap (cap) : -1;
        const auto mapped = note >= 0;
        // 白键显示白色、黑键显示黑色: the cap is painted the way the piano key it
        // plays is painted.
        const auto black = mapped && music::piano::isBlackKey (note);

        auto fill = theme::panelRaised;
        auto textColour = theme::dimText.withAlpha (0.7f);

        if (mapped)
        {
            fill = black ? juce::Colour (0xff17171b) : juce::Colour (0xffe6e6ee);
            textColour = black ? juce::Colour (0xffe6e6ee) : juce::Colour (0xff17171b);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (rect, 2.5f);

        if (mapped && isNoteDown != nullptr && isNoteDown (note))
        {
            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillRoundedRectangle (rect, 2.5f);
        }

        g.setColour (theme::outline);
        g.drawRoundedRectangle (rect.reduced (0.5f), 2.5f, 1.0f);

        auto text = rect.reduced (2.0f, 1.0f);

        if (mapped && text.getWidth() >= 30.0f && text.getHeight() >= 26.0f)
        {
            auto noteStrip = text.removeFromBottom (text.getHeight() * 0.38f);
            g.setColour (textColour.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.drawFittedText (noteNameOf (note), noteStrip.toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
        }

        g.setColour (textColour);
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (9.0f, 13.0f,
                                                               rect.getHeight() * 0.5f), true)));
        g.drawFittedText (keyboard::keyName (cap.key), text.toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
VirtualKeyboardPanel::VirtualKeyboardPanel (ProjectDocument& documentToUse,
                                            AppState& appStateToUse)
    : document (documentToUse),
      appState (appStateToUse),
      keyboard (keyboardState),
      arpControls (documentToUse)
{
    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (keyboard::kLowestNote, keyboard::kHighestNote);
    keyboard.setKeyWidth (kKeyWidth);
    keyboard.setScrollButtonsVisible (true);
    keyboard.setLowestVisibleKey (keyboard::kLowestNote);
    keyboard.setOctaveForMiddleC (4);             // HM calls MIDI 60 "C4"
    keyboard.setWantsKeyboardFocus (true);
    keyboard.setVelocity (settings.velocity / 127.0f, false);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId,
                        juce::Colour (0xffe6e6ee));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId,
                        juce::Colour (0xff17171b));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::outline);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accentDim.withAlpha (0.5f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dimText);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId,
                        theme::windowBackground.withAlpha (0.4f));

    // The letter on a key comes from the same table that built the key mapping, so
    // the picture and the keyboard can never drift apart.
    keyboard.lettersForNote = [this] (int midiNote)
    {
        return keyboard::keyLettersFor (settings.layout, settings.melodyShift,
                                        settings.bassShift, midiNote);
    };

    addAndMakeVisible (diagram);
    diagram.getCaps = [this] { return keyboard::capsFor (settings.layout); };
    diagram.noteForCap = [this] (const keyboard::Cap& cap)
    {
        return keyboard::noteForCap (settings.layout, cap, settings.melodyShift,
                                     settings.bassShift);
    };
    diagram.isNoteDown = [this] (int midiNote) { return isNoteHeld (midiNote); };

    // The computer keyboard is the panel's own business (see keyStateChanged), so the
    // piano component keeps no key mappings at all: one table, one place that plays.
    keyboard.clearKeyMappings();
    keyboardState.addListener (this);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (velocityCaption);
    velocityCaption.setText (NI_TXT ("力度"), juce::dontSendNotification);
    velocityCaption.setFont (theme::font (11.5f));
    velocityCaption.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (velocitySlider);
    velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.setValue (settings.velocity, juce::dontSendNotification);
    velocitySlider.onValueChange = [this]
    {
        settings.velocity = juce::jlimit (1, 127, (int) velocitySlider.getValue());
        keyboard.setVelocity ((float) settings.velocity / 127.0f, false);

        if (onWindowStateChanged != nullptr)
            onWindowStateChanged();
    };

    addAndMakeVisible (layoutBox);
    layoutBox.setTooltip (NI_TXT ("电脑键盘布局：常规（两排）/ 巴扬（三排旋律 + 数字排低音）"));
    layoutBox.addItem (keyboard::layoutName (keyboard::Layout::normal), 1);
    layoutBox.addItem (keyboard::layoutName (keyboard::Layout::bayan), 2);
    layoutBox.setSelectedId (1, juce::dontSendNotification);
    layoutBox.onChange = [this]
    {
        if (layoutBox.getSelectedId() == 2)
            setLayout (keyboard::Layout::bayan);
        else
            setLayout (keyboard::Layout::normal);
    };

    const auto setupShift = [this] (juce::TextButton& button, const juce::String& text,
                                    const juce::String& tooltip, int step, bool melody)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
        button.setTooltip (tooltip);
        button.onClick = [this, step, melody]
        {
            if (melody)
                shiftMelody (step);
            else
                shiftBass (step);
        };
    };

    setupShift (melodySemiDown, juce::String ("-1"), NI_TXT ("旋律排往下 1 个半音"), -1, true);
    setupShift (melodySemiUp, juce::String ("+1"), NI_TXT ("旋律排往上 1 个半音"), 1, true);
    setupShift (melodyOctDown, juce::String ("-12"), NI_TXT ("旋律排往下一个八度"), -12, true);
    setupShift (melodyOctUp, juce::String ("+12"), NI_TXT ("旋律排往上一个八度"), 12, true);
    setupShift (bassSemiDown, juce::String ("-1"), NI_TXT ("低音排往下 1 个半音"), -1, false);
    setupShift (bassSemiUp, juce::String ("+1"), NI_TXT ("低音排往上 1 个半音"), 1, false);
    setupShift (bassFifthDown, juce::String ("-5"), NI_TXT ("低音排往下一个五度"), -7, false);
    setupShift (bassFifthUp, juce::String ("+5"), NI_TXT ("低音排往上一个五度"), 7, false);
    setupShift (bassOctDown, juce::String ("-12"), NI_TXT ("低音排往下一个八度"), -12, false);
    setupShift (bassOctUp, juce::String ("+12"), NI_TXT ("低音排往上一个八度"), 12, false);

    const auto setupCaption = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);
    };

    setupCaption (melodyCaption, NI_TXT ("旋律"));
    setupCaption (bassCaption, NI_TXT ("低音"));

    for (auto* label : { &melodyReadout, &bassReadout })
    {
        addAndMakeVisible (*label);
        label->setFont (theme::font (11.5f));
        label->setColour (juce::Label::textColourId, theme::text);
        label->setJustificationType (juce::Justification::centredLeft);
    }

    addAndMakeVisible (pinButton);
    pinButton.setClickingTogglesState (true);
    pinButton.setButtonText (NI_TXT ("置顶"));
    pinButton.setTooltip (NI_TXT ("让虚拟键盘浮在其它窗口之上（关掉就是普通窗口）"));
    pinButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    pinButton.onClick = [this] { setPinned (pinButton.getToggleState()); };

    addAndMakeVisible (collapseButton);
    collapseButton.setButtonText (NI_TXT ("折叠"));
    collapseButton.setTooltip (NI_TXT ("收成一个小窗：只留电脑键盘图示"));
    collapseButton.onClick = [this] { setCollapsed (! settings.collapsed); };

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (theme::font (11.5f));
    statusLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (arpControls);
    arpControls.onChanged = [this]
    {
        updateStatus();

        if (onArpSettingsChanged != nullptr)
            onArpSettingsChanged();
    };

    updateControls();
    updateStatus();
    updatePollingTimer();
}

void VirtualKeyboardPanel::visibilityChanged()
{
    // The typing poll is a *poll*: while the window is on screen the layout is read
    // from the system every tick, so a key whose event never arrives (and a third
    // key held with two others) is still seen.
    updatePollingTimer();
}

void VirtualKeyboardPanel::updatePollingTimer()
{
    if (isShowing())
        startTimerHz (30);
    else
        stopTimer();
}

VirtualKeyboardPanel::~VirtualKeyboardPanel()
{
    // Everything this window started goes with it.
    releaseWindowKeys();
    keyboardState.removeListener (this);
}

void VirtualKeyboardPanel::setSettings (const keyboard::Settings& wanted)
{
    settings = wanted;
    keyboard::clamp (settings);

    keyboard.setVelocity ((float) settings.velocity / 127.0f, false);
    updateControls();
    updateStatus();
    repaint();
}

void VirtualKeyboardPanel::releaseWindowKeys()
{
    // The computer keys first, then everything else this window started. Both go
    // through the keyboard state, so the owner hears each note-off and the engine
    // really stops them - each one with the source it came from, so the owner
    // releases that entry and nobody else's.
    releaseTypingKeys();

    const auto started = startedNotes;
    startedNotes.clear();

    for (const auto& [trigger, note] : started)
    {
        const juce::ScopedValueSetter<juce::int64> source (currentTrigger, trigger);
        keyboardState.noteOff (1, note, 0.0f);
    }
}

void VirtualKeyboardPanel::noteStarted (juce::int64 trigger, int midiNote)
{
    for (const auto& [existingTrigger, existingNote] : startedNotes)
        if (existingTrigger == trigger && existingNote == midiNote)
            return;

    startedNotes.push_back ({ trigger, midiNote });
}

void VirtualKeyboardPanel::noteStopped (juce::int64 trigger, int midiNote)
{
    startedNotes.erase (std::remove (startedNotes.begin(), startedNotes.end(),
                                     std::make_pair (trigger, midiNote)),
                        startedNotes.end());
}

void VirtualKeyboardPanel::setLayout (keyboard::Layout layout)
{
    if (settings.layout == layout)
        return;

    releaseWindowKeys();
    settings.layout = layout;
    keyboard::clamp (settings);
    updateControls();
    updateStatus();
    repaint();

    if (onWindowStateChanged != nullptr)
        onWindowStateChanged();
}

void VirtualKeyboardPanel::shiftMelody (int step)
{
    const auto range = keyboard::melodyShiftRange (settings.layout);
    const auto wanted = juce::jlimit (range.lowest, range.highest, settings.melodyShift + step);

    if (wanted == settings.melodyShift)
        return;

    releaseWindowKeys();
    settings.melodyShift = wanted;
    updateControls();
    updateStatus();
    repaint();

    if (onWindowStateChanged != nullptr)
        onWindowStateChanged();
}

void VirtualKeyboardPanel::shiftBass (int step)
{
    const auto range = keyboard::bassShiftRange();
    const auto wanted = juce::jlimit (range.lowest, range.highest, settings.bassShift + step);

    if (wanted == settings.bassShift)
        return;

    releaseWindowKeys();
    settings.bassShift = wanted;
    updateControls();
    updateStatus();
    repaint();

    if (onWindowStateChanged != nullptr)
        onWindowStateChanged();
}

void VirtualKeyboardPanel::setCollapsed (bool shouldCollapse)
{
    if (settings.collapsed == shouldCollapse)
        return;

    releaseWindowKeys();
    settings.collapsed = shouldCollapse;
    updateControls();
    resized();

    if (onWindowStateChanged != nullptr)
        onWindowStateChanged();
}

void VirtualKeyboardPanel::setPinned (bool shouldPin)
{
    if (settings.alwaysOnTop == shouldPin)
        return;

    settings.alwaysOnTop = shouldPin;
    updateControls();

    if (onWindowStateChanged != nullptr)
        onWindowStateChanged();
}

void VirtualKeyboardPanel::updateControls()
{
    layoutBox.setSelectedId (settings.layout == keyboard::Layout::bayan ? 2 : 1,
                             juce::dontSendNotification);

    pinButton.setToggleState (settings.alwaysOnTop, juce::dontSendNotification);
    collapseButton.setButtonText (settings.collapsed ? NI_TXT ("展开") : NI_TXT ("折叠"));

    // 折叠: only the control row and the diagram stay.
    const auto expanded = ! settings.collapsed;
    velocityCaption.setVisible (expanded);
    velocitySlider.setVisible (expanded);
    statusLabel.setVisible (expanded);
    keyboard.setVisible (expanded);
    arpControls.setVisible (expanded);
    melodyCaption.setVisible (expanded);
    bassCaption.setVisible (expanded);

    const auto bassApplies = settings.layout == keyboard::Layout::bayan;

    for (auto* button : { &bassSemiDown, &bassSemiUp, &bassFifthDown, &bassFifthUp,
                          &bassOctDown, &bassOctUp })
    {
        button->setEnabled (bassApplies);
        button->setAlpha (bassApplies ? 1.0f : 0.45f);
    }

    bassReadout.setEnabled (bassApplies);
    bassReadout.setAlpha (bassApplies ? 1.0f : 0.45f);

    const auto melodyBase = keyboard::baseNoteFor (settings.layout, settings.melodyShift);
    const auto melodyText = juce::String (settings.melodyShift) + NI_TXT (" · ")
                                + noteNameOf (melodyBase) + NI_TXT (" 起");

    melodyReadout.setText (expanded ? melodyText : juce::String (settings.melodyShift),
                           juce::dontSendNotification);

    const auto bassFive = keyboard::bassNoteForKey (U'5', settings.bassShift);
    const auto bassSix = keyboard::bassNoteForKey (U'6', settings.bassShift);

    bassReadout.setText (expanded
                             ? (juce::String (settings.bassShift) + NI_TXT (" · 5=")
                                    + noteNameOf (bassFive) + NI_TXT (" 6=") + noteNameOf (bassSix))
                             : (bassApplies ? juce::String (settings.bassShift) : juce::String ("—")),
                         juce::dontSendNotification);

}

void VirtualKeyboardPanel::refresh()
{
    arpControls.refresh();
    updateControls();
    updateStatus();
}

void VirtualKeyboardPanel::updateStatus()
{
    const auto pianoMode = document.viewState().editorMode == EditorMode::piano;
    const auto* track = document.getProject().findTrack (appState.selectedTrackId);

    auto text = NI_TXT ("轨道 ")
                    + (track != nullptr ? track->name : NI_TXT ("（未选中）"))
                    + NI_TXT (" · 最近力度 ") + juce::String (lastVelocity)
                    + NI_TXT (" · ")
                    + (pianoMode ? NI_TXT ("按键走十二平均律（钢琴窗模式）")
                                 : NI_TXT ("按键走 MIDI 映射（参考线模式）"))
                    + NI_TXT (" · ") + arpControls.getSummaryForTest();

    // While computer keys are held, name the ones the *system* reports. If somebody
    // holds three and only two are listed here, the keyboard itself is not sending
    // the third (its key-rolling is limited) - which is exactly the thing that looks
    // like "a conflict" when a chord will not play.
    if (! typingDown.empty())
        text << NI_TXT (" · 系统报告按下的键：") << typingKeysText();

    statusLabel.setText (text, juce::dontSendNotification);
}

void VirtualKeyboardPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
}

void VirtualKeyboardPanel::resized()
{
    auto area = getLocalBounds().reduced (kMargin, 6);

    const auto placeShiftPair = [] (juce::Rectangle<int>& row, juce::TextButton& down,
                                    juce::TextButton& up, int width)
    {
        down.setBounds (row.removeFromLeft (width).reduced (1, 1));
        up.setBounds (row.removeFromLeft (width).reduced (1, 1));
    };

    if (settings.collapsed)
    {
        auto row = area.removeFromTop (kRowHeight);

        layoutBox.setBounds (row.removeFromLeft (70).reduced (0, 1));
        row.removeFromLeft (6);
        placeShiftPair (row, melodySemiDown, melodySemiUp, 20);
        placeShiftPair (row, melodyOctDown, melodyOctUp, 20);
        melodyReadout.setBounds (row.removeFromLeft (44));
        row.removeFromLeft (6);
        placeShiftPair (row, bassSemiDown, bassSemiUp, 20);
        placeShiftPair (row, bassFifthDown, bassFifthUp, 20);
        placeShiftPair (row, bassOctDown, bassOctUp, 20);
        bassReadout.setBounds (row.removeFromLeft (44));
        row.removeFromLeft (6);
        pinButton.setBounds (row.removeFromRight (40).reduced (0, 1));
        row.removeFromRight (4);
        collapseButton.setBounds (row.removeFromRight (44).reduced (0, 1));

        area.removeFromTop (4);
        diagram.setBounds (area);
        return;
    }

    auto firstRow = area.removeFromTop (kRowHeight);
    velocityCaption.setBounds (firstRow.removeFromLeft (34));
    velocitySlider.setBounds (firstRow.removeFromLeft (200).reduced (0, 2));
    firstRow.removeFromLeft (10);
    pinButton.setBounds (firstRow.removeFromRight (60).reduced (0, 1));
    firstRow.removeFromRight (6);
    collapseButton.setBounds (firstRow.removeFromRight (60).reduced (0, 1));
    firstRow.removeFromRight (10);
    statusLabel.setBounds (firstRow);

    area.removeFromTop (kRowGap);

    auto secondRow = area.removeFromTop (kRowHeight);
    layoutBox.setBounds (secondRow.removeFromLeft (92).reduced (0, 1));
    secondRow.removeFromLeft (10);
    melodyCaption.setBounds (secondRow.removeFromLeft (34));
    placeShiftPair (secondRow, melodySemiDown, melodySemiUp, 30);
    placeShiftPair (secondRow, melodyOctDown, melodyOctUp, 34);
    melodyReadout.setBounds (secondRow.removeFromLeft (96));
    secondRow.removeFromLeft (12);
    bassCaption.setBounds (secondRow.removeFromLeft (34));
    placeShiftPair (secondRow, bassSemiDown, bassSemiUp, 30);
    placeShiftPair (secondRow, bassFifthDown, bassFifthUp, 30);
    placeShiftPair (secondRow, bassOctDown, bassOctUp, 30);
    bassReadout.setBounds (secondRow.removeFromLeft (110));

    area.removeFromTop (kRowGap);
    keyboard.setBounds (area.removeFromTop (kKeyboardHeight));
    area.removeFromTop (kRowGap);

    arpControls.setBounds (area.removeFromBottom (ArpControls::kDesignHeight));
    area.removeFromBottom (kRowGap);
    diagram.setBounds (area);
}

bool VirtualKeyboardPanel::keyPressed (const juce::KeyPress& key)
{
    // Nothing of the panel's own: every computer key belongs to the keyboard, and
    // the shift buttons are the controls for shifting.
    juce::ignoreUnused (key);
    return false;
}

bool VirtualKeyboardPanel::keyStateChanged (bool isKeyDown)
{
    juce::ignoreUnused (isKeyDown);
    return pollTyping();
}

bool VirtualKeyboardPanel::keyPressed (const juce::KeyPress&, juce::Component*)
{
    return false;   // every key press belongs to the keyboard, not to the panel
}

bool VirtualKeyboardPanel::keyStateChanged (bool isKeyDown, juce::Component*)
{
    juce::ignoreUnused (isKeyDown);
    return pollTyping();
}

bool VirtualKeyboardPanel::pollTyping()
{

    // This is the computer-keyboard playing, and it is deliberately *here* rather
    // than on the piano component: the event bubbles up the parent chain, so the
    // keys work while the focus is on any control of this window - no need to click
    // the piano first. It also means the panel owns the "which keys are down" state,
    // which is what lets the watchdog below clear a note whose key-up went missing.
    const auto used = ! typingDown.empty();
    updateTyping();

    return used || ! typingDown.empty();
}

void VirtualKeyboardPanel::timerCallback()
{
    runTypingWatchdogForTest();
}

void VirtualKeyboardPanel::runTypingWatchdogForTest()
{
    // ① A key that is no longer physically down must not keep sounding: this is the
    //    self healing path for a key-up that never reached us (the focus moved, the
    //    window went behind another one, a control took the keyboard...).
    updateTyping();

    // ② Leaving the application ends it: a window nobody is looking at cannot be
    //    typed into. Having the focus in *another* HM window is not leaving - the
    //    keys keep playing there, so a patch can be auditioned with a knob under
    //    the mouse.
    if (! typingDown.empty() && ! typingIsLive())
        releaseTypingKeys();
}

juce::String VirtualKeyboardPanel::typingKeysText() const
{
    juce::String text;

    for (const auto& key : typingKeys())
        if (typingDown.count (key.keyCode) > 0)
        {
            if (text.isNotEmpty())
                text << ' ';

            text << keyboard::keyName ((juce::juce_wchar) key.keyCode);
        }

    return text;
}

std::vector<VirtualKeyboardPanel::TypingKey> VirtualKeyboardPanel::typingKeys() const
{
    std::vector<TypingKey> keys;

    for (const auto& cap : keyboard::capsFor (settings.layout))
    {
        const auto note = keyboard::noteForCap (settings.layout, cap, settings.melodyShift,
                                                settings.bassShift);

        if (note >= 0 && note <= 127)
            keys.push_back ({ (int) cap.key, note });
    }

    return keys;
}

bool VirtualKeyboardPanel::isTypingKey (int keyCode) const
{
    for (const auto& key : typingKeys())
        if (key.keyCode == keyCode)
            return true;

    return false;
}

bool VirtualKeyboardPanel::keyIsDown (int keyCode) const
{
    if (forcedKeyDown.count (keyCode) > 0)
        return true;   // self test: the fake system says this one is down

    return juce::KeyPress::isKeyCurrentlyDown (keyCode);
}

bool VirtualKeyboardPanel::typingIsLive() const
{
    return typingIsLiveForTest (juce::Component::getCurrentlyFocusedComponent());
}

bool VirtualKeyboardPanel::typingIsLiveForTest (juce::Component* focused) const
{
    // A window that is not on screen cannot be played from the computer keys.
    if (! isShowing())
        return false;

    // Self test: the user switched to another application.
    if (forceWindowUnfocused)
        return false;

    // A text box keeps the keys, wherever it lives: typing a velocity or a name must
    // not play notes.
    if (isInsideTextBox (focused))
        return false;

    // Nothing at all has the focus (a window that was never clicked, the offscreen
    // self test), this window has it, or the focus is inside it: play.
    if (focused == nullptr || focused == this || isParentOf (focused))
        return true;

    // The focus sits in another window of this application - the synth editor, the
    // mixer, an effects window. Keep playing, so a sound can be auditioned while a
    // knob is turned; the moment HM itself goes to the background it stops.
    return applicationIsForeground();
}

bool VirtualKeyboardPanel::applicationIsForeground() const
{
    if (forceAppForeground >= 0)
        return forceAppForeground != 0;

    return juce::Process::isForegroundProcess();
}

bool VirtualKeyboardPanel::isInsideTextBox (juce::Component* component)
{
    for (; component != nullptr; component = component->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (component) != nullptr)
            return true;

    return false;
}

void VirtualKeyboardPanel::pollTypingNow()
{
    pollTyping();
}

void VirtualKeyboardPanel::updateTyping()
{
    // No notes while HM is in the background, while the window is off screen, or
    // while a text box has the keys: typing a number into the velocity box must not
    // play notes (the same rule the space bar follows).
    if (! typingIsLive())
    {
        releaseTypingKeys();
        return;
    }

    const auto keys = typingKeys();
    const auto velocity = (float) juce::jlimit (1, 127, settings.velocity);

    for (const auto& key : keys)
    {
        const auto down = keyIsDown (key.keyCode);
        const auto wasDown = typingDown.count (key.keyCode) > 0;

        if (down == wasDown)
            continue;

        // Which source this is: the panel is playing a computer key, and each key is
        // its own source (two keys on the same note sound twice).
        const juce::ScopedValueSetter<juce::int64> trigger (currentTrigger,
                                                            keyboard::typingTriggerFor (key.keyCode));

        if (down)
        {
            typingDown.insert (key.keyCode);
            keyboardState.noteOn (1, key.midiNote, velocity / 127.0f);
        }
        else
        {
            typingDown.erase (key.keyCode);
            keyboardState.noteOff (1, key.midiNote, 0.0f);
        }
    }

}

void VirtualKeyboardPanel::releaseTypingKeys()
{
    const auto keys = typingDown;
    typingDown.clear();
    forcedKeyDown.clear();   // the self test's fake system state goes with them

    for (const auto keyCode : keys)
    {
        const juce::ScopedValueSetter<juce::int64> source (currentTrigger,
                                                          keyboard::typingTriggerFor (keyCode));

        for (const auto& key : typingKeys())
            if (key.keyCode == keyCode)
                keyboardState.noteOff (1, key.midiNote, 0.0f);
    }
}

void VirtualKeyboardPanel::setKeyDownForTest (int keyChar, bool isDown)
{
    if (isDown)
        forcedKeyDown.insert (keyChar);
    else
        forcedKeyDown.erase (keyChar);

    updateTyping();
}

void VirtualKeyboardPanel::setWindowFocusedForTest (bool isFocused)
{
    forceWindowUnfocused = ! isFocused;
}

void VirtualKeyboardPanel::setApplicationForegroundForTest (bool isForeground)
{
    forceAppForeground = isForeground ? 1 : 0;
}

void VirtualKeyboardPanel::simulateMissedKeyUpForTest (int keyChar)
{
    // Deliberately no updateTyping(): the point is that no event ever arrives.
    forcedKeyDown.erase (keyChar);
}

void VirtualKeyboardPanel::handleNoteOn (juce::MidiKeyboardState*, int, int midiNoteNumber,
                                         float velocity)
{
    lastVelocity = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));

    // Our own "these keys are down" must not be mistaken for a new press.
    if (! applyingHeldKeys)
    {
        noteStarted (currentTrigger, midiNoteNumber);

        if (onNoteOn != nullptr)
            onNoteOn (midiNoteNumber, velocity * 127.0f, currentTrigger);
    }

    updateStatus();
}

void VirtualKeyboardPanel::handleNoteOff (juce::MidiKeyboardState*, int, int midiNoteNumber,
                                          float)
{
    if (! applyingHeldKeys)
    {
        noteStopped (currentTrigger, midiNoteNumber);

        if (onNoteOff != nullptr)
            onNoteOff (midiNoteNumber, currentTrigger);
    }

    updateStatus();
}

void VirtualKeyboardPanel::setKeysDown (const std::vector<int>& notes)
{
    heldKeys = notes;

    const juce::ScopedValueSetter<bool> guard (applyingHeldKeys, true);

    for (int note = keyboard::kLowestNote; note <= keyboard::kHighestNote; ++note)
    {
        const auto shouldBeDown = std::find (notes.begin(), notes.end(), note) != notes.end();
        const auto isDown = keyboardState.isNoteOn (1, note);

        if (shouldBeDown && ! isDown)
            keyboardState.noteOn (1, note, 100.0f);
        else if (! shouldBeDown && isDown)
            keyboardState.noteOff (1, note, 0.0f);
    }

    diagram.repaint();
}

void VirtualKeyboardPanel::setLastVelocity (int velocity)
{
    lastVelocity = juce::jlimit (1, 127, velocity);
    updateStatus();
}

bool VirtualKeyboardPanel::isNoteHeld (int midiNote) const
{
    return std::find (heldKeys.begin(), heldKeys.end(), midiNote) != heldKeys.end();
}

bool VirtualKeyboardPanel::isKeyDownForTest (int midiNote) const
{
    return keyboardState.isNoteOn (1, midiNote);
}

int VirtualKeyboardPanel::typingNoteForTest (int keyChar) const
{
    return keyboard::melodyNoteFor (settings.layout, (juce::juce_wchar) keyChar,
                                    settings.melodyShift);
}

int VirtualKeyboardPanel::bassNoteForTest (int keyChar) const
{
    if (settings.layout != keyboard::Layout::bayan)
        return -1;

    return keyboard::bassNoteForKey ((juce::juce_wchar) keyChar, settings.bassShift);
}

juce::String VirtualKeyboardPanel::getPianoLetterForTest (int midiNote) const
{
    return keyboard::keyLettersFor (settings.layout, settings.melodyShift, settings.bassShift,
                                    midiNote);
}

bool VirtualKeyboardPanel::isComponentVisibleForTest (int which) const
{
    switch (which)
    {
        case 0: return keyboard.isVisible();
        case 1: return diagram.isVisible();
        case 2: return arpControls.isVisible();
        case 3: return velocitySlider.isVisible();
        case 4: return statusLabel.isVisible();
        default: break;
    }

    return false;
}

void VirtualKeyboardPanel::pressKeyForTest (int midiNote, float velocity)
{
    keyboardState.noteOn (1, midiNote, juce::jlimit (1.0f, 127.0f, velocity) / 127.0f);
}

void VirtualKeyboardPanel::releaseKeyForTest (int midiNote)
{
    keyboardState.noteOff (1, midiNote, 0.0f);
}

namespace
{
    /** A synthetic mouse event on the piano, for the self test's "click it as the
        user would" checks. */
    juce::MouseEvent pianoMouseEvent (juce::Component& piano, juce::Point<float> position,
                                      bool wasDragged)
    {
        return { juce::Desktop::getInstance().getMainMouseSource(),
                 position, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &piano, &piano, juce::Time::getCurrentTime(), position,
                 juce::Time::getCurrentTime(), 1, wasDragged };
    }
}

void VirtualKeyboardPanel::pressKeyWithMouseForTest (int midiNote)
{
    const auto centre = keyboard.getRectangleForKey (midiNote).getCentre().toFloat();
    const auto event = pianoMouseEvent (keyboard, centre, false);

    keyboard.mouseDown (event);
}

void VirtualKeyboardPanel::releaseKeyWithMouseForTest (int midiNote)
{
    const auto centre = keyboard.getRectangleForKey (midiNote).getCentre().toFloat();
    const auto event = pianoMouseEvent (keyboard, centre, false);

    keyboard.mouseUp (event);
}

void VirtualKeyboardPanel::setLayoutForTest (int comboId)
{
    layoutBox.setSelectedId (comboId, juce::sendNotificationSync);
}

void VirtualKeyboardPanel::shiftMelodyForTest (int step)
{
    shiftMelody (step);
}

void VirtualKeyboardPanel::shiftBassForTest (int step)
{
    shiftBass (step);
}

void VirtualKeyboardPanel::toggleCollapseForTest()
{
    setCollapsed (! settings.collapsed);
}

void VirtualKeyboardPanel::togglePinForTest()
{
    setPinned (! settings.alwaysOnTop);
}

//==============================================================================
VirtualKeyboardWindow::VirtualKeyboardWindow (ProjectDocument& document, AppState& appState)
    : juce::DocumentWindow (NI_TXT ("虚拟键盘"), theme::panelBackground,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);

    panel = new VirtualKeyboardPanel (document, appState);

    panel->onNoteOn = [this] (int midiNote, float velocity, juce::int64 trigger)
    {
        if (onNoteOn != nullptr)
            onNoteOn (midiNote, velocity, trigger);
    };

    panel->onNoteOff = [this] (int midiNote, juce::int64 trigger)
    {
        if (onNoteOff != nullptr)
            onNoteOff (midiNote, trigger);
    };

    panel->onArpSettingsChanged = [this]
    {
        if (onArpSettingsChanged != nullptr)
            onArpSettingsChanged();
    };

    panel->onWindowStateChanged = [this]
    {
        applyWindowState();
        saveSettings();
    };

    setContentOwned (panel, false);

    // The panel listens to the window's key events as well: `toFront()` gives the
    // keyboard focus to the window itself (not to a child), and this way the computer
    // keys reach the panel whichever of the two has it.
    addKeyListener (panel);
    panelIsKeyListener = true;

    // The remembered state (collapsed / pinned / layout / shifts / velocity).
    const auto file = getSettingsFile();
    auto settings = keyboard::Settings();

    if (file.existsAsFile())
        settings = keyboard::settingsFromVar (juce::JSON::parse (file.loadFileAsString()));

    panel->setSettings (settings);
    applyWindowState();
}

VirtualKeyboardWindow::~VirtualKeyboardWindow()
{
    panel = nullptr;
}

juce::File VirtualKeyboardWindow::getDefaultSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HMDAW")
               .getChildFile ("keyboard.json");
}

juce::File VirtualKeyboardWindow::getSettingsFile()
{
    return settingsFileOverride() != juce::File() ? settingsFileOverride()
                                                  : getDefaultSettingsFile();
}

void VirtualKeyboardWindow::setSettingsFileForTesting (juce::File file)
{
    settingsFileOverride() = std::move (file);
}

juce::String VirtualKeyboardWindow::getSettingsFileTextForTest() const
{
    const auto file = getSettingsFile();
    return file.existsAsFile() ? file.loadFileAsString() : juce::String();
}

void VirtualKeyboardWindow::saveSettings() const
{
    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (keyboard::settingsToVar (panel->getSettings()), false),
                          false, false, "\n");
}

void VirtualKeyboardWindow::applyWindowState()
{
    if (panel == nullptr)
        return;

    const auto& settings = panel->getSettings();

    setAlwaysOnTop (settings.alwaysOnTop);

    const auto wantedWidth = settings.collapsed ? VirtualKeyboardPanel::kCollapsedWidth
                                                : VirtualKeyboardPanel::kDesignWidth;
    const auto wantedHeight = settings.collapsed ? VirtualKeyboardPanel::kCollapsedHeight
                                                 : VirtualKeyboardPanel::kDesignHeight;

    // A collapsed board is a small thing on purpose, so its minimum gives way too.
    setResizeLimits (settings.collapsed ? 420 : 700, settings.collapsed ? 100 : 320,
                     2200, 900);

    if (! isVisible())
        return;

    // Resizing in place (rather than re-centring) keeps the board where the player
    // put it; the clamp keeps the whole of it on the screen.
    const auto area = placement::usableArea (placement::displayAreaFor (nullptr));
    const auto position = getPosition();

    setBounds (juce::Rectangle<int> (juce::jlimit (area.getX(),
                                                   juce::jmax (area.getX(),
                                                               area.getRight() - wantedWidth),
                                                   position.x),
                                     juce::jlimit (area.getY(),
                                                   juce::jmax (area.getY(),
                                                               area.getBottom() - wantedHeight),
                                                   position.y),
                                     wantedWidth, wantedHeight));
}

void VirtualKeyboardWindow::closeButtonPressed()
{
    hide();
}

void VirtualKeyboardWindow::activeWindowStatusChanged()
{
    // Coming to the front hands the typing to the panel: opening the window (or
    // clicking back into it) is enough, no need to click the piano first.
    if (isActiveWindow() && panel != nullptr)
        panel->focusTyping();
}

void VirtualKeyboardWindow::hide()
{
    if (panel != nullptr)
        panel->releaseWindowKeys();   // nothing is left sounding behind a closed window

    setVisible (false);
}

void VirtualKeyboardWindow::show()
{
    if (panel == nullptr)
        return;

    applyWindowState();

    if (! isVisible())
    {
        const auto& settings = panel->getSettings();
        const auto wanted = settings.collapsed
                                ? juce::Point<int> (VirtualKeyboardPanel::kCollapsedWidth,
                                                    VirtualKeyboardPanel::kCollapsedHeight)
                                : juce::Point<int> (VirtualKeyboardPanel::kDesignWidth,
                                                    VirtualKeyboardPanel::kDesignHeight);

        setBounds (placement::placeWindow (wanted, placement::displayAreaFor (nullptr)));
    }

    setVisible (true);
    toFront (true);
    panel->refresh();
    panel->focusTyping();

    // `toFront()` has just taken the focus for the window; ask for it once more after
    // the activation has gone through, so the very first key press is already heard.
    juce::Component::SafePointer<VirtualKeyboardPanel> safePanel (panel);
    juce::MessageManager::callAsync ([safePanel]
    {
        if (safePanel != nullptr && safePanel->isShowing())
            safePanel->focusTyping();
    });
}

void VirtualKeyboardWindow::refresh()
{
    if (panel != nullptr)
        panel->refresh();
}

void VirtualKeyboardWindow::refreshArpControls()
{
    if (panel != nullptr)
    {
        panel->getArpControlsForTest().refresh();
        panel->refresh();
    }
}

void VirtualKeyboardWindow::setKeysDown (const std::vector<int>& notes)
{
    if (panel != nullptr)
        panel->setKeysDown (notes);
}

void VirtualKeyboardWindow::setLastVelocity (int velocity)
{
    if (panel != nullptr)
        panel->setLastVelocity (velocity);
}

} // namespace hmdaw
