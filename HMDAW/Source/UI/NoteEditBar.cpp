#include "NoteEditBar.h"
#include "Theme.h"

namespace hmdaw
{

namespace
{
    constexpr double kPresetRatios[] = { 2.0, 3.0, 5.0, 7.0, 9.0 };
}

/** One row of the "every selected note" list, shown while several notes are
    selected. Editing a field only touches the note the row belongs to. */
class NoteEditBar::MultiRow : public juce::Component
{
public:
    MultiRow (ProjectDocument& documentToUse, AppState& appStateToUse,
              juce::int64 noteIdToUse, int indexToUse)
        : document (documentToUse), appState (appStateToUse),
          noteId (noteIdToUse), index (indexToUse)
    {
        addAndMakeVisible (indexLabel);
        indexLabel.setFont (theme::font (11.0f));
        indexLabel.setColour (juce::Label::textColourId, theme::dimText);
        indexLabel.setJustificationType (juce::Justification::centred);
        indexLabel.setText (juce::String (index + 1), juce::dontSendNotification);

        addAndMakeVisible (frequencyEditor);
        frequencyEditor.setJustification (juce::Justification::centredRight);
        frequencyEditor.setSelectAllWhenFocused (true);
        frequencyEditor.onReturnKey = [this] { applyFrequency(); };
        frequencyEditor.onFocusLost = [this] { applyFrequency(); };

        addAndMakeVisible (velocitySlider);
        velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
        velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 18);
        velocitySlider.setRange (0.0, 127.0, 1.0);
        velocitySlider.onDragStart = [this] { document.beginEdit (NI_TXT ("修改力度")); };
        velocitySlider.onValueChange = [this] { applyVelocity(); };

        addAndMakeVisible (commentEditor);
        commentEditor.setSelectAllWhenFocused (true);
        commentEditor.setTextToShowWhenEmpty (NI_TXT ("备注"), theme::dimText);
        commentEditor.onReturnKey = [this] { applyComment(); };
        commentEditor.onFocusLost = [this] { applyComment(); };

        addAndMakeVisible (muteButton);
        muteButton.setButtonText (NI_TXT ("静音"));
        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        muteButton.setTooltip (NI_TXT ("静音后这个音符变灰，也不发声"));
        muteButton.onClick = [this] { applyMute (muteButton.getToggleState()); };
    }

    juce::int64 getNoteId() const noexcept { return noteId; }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto* n = findNote();

        if (n == nullptr)
            return;

        // On a drum track the first column names the drum rather than showing a
        // frequency, and it is not editable there.
        const auto* owner = document.findTrack (appState.selectedTrackId);
        const auto drums = owner != nullptr && owner->kind == TrackKind::drums;

        frequencyEditor.setReadOnly (drums);
        frequencyEditor.setEnabled (! drums);

        if (! frequencyEditor.hasKeyboardFocus (true))
            frequencyEditor.setText (drums ? drumPadName (n->pad)
                                           : juce::String (n->frequencyHz, 2),
                                     juce::dontSendNotification);

        velocitySlider.setValue ((double) n->velocity, juce::dontSendNotification);

        if (! commentEditor.hasKeyboardFocus (true))
            commentEditor.setText (n->comment, juce::dontSendNotification);

        muteButton.setToggleState (n->muted, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panelRaised.withAlpha (0.35f));
        g.fillRect (getLocalBounds().reduced (0, 1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 1);
        indexLabel.setBounds (area.removeFromLeft (20));
        frequencyEditor.setBounds (area.removeFromLeft (86));
        area.removeFromLeft (6);
        velocitySlider.setBounds (area.removeFromLeft (180));
        area.removeFromLeft (6);
        muteButton.setBounds (area.removeFromRight (48));
        area.removeFromRight (6);
        commentEditor.setBounds (area);
    }

    /** Diagnostic hooks for the offscreen self test. */
    bool isMuted() const { return muteButton.getToggleState(); }
    juce::Rectangle<int> getMuteBounds() const { return muteButton.getBounds(); }

    /** A real click flips the toggle first, then runs the handler. */
    void pressMute()
    {
        muteButton.setToggleState (! muteButton.getToggleState(), juce::dontSendNotification);

        if (muteButton.onClick != nullptr)
            muteButton.onClick();
    }

    /** Diagnostic hook: sets the frequency field and applies it, exactly as typing
        into the editor and pressing return would. */
    void setFrequencyForTest (double hz)
    {
        frequencyEditor.setText (juce::String (hz, 2), juce::dontSendNotification);
        applyFrequency();
    }

private:
    Note* findNote()
    {
        return document.findNote (appState.selectedTrackId, noteId);
    }

