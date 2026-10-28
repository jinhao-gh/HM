#pragma once

#include "../Core/KeyboardLayouts.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "ArpControls.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>
#include <vector>

namespace nidaw
{

/** The piano, with the computer keys that play each note written on the keys.

    A thin subclass: everything else is JUCE's own on-screen keyboard (mouse, drag,
    the computer-keyboard mapping, the highlighting).
*/
class LetteredKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit LetteredKeyboard (juce::MidiKeyboardState& state);

    /** The computer keys that play this note ("w·6"), or an empty string. */
    std::function<juce::String (int midiNote)> lettersForNote;

    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour,
                        juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    void drawLetters (juce::Graphics& g, int midiNote, juce::Rectangle<float> area,
                      juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LetteredKeyboard)
};

//==============================================================================
/** The drawn computer keyboard: four staggered rows of caps, each cap painted the
    way the piano key it plays is painted (white key = white cap, black key = black
    cap), with the character and the note name written inside it. */
class ComputerKeyboardView : public juce::Component
{
public:
    ComputerKeyboardView() = default;

    void paint (juce::Graphics& g) override;

    /** The caps to draw, in the panel's current layout. */
    std::function<std::vector<keyboard::Cap>()> getCaps;
    /** Which note a cap plays (-1 = not mapped). */
    std::function<int (const keyboard::Cap&)> noteForCap;
    /** Whether that note is down right now (the cap is highlighted). */
    std::function<bool (int midiNote)> isNoteDown;

    /** The board is 15 key units wide (the number row with Backspace). */
    static constexpr float kBoardUnits = 15.0f;
    /** Four rows of caps, in key units tall (so the caps keep their proportions). */
    static constexpr float kBoardRows = 4.0f;
    static constexpr int   kHeightForTest = 0;   // (the diagram fills what it is given)

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComputerKeyboardView)
};

