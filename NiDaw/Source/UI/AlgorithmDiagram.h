#pragma once

#include "../Audio/SynthEngine.h"
#include "../Core/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace nidaw
{

/** One FM algorithm drawn as boxes and lines.

    The routing comes straight from `getFmAlgorithm()`: every operator is a box,
    an arrow runs from each modulator into the operator it feeds, and the carriers
    run down into an "输出" bar. Operators are stacked so that carriers sit at the
    bottom and every modulator is one row above the operator it feeds, which is
    what makes a stack like "6-5-4-3-2-1" read at a glance.

    Two sizes: the thumbnail used by the 32-picture library, and the full size used
    by the operator overview. Only the full size shows the ratio / level text and
    the per-operator switches.
*/
class AlgorithmDiagram : public juce::Component
{
public:
    AlgorithmDiagram();

    void setAlgorithm (int algorithm);            // 1..32
    int  getAlgorithm() const noexcept { return algorithm; }

    /** Supplies the numbers drawn on the boxes and the switch states. */
    void setPatch (const FmPatch& newPatch);

    void setThumbnail (bool shouldBeThumbnail);
    void setSelected (bool shouldBeSelected);      // the "this is the current one" frame
    void setHighlightedOperator (int index);       // -1 for none

    /** Fired when a box body is clicked; -1 when a switch was clicked instead. */
    std::function<void (int)> onOperatorClicked;
    /** Fired when the little switch at a box's top right corner is clicked. */
    std::function<void (int)> onOperatorToggled;
    /** Fired for any click, before the per-box handling. The library of 32
        thumbnails uses it so a click anywhere on the picture picks the algorithm. */
    std::function<void()> onDiagramClicked;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    /** The box for an operator, in local coordinates; empty when it is not laid
        out yet. Used by the self test and by the hit testing. */
    juce::Rectangle<int> getOperatorBox (int index) const;
    /** The little switch inside a box (full size only). */
    juce::Rectangle<int> getOperatorSwitch (int index) const;

private:
    /** How far each operator sits above the output: carriers at 0. */
    std::array<int, kFmOperators> computeDepths() const;
    void layoutBoxes();

    int algorithm = 1;
    FmPatch patch;
    bool thumbnail = true;
    bool selected = false;
    int highlighted = -1;

    std::array<juce::Rectangle<int>, kFmOperators> boxes;
    juce::Rectangle<int> outputBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlgorithmDiagram)
};

} // namespace nidaw