    void applyFrequency()
    {
        if (updating)
            return;

        // A drum track's pitch comes from the kit, so this field only names the
        // drum and never writes anything.
        if (const auto* owner = document.findTrack (appState.selectedTrackId);
            owner != nullptr && owner->kind == TrackKind::drums)
            return;

        const auto* n = findNote();
        const auto value = frequencyEditor.getText().getDoubleValue();

        if (n == nullptr)
            return;

        if (value < kMinVisibleHz || value > kMaxVisibleHz)
        {
            frequencyEditor.setText (juce::String (n->frequencyHz, 2), juce::dontSendNotification);
            return;
        }

        if (std::abs (value - n->frequencyHz) < 1.0e-9)
            return;

        document.applyEdit (NI_TXT ("修改音符频率"), [this, value] (Project& project)
        {
            if (auto* clip = project.findClip (appState.selectedClipId))
                for (auto& n2 : clip->notes)
                    if (n2.id == noteId)
                        n2.frequencyHz = value;
        });
    }

    void applyVelocity()
    {
        if (updating)
            return;

        const auto* n = findNote();
        const auto value = (int) std::lround (velocitySlider.getValue());

        if (n == nullptr || value == n->velocity)
            return;

        document.applyEdit (NI_TXT ("修改力度"), [this, value] (Project& project)
        {
            if (auto* clip = project.findClip (appState.selectedClipId))
                for (auto& n2 : clip->notes)
                    if (n2.id == noteId)
                        n2.velocity = juce::jlimit (0, 127, value);
        });
    }

    void applyComment()
    {
        if (updating)
            return;

        const auto text = commentEditor.getText();
        const auto* n = findNote();

        if (n == nullptr || n->comment == text)
            return;

        document.applyEdit (NI_TXT ("修改备注"), [this, text] (Project& project)
        {
            if (auto* clip = project.findClip (appState.selectedClipId))
                for (auto& n2 : clip->notes)
                    if (n2.id == noteId)
                        n2.comment = text;
        });
    }

    void applyMute (bool shouldMute)
    {
        if (updating)
            return;

        const auto* n = findNote();

        if (n == nullptr || n->muted == shouldMute)
            return;

        document.applyEdit (shouldMute ? NI_TXT ("静音音符") : NI_TXT ("取消静音"),
                            [this, shouldMute] (Project& project)
        {
            if (auto* clip = project.findClip (appState.selectedClipId))
                for (auto& n2 : clip->notes)
                    if (n2.id == noteId)
                        n2.muted = shouldMute;
        });
    }

    ProjectDocument& document;
    AppState& appState;
    juce::int64 noteId;
    int index;

    juce::Label indexLabel;
    juce::TextEditor frequencyEditor, commentEditor;
    juce::Slider velocitySlider;
    juce::TextButton muteButton;
    bool updating = false;
};