//==============================================================================
/** 虚拟键盘: a playable 88-key piano, the way Cubase's on-screen keyboard is.

    It is **the same instrument as a hardware MIDI keyboard**: every key it plays
    goes through `MainComponent::handleLiveNoteOn/Off`, so the mapping (or twelve
    tone equal temperament while the editor is in the piano window), the
    arpeggiator, recording and the on-screen highlighting all behave identically.
    Keys played *from anywhere* - a real keyboard, the mouse, the computer keyboard
    - light up here.

    The computer keyboard itself has two layouts: 常规 (the two-row one HM has always
    had) and 巴扬 (the accordion's three rows plus its bass row on the number row).
*/
class VirtualKeyboardPanel : public juce::Component,
                             public juce::KeyListener,
                             private juce::MidiKeyboardState::Listener,
                             private juce::Timer
{
public:
    VirtualKeyboardPanel (ProjectDocument& documentToUse, AppState& appStateToUse);
    ~VirtualKeyboardPanel() override;

    /** A key went down here: the owner routes it exactly like a MIDI message.
        `trigger` says which physical thing it was (the on-screen piano, or one
        computer key), because two sources on the same note sound twice. */
    std::function<void (int midiNote, float velocity, juce::int64 trigger)> onNoteOn;
    std::function<void (int midiNote, juce::int64 trigger)> onNoteOff;
    /** The 琶音 controls changed (the owner hands the new settings to the player). */
    std::function<void()> onArpSettingsChanged;
    /** 折叠 / 置顶 changed: the window resizes itself and saves the file. */
    std::function<void()> onWindowStateChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    /** The computer-keyboard playing: every mapped key of the current layout is
        polled here, so it works while the focus is anywhere inside this window (the
        event bubbles up the parent chain) - no need to click the piano first. */
    bool keyStateChanged (bool isKeyDown) override;
    /** The same, as a KeyListener hung on the *window*: `toFront()` hands the
        keyboard focus to the window itself, so the key events arrive there rather
        than at any of its children. */
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;
    bool keyStateChanged (bool isKeyDown, juce::Component* origin) override;
    /** The typing poll runs while the window is on screen: start / stop it here. */
    void visibilityChanged() override;

    void refresh();
    /** The keys that are down (whatever played them): the keyboard draws them. */
    void setKeysDown (const std::vector<int>& notes);
    /** What the status line calls "最近力度" (a hardware key's velocity is not
        visible from here, the owner tells us). */
    void setLastVelocity (int velocity);
    /** Gives this window's typing the focus (the panel itself, so a control inside
        the window keeps working too). */
    void focusTyping() { grabKeyboardFocus(); }

    /** Lets go of every note *this window* started.

        The owner hears about each one (the state notifies its listener), so the
        engine really stops them, and JUCE's own "which keys are down" bookkeeping is
        cleared at the same time. It is called before anything that would leave a key
        stranded: a shift, a layout change, the window being hidden or destroyed.
    */
    void releaseWindowKeys();

    /** The window's remembered state (the window reads and writes the file). */
    const keyboard::Settings& getSettings() const noexcept { return settings; }
    void setSettings (const keyboard::Settings& wanted);

    static constexpr int kDesignWidth  = 980;
    static constexpr int kDesignHeight = 360;
    static constexpr int kCollapsedWidth  = 520;
    static constexpr int kCollapsedHeight = 120;

    // ---- diagnostic hooks ---------------------------------------------------
    juce::Rectangle<int> getKeyboardBoundsForTest() const { return keyboard.getBounds(); }
    juce::Rectangle<int> getArpBoundsForTest() const { return arpControls.getBounds(); }
    juce::Rectangle<int> getDiagramBoundsForTest() const { return diagram.getBounds(); }
    juce::Rectangle<int> getVelocityBoundsForTest() const { return velocitySlider.getBounds(); }
    /** The bass row's octave buttons (0 = -12, 1 = +12), wherever the layout put them. */
    juce::Rectangle<int> getBassOctaveBoundsForTest (int which) const
    {
        const auto& button = which == 0 ? bassOctDown : bassOctUp;
        return button.getBounds();
    }
    bool isComponentVisibleForTest (int which) const;
    juce::String getStatusForTest() const { return statusLabel.getText(); }
    int  getLowestVisibleKeyForTest() const { return keyboard.getLowestVisibleKey(); }
    /** Which MIDI note a character plays through the melody row (-1 = not mapped). */
    int  typingNoteForTest (int keyChar) const;
    /** Which MIDI note a character plays through the bass row (-1 = not mapped). */
    int  bassNoteForTest (int keyChar) const;
    /** The letters the piano writes on that note ("w·6"). */
    juce::String getPianoLetterForTest (int midiNote) const;
    int  getLastVelocityForTest() const noexcept { return lastVelocity; }
    bool isKeyDownForTest (int midiNote) const;
    float getVelocityForTest() const { return (float) velocitySlider.getValue(); }
    void setVelocityForTest (double velocity) { velocitySlider.setValue (velocity, juce::sendNotificationSync); }
    ArpControls& getArpControlsForTest() noexcept { return arpControls; }
    /** The mouse's way in: press, hold, let go (the component's own path). */
    void pressKeyForTest (int midiNote, float velocity);
    void releaseKeyForTest (int midiNote);
    /** The mouse's *real* path: a synthetic mouse-down / mouse-up on the piano's own
        key, exactly what a click does (the state-based hooks above skip the
        component's mouse handling, and the two have to agree). */
    void pressKeyWithMouseForTest (int midiNote);
    void releaseKeyWithMouseForTest (int midiNote);

    keyboard::Layout getLayoutForTest() const noexcept { return settings.layout; }
    void setLayoutForTest (int comboId);
    int  getMelodyShiftForTest() const noexcept { return settings.melodyShift; }
    int  getBassShiftForTest() const noexcept { return settings.bassShift; }
    /** The shift buttons, exactly as the mouse would press them. */
    void shiftMelodyForTest (int step);
    void shiftBassForTest (int step);
    /** Self test: pretend the system says that computer key is down / up (the
        polling reads this first, so the whole typing path - watchdog included - can
        be driven without a real keyboard). */
    void setKeyDownForTest (int keyChar, bool isDown);
    /** Self test: the key-up event never arrived (the system simply stopped
        reporting that key) - what the watchdog has to notice on its own. */
    void simulateMissedKeyUpForTest (int keyChar);
    /** Self test: one pulse of the 100 ms watchdog. */
    void runTypingWatchdogForTest();
    /** Self test: how many computer keys this window thinks are down. */
    int  getTypingKeyCountForTest() const noexcept { return (int) typingDown.size(); }
    /** Self test: which computer keys the system reports as down right now (the same
        text the status line shows, so the drawing and the check cannot disagree). */
    juce::String getTypingKeysTextForTest() const { return typingKeysText(); }
    /** Self test: the typing poll ticks while the window is on screen. */
    bool isTypingPollRunningForTest() const { return isTimerRunning(); }
    /** Self test: pretend the window lost the keyboard focus (the rule that lets go
        of everything when the user clicks another window). */
    void setWindowFocusedForTest (bool isFocused);
    bool isCollapsedForTest() const noexcept { return settings.collapsed; }
    void toggleCollapseForTest();
    bool isPinnedForTest() const noexcept { return settings.alwaysOnTop; }
    void togglePinForTest();
    /** How many notes this window is holding right now (the release checks). */
    int  getStartedNoteCountForTest() const noexcept { return (int) startedNotes.size(); }

private:
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber,
                       float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber,
                        float velocity) override;
    /** One pulse of the watchdog: see `runTypingWatchdogForTest`. */
    void timerCallback() override;

    /** The keys the computer-keyboard layout maps right now, in the order the panel
        polls them. */
    struct TypingKey { int keyCode; int midiNote; };
    std::vector<TypingKey> typingKeys() const;
    /** The polling itself, shared by the component and the KeyListener paths. */
    bool pollTyping();
    /** "Z X C": the mapped keys the system currently reports as down, in the order
        the layout table lists them. */
    juce::String typingKeysText() const;
    /** Starts / stops the 30 Hz typing poll with the window's visibility. */
    void updatePollingTimer();
    /** True when the system (or the self test) says that computer key is down. */
    bool keyIsDown (int keyCode) const;
    /** True when the focus belongs to this window (rule: a window that is not being
        looked at cannot be typed into). */
    bool focusIsInsideWindow() const;
    /** True when the focused thing is a text box: typing goes to it, not to notes. */
    bool focusIsATextBox() const;
    /** Polls every mapped key, plays the ones that went down and lets go of the ones
        that came up. */
    void updateTyping();
    /** Lets go of every note the computer keys started (through the keyboard state,
        so the owner really stops them). */
    void releaseTypingKeys();
    void setLayout (keyboard::Layout layout);
    void shiftMelody (int step);
    void shiftBass (int step);
    void setCollapsed (bool shouldCollapse);
    void setPinned (bool shouldPin);
    void updateControls();
    void updateStatus();
    /** Every note the current layout can play, for the diagram / the owner. */
    bool isNoteHeld (int midiNote) const;

    ProjectDocument& document;
    AppState& appState;

    keyboard::Settings settings;

    juce::MidiKeyboardState keyboardState;
    LetteredKeyboard keyboard;
    ComputerKeyboardView diagram;

    juce::Slider velocitySlider;
    juce::Label velocityCaption, statusLabel;
    juce::ComboBox layoutBox;
    juce::Label melodyCaption, bassCaption, melodyReadout, bassReadout;
    juce::TextButton melodySemiDown, melodySemiUp, melodyOctDown, melodyOctUp;
    juce::TextButton bassSemiDown, bassSemiUp, bassFifthDown, bassFifthUp;
    juce::TextButton bassOctDown, bassOctUp;
    juce::TextButton pinButton, collapseButton;
    ArpControls arpControls;

    /** True while the panel itself is pushing "these keys are down" into the
        keyboard state: its own listener must not take that for a key press. */
    bool applyingHeldKeys = false;
    int  lastVelocity = 100;
    /** The notes this window started, as (trigger, note) pairs: a structural change
        can let go of exactly those - with the source each one came from, so the
        owner releases the right entry - and leave a hardware player's keys alone. */
    std::vector<std::pair<juce::int64, int>> startedNotes;

    void noteStarted (juce::int64 trigger, int midiNote);
    void noteStopped (juce::int64 trigger, int midiNote);
    /** The computer keys this window believes are down (key codes). */
    std::set<int> typingDown;
    /** Self test: keys the fake system says are down. */
    std::set<int> forcedKeyDown;
    /** Self test: pretend the window is / is not focused. */
    bool forceWindowUnfocused = false;
    /** Which source the next `keyboardState` call belongs to (the listener forwards
        it to the owner): the on-screen piano, or one computer key. */
    juce::int64 currentTrigger = keyboard::kMouseTrigger;
    /** Which keys the owner says are down (the diagram highlights them). */
    std::vector<int> heldKeys;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualKeyboardPanel)
};

