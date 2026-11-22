#include "ArrangeKeyboardDock.h"
#include "Theme.h"

#include <algorithm>

namespace hmdaw
{

ArrangeKeyboardDock::ArrangeKeyboardDock (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse),
      keyboard (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setOpaque (true);

    addAndMakeVisible (titleLabel);
    titleLabel.setText (NI_TXT ("閿洏"), juce::dontSendNotification);
    titleLabel.setFont (theme::font (13.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (36, 84);
    keyboard.setLowestVisibleKey (48);
    keyboard.setKeyWidth (18.0f);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId,
                        juce::Colour (0xffe8e6df));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId,
                        juce::Colour (0xff202027));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId,
                        juce::Colour (0xff555560));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        theme::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                        theme::accent.withAlpha (0.55f));

    addAndMakeVisible (velocity);
    velocity.setSliderStyle (juce::Slider::LinearHorizontal);
    velocity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 18);
    velocity.setRange (1.0, 127.0, 1.0);
    velocity.setValue (100.0);

    addAndMakeVisible (octaveDown);
    addAndMakeVisible (octaveUp);
    octaveDown.onClick = [this] { setOctaveForTest (octave - 1); };
    octaveUp.onClick = [this] { setOctaveForTest (octave + 1); };

    addAndMakeVisible (octaveLabel);
    octaveLabel.setFont (theme::font (11.0f));
    octaveLabel.setColour (juce::Label::textColourId, theme::dimText);
    octaveLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (midiButton);
    midiButton.onClick = [this] { if (onOpenMidiInput) onOpenMidiInput(); };

    addAndMakeVisible (arpButton);
    arpButton.onClick = [this] { if (onOpenArp) onOpenArp(); };

    state.addListener (this);
    applyRange();
    setSize (kDesignWidth, kDesignHeight);
}

ArrangeKeyboardDock::~ArrangeKeyboardDock()
{
    state.removeListener (this);
    state.allNotesOff (1);
}

void ArrangeKeyboardDock::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds(), 1);
}

void ArrangeKeyboardDock::resized()
{
    auto r = getLocalBounds().reduced (8);

    auto top = r.removeFromTop (24);
    titleLabel.setBounds (top.removeFromLeft (50));
    midiButton.setBounds (top.removeFromRight (78));
    top.removeFromRight (4);
    arpButton.setBounds (top.removeFromRight (54));

    auto bottom = r.removeFromBottom (24);
    octaveDown.setBounds (bottom.removeFromLeft (24));
    bottom.removeFromLeft (2);
    octaveUp.setBounds (bottom.removeFromLeft (24));
    octaveLabel.setBounds (bottom.removeFromLeft (52));
    bottom.removeFromLeft (8);
    velocity.setBounds (bottom);

    keyboard.setBounds (r);
}

void ArrangeKeyboardDock::setOctaveForTest (int newOctave)
{
    octave = juce::jlimit (0, 7, newOctave);
    applyRange();
}

void ArrangeKeyboardDock::applyRange()
{
    const auto low = juce::jlimit (0, 127, 12 * octave);
    const auto high = juce::jlimit (low + 12, 127, low + 36);
    keyboard.setAvailableRange (low, high);
    keyboard.setLowestVisibleKey (low);
    octaveLabel.setText ("C" + juce::String (octave), juce::dontSendNotification);
}

void ArrangeKeyboardDock::setKeysDown (const std::vector<int>& notes)
{
    for (const auto note : notes)
        if (heldNotes.insert (note).second)
            state.noteOn (1, note, 100.0f);

    for (auto it = heldNotes.begin(); it != heldNotes.end();)
        if (std::find (notes.begin(), notes.end(), *it) == notes.end())
        {
            state.noteOff (1, *it, 0.0f);
            it = heldNotes.erase (it);
        }
        else
            ++it;
}

void ArrangeKeyboardDock::releaseAllNotes()
{
    state.allNotesOff (1);
    heldNotes.clear();
}

void ArrangeKeyboardDock::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel)
{
    if (onNoteOn)
        onNoteOn (note, vel * 127.0f, triggerBase + note);
}

void ArrangeKeyboardDock::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    if (onNoteOff)
        onNoteOff (note, triggerBase + note);
}

void ArrangeKeyboardDock::pressKeyForTest (int midiNote, float velocityValue)
{
    state.noteOn (1, midiNote, juce::jlimit (0.0f, 1.0f, velocityValue / 127.0f));
}

void ArrangeKeyboardDock::releaseKeyForTest (int midiNote)
{
    state.noteOff (1, midiNote, 0.0f);
}

} // namespace hmdaw

