#pragma once

#include "../Audio/AudioEngine.h"
#include "../Audio/PluginHost.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "LaneHeader.h"
#include "SnapControl.h"
#include "TempoMarkerTrack.h"
#include "TimelineRuler.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** The arrangement area: a shared timeline on top, then one lane per instrument
    holding clips.

    Clips can be created (press and hold, then drag), moved (including to another
    track), resized from either edge, and looped by dragging the middle of the
    right edge. Clicking a clip opens it in the editor.
*/
class TrackArea : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    TrackArea (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine,
               PluginHostManager& pluginHost);
    ~TrackArea() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void refresh();
    /** Called when a whole new project has arrived (open / new): the lane headers
        have to be rebuilt even when the track ids happen to match, and the lane
        scroll goes back to the top. */
    void reloadForNewProject();

    /** Adds a track of the given kind (empty for a new project, + menu otherwise). */
    /** Adds a track and returns its id (0 when the limit is reached). */
    juce::int64 addTrack (TrackKind kind);
    /** Adds a VST track wired to one plugin. The picker calls this; the self test
        calls it directly, which is why it takes the plugin rather than a dialog. */
    juce::int64 addVstTrack (const juce::String& pluginName, const juce::String& pluginPath,
                             const juce::String& pluginUid);
    /** Asks which plugin to use and then adds the track. */
    void addVstTrackWithPicker();
    /** Asks for a file and drops it on the track as an audio clip. */
    void importAudioInto (juce::int64 trackId);
    /** Does the work of the above for a known file; returns the new clip id. */
    juce::int64 importAudioFile (juce::int64 trackId, const juce::File& file);
    void deleteTrack (juce::int64 trackId);
    /** Deletes without asking (the self test uses this instead of the dialog). */
    void deleteTrackNow (juce::int64 trackId);

    /** The + menu, and a hook so the self test can check its entries. */
    juce::PopupMenu buildAddMenu() const;
    std::vector<std::pair<juce::String, bool>> getAddMenuItemsForTest() const;

    /** Diagnostic hook: the instrument header of one lane, or nullptr. */
    LaneHeader* getLaneHeaderForTest (int laneIndex) const;
    /** Where that header sits in this component's coordinates (the headers live in
        a holder that only covers the lane area). */
    juce::Rectangle<int> getLaneHeaderBoundsForTest (int laneIndex) const;
    /** The lane area's own ruler, so the self test can prove the ruler and the
        clips are drawn with the same mapping. */
    TimelineRuler& getRuler() noexcept { return ruler; }

    /** Diagnostic hooks for the drag and auto-scroll checks. */
    juce::Rectangle<int> clipBoundsForTest (juce::int64 clipId) const;
    /** The note bars the clip's thumbnail would draw, in this component's
        coordinates (self test). */
    std::vector<juce::Rectangle<float>> getClipThumbnailRectsForTest (juce::int64 clipId) const;
    juce::int64 getDragPreviewEndTickForTest() const noexcept
    {
        return previewStartTick + previewLengthTicks;
    }
    /** Runs one auto-scroll step without waiting for the timer. */
    void tickAutoScrollForTest() { updateAutoScroll(); }
    bool isAutoScrollingForTest() const noexcept { return autoScrolling; }
    /** How far the lanes are scrolled up, in pixels. */
    int getLaneScrollForTest() const noexcept { return laneScroll; }
    int getMaxLaneScrollForTest() const { return maxLaneScroll(); }
    SnapControl& getSnapControlForTest() noexcept { return snapControl; }
    /** Scrolls the lanes by hand (self test): the wheel is the only user-facing way. */
    void setLaneScrollForTest (int pixels)
    {
        laneScroll = juce::jlimit (0, maxLaneScroll(), pixels);
        resized();
        repaint();
    }

    /** Set by MainComponent: opens the instrument editor for a track. */
    std::function<void (juce::int64 trackId)> onEditTimbre;
    /** 效果 in a lane header: the track's insert chain, in its own window. */
    std::function<void (juce::int64 trackId)> onEditEffects;
    /** A right click on a lane header: that track's own settings (see
        `MainComponent::showTrackMenu`). */
    std::function<void (juce::int64 trackId)> onTrackMenu;

    /** Set by MainComponent: puts a clip on the shared clip clipboard (the right
        click menu's 复制, which is the same thing Ctrl+C does). */
    std::function<void (juce::int64 clipId)> onCopyClip;
    /** 重命名… / 重复… from the clip menu: MainComponent owns the dialogs. */
    std::function<void (juce::int64 clipId)> onRenameClip;
    std::function<void (juce::int64 clipId)> onRepeatClip;

    /** What the right-click menu can do to a clip. */
    enum class ClipCommand { deleteClip, split, glue, copy, rename, toggleLoop, repeat };

    /** Runs one menu command. `atTick` is where the split goes (it is what the
        click position means); the other commands ignore it. */
    bool performClipCommand (ClipCommand command, juce::int64 clipId, juce::int64 atTick);

    /** The right-click menu for a clip, plus a diagnostic list of its entries and
        whether each one is enabled. */
    juce::PopupMenu buildClipMenu (juce::int64 clipId, juce::int64 atTick) const;
    /** One entry of the clip menu, as the menu itself holds it: its text, whether it
        can be picked, and whether it carries a tick (循环 does). */
    struct ClipMenuItemForTest
    {
        juce::String name;
        bool         enabled = true;
        bool         ticked  = false;
    };

    std::vector<ClipMenuItemForTest> getClipMenuItemsForTest (juce::int64 clipId,
                                                              juce::int64 atTick) const;

    /** Runs one clip-menu command by its menu id (1..7), exactly as the menu does;
        the self test uses this instead of a popup it cannot click. */
    bool performClipMenuCommandForTest (int menuId, juce::int64 clipId, juce::int64 atTick)
    {
        return menuId >= 1 && menuId <= 7
                   ? performClipCommand ((ClipCommand) (menuId - 1), clipId, atTick)
                   : false;
    }

    /** The snap grid in ticks at the current position (the menu and the split
        command both need it, so the two can never disagree). */
    juce::int64 snapTicksNow() const;

    /** Snapped tick for an x offset inside the lane area (used by the menu). */
    juce::int64 tickAtX (int x) const;

    /** Maps a tick to an x offset inside the lane area, and back. */
    double xForTick (double tick) const;
    double tickForX (double x) const;

    /** Diagnostic hooks for the offscreen self test. */
    bool isLongPressPendingForTest() const noexcept { return mode == Mode::pendingCreate; }
    bool isCreatingClipForTest() const noexcept { return mode == Mode::creating; }

    /** Completes the long press without waiting for the timer. */
    void forceLongPressForTest()
    {
        if (mode == Mode::pendingCreate)
        {
            stopTimer();
            mode = Mode::creating;
        }
    }

    static constexpr int laneHeight = 86;
    static constexpr int headerWidth = 250;
    static constexpr int topBarHeight = 24;
    static constexpr int markerHeight = 26;
    static constexpr int rulerHeight = 24;
    static constexpr int handleWidth = 6;
    static constexpr int longPressMs = 300;