//==============================================================================
/** The window itself: reused, hidden when closed, placed like the others.

    It can be collapsed into a small floating board (just the computer-keyboard
    diagram) and pinned above the other windows; both are remembered in
    `%APPDATA%\NiDaw\keyboard.json`.
*/
class VirtualKeyboardWindow : public juce::DocumentWindow
{
public:
    VirtualKeyboardWindow (ProjectDocument& document, AppState& appState);
    ~VirtualKeyboardWindow() override;

    void closeButtonPressed() override;
    /** Coming to the front hands the typing back to the panel, so the computer
        keyboard works without clicking the piano first. */
    void activeWindowStatusChanged() override;

    void show();
    /** Puts the window away, letting go of whatever it was playing. */
    void hide();
    void refresh();
    void refreshArpControls();
    void setKeysDown (const std::vector<int>& notes);
    /** What the status line calls "最近力度". */
    void setLastVelocity (int velocity);

    std::function<void (int midiNote, float velocity, juce::int64 trigger)> onNoteOn;
    std::function<void (int midiNote, juce::int64 trigger)> onNoteOff;
    std::function<void()> onArpSettingsChanged;

    VirtualKeyboardPanel& getPanel() noexcept { return *panel; }
    /** Self test: the panel is hung on the window as a key listener, which is what
        makes the computer keys work when the focus sits on the window itself. */
    bool isPanelOnKeyListenersForTest() const noexcept { return panelIsKeyListener; }
    juce::String getSettingsFileTextForTest() const;
    static void setSettingsFileForTesting (juce::File file);
    /** Where the remembered state lives: app level, like `midi.json`. */
    static juce::File getDefaultSettingsFile();
    /** The file that will really be read and written (the test override when the
        self test has set one). */
    static juce::File getSettingsFile();

private:
    /** The size and the always-on-top flag the settings ask for. */
    void applyWindowState();
    void saveSettings() const;

    VirtualKeyboardPanel* panel = nullptr;
    bool panelIsKeyListener = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualKeyboardWindow)
};

} // namespace nidaw