NoteEditBar::NoteEditBar (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse)
{
    auto configureLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
    };

    configureLabel (titleLabel, NI_TXT ("音符编辑"));
    configureLabel (frequencyLabel, NI_TXT ("频率"));
    configureLabel (velocityLabel, NI_TXT ("力度"));
    configureLabel (commentLabel, NI_TXT ("备注"));
    configureLabel (ratioLabel, NI_TXT ("频率比"));
    configureLabel (targetLabel, juce::String());

    titleLabel.setColour (juce::Label::textColourId, theme::accent);
    titleLabel.setFont (theme::font (12.0f, true));

    addAndMakeVisible (frequencyEditor);
    frequencyEditor.setJustification (juce::Justification::centredRight);
    frequencyEditor.setSelectAllWhenFocused (true);
    frequencyEditor.onReturnKey = [this] { applyFrequency(); };
    frequencyEditor.onFocusLost = [this] { applyFrequency(); };

    frequencyLabel.setText (NI_TXT ("头部"), juce::dontSendNotification);
    configureLabel (tailFrequencyLabel, NI_TXT ("尾部"));

    addAndMakeVisible (tailFrequencyEditor);
    tailFrequencyEditor.setJustification (juce::Justification::centredRight);
    tailFrequencyEditor.setSelectAllWhenFocused (true);
    tailFrequencyEditor.onReturnKey = [this] { applyTailFrequency(); };
    tailFrequencyEditor.onFocusLost = [this] { applyTailFrequency(); };

    addAndMakeVisible (commentEditor);
    commentEditor.setSelectAllWhenFocused (true);
    commentEditor.setTextToShowWhenEmpty (NI_TXT ("可写文字备注，编辑栏中可见"), theme::dimText);
    commentEditor.onReturnKey = [this] { applyComment (commentEditor.getText()); };
    commentEditor.onFocusLost = [this] { applyComment (commentEditor.getText()); };

    addAndMakeVisible (customRatioEditor);
    customRatioEditor.setSelectAllWhenFocused (true);
    customRatioEditor.setTextToShowWhenEmpty (NI_TXT ("如 1:4"), theme::dimText);
    customRatioEditor.onReturnKey = [this] { applyCustomRatio(); };
    customRatioEditor.onFocusLost = [this] { applyCustomRatio(); };

    addAndMakeVisible (velocitySlider);
    velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    velocitySlider.setRange (0.0, 127.0, 1.0);
    velocitySlider.setValue (100.0, juce::dontSendNotification);
    velocitySlider.onDragStart = [this] { document.beginEdit (NI_TXT ("修改力度")); };
    velocitySlider.onValueChange = [this] { applyVelocity(); };

    addAndMakeVisible (muteButton);
    muteButton.setButtonText (NI_TXT ("静音"));
    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    muteButton.setTooltip (NI_TXT ("静音后音符变灰，播放与导出里都不发声"));
    muteButton.onClick = [this] { applyMute (muteButton.getToggleState()); };

    addChildComponent (groupButton);
    groupButton.setButtonText (NI_TXT ("应用为参考线组"));
    groupButton.setTooltip (NI_TXT ("把选中的多个音符定格成一条参考线组（含八度族），并存入历史"));
    groupButton.onClick = [this] { applyGroupFromSelection(); };

    const juce::StringArray ratioNames { "1:2", "1:3", "1:5", "1:7", "1:9", NI_TXT ("自定义") };

    for (int i = 0; i < ratioNames.size(); ++i)
    {
        auto* button = ratioButtons.add (new juce::TextButton (ratioNames[i]));
        addAndMakeVisible (button);
        button->setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
        button->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        button->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                 | (i < ratioNames.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        button->onClick = [this, i]
        {
            if (i < 5)
                applyPresetRatio (i);
            else
                applyCustomRatio();
        };
    }

    addAndMakeVisible (endpointButton);
    endpointButton.setButtonText (NI_TXT ("目标：头"));
    endpointButton.setTooltip (NI_TXT ("频率比的目标使用音符头部或尾部"));
    endpointButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    endpointButton.onClick = [this]
    {
        appState.setCurrentPitchEndpoint (
            appState.currentPitchEndpoint == AppState::PitchEndpoint::head
                ? AppState::PitchEndpoint::tail : AppState::PitchEndpoint::head);
    };

    addChildComponent (pitchCurveLabel);
    addChildComponent (tiltStartLabel);
    addChildComponent (tiltEndLabel);
    pitchCurveLabel.setText (NI_TXT ("曲率"), juce::dontSendNotification);
    tiltStartLabel.setText (NI_TXT ("开始 %"), juce::dontSendNotification);
    tiltEndLabel.setText (NI_TXT ("结束 %"), juce::dontSendNotification);

    for (auto* label : { &pitchCurveLabel, &tiltStartLabel, &tiltEndLabel })
    {
        label->setFont (theme::font (10.0f));
        label->setColour (juce::Label::textColourId, theme::dimText);
    }

    auto setupShapeSlider = [this] (juce::Slider& slider, double lo, double hi,
                                    double interval)
    {
        addChildComponent (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 18);
        slider.setRange (lo, hi, interval);
        slider.onDragStart = [this]
        {
            if (! shapeEditOpen)
            {
                shapeEditOpen = true;
                document.beginEdit (NI_TXT ("调整音符倾斜"));
            }
        };
        slider.onValueChange = [this] { applyPitchShapeControls(); };
        slider.onDragEnd = [this]
        {
            if (shapeEditOpen)
            {
                document.notifyEdit();
                shapeEditOpen = false;
            }
        };
    };

    setupShapeSlider (pitchCurveSlider, -100.0, 100.0, 1.0);
    setupShapeSlider (tiltStartSlider, 0.0, 99.0, 1.0);
    setupShapeSlider (tiltEndSlider, 1.0, 100.0, 1.0);
    pitchCurveSlider.setTextValueSuffix ("%");
    tiltStartSlider.setTextValueSuffix ("%");
    tiltEndSlider.setTextValueSuffix ("%");

    document.addChangeListener (this);
    appState.addChangeListener (this);

    addChildComponent (multiViewport);
    multiViewport.setViewedComponent (&multiHolder, false);
    multiViewport.setScrollBarsShown (true, false);
}

NoteEditBar::~NoteEditBar()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