private:
    enum class Mode { none, pendingCreate, creating, movingClip, resizingLeft, resizingRight, looping };
    enum class Zone { none, body, leftEdge, rightEdge, rightCorner };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    int laneCount() const;
    int lanesTop() const;
    int laneIndexAtY (int y) const;
    int laneOfTrack (juce::int64 trackId) const;
    juce::Rectangle<int> laneBounds (int laneIndex) const;
    /** The lane area's height: everything below the marker track and the ruler. */
    int lanesHeight() const;
    /** The furthest the lanes may be scrolled up, in pixels. */
    int maxLaneScroll() const;
    /** True when the headers still describe the project's tracks. */
    bool headersMatchTracks() const;
    juce::Rectangle<int> clipBounds (const Clip& clip, int laneIndex) const;
    Clip* clipAt (juce::Point<int> point, juce::int64* trackIdOut, int* laneOut) const;
    /** True when a cut at that tick leaves something on both sides of the clip. */
    bool canSplitClipAt (const Clip& clip, juce::int64 atTick) const;
    /** The clip that starts exactly where this one ends and could be glued on. */
    const Clip* findGlueNeighbour (const InstrumentTrack& track, const Clip& clip) const;
    Zone zoneAt (const Clip& clip, juce::Point<int> point, int laneIndex) const;
    juce::int64 snappedTick (double tick) const;
    void commitCreation();
    void commitClipDrag();
    /** Recomputes the live drag preview from a mouse position. Shared with the
        auto-scroll timer, which replays it with the pointer pinned to the edge. */
    void updateDragFromPoint (juce::Point<int> point);
    /** Scrolls the shared timeline while a drag sits against an edge, so a clip
        can be dragged past the end of the visible area. */
    void updateAutoScroll();
    void selectTrackOnly (int laneIndex);
    void rebuildLaneHeaders (bool force = false);
    /** Draws an audio clip's peak envelope inside its rectangle. */
    void drawSampleThumbnail (juce::Graphics& g, const Clip& clip,
                              juce::Rectangle<int> clipRect, juce::Rectangle<int> inner) const;
    /** The little note bars a clip's thumbnail is made of, in this component's
        coordinates.

        One source of truth for the painter and the self test, and built from the
        same expansion the engine plays (music::expandClip): a looped clip shows one
        period repeated at the real tick scale, which is what its repeat dividers
        and its sound do. Stretching one period across the whole clip - what this
        used to do - meant the thumbnail changed shape while the clip was resized. */
    std::vector<juce::Rectangle<float>> clipThumbnailBars (const Clip& clip, int laneIndex) const;

    ProjectDocument& document;
    AppState& appState;
    AudioEngine& audio;
    PluginHostManager& pluginHost;

    TempoMarkerTrack markerTrack;
    TimelineRuler ruler;
    SnapControl snapControl;
    /** Holds the lane headers, sized to the lane area so scrolled-up headers cannot
        paint over the + bar, the marker track or the ruler. */
    juce::Component lanesHolder;
    juce::TextButton addButton;
    juce::OwnedArray<LaneHeader> laneHeaders;

    Mode mode = Mode::none;
    int  pendingLane = -1;
    /** How far the lanes are scrolled up, in pixels: with more tracks than fit,
        the wheel scrolls them so every lane header stays reachable. */
    int  laneScroll = 0;
    juce::Point<int> dragStartPoint;
    bool dragStarted = false;

    juce::int64 creatingAnchorTick = 0;
    juce::int64 creatingCurrentTick = 0;
    juce::uint32 longPressStartMs = 0;

    /** True once the press has been held long enough to count as a long press. */
    bool longPressElapsed() const noexcept;

    juce::int64 dragClipId = 0;
    Clip dragOriginal;
    int  dragSourceLane = -1;
    int  dragTargetLane = -1;
    juce::int64 dragOriginalEnd = 0;
    juce::int64 dragAnchorTick = 0;
    /** Last mouse position seen during a drag, in this component's coordinates. */
    juce::Point<int> dragLastPoint;
    /** Set while the auto-scroll timer is running for a drag. */
    bool autoScrolling = false;

    /** Live preview of the clip being dragged, drawn instead of the original. */
    juce::int64 previewStartTick = 0;
    juce::int64 previewLengthTicks = 0;
    bool        previewLooped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackArea)
};

} // namespace nidaw
