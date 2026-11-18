#pragma once

#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The thin strip above a grid. Left click creates or edits a marker; the marker
    carries the time signature and tempo that apply until the next one.

    Like the ruler, it is drawn with one pane's horizontal timeline: the
    arrangement's markers and the editor's are independent of each other.
*/
class TempoMarkerTrack : public juce::Component,
                         private juce::ChangeListener
{
public:
    TempoMarkerTrack (ProjectDocument& document, AppState& appState,
                      TimelineView timeline = TimelineView::editor);
    ~TempoMarkerTrack() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    void refresh();

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    double xForTick (double tick) const;
    int markerIndexAtX (int x) const;
    void editMarker (int index, juce::int64 tick, bool isNew);

    ProjectDocument& document;
    AppState& appState;
    /** Which pane's horizontal timeline this strip is drawn with. */
    TimelineView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoMarkerTrack)
};

} // namespace hmdaw