Note* NoteEditBar::primaryNote()
{
    return document.findNote (appState.selectedTrackId, appState.primaryNoteId);
}

const Note* NoteEditBar::primaryNote() const
{
    return const_cast<NoteEditBar*> (this)->primaryNote();
}

void NoteEditBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void NoteEditBar::focusFrequencyEditor()
{
    frequencyEditor.grabKeyboardFocus();
    frequencyEditor.selectAll();
}

void NoteEditBar::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto* note = primaryNote();
    const auto hasNote = note != nullptr;
    // The ratio row is on screen for any note selection; the lens inside it needs
    // exactly one note, so with several it goes grey and says so.
    const auto rowOnScreen = ratioRowVisible();
    const auto lens = ratioLensEnabled() && hasNote;
    const auto drums = drumMode();

    titleLabel.setText (hasNote ? NI_TXT ("音符编辑") : NI_TXT ("未选中音符"),
                        juce::dontSendNotification);

    // On a drum track this field names the drum, so it reads but never writes.
    frequencyLabel.setText (drums ? NI_TXT ("鼓") : NI_TXT ("频率"), juce::dontSendNotification);
    frequencyEditor.setEnabled (hasNote && ! drums);
    frequencyEditor.setReadOnly (! hasNote);
    tailFrequencyEditor.setEnabled (hasNote && ! drums);
    tailFrequencyEditor.setReadOnly (! hasNote);
    velocitySlider.setEnabled (hasNote);
    commentEditor.setEnabled (hasNote);
    muteButton.setEnabled (hasNote);
    // The custom box is an input for the ratio lens, not for the note.
    customRatioEditor.setEnabled (lens);

    ratioLabel.setVisible (rowOnScreen);
    customRatioEditor.setVisible (rowOnScreen);
    targetLabel.setVisible (rowOnScreen);

    // Only a multi-note selection can become a reference line group.
    groupButton.setVisible (! drums && appState.selectedNoteIds.size() >= 2);

    for (auto* button : ratioButtons)
    {
        button->setVisible (rowOnScreen);
        button->setEnabled (lens);
    }

    if (note != nullptr)
    {
        if (! frequencyEditor.hasKeyboardFocus (true))
            frequencyEditor.setText (drums ? drumPadName (note->pad)
                                           : juce::String (note->frequencyHz, 2),
                                     juce::dontSendNotification);

        if (! tailFrequencyEditor.hasKeyboardFocus (true))
            tailFrequencyEditor.setText (drums ? drumPadName (note->pad)
                                               : juce::String (note->effectiveTailHz(), 2),
                                         juce::dontSendNotification);

        if (! commentEditor.hasKeyboardFocus (true))
            commentEditor.setText (note->comment, juce::dontSendNotification);

        velocitySlider.setValue ((double) note->velocity, juce::dontSendNotification);
        muteButton.setToggleState (note->muted, juce::dontSendNotification);

        if (lens)
        {
            const auto targetHz = appState.currentPitchEndpoint == AppState::PitchEndpoint::head
                                      ? note->frequencyHz : note->effectiveTailHz();
            const auto upper = targetHz * appState.currentRatio;
            const auto lower = targetHz / appState.currentRatio;

            targetLabel.setText (NI_TXT ("目标频率  ") + formatHz (upper) + "   /   "
                                     + formatHz (lower),
                                 juce::dontSendNotification);
        }
        else
        {
            // Not "the options vanished": say why the keys are grey.
            targetLabel.setText (NI_TXT ("选中一个音符时可用频率比"), juce::dontSendNotification);
        }

        if (vstMode())
            targetLabel.setText (targetLabel.getText()
                                     + NI_TXT ("   · VST 倾斜仅作标注，按首段频率发声"),
                                 juce::dontSendNotification);
    }
    else
    {
        targetLabel.setText (juce::String(), juce::dontSendNotification);
    }

    frequencyLabel.setVisible (! drums);
    tailFrequencyLabel.setVisible (! drums);
    tailFrequencyEditor.setVisible (! drums && hasNote);
    endpointButton.setVisible (rowOnScreen);
    endpointButton.setButtonText (appState.currentPitchEndpoint == AppState::PitchEndpoint::head
                                      ? NI_TXT ("目标：头") : NI_TXT ("目标：尾"));
    endpointButton.setToggleState (appState.currentPitchEndpoint == AppState::PitchEndpoint::tail,
                                   juce::dontSendNotification);

    const auto showShape = shapeRowVisible();
    for (auto* component : { static_cast<juce::Component*> (&pitchCurveLabel),
                             static_cast<juce::Component*> (&tiltStartLabel),
                             static_cast<juce::Component*> (&tiltEndLabel),
                             static_cast<juce::Component*> (&pitchCurveSlider),
                             static_cast<juce::Component*> (&tiltStartSlider),
                             static_cast<juce::Component*> (&tiltEndSlider) })
        component->setVisible (showShape);

    if (note != nullptr)
    {
        pitchCurveSlider.setValue ((double) note->pitchCurve * 100.0,
                                   juce::dontSendNotification);
        tiltStartSlider.setValue ((double) note->tiltStart * 100.0,
                                  juce::dontSendNotification);
        tiltEndSlider.setValue ((double) note->tiltEnd * 100.0,
                                juce::dontSendNotification);
    }

    updateRatioButtons();
    rebuildMultiRows();

    // Lay out again after the visibility above changed: a component is only asked
    // to lay itself out when its *bounds* change, and this bar's bounds do not change
    // between "nothing selected" and "one note selected" (the strip is a fixed slot
    // on purpose, so the canvas never jumps). Without this the ratio row kept the
    // zero-sized rectangle it was given while nothing was selected - the row was
    // "visible" but drawn nowhere, which is exactly what "the ratio options are not
    // there with one note" looked like.
    resized();
}

