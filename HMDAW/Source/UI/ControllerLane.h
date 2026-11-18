#pragma once

#include "../Core/ControllerMath.h"
#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "SnapControl.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hmdaw
{

/** The controller lane under the editor's canvas: Cubase's "controller lane".

    One row of the editor, sharing the canvas's horizontal mapping exactly, showing
    either the **velocity** of the open clip's notes as a bar per note (drag it up and
    down) or one **MIDI controller** as a curve the user draws with the mouse. The
    curve lives in the clip (ControllerLane), so it tiles with the notes when the clip
    loops, and the engine plays it back - see AudioEngine::processControllers.

    Everything a gesture changes goes through the document, so a drag is one undo
    step, and while nothing is selected or the clip is an audio clip the lane simply
    says so.
*/
class ControllerLane : public juce::Component,
                       private juce::ChangeListener
{
public:
    ControllerLane (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine);
    ~ControllerLane() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** Re-reads the selection and repaints (selection, document, mode changes). */
    void refresh();
    /** Moves the playhead line without repainting the whole lane. */
    void refreshPlayhead();

    /** Height of the header row: the signal picker and the read-out live there. */
    static constexpr int headerHeight = 24;
    /** How tall the lane is the first time (before anyone drags it). */
    static constexpr int defaultHeight = 110;
    /** The lane never goes below this, however far the handle is dragged. */
    static constexpr int minHeight = 48;
    /** The narrow strip along the left of the plot carrying the value scale. */
    static constexpr int scaleWidth = 30;

    /** One entry of the signal picker: "力度" or a named controller. */
    struct Signal { int controller; const char* label; };

    /** The named controllers the picker offers, in the order they appear (the
        velocity row is added on top of these). Numbers the host uses itself are
        deliberately absent. */
    static const std::vector<Signal>& namedSignals();

    // ---- test hooks -------------------------------------------------------
    /** The plot area, i.e. everything below the header. */
    juce::Rectangle<int> getPlotBoundsForTest() const;
    juce::Rectangle<int> getHeaderBoundsForTest() const;
    SnapControl& getSnapControlForTest() noexcept { return snapControl; }
    int getSignalComboIdForTest() const { return signalCombo.getSelectedId(); }
    juce::String getSignalComboTextForTest() const { return signalCombo.getText(); }
    juce::Rectangle<int> getSignalComboBoundsForTest() const { return signalCombo.getBounds(); }
    bool isCustomEditorVisibleForTest() const { return customControllerEditor.isVisible(); }
    juce::Rectangle<int> getCustomEditorBoundsForTest() const
    {
        return customControllerEditor.getBounds();
    }
    /** Types a controller number the way the field's Enter would. */
    void setCustomControllerForTest (int controller);
    juce::String getReadoutTextForTest() const { return readoutLabel.getText(); }

    /** Picks a signal through the same path the combo uses. */
    void selectSignalForTest (bool showsVelocity, int controller);

    /** The bars the velocity view would draw, in plot coordinates - produced by the
        same code that paints them, so "what is drawn" and "what is checked" cannot
        drift apart. */
    std::vector<juce::Rectangle<int>> getVelocityBarsForTest() const;
    /** The polyline the CC view would draw, plus the knee squares. */
    std::vector<juce::Point<int>> getCurvePointsForTest() const;
    std::vector<juce::Rectangle<int>> getCurveKneesForTest() const;
    /** Where a tick / a value lands in the plot (the mapping the mouse uses). */
    int getXForTickForTest (double tick) const;
    int getYForValueForTest (double value) const;
    double getTickForXForTest (double x) const;
    int getValueForYForTest (double y) const;
    /** The clip the lane is editing right now (0 when there is none). */
    juce::int64 getClipIdForTest() const;
    /** How many times the panel has told the lane to follow the view. The lane
        follows the editor's horizontal view, so it is redrawn whole on that pass -
        the same pass the ruler and the marker track use (self test). */
    int getViewRefreshesForTest() const { return viewRefreshes; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    const Clip* openClip() const;
    const InstrumentTrack* openTrack() const;
    bool isAudioClip() const;
    bool isDrumClip() const;

    /** The editor's horizontal view, the same one the grid uses. */
    const EditorViewState& view() const;

    int yForValue (double value) const;
    double valueForY (double y) const;
    double xForTick (double tick) const;
    double tickForX (double x) const;
    /** The pointer's tick, quantized by the editor's 吸附 setting. */
    juce::int64 snappedTickForX (double x) const;
    double snapGridTicksAt (juce::int64 tick) const;

    /** Which knee is within a few pixels of the pointer, or nullptr. */
    ControllerPoint* kneeNear (juce::Point<int> point, juce::int64& tickOut);
    /** The velocity bar under the pointer (as an index into the drawn bars). */
    int velocityBarNear (juce::Point<int> point) const;

    void rebuildSignalList();
    void applyLaneSignalFromCombo();
    void commitCustomController (const juce::String& text);
    juce::String readoutText() const;
    void setReadout (const juce::String& text);
    void simplifyCurveGesture (juce::int64 fromTick, juce::int64 toTick);

    /** The notes of the open clip as they actually sound (tiled by the loop). */
    struct BarHit { juce::int64 noteId = 0; juce::Rectangle<int> bounds; };
    std::vector<BarHit> velocityBars() const;
    /** The area the values are drawn in: everything below the header. */
    juce::Rectangle<int> plotBounds() const;
    /** The polyline of the open clip's curve, knees only, tiled by the loop. */
    std::vector<juce::Point<int>> curvePoints() const;

    ProjectDocument& document;
    AppState& appState;
    AudioEngine& audio;
    SnapControl snapControl;

    juce::ComboBox signalCombo;
    juce::TextEditor customControllerEditor;
    juce::Label readoutLabel;

    /** What a gesture is doing right now. */
    enum class Drag { none, velocity, moveKnee, drawCurve, drawLine };
    Drag drag = Drag::none;

    /** Velocity drag: where it started, and the velocities it started from. */
    int velocityDragStartY = 0;
    std::vector<std::pair<juce::int64, int>> velocityStartValues;

    /** CC drag state: the knee being moved, and where the gesture began. */
    juce::int64 dragKneeTick = 0;
    juce::Point<int> dragStartPoint;
    juce::int64 lastDrawnTick = -1;
    int lastDrawnPixelX = -1;
    juce::int64 curveGestureStartTick = 0;
    juce::int64 curveGestureEndTick = 0;
    juce::int64 lineStartTick = 0;
    int lineStartValue = 0;

    /** Counts the "follow the view" passes, for the self test. */
    int viewRefreshes = 0;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControllerLane)
};

} // namespace hmdaw
