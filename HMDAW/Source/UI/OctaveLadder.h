#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace hmdaw
{

/** The reference-group segment.

    A vertical log-frequency line, one octave tall for the frame, with the pitch
    classes of the current reference group drawn as dots. It can show several
    octaves at once (zoomed out) so the periodic repetition - the whole point of
    "frequencies an octave apart are the same class of pitch" - is visible.

    Ladder coordinates are "octave positions": an integer part is the octave index
    and a fractional part is the position inside that octave, so the frame octave
    spans 0 to 1.
*/
class OctaveLadder : public juce::Component,
                     public juce::ChangeBroadcaster,
                     private juce::ChangeListener
{
public:
    OctaveLadder (ProjectDocument& document, AppState& appState);
    ~OctaveLadder() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** What this ladder is standing in for, which is all the group editor changes
        about it: the plain straight line, and labels that are frequencies instead of
        ratios. The reference group column leaves these at their defaults. */
    struct Look
    {
        /** True: the octave line is one plain straight line, instead of a brighter
            stretch inside the frame. */
        bool plainLine = false;
        /** True: a dot is labelled with its frequency (the group pinned at the
            anchor below) instead of with its ratio inside the octave. */
        bool frequencyLabels = false;
        double anchorHz = 440.0;
        double anchorPosition = 0.0;
    };

    void setLook (Look newLook);
    const Look& getLook() const noexcept { return look; }

    /** Pressing 1..5 on the ladder asks for a ratio (index 0..4 = 1:2, 1:3, 1:5,
        1:7, 1:9). The panels set this to their own ratio button. */
    std::function<void (int index)> onRatioKey;

    /** Crosses produced by a ratio button: the selected dot x r and / r. */
    void showRatioCrosses (double ratio);

    /** Crosses dividing the two selected dots into parts equal pieces. */
    void showDivisionCrosses (int parts);

    void clearTransientCrosses();

    void refresh();

    /** Maps a ladder position to a point in this component's coordinates.
        Exposed so the offscreen self test can drive real mouse clicks. */
    juce::Point<float> pointForPosition (double ladderPosition) const;

    /** Position of the single selected dot, or -1. */
    double getSelectedDotPosition() const;
    int getSelectedDotCount() const noexcept { return (int) selectedDots.size(); }
    /** Every selected dot position, in selection order. The group controls colour
        all of them in one go, so this is a real accessor rather than a test hook. */
    const std::vector<double>& getSelectedDotPositions() const noexcept { return selectedDots; }

    /** True when the single selected dot is one of the two frame dots; those have
        no stored colour so the palette is disabled for them. */
    bool isFrameDotSelected() const;

    /** Diagnostic hook: the crosses currently shown on the segment. */
    std::vector<double> getCrossPositions() const;
    /** The caption each of those crosses carries (正 1:3 / 负 1:3 / 2/3 …). */
    juce::StringArray getCrossLabelsForTest() const;
    /** The caption drawn beside the cross under the pointer, or empty. */
    juce::String getHoverLabelForTest() const { return hoveredCross.label; }
    /** True while the rubber band is being dragged. */
    bool isMarqueeActiveForTest() const { return marqueeActive; }
    /** The selected dot positions, for "what did the band pick up". */
    std::vector<double> getSelectedDotPositionsForTest() const { return selectedDots; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    const ReferenceGroup& group() const;
    void commitGroupChange (const juce::String& undoName,
                            const std::function<void (ReferenceGroup&)>& mutation);

    double pixelsPerOctave() const;
    double yForPosition (double ladderPosition) const;
    double positionForY (double y) const;

    /** One proposed position, with the caption that explains it. */
    struct Cross
    {
        double position = 0.0;
        juce::String label;
    };

    std::vector<Cross> currentCrosses() const;
    std::vector<double> projectedPositions() const;
    bool isDotAt (double position) const;
    int findCrossAt (float y, double* positionOut) const;
    int findDotAt (float y, double* positionOut) const;
    void selectDot (double position, bool addToSelection);
    void clearDotSelection();
    /** Turns the cross at that position into a dot (one edit). */
    void promoteCross (double position);

    ProjectDocument& document;
    AppState& appState;

    Look look;
    std::vector<double> selectedDots;
    std::vector<Cross> transientCrosses;

    /** The rubber band: where it started, and where the pointer is now. */
    bool marqueeActive = false;
    juce::Point<float> marqueeStart, marqueeCurrent;
    /** A press on (or right beside) a dot or a cross arms a band instead of acting
        at once: a click is a press and release in place, a drag is a band. Without
        this, the 7 px hit radius around every dot swallowed the start of the
        gesture and nothing could be banded from next to one. */
    bool bandArmedAtPress = false;
    /** True when that press landed on a dot, so the band keeps it selected even if
        its rectangle starts a pixel or two outside the dot. */
    bool bandKeepsPressedDot = false;
    /** The cross the pointer went down on: it is promoted on release, but only if
        the press turns out to be a click rather than the start of a band. */
    bool pendingCrossValid = false;
    double pendingCrossPosition = 0.0;
    /** The cross under the pointer, so its caption can be drawn beside it. */
    Cross hoveredCross;

    double visibleOctaves = 1.0;
    double viewCentre = 0.5;      // ladder position at the vertical centre

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaveLadder)
};

} // namespace hmdaw