int NoteEditBar::getExtraHeight() const
{
    const auto notes = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                        appState.selectedNoteIds);

    auto extra = shapeRowVisible() ? 36 : 0;

    if (notes.size() >= 2)
        extra += juce::jmin ((int) notes.size(), multiMaxRows) * multiRowHeight + 4;

    return extra;
}

int NoteEditBar::getBaseHeight() const
{
    // Two rows as soon as there is a note to talk about: the frequency / velocity /
    // comment row on top, the ratio row underneath.
    return ratioRowVisible() ? singleNoteHeight : multiNoteHeight;
}

bool NoteEditBar::ratioRowVisible() const
{
    // A drum track has no frequency to take a ratio of, so the row has nothing to
    // say there. Everywhere else it stays put while a note is selected.
    if (const auto* owner = document.findTrack (appState.selectedTrackId);
        owner != nullptr && owner->kind == TrackKind::drums)
        return false;

    return ! appState.selectedNoteIds.empty();
}

bool NoteEditBar::ratioLensEnabled() const
{
    // The lens is a view from one origin, so it only means something for exactly one
    // selected note.
    return appState.selectedNoteIds.size() == 1;
}

bool NoteEditBar::areRatioKeysEnabledForTest() const
{
    for (auto* button : ratioButtons)
        if (button != nullptr && button->isVisible())
            return button->isEnabled();

    return false;
}

bool NoteEditBar::drumMode() const
{
    const auto* owner = document.findTrack (appState.selectedTrackId);
    return owner != nullptr && owner->kind == TrackKind::drums;
}

bool NoteEditBar::vstMode() const
{
    const auto* owner = document.findTrack (appState.selectedTrackId);
    return owner != nullptr && owner->kind == TrackKind::vst;
}

bool NoteEditBar::shapeRowVisible() const
{
    if (drumMode() || appState.selectedNoteIds.empty())
        return false;

    const auto notes = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                        appState.selectedNoteIds);

    for (const auto& note : notes)
        if (note.hasPitchTilt())
            return true;

    return false;
}

void NoteEditBar::applyPitchShapeControls()
{
    if (updating)
        return;

    const auto notes = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                        appState.selectedNoteIds);

    if (notes.empty())
        return;

    auto start = (float) tiltStartSlider.getValue() * 0.01f;
    auto end = (float) tiltEndSlider.getValue() * 0.01f;
    const auto curve = (float) pitchCurveSlider.getValue() * 0.01f;

    start = juce::jlimit (0.0f, 0.999f, start);
    end = juce::jlimit (start + 0.001f, 1.0f, end);

    std::set<juce::int64> ids;

    for (const auto& note : notes)
        ids.insert (note.id);

    const auto clipId = appState.selectedClipId;
    const auto beganHere = ! shapeEditOpen;

    if (beganHere)
        document.beginEdit (NI_TXT ("调整音符倾斜"));

    document.applyDerivedEdit ([clipId, ids, curve, start, end] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            for (auto& note : clip->notes)
                if (ids.find (note.id) != ids.end())
                {
                    note.pitchCurve = curve;
                    note.tiltStart = start;
                    note.tiltEnd = end;
                }
    });

    if (beganHere)
        shapeEditOpen = false;
}

void NoteEditBar::setPitchShapeForTest (float curve, float start, float end)
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    pitchCurveSlider.setValue ((double) curve * 100.0, juce::sendNotificationSync);
    tiltStartSlider.setValue ((double) start * 100.0, juce::sendNotificationSync);
    tiltEndSlider.setValue ((double) end * 100.0, juce::sendNotificationSync);
    updating = false;
    applyPitchShapeControls();
}

