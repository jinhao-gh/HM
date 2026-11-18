#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The clickable time ruler above a grid: bar numbers, beat ticks and the transport
    position marker. Left click or drag sets the playback start point.

    It draws with one pane's horizontal timeline (`timeline`), so its ticks line up
    exactly with the grid or lanes below it. The arrangement's ruler and the editor's
    ruler are independent of each other, which is why each one has to be told which
    it is.
*/
class TimelineRuler : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::ChangeListener
{
public:
    TimelineRuler (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine,
                   TimelineView timeline = TimelineView::editor);
    ~TimelineRuler() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    void refresh();
    void refreshPlayhead();

    /** Where the loop band was drawn last, for the offscreen self test. */
    juce::Rectangle<int> getLoopBandBoundsForTest() const { return loopBandBounds; }
    /** The x this ruler would put a tick at (self test): the lanes and the ruler of
        one pane must agree pixel for pixel. */
    double getXForTickForTest (double tick) const { return xForTick (tick); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    double xForTick (double tick) const;
    double tickForX (double x) const;
    juce::int64 snappedTick (double tick) const;
    void setPositionFromMouse (int x);
    void commitLoopDrag();
    /** The region being dragged, or the stored one when nothing is being dragged. */
    LoopRegion displayedLoop() const;

    ProjectDocument& document;
    AppState& appState;
    AudioEngine& audio;
    /** Which pane's horizontal timeline this ruler is drawn with. */
    TimelineView view;

    bool        loopDragging   = false;
    juce::int64 loopAnchorTick = 0;
    juce::int64 loopDragTick   = 0;
    juce::Rectangle<int> loopBandBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineRuler)
};

} // namespace hmdaw
