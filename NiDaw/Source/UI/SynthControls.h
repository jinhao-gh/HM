#pragma once

#include "../Core/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace nidaw
{

/** Gives a parameter slider the compact rotary feel used by the hybrid
    synthesizer: vertical drag, value under the knob, no bar track. */
void configureSynthKnob (juce::Slider& slider);

/** Volume and gain stay bars: they are read at a glance and dragged like a fader. */
void configureVolumeBar (juce::Slider& slider);

/** An ADSR curve whose nodes can be dragged directly.

    The four values are the same ones the engines already use. Dragging the attack
    or release node changes its time; dragging the decay/sustain node changes the
    decay time or the sustain level. The callback is called once with `begin` when
    a gesture starts and then for every update, so the host can make one undo step.
*/
class EnvelopeEditorView : public juce::Component
{
public:
    struct Values
    {
        float attack  = 0.005f;
        float decay   = 0.200f;
        float sustain = 0.700f;
        float release = 0.300f;

        bool operator== (const Values&) const = default;
    };

    EnvelopeEditorView();

    void setValues (Values newValues);
    Values getValues() const noexcept { return values; }
    void setPlayhead (float phase, float value);
    void setTitle (const juce::String& newTitle) { title = newTitle; repaint(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    std::function<void (bool begin, Values)> onGesture;

    juce::Rectangle<int> getPlotBoundsForTest() const { return plot; }
    juce::Point<int> getAttackNodeForTest() const { return node1; }
    juce::Point<int> getSustainNodeForTest() const { return node2; }
    juce::Point<int> getReleaseNodeForTest() const { return node3; }

private:
    enum class Node { none, attack, sustain, release };

    Node nodeAt (juce::Point<int> point) const;
    void beginGesture (Node node, juce::Point<int> point);
    void updateGesture (juce::Point<int> point);
    void layoutNodes();

    Values values;
    float playheadPhase = 0.0f;
    float playheadValue = 0.0f;
    juce::String title;
    Values gestureStartValues;
    juce::Rectangle<int> plot;
    juce::Point<int> node0, node1, node2, node3;
    Node dragging = Node::none;
    juce::Point<int> dragStart;
    bool gestureOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditorView)
};

/** A filter magnitude response that can be played with directly.

    Horizontal drag moves the cutoff on a logarithmic axis and vertical drag moves
    resonance. Holding Shift while dragging changes the filter envelope amount.
    The view only reports values; the editor writes them to the track.
*/
class FilterResponseView : public juce::Component
{
public:
    struct Values
    {
        FilterMode mode = FilterMode::lowPass;
        int slopeDb = 24;
        float cutoffHz = 2000.0f;
        float resonance = 0.707f;
        float envAmount = 0.5f;
        float keyTrack = 0.0f;

        bool operator== (const Values&) const = default;
    };

    FilterResponseView();

    void setValues (Values newValues);
    Values getValues() const noexcept { return values; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void (bool begin, Values)> onGesture;
    std::function<void()> onRightClick;

    juce::Rectangle<int> getPlotBoundsForTest() const { return plot; }

    static double magnitudeAtHz (const Values& v, double hz) noexcept;

private:
    void updateFromPoint (juce::Point<int> point, bool shift);
    void beginGesture (juce::Point<int> point, bool shift);

    Values values;
    juce::Rectangle<int> plot;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterResponseView)
};

/** A small LFO waveform display with a live phase/value marker. */
class LfoEditorView : public juce::Component
{
public:
    LfoEditorView();

    void setValues (int waveform, float phase, float value);
    void paint (juce::Graphics& g) override;

private:
    int waveform = 0;
    float phase = 0.0f;
    float value = 0.0f;
};

} // namespace nidaw