juce::Rectangle<int> NoteEditBar::getRatioButtonBoundsForTest (int index) const
{
    if (index < 0 || index >= ratioButtons.size())
        return {};

    return ratioButtons[index]->getBounds();
}

bool NoteEditBar::triggerRatioButtonForTest (int index)
{
    if (index < 0 || index >= ratioButtons.size())
        return false;

    ratioButtons[index]->triggerClick();
    return true;
}

void NoteEditBar::triggerGroupApplyForTest()
{
    // A real click goes through the button's handler.
    if (groupButton.onClick != nullptr)
        groupButton.onClick();
}

void NoteEditBar::triggerMute()
{
    // A real click flips the toggle first, then runs the handler.
    muteButton.setToggleState (! muteButton.getToggleState(), juce::dontSendNotification);

    if (muteButton.onClick != nullptr)
        muteButton.onClick();
}

void NoteEditBar::triggerMultiRowMute (int index)
{
    if (index >= 0 && index < multiRows.size())
        multiRows[index]->pressMute();
}

bool NoteEditBar::isMultiRowMuted (int index) const
{
    return index >= 0 && index < multiRows.size() && multiRows[index]->isMuted();
}

juce::Rectangle<int> NoteEditBar::getMultiRowMuteBounds (int index) const
{
    if (index < 0 || index >= multiRows.size())
        return {};

    return multiRows[index]->getMuteBounds();
}

juce::Rectangle<int> NoteEditBar::getMultiRowBounds (int index) const
{
    if (index < 0 || index >= multiRows.size())
        return {};

    return multiRows[index]->getBounds();
}

void NoteEditBar::applyMultiRowFrequency (int index, double hz)
{
    if (index < 0 || index >= multiRows.size())
        return;

    multiRows[index]->setFrequencyForTest (hz);
}

void NoteEditBar::rebuildMultiRows()
{
    const auto notes = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                        appState.selectedNoteIds);
    const auto show = notes.size() >= 2;

    multiViewport.setVisible (show);

    if (! show)
    {
        multiRows.clear();
        return;
    }

    // Rebuild only when the set of notes changed, so typing in a row is not
    // interrupted by the change messages its own edit produces.
    auto sameSet = multiRows.size() == (int) notes.size();

    for (int i = 0; sameSet && i < multiRows.size(); ++i)
        sameSet = multiRows[i]->getNoteId() == notes[(size_t) i].id;

    if (sameSet)
    {
        for (auto* row : multiRows)
            row->refresh();

        return;
    }

    multiRows.clear();

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        auto* row = multiRows.add (new MultiRow (document, appState, notes[(size_t) i].id, i));
        multiHolder.addAndMakeVisible (row);
        row->refresh();
    }
}

void NoteEditBar::updateRatioButtons()
{
    const auto ratio = appState.currentRatio;

    for (int i = 0; i < ratioButtons.size(); ++i)
    {
        const auto isPreset = i < 5;
        const auto matches = isPreset && std::abs (ratio - kPresetRatios[i]) < 1.0e-9;

        ratioButtons[i]->setToggleState (matches, juce::dontSendNotification);
    }

    if (! updating && appState.selectedNoteIds.size() == 1
        && ! customRatioEditor.hasKeyboardFocus (true))
    {
        const auto isPreset = std::any_of (std::begin (kPresetRatios), std::end (kPresetRatios),
                                           [ratio] (double r) { return std::abs (ratio - r) < 1.0e-9; });

        if (! isPreset)
            customRatioEditor.setText (music::formatRatioText (ratio), juce::dontSendNotification);
    }
}

void NoteEditBar::applyFrequency()
{
    if (updating)
        return;

    auto* note = primaryNote();

    if (note == nullptr)
        return;

    const auto value = frequencyEditor.getText().getDoubleValue();

    if (value < kMinVisibleHz || value > kMaxVisibleHz)
    {
        frequencyEditor.setText (juce::String (note->frequencyHz, 2), juce::dontSendNotification);
        return;
    }

    if (std::abs (value - note->frequencyHz) < 1.0e-9)
        return;

    const auto noteId = note->id;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("修改音符频率"), [clipId, noteId, value] (Project& project)
    {
        if (auto* target = project.findClip (clipId))
            for (auto& n : target->notes)
                if (n.id == noteId)
                    n.frequencyHz = value;
    });
}

void NoteEditBar::applyTailFrequency()
{
    if (updating)
        return;

    auto* note = primaryNote();

    if (note == nullptr || drumMode())
        return;

    const auto value = tailFrequencyEditor.getText().getDoubleValue();

    if (value < kMinVisibleHz || value > kMaxVisibleHz)
    {
        tailFrequencyEditor.setText (juce::String (note->effectiveTailHz(), 2),
                                     juce::dontSendNotification);
        return;
    }

    if (std::abs (value - note->effectiveTailHz()) < 1.0e-9)
        return;

    const auto noteId = note->id;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("修改尾部频率"), [clipId, noteId, value] (Project& project)
    {
        if (auto* target = project.findClip (clipId))
            for (auto& n : target->notes)
                if (n.id == noteId)
                    n.tailFrequencyHz = value;
    });
}

void NoteEditBar::applyVelocity()
{
    if (updating)
        return;

    auto* note = primaryNote();

    if (note == nullptr)
        return;

    const auto value = (int) std::lround (velocitySlider.getValue());

    if (value == note->velocity)
        return;

    const auto noteId = note->id;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("修改力度"), [clipId, noteId, value] (Project& project)
    {
        if (auto* target = project.findClip (clipId))
            for (auto& n : target->notes)
                if (n.id == noteId)
                    n.velocity = juce::jlimit (0, 127, value);
    });
}

void NoteEditBar::applyComment (const juce::String& text)
{
    if (updating)
        return;

    auto* note = primaryNote();

    if (note == nullptr || note->comment == text)
        return;

    const auto noteId = note->id;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (NI_TXT ("修改备注"), [clipId, noteId, text] (Project& project)
    {
        if (auto* target = project.findClip (clipId))
            for (auto& n : target->notes)
                if (n.id == noteId)
                    n.comment = text;
    });
}

void NoteEditBar::applyPresetRatio (int index)
{
    if (index >= 0 && index < 5)
        applyRatio (kPresetRatios[index]);
}

void NoteEditBar::applyCustomRatio()
{
    if (updating)
        return;

    const auto value = music::parseRatioText (customRatioEditor.getText());

    if (value <= 0.0)
        return;

    applyRatio (value);
}

void NoteEditBar::applyRatio (double ratio)
{
    // The ratio is a view setting on the current selection, not note data: it only
    // means something for the one note the second kind of reference line follows,
    // and it is never written to the document (hence no undo step).
    if (ratio <= 0.0 || appState.selectedNoteIds.size() != 1)
        return;

    appState.setCurrentRatio (ratio);
}

void NoteEditBar::applyMute (bool shouldMute)
{
    if (updating)
        return;

    auto* note = primaryNote();

    if (note == nullptr || note->muted == shouldMute)
        return;

    const auto noteId = note->id;
    const auto clipId = appState.selectedClipId;

    document.applyEdit (shouldMute ? NI_TXT ("静音音符") : NI_TXT ("取消静音"),
                        [clipId, noteId, shouldMute] (Project& project)
    {
        if (auto* clip = project.findClip (clipId))
            for (auto& n : clip->notes)
                if (n.id == noteId)
                    n.muted = shouldMute;
    });
}

void NoteEditBar::applyGroupFromSelection()
{
    if (updating)
        return;

    const auto notes = selectedNotesOf (document.getProject(), appState.selectedClipId,
                                        appState.selectedNoteIds);

    if (notes.size() < 2)
        return;

    // The selected notes themselves become the reference line group: the lowest
    // one is the frame dot, the rest are its intervals. The ladder's own segment
    // (参考组) is not involved at all.
    auto group = music::makeGroupFromNotes (notes);

    if (group.anchorHz <= 0.0)
        return;

    group.name = NI_TXT ("参考线组 ") + juce::String (appState.referenceGroupSerial + 1);

    juce::int64 newEntryId = 0;

    document.applyEdit (NI_TXT ("应用参考线组"), [group, &newEntryId] (Project& project)
    {
        auto entry = music::makeGroupEntry (project.nextReferenceLineId++, group);

        // This one is meant to stay: sealed entries survive the next new entry and
        // are the only ones written to the project.
        entry.locked = true;

        music::clearUnlockedEntries (project.referenceLines, entry.id);
        newEntryId = entry.id;
        project.referenceLines.push_back (std::move (entry));
    });

    if (newEntryId != 0)
        appState.selectReferenceLine (newEntryId);

    appState.referenceGroupSerial++;
    appState.pushReferenceLineHistory (group);
}

void NoteEditBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panelRaised);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void NoteEditBar::resized()
{
    auto area = getLocalBounds().reduced (6, 4);

    // With several notes selected the strip is taller and the extra space at the
    // bottom lists every one of them.
    const auto extra = getExtraHeight();
    const auto shapeHeight = shapeRowVisible() ? 36 : 0;
    const auto listHeight = juce::jmax (0, extra - shapeHeight);
    auto listArea = listHeight > 0 ? area.removeFromBottom (listHeight)
                                   : juce::Rectangle<int>();
    auto shapeArea = shapeHeight > 0 ? area.removeFromBottom (shapeHeight)
                                     : juce::Rectangle<int>();

    // The ratio row exists for a single selected note, and both refresh() and this
    // go through ratioRowVisible() so the geometry can never disagree.
    const auto rowOnScreen = ratioRowVisible();
    auto row2 = rowOnScreen ? area.removeFromBottom (juce::jmax (0, area.getHeight() / 2))
                            : juce::Rectangle<int>();
    ratioRowBounds = row2;
    auto row1 = area;

    titleLabel.setBounds (row1.removeFromLeft (66));

    muteButton.setBounds (row1.removeFromRight (62));

    if (groupButton.isVisible())
    {
        row1.removeFromRight (6);
        groupButton.setBounds (row1.removeFromRight (118).reduced (0, 1));
    }
    else
    {
        groupButton.setBounds (0, 0, 0, 0);
    }

    row1.removeFromRight (8);
    frequencyLabel.setBounds (row1.removeFromLeft (28));
    frequencyEditor.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (4);
    tailFrequencyLabel.setBounds (row1.removeFromLeft (28));
    tailFrequencyEditor.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    velocityLabel.setBounds (row1.removeFromLeft (30));
    velocitySlider.setBounds (row1.removeFromLeft (170));
    row1.removeFromLeft (8);
    commentLabel.setBounds (row1.removeFromLeft (30));
    commentEditor.setBounds (row1);

    // The ratio keys are the point of this row, so they are laid out before the
    // target read-out and never collapse to nothing: a narrow window (or the preset
    // side panel taking its share of the width) shrinks them and gives the read-out
    // whatever is left. A row of zero-width keys is exactly what "the ratio options
    // are gone" looked like.
    constexpr int labelWidth = 48;
    constexpr int customWidth = 64;
    constexpr int customEditorWidth = 74;
    constexpr int endpointWidth = 68;
    constexpr int rowGap = 10;
    constexpr int idealKeyWidth = 46;

    const auto rowWidth = juce::jmax (0, row2.getWidth());
    const auto roomForKeys = juce::jmax (0, rowWidth - labelWidth - customWidth
                                                - customEditorWidth - endpointWidth
                                                - rowGap);
    const auto keyWidth = juce::jmin (idealKeyWidth, roomForKeys / 5);

    ratioLabel.setBounds (row2.removeFromLeft (labelWidth));

    for (int i = 0; i < 5; ++i)
        if (auto* button = ratioButtons[i])
            button->setBounds (row2.removeFromLeft (keyWidth));

    if (auto* custom = ratioButtons[5])
        custom->setBounds (row2.removeFromLeft (customWidth));

    customRatioEditor.setBounds (row2.removeFromLeft (customEditorWidth));
    endpointButton.setBounds (row2.removeFromLeft (endpointWidth).reduced (2, 1));
    row2.removeFromLeft (juce::jmin (rowGap, juce::jmax (0, row2.getWidth())));
    targetLabel.setBounds (row2);

    if (shapeHeight > 0)
    {
        auto shapeRow = shapeArea.reduced (2, 1);
        auto curveArea = shapeRow.removeFromLeft (shapeRow.getWidth() / 3).reduced (2, 0);
        auto startArea = shapeRow.removeFromLeft (shapeRow.getWidth() / 2).reduced (2, 0);
        auto endArea = shapeRow.reduced (2, 0);

        pitchCurveLabel.setBounds (curveArea.removeFromTop (12));
        pitchCurveSlider.setBounds (curveArea);
        tiltStartLabel.setBounds (startArea.removeFromTop (12));
        tiltStartSlider.setBounds (startArea);
        tiltEndLabel.setBounds (endArea.removeFromTop (12));
        tiltEndSlider.setBounds (endArea);
    }

    if (listHeight > 0)
    {
        multiViewport.setBounds (listArea);

        const auto width = juce::jmax (60, listArea.getWidth()
                                              - (multiViewport.isVerticalScrollBarShown() ? 12 : 0));
        multiHolder.setSize (width, juce::jmax (listArea.getHeight(),
                                                multiRows.size() * multiRowHeight));

        for (int i = 0; i < multiRows.size(); ++i)
            multiRows[i]->setBounds (0, i * multiRowHeight, width, multiRowHeight);
    }
}

} // namespace hmdaw
