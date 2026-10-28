#include "TrackArea.h"
#include "Theme.h"
#include "PluginPicker.h"
#include "AutoScroll.h"
#include "../Core/FmPresets.h"
#include "../Core/SubtractivePresets.h"
#include "../Core/WavetablePresets.h"
#include "../Audio/SamplePlayer.h"

namespace nidaw
{

namespace
{
    constexpr int kClipInset = 6;   // vertical inset of a clip inside its lane

}

TrackArea::TrackArea (ProjectDocument& documentToUse, AppState& appStateToUse, AudioEngine& audioToUse,
                      PluginHostManager& pluginHostToUse)
    : document (documentToUse),
      appState (appStateToUse),
      audio (audioToUse),
      pluginHost (pluginHostToUse),
      // The lanes, their ruler and their marker strip all draw with the
      // arrangement's own timeline, which the editor's does not touch.
      markerTrack (documentToUse, appStateToUse, TimelineView::arrangement),
      ruler (documentToUse, appStateToUse, audioToUse, TimelineView::arrangement),
      snapControl (documentToUse, appStateToUse, SnapArea::track)
{
    addAndMakeVisible (markerTrack);
    addAndMakeVisible (ruler);
    addAndMakeVisible (snapControl);
    // The lane headers live in their own holder, which covers exactly the lane
    // area: scrolled up, they would otherwise paint over the + bar, the tempo
    // markers and the ruler (they are children of the whole track area).
    addAndMakeVisible (lanesHolder);
    // ...but the holder itself must not *catch* anything: a plain Component takes
    // every click that lands on it, which would leave the lanes below it unable to
    // select a track, drag a clip or read the mouse wheel. Only its children (the
    // headers) keep their clicks; the rest falls through to the track area.
    lanesHolder.setInterceptsMouseClicks (false, true);

    // The lanes are a canvas, not a form: clicking one has to land on something that
    // wants the keyboard, otherwise JUCE hands the focus to the first focusable child
    // it can find - the first track's name box, which is what made clicking empty
    // space look like "it jumped into renaming".
    setWantsKeyboardFocus (true);

    addAndMakeVisible (addButton);
    addButton.setButtonText (NI_TXT ("＋ 新建轨道"));
    addButton.setTooltip (NI_TXT ("选择要新建的轨道类型"));
    addButton.onClick = [this]
    {
        buildAddMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                                      [this] (int result)
        {
            if (result == 1)
                addTrack (TrackKind::synth);
            else if (result == 2)
                addTrack (TrackKind::subtractive);
            else if (result == 3)
                addTrack (TrackKind::wavetable);
            else if (result == 4)
                addTrack (TrackKind::drums);
            else if (result == 5)
                addTrack (TrackKind::sampler);
            else if (result == 6)
            {
                // A granular track is silent until a file is imported, but it is
                // still the instrument the user asked for: open the editor, which
                // says so and has the 导入音频… button.
                addTrack (TrackKind::granular);
            }
            else if (result == 7)
            {
                // A file is the whole point of an audio track, so ask for one now.
                if (const auto id = addTrack (TrackKind::audio); id != 0)
                    importAudioInto (id);
            }
            else if (result == 8)
            {
                // Which plugin the track gets is the whole point, so ask first.
                addVstTrackWithPicker();
            }
            else if (result == 9)
            {
                addTrack (TrackKind::hybrid);
            }
        });
    };

    document.addChangeListener (this);
    appState.addChangeListener (this);

    rebuildLaneHeaders();
}

juce::int64 TrackArea::addVstTrack (const juce::String& pluginName, const juce::String& pluginPath,
                                    const juce::String& pluginUid)
{
    if ((int) document.getProject().tracks.size() >= kMaxTracks)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                NI_TXT ("无法新建轨道"),
                                                NI_TXT ("轨道数已达上限（")
                                                    + juce::String (kMaxTracks) + NI_TXT (" 条）。"));
        return 0;
    }

    juce::int64 newTrackId = 0;

    document.applyEdit (NI_TXT ("新建轨道"), [&newTrackId, &pluginName, &pluginPath, &pluginUid]
                                              (Project& project)
    {
        newTrackId = nidaw::addTrack (project, TrackKind::vst);

        if (auto* track = project.findTrack (newTrackId))
        {
            const auto name = pluginName.isNotEmpty() ? pluginName : NI_TXT ("VST 乐器");
            track->name      = name;
            track->vst.name  = name;
            track->vst.pluginPath = pluginPath;
            track->vst.pluginUid  = pluginUid;
        }
    });

    if (newTrackId == 0)
        return 0;

    appState.selectTrack (newTrackId);
    appState.setEditorOpen (true);
    rebuildLaneHeaders();

    if (const auto* track = document.getProject().findTrack (newTrackId))
        pluginHost.requestInstrument (newTrackId, track->vst);

    return newTrackId;
}

void TrackArea::addVstTrackWithPicker()
{
    auto* picker = new PluginPicker (pluginHost, NI_TXT ("选择 VST 乐器"));

    picker->onChosen = [this] (const juce::PluginDescription& description)
    {
        addVstTrack (description.name, description.fileOrIdentifier,
                     juce::String ((juce::int64) description.uniqueId));
    };

    picker->launch (getTopLevelComponent());
}

juce::int64 TrackArea::addTrack (TrackKind kind)
{
    if ((int) document.getProject().tracks.size() >= kMaxTracks)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                NI_TXT ("无法新建轨道"),
                                                NI_TXT ("轨道数已达上限（")
                                                    + juce::String (kMaxTracks) + NI_TXT (" 条）。"));
        return 0;
    }

    juce::int64 newTrackId = 0;

    document.applyEdit (NI_TXT ("新建轨道"), [kind, &newTrackId] (Project& project)
    {
        newTrackId = nidaw::addTrack (project, kind);

        // A brand new synth track starts on the electric piano rather than on a bare
        // sine, so pressing a key immediately plays something musical.
        if (kind == TrackKind::synth)
            if (auto* track = project.findTrack (newTrackId))
                fm::applyPreset (track->synth.patch, 0);

        // Same idea for the subtractive one: start on the warm pad.
        if (kind == TrackKind::subtractive)
            if (auto* track = project.findTrack (newTrackId))
                subtractive::applyPreset (track->subtractive, 0);

        if (kind == TrackKind::wavetable)
            if (auto* track = project.findTrack (newTrackId))
                wavetable::applyPreset (track->wavetable, 0);

        if (kind == TrackKind::hybrid)
            if (auto* track = project.findTrack (newTrackId))
            {
                auto& layer = track->hybrid.layers[0];
                layer.mode = HybridLayerMode::wavetable;
                wavetable::applyPreset (layer.wavetable, 0);
                track->hybrid.name = NI_TXT ("初始化混合音色");
            }
    });

    if (newTrackId == 0)
        return 0;

    appState.selectTrack (newTrackId);
    appState.setEditorOpen (true);
    rebuildLaneHeaders();
    return newTrackId;
}

void TrackArea::importAudioInto (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (NI_TXT ("导入音频"),
                                                        juce::File::getSpecialLocation (
                                                            juce::File::userMusicDirectory),
                                                        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<TrackArea> (this),
                           chooser, trackId] (const juce::FileChooser& fc)
    {
        // The window can close while the system dialog is up.
        if (safeThis == nullptr)
            return;

        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        if (safeThis->importAudioFile (trackId, file) == 0)
            return;
    });
}

juce::int64 TrackArea::importAudioFile (juce::int64 trackId, const juce::File& file)
{
    if (document.getProject().findTrack (trackId) == nullptr)
        return 0;

    juce::String error;
    auto sample = getCachedSample (file.getFullPathName(), error);

    if (sample == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                NI_TXT ("无法导入音频"), error);
        return 0;
    }

    const auto duration = sample->lengthSeconds();
    const auto path = file.getFullPathName();
    const auto name = file.getFileName();
    const auto clipName = file.getFileNameWithoutExtension();
    const auto desiredStart = music::TempoMap::snapToGrid (
        juce::jmax ((juce::int64) 0,
                    document.makeTempoMap().tickAtSeconds (audio.getPositionSeconds())),
        kTicksPerQuarter);

    juce::int64 newClipId = 0;

    document.applyEdit (NI_TXT ("导入音频"), [trackId, path, name, clipName, duration,
                                              desiredStart, &newClipId] (Project& project)
    {
        auto* target = project.findTrack (trackId);

        if (target == nullptr)
            return;

        const music::TempoMap tempoMap (project.tempoMarkers);
        const auto startSeconds = tempoMap.secondsAtTick (desiredStart);
        const auto lengthTicks = juce::jmax (kMinClipTicks,
                                             tempoMap.tickAtSeconds (startSeconds + duration)
                                                 - desiredStart);

        Clip clip = makeDefaultClip (project);
        clip.name = clipName;
        clip.audioFilePath = path;
        clip.audioFileName = name;
        clip.audioOffsetSeconds = 0.0;
        clip.audioGain = 1.0f;
        clip.startTick = music::findFreeSlot (*target, desiredStart, lengthTicks);
        clip.lengthTicks = lengthTicks;
        clip.loopLengthTicks = lengthTicks;
        newClipId = clip.id;

        target->clips.push_back (std::move (clip));
    });

    if (newClipId == 0)
        return 0;

    appState.selectTrack (trackId);
    appState.selectClip (newClipId);
    appState.setEditorOpen (true);
    rebuildLaneHeaders();
    return newClipId;
}

void TrackArea::deleteTrack (juce::int64 trackId)
{
    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    const auto name = track->name;

    juce::AlertWindow window (NI_TXT ("删除轨道"),
                              NI_TXT ("确定删除「") + name + NI_TXT ("」及其全部内容吗？"),
                              juce::MessageBoxIconType::WarningIcon, this);
    window.addButton (NI_TXT ("删除"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (window.runModalLoop() != 1)
        return;

    deleteTrackNow (trackId);
}

void TrackArea::deleteTrackNow (juce::int64 trackId)
{
    if (document.getProject().findTrack (trackId) == nullptr)
        return;

    // The plugin's state is only knowable while it is loaded, and this is about to
    // take the instance away. Capturing it first (as a derived edit, so it is not an
    // undo step of its own) means "delete, then undo" brings the track back playing
    // what it was playing, instead of dropping to the last saved sound.
    document.applyDerivedEdit ([this, trackId] (Project& project)
    {
        pluginHost.captureStateFor (trackId, project);
    });

    document.applyEdit (NI_TXT ("删除轨道"), [trackId] (Project& project)
    {
        auto& all = project.tracks;
        all.erase (std::remove_if (all.begin(), all.end(),
                                   [trackId] (const InstrumentTrack& t) { return t.id == trackId; }),
                   all.end());
    });

    // The plugin itself is kept loaded (see PluginHostManager), but this track no
    // longer owns it: a new track with the same id must not inherit it.
    pluginHost.forgetTrack (trackId);

    if (appState.selectedTrackId == trackId)
    {
        const auto& remaining = document.getProject().tracks;
        appState.selectTrack (remaining.empty() ? (juce::int64) 0 : remaining.front().id);
        appState.clearNotes();
    }

    rebuildLaneHeaders();
}

juce::PopupMenu TrackArea::buildAddMenu() const
{
    juce::PopupMenu menu;
    // The two synths come first, then the two kinds that bring their own audio.
    // The hybrid is the new headline instrument, so it sits at the top of the
    // menu instead of below every legacy kind.
    menu.addItem (9, NI_TXT ("混合合成器"), true);
    menu.addItem (1, NI_TXT ("FM合成器"), true);
    menu.addItem (4, NI_TXT ("鼓机"), true);
    menu.addItem (5, NI_TXT ("采样器"), true);
    menu.addItem (6, NI_TXT ("粒子合成器"), true);
    menu.addItem (7, NI_TXT ("音频"), true);
    menu.addItem (8, NI_TXT ("VST 乐器"), true);
    return menu;
}

std::vector<std::pair<juce::String, bool>> TrackArea::getAddMenuItemsForTest() const
{
    std::vector<std::pair<juce::String, bool>> items;
    const auto menu = buildAddMenu();

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.push_back ({ it.getItem().text, it.getItem().isEnabled });

    return items;
}

LaneHeader* TrackArea::getLaneHeaderForTest (int laneIndex) const
{
    if (laneIndex < 0 || laneIndex >= laneHeaders.size())
        return nullptr;

    return laneHeaders[laneIndex];
}

juce::Rectangle<int> TrackArea::getLaneHeaderBoundsForTest (int laneIndex) const
{
    if (laneIndex < 0 || laneIndex >= laneHeaders.size())
        return {};

    return { 0, laneBounds (laneIndex).getY(), headerWidth, laneHeight };
}

TrackArea::~TrackArea()
{
    stopTimer();
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

//==============================================================================
double TrackArea::xForTick (double tick) const
{
    return xForTimelineTick (document.viewState(), TimelineView::arrangement, tick);
}

double TrackArea::tickForX (double x) const
{
    return timelineTickForX (document.viewState(), TimelineView::arrangement, x);
}

juce::int64 TrackArea::snappedTick (double tick) const
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto rounded = (juce::int64) std::llround (juce::jmax (0.0, tick));

    // 不吸附: clips land on the exact tick under the pointer. The subdivision is
    // still what snapTicksNow() reports, so the minimum sizes stay as they were.
    if (! document.viewState().trackSnap.enabled)
        return rounded;

    const auto divisor = document.viewState().trackSnap.clampedDenominator();
    const auto grid = juce::jmax (1.0, tempoMap.ticksPerBeatAt (rounded) / (double) divisor);

    return juce::jmax ((juce::int64) 0, music::TempoMap::snapToGrid (rounded, grid));
}

int TrackArea::laneCount() const
{
    return (int) document.getProject().tracks.size();
}

int TrackArea::lanesTop() const
{
    return topBarHeight + markerHeight + rulerHeight;
}

juce::Rectangle<int> TrackArea::laneBounds (int laneIndex) const
{
    return { 0, lanesTop() + laneIndex * laneHeight - laneScroll, getWidth(), laneHeight };
}

int TrackArea::lanesHeight() const
{
    return juce::jmax (0, getHeight() - lanesTop());
}

int TrackArea::maxLaneScroll() const
{
    return juce::jmax (0, laneCount() * laneHeight - lanesHeight());
}

int TrackArea::laneIndexAtY (int y) const
{
    const auto index = (y - lanesTop() + laneScroll) / laneHeight;

    if (index < 0 || index >= laneCount())
        return -1;

    return index;
}

int TrackArea::laneOfTrack (juce::int64 trackId) const
{
    const auto& tracks = document.getProject().tracks;

    for (int i = 0; i < (int) tracks.size(); ++i)
        if (tracks[(size_t) i].id == trackId)
            return i;

    return -1;
}

juce::Rectangle<int> TrackArea::clipBounds (const Clip& clip, int laneIndex) const
{
    const auto lane = laneBounds (laneIndex);
    const auto left  = headerWidth + (int) xForTick ((double) clip.startTick);
    const auto right = headerWidth + (int) xForTick ((double) clip.endTick());

    return { left, lane.getY() + kClipInset, juce::jmax (4, right - left), laneHeight - kClipInset * 2 };
}

Clip* TrackArea::clipAt (juce::Point<int> point, juce::int64* trackIdOut, int* laneOut) const
{
    const auto laneIndex = laneIndexAtY (point.y);

    if (laneIndex < 0 || point.x < headerWidth)
        return nullptr;

    auto& track = const_cast<Project&> (document.getProject()).tracks[(size_t) laneIndex];

    for (auto it = track.clips.rbegin(); it != track.clips.rend(); ++it)
    {
        if (clipBounds (*it, laneIndex).contains (point))
        {
            if (trackIdOut != nullptr)
                *trackIdOut = track.id;

            if (laneOut != nullptr)
                *laneOut = laneIndex;

            return &(*it);
        }
    }

    return nullptr;
}

//==============================================================================
void TrackArea::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A project that was just opened brings a different set of tracks, and the
    // headers do not follow a repaint: without this the instrument column keeps
    // showing the previous project's tracks (names, kinds, buttons and all).
    if (! headersMatchTracks())
        rebuildLaneHeaders();

    repaint();
}

void TrackArea::refresh()
{
    rebuildLaneHeaders();
    snapControl.refresh();
    repaint();
}

void TrackArea::reloadForNewProject()
{
    // The ids of two projects often coincide (both start at 1), so a new project
    // has to force the rebuild; and its lanes start at the top.
    laneScroll = 0;
    rebuildLaneHeaders (true);
    snapControl.refresh();
    resized();
    repaint();
}

bool TrackArea::headersMatchTracks() const
{
    const auto& tracks = document.getProject().tracks;

    if (laneHeaders.size() != (int) tracks.size())
        return false;

    for (int i = 0; i < laneHeaders.size(); ++i)
        if (laneHeaders[i]->getTrackId() != tracks[(size_t) i].id)
            return false;

    return true;
}

void TrackArea::rebuildLaneHeaders (bool force)
{
    const auto& tracks = document.getProject().tracks;

    // Only rebuild when the set of tracks changed: the headers own text editors and
    // sliders, and rebuilding them mid-edit would steal focus.
    const auto sameTracks = ! force && headersMatchTracks();

    if (! sameTracks)
    {
        laneHeaders.clear();

        for (const auto& track : tracks)
        {
            auto* header = laneHeaders.add (new LaneHeader (document, appState, track.id));

            header->onSelect = [this] (juce::int64 trackId) { selectTrackOnly (laneOfTrack (trackId)); };
            header->onEditTimbre = [this] (juce::int64 trackId)
            {
                if (onEditTimbre != nullptr)
                    onEditTimbre (trackId);
            };
            header->onImportAudio = [this] (juce::int64 trackId) { importAudioInto (trackId); };
            header->onEditEffects = [this] (juce::int64 trackId)
            {
                if (onEditEffects != nullptr)
                    onEditEffects (trackId);
            };
            header->onMenu = [this] (juce::int64 trackId)
            {
                if (onTrackMenu != nullptr)
                    onTrackMenu (trackId);
            };
            header->onDelete = [this] (juce::int64 trackId) { deleteTrack (trackId); };

            lanesHolder.addAndMakeVisible (header);
        }
    }

    for (auto* header : laneHeaders)
        header->refresh();

    resized();
    repaint();
}

void TrackArea::timerCallback()
{
    // The timer has two jobs: the one-shot long press cue, and repeating the edge
    // scroll while a drag is in flight.
    if (mode == Mode::pendingCreate)
    {
        stopTimer();

        // The timer only provides the visual cue; whether a press counts as a long
        // press is decided by elapsed time, so a message loop that does not service
        // timers (the offscreen self test) can still create clips.
        mode = Mode::creating;
        repaint();
        return;
    }

    if (autoScrolling)
        updateAutoScroll();
}

bool TrackArea::longPressElapsed() const noexcept
{
    return juce::Time::getMillisecondCounter() - longPressStartMs >= (juce::uint32) longPressMs;
}

juce::Rectangle<int> TrackArea::clipBoundsForTest (juce::int64 clipId) const
{
    const auto& project = document.getProject();

    for (int lane = 0; lane < (int) project.tracks.size(); ++lane)
        if (const auto* clip = project.tracks[(size_t) lane].findClip (clipId))
            return clipBounds (*clip, lane);

    return {};
}

std::vector<juce::Rectangle<float>> TrackArea::clipThumbnailBars (const Clip& clip,
                                                                 int laneIndex) const
{
    std::vector<juce::Rectangle<float>> bars;

    if (clip.isAudio() || clip.lengthTicks <= 0)
        return bars;

    const auto clipRect = clipBounds (clip, laneIndex);
    auto inner = clipRect.reduced (5, 3);
    inner.removeFromTop (13);        // the clip's name is drawn here

    if (inner.getHeight() <= 6)
        return bars;

    const auto pixelsPerTick = pixelsPerQuarterFor (document.viewState(),
                                                    TimelineView::arrangement)
                               / (double) kTicksPerQuarter;

    // The same expansion the engine plays, so the picture cannot disagree with the
    // sound: one period of a looped clip, repeated at the real tick scale and cut
    // off at the clip's window.
    for (const auto& placement : music::expandClip (clip))
    {
        if (placement.note == nullptr)
            continue;

        const auto left  = (double) clipRect.getX()
                           + (double) (placement.startTick - clip.startTick) * pixelsPerTick;
        const auto right = (double) clipRect.getX()
                           + (double) (placement.endTick - clip.startTick) * pixelsPerTick;

        // Frequency mapped over the track's own range keeps the shape readable.
        const auto y = (float) inner.getY()
                     + (1.0f - juce::jlimit (0.0f, 1.0f,
                                             (float) (std::log2 (placement.note->frequencyHz) - 4.0)
                                                 / 6.0f))
                           * (float) inner.getHeight();

        bars.push_back (juce::Rectangle<float> ((float) left, y - 1.5f,
                                                juce::jmax (2.0f, (float) (right - left)), 3.0f));
    }

    return bars;
}

std::vector<juce::Rectangle<float>> TrackArea::getClipThumbnailRectsForTest (juce::int64 clipId) const
{
    const auto& project = document.getProject();

    for (int lane = 0; lane < (int) project.tracks.size(); ++lane)
        if (const auto* clip = project.tracks[(size_t) lane].findClip (clipId))
            return clipThumbnailBars (*clip, lane);

    return {};
}

void TrackArea::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (topBarHeight);
    // The + button sits at the left, directly above the instrument column.
    addButton.setBounds (6, top.getY() + 2, 116, topBarHeight - 4);
    snapControl.setBounds (128, top.getY() + 2, 106, topBarHeight - 4);

    markerTrack.setBounds (area.removeFromTop (markerHeight).withTrimmedLeft (headerWidth));
    ruler.setBounds (area.removeFromTop (rulerHeight).withTrimmedLeft (headerWidth));

    laneScroll = juce::jlimit (0, maxLaneScroll(), laneScroll);

    lanesHolder.setBounds (0, lanesTop(), getWidth(), lanesHeight());

    for (int i = 0; i < laneHeaders.size(); ++i)
        laneHeaders[i]->setBounds (0, laneBounds (i).getY() - lanesTop(),
                                   headerWidth, laneHeight);
}

void TrackArea::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // The view is shared with the editor, so the same gestures work here.
    const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (delta == 0.0f || delta == 0.0f)
        return;

    if (e.mods.isAltDown())
    {
        const auto local = (double) e.position.x - (double) headerWidth;
        const auto anchorTick = tickForX (local);
        auto& view = document.viewState();
        const auto zoom = pixelsPerQuarterFor (view, TimelineView::arrangement)
                          * std::pow (2.0, (double) delta * 0.6);

        setPixelsPerQuarterFor (view, TimelineView::arrangement, zoom);
        setHorizontalOffsetFor (view, TimelineView::arrangement,
                                anchorTick - local * (double) kTicksPerQuarter
                                                 / pixelsPerQuarterFor (view, TimelineView::arrangement));
    }
    else if (e.mods.isShiftDown())
    {
        // Shift+wheel pans the timeline (this used to be the plain wheel).
        auto& view = document.viewState();
        const auto visible = (double) getWidth() * (double) kTicksPerQuarter
                             / pixelsPerQuarterFor (view, TimelineView::arrangement);

        setHorizontalOffsetFor (view, TimelineView::arrangement,
                                horizontalOffsetFor (view, TimelineView::arrangement)
                                    - (double) delta * visible * 0.25);
    }
    else
    {
        // Plain wheel: move up and down the lanes, so a track whose header sits
        // below the visible area is still reachable (the same direction as the
        // editor's plain-wheel scroll: up means "earlier").
        const auto step = (double) laneHeight * 0.5;
        laneScroll = juce::jlimit (0, maxLaneScroll(),
                                   laneScroll - (int) std::lround ((double) delta * step));
        resized();
    }

    repaint();
}

void TrackArea::mouseDown (const juce::MouseEvent& e)
{
    // A click here ends any inline editing that was going on (the lane header's
    // name editor, most of all): the lane area is not focusable itself, so without
    // this the editor keeps the focus and stays in its editing state.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (isParentOf (focused))
            focused->giveAwayKeyboardFocus();

    const auto point = e.getPosition();
    const auto laneIndex = laneIndexAtY (point.y);

    dragStarted = false;
    dragStartPoint = point;
    mode = Mode::none;
    pendingLane = -1;

    juce::int64 trackId = 0;
    int clipLane = -1;
    auto* clip = clipAt (point, &trackId, &clipLane);

    if (e.mods.isPopupMenu())
    {
        if (clip == nullptr)
            return;

        // Right clicking a clip opens a menu; it no longer deletes it outright.
        const auto clipId = clip->id;
        const auto atTick = tickAtX (point.x - headerWidth);
        auto menu = buildClipMenu (clipId, atTick);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                        .withTargetScreenArea ({ e.getScreenX(),
                                                                                 e.getScreenY(), 1, 1 }),
                            [this, clipId, atTick] (int result)
                            {
                                if (result == 0)
                                    return;

                                performClipCommand ((ClipCommand) (result - 1), clipId, atTick);
                            });

        return;
    }

    if (clip != nullptr)
    {
        appState.selectTrack (trackId);
        appState.selectClip (clip->id);
        appState.setEditorOpen (true);

        dragClipId     = clip->id;
        dragOriginal   = *clip;
        dragSourceLane = clipLane;
        dragTargetLane = clipLane;
        dragOriginalEnd = clip->endTick();
        dragAnchorTick = snappedTick (tickForX ((double) point.x - headerWidth));

        previewStartTick   = clip->startTick;
        previewLengthTicks = clip->lengthTicks;
        previewLooped      = clip->looped;

        switch (zoneAt (*clip, point, clipLane))
        {
            case Zone::leftEdge:    mode = Mode::resizingLeft;  break;
            case Zone::rightCorner: mode = Mode::resizingRight; break;
            case Zone::rightEdge:   mode = Mode::looping;       break;
            case Zone::body:
            case Zone::none:        mode = Mode::movingClip;    break;
        }

        return;
    }

    if (laneIndex < 0)
        return;

    // The header column is covered by the LaneHeader children, but guard anyway:
    // a press there must never start dragging out a clip.
    if (point.x < headerWidth)
        return;

    // Empty lane: hold still for a moment, then drag out a new clip.
    selectTrackOnly (laneIndex);
    pendingLane = laneIndex;
    creatingAnchorTick = snappedTick (tickForX ((double) point.x - headerWidth));
    creatingCurrentTick = creatingAnchorTick;
    mode = Mode::pendingCreate;
    longPressStartMs = juce::Time::getMillisecondCounter();
    startTimer (longPressMs);
}

void TrackArea::selectTrackOnly (int laneIndex)
{
    if (laneIndex < 0 || laneIndex >= laneCount())
        return;

    appState.selectTrack (document.getProject().tracks[(size_t) laneIndex].id);
    appState.selectClip (0);
    appState.setEditorOpen (true);
}

TrackArea::Zone TrackArea::zoneAt (const Clip& clip, juce::Point<int> point, int laneIndex) const
{
    const auto bounds = clipBounds (clip, laneIndex);
    const auto edge = juce::jmin (handleWidth, bounds.getWidth() / 3);

    if (point.x <= bounds.getX() + edge)
        return Zone::leftEdge;

    if (point.x >= bounds.getRight() - edge)
    {
        // The lower third of the right edge resizes, the upper part loops.
        const auto lowerStart = bounds.getY() + (bounds.getHeight() * 2) / 3;
        return point.y >= lowerStart ? Zone::rightCorner : Zone::rightEdge;
    }

    return Zone::body;
}

void TrackArea::mouseDrag (const juce::MouseEvent& e)
{
    const auto point = e.getPosition();

    if (! dragStarted && point.getDistanceFrom (dragStartPoint) > 3)
        dragStarted = true;

    if (mode == Mode::pendingCreate)
    {
        if (longPressElapsed())
        {
            // Held long enough: this drag is now creating a clip.
            stopTimer();
            mode = Mode::creating;
        }
        else if (dragStarted)
        {
            // Moving before the hold completes means "not creating".
            stopTimer();
            mode = Mode::none;
            return;
        }
        else
        {
            return;
        }
    }

    if (mode == Mode::creating)
    {
        // A drag is under way: keep a repeating tick so the view scrolls when the
        // pointer is held against an edge.
        autoScrolling = true;
        startTimerHz (30);
        updateDragFromPoint (point);
        return;
    }

    if (mode == Mode::none || ! dragStarted)
        return;

    autoScrolling = true;
    startTimerHz (30);
    updateDragFromPoint (point);
}

void TrackArea::updateDragFromPoint (juce::Point<int> point)
{
    dragLastPoint = point;

    const auto tick = snappedTick (tickForX ((double) point.x - headerWidth));

    if (mode == Mode::creating)
    {
        creatingCurrentTick = tick;
        repaint();
        return;
    }

    if (mode == Mode::none || ! dragStarted)
        return;

    const auto delta = tick - dragAnchorTick;

    switch (mode)
    {
        case Mode::movingClip:
            previewStartTick = juce::jmax ((juce::int64) 0, dragOriginal.startTick + delta);
            previewLengthTicks = dragOriginal.lengthTicks;
            dragTargetLane = juce::jlimit (0, juce::jmax (0, laneCount() - 1),
                                           laneIndexAtY (point.y));
            break;

        case Mode::resizingRight:
        case Mode::looping:
            previewStartTick = dragOriginal.startTick;
            previewLengthTicks = juce::jmax (kMinClipTicks, dragOriginalEnd + delta - dragOriginal.startTick);
            previewLooped = (mode == Mode::looping);
            break;

        case Mode::resizingLeft:
        {
            const auto newStart = juce::jlimit ((juce::int64) 0, dragOriginalEnd - kMinClipTicks,
                                                dragOriginal.startTick + delta);
            previewStartTick = newStart;
            previewLengthTicks = dragOriginalEnd - newStart;
            break;
        }

        default:
            break;
    }

    repaint();
}

juce::int64 TrackArea::snapTicksNow() const
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto divisor = document.viewState().trackSnap.clampedDenominator();
    return juce::jmax ((juce::int64) 1,
                       (juce::int64) std::llround (tempoMap.ticksPerBeatAt (0) / (double) divisor));
}

bool TrackArea::canSplitClipAt (const Clip& clip, juce::int64 atTick) const
{
    const auto margin = snapTicksNow();
    return atTick - clip.startTick >= margin && clip.endTick() - atTick >= margin;
}

const Clip* TrackArea::findGlueNeighbour (const InstrumentTrack& track, const Clip& clip) const
{
    for (const auto& other : track.clips)
        if (other.id != clip.id && music::canGlueClips (clip, other))
            return &other;

    return nullptr;
}

juce::int64 TrackArea::tickAtX (int x) const
{
    return snappedTick (tickForX ((double) x));
}

juce::PopupMenu TrackArea::buildClipMenu (juce::int64 clipId, juce::int64 atTick) const
{
    juce::PopupMenu menu;

    const auto& project = document.getProject();
    const auto* clip = project.findClip (clipId);
    const auto* track = project.findTrackOfClip (clipId);

    if (clip == nullptr || track == nullptr)
        return menu;

    const auto glueNeighbour = findGlueNeighbour (*track, *clip);

    // The ids run 1..7 and map straight onto ClipCommand.
    menu.addItem (1, NI_TXT ("删除"), true);
    menu.addItem (2, NI_TXT ("切割"), canSplitClipAt (*clip, atTick));
    menu.addItem (3, NI_TXT ("粘合"), glueNeighbour != nullptr);
    menu.addSeparator();
    menu.addItem (4, NI_TXT ("复制"), true);
    // 重命名 / 循环 / 重复 used to live in the clip strip down the right of the
    // arrangement; that column is gone, so they are here now.
    menu.addItem (5, NI_TXT ("重命名…"), true);
    menu.addItem (6, NI_TXT ("循环"), true, clip->looped);
    menu.addItem (7, NI_TXT ("重复…"), true);
    return menu;
}

std::vector<TrackArea::ClipMenuItemForTest> TrackArea::getClipMenuItemsForTest (
    juce::int64 clipId, juce::int64 atTick) const
{
    std::vector<ClipMenuItemForTest> items;

    const auto menu = buildClipMenu (clipId, atTick);

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (! it.getItem().isSeparator)
            items.push_back ({ it.getItem().text, it.getItem().isEnabled,
                               it.getItem().isTicked });

    return items;
}

bool TrackArea::performClipCommand (ClipCommand command, juce::int64 clipId, juce::int64 atTick)
{
    const auto& project = document.getProject();
    const auto* clip = project.findClip (clipId);
    const auto* track = project.findTrackOfClip (clipId);

    if (clip == nullptr || track == nullptr)
        return false;

    if (command == ClipCommand::copy)
    {
        if (onCopyClip != nullptr)
            onCopyClip (clipId);

        return true;
    }

    if (command == ClipCommand::rename)
    {
        if (onRenameClip != nullptr)
            onRenameClip (clipId);

        return true;
    }

    if (command == ClipCommand::repeat)
    {
        appState.selectClip (clipId);

        if (onRepeatClip != nullptr)
            onRepeatClip (clipId);

        return true;
    }

    if (command == ClipCommand::toggleLoop)
    {
        // Turning the loop on gives it a period to loop over: the clip's own length,
        // which is exactly what the clip strip used to do.
        const auto shouldLoop = ! clip->looped;

        document.applyEdit (NI_TXT ("循环片段"), [clipId, shouldLoop] (Project& project)
        {
            auto* target = project.findClip (clipId);

            if (target == nullptr)
                return;

            target->looped = shouldLoop;

            if (shouldLoop && target->loopLengthTicks <= 0)
                target->loopLengthTicks = target->lengthTicks;
        });

        return true;
    }

    if (command == ClipCommand::deleteClip)
    {
        document.applyEdit (NI_TXT ("删除片段"), [clipId] (Project& p)
        {
            if (auto* owner = p.findTrackOfClip (clipId))
            {
                auto& clips = owner->clips;
                clips.erase (std::remove_if (clips.begin(), clips.end(),
                                             [clipId] (const Clip& c) { return c.id == clipId; }),
                             clips.end());
            }
        });

        if (appState.selectedClipId == clipId)
            appState.selectClip (0);

        return true;
    }

    if (command == ClipCommand::split)
    {
        // Checked before the edit so a refused cut does not leave an undo step
        // that does nothing.
        if (! canSplitClipAt (*clip, atTick))
            return false;

        const auto margin = snapTicksNow();
        auto split = false;

        document.applyEdit (NI_TXT ("切割片段"), [clipId, atTick, margin, &split] (Project& p)
        {
            auto* owner = p.findTrackOfClip (clipId);
            auto* target = owner != nullptr ? owner->findClip (clipId) : nullptr;

            if (owner == nullptr || target == nullptr)
                return;

            const music::TempoMap tempoMap (p.tempoMarkers);
            Clip right;

            if (! music::splitClipAt (*target, atTick, tempoMap, margin,
                                      p.nextNoteId, p.nextClipId, right))
                return;

            owner->clips.push_back (std::move (right));
            std::sort (owner->clips.begin(), owner->clips.end(),
                       [] (const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
            split = true;
        });

        return split;
    }

    if (command == ClipCommand::glue)
    {
        const auto* neighbour = findGlueNeighbour (*track, *clip);

        if (neighbour == nullptr)
            return false;

        const auto neighbourId = neighbour->id;
        auto glued = false;

        document.applyEdit (NI_TXT ("粘合片段"), [clipId, neighbourId, &glued] (Project& p)
        {
            auto* owner = p.findTrackOfClip (clipId);
            auto* target = owner != nullptr ? owner->findClip (clipId) : nullptr;
            auto* other = owner != nullptr ? owner->findClip (neighbourId) : nullptr;

            if (owner == nullptr || target == nullptr || other == nullptr)
                return;

            // Copies, because erase() below shuffles the vector the pointers point into.
            const Clip neighbourCopy = *other;

            if (! music::glueClips (*target, neighbourCopy, p.nextNoteId))
                return;

            auto& clips = owner->clips;
            clips.erase (std::remove_if (clips.begin(), clips.end(),
                                         [neighbourId] (const Clip& c) { return c.id == neighbourId; }),
                         clips.end());
            glued = true;
        });

        if (glued && appState.selectedClipId == neighbourId)
            appState.selectClip (clipId);

        return glued;
    }

    return false;
}

void TrackArea::updateAutoScroll()
{
    if (mode == Mode::none)
        return;   // nothing is being dragged, so there is nothing to keep going

    const auto laneLeft = headerWidth;
    const auto laneRight = juce::jmax (laneLeft + 1, getWidth());

    // The time axis: a clip dragged past the right edge keeps moving in time. The
    // edge/step gesture itself is shared with the editor.
    const auto pixels = autoscroll::stepFor ((double) dragLastPoint.x,
                                             (double) laneLeft, (double) laneRight);

    // The lane axis: a clip dragged to the top or bottom edge keeps moving between
    // tracks, so a track that is scrolled out of sight is still a drop target.
    // Only a clip being moved changes lanes - creating one is pinned to the lane it
    // started in, and the resize / loop handles only move in time.
    const auto lanesTopEdge = lanesTop();
    const auto lanesBottomEdge = lanesTop() + lanesHeight();

    const auto verticalPixels = mode == Mode::movingClip
                                    ? autoscroll::stepFor ((double) dragLastPoint.y,
                                                           (double) lanesTopEdge,
                                                           (double) lanesBottomEdge)
                                    : 0;

    if (pixels == 0 && verticalPixels == 0)
        return;

    auto& view = document.viewState();
    auto scrolled = false;

    if (pixels != 0)
    {
        const auto ticksPerPixel = (double) kTicksPerQuarter
                                   / pixelsPerQuarterFor (view, TimelineView::arrangement);
        const auto deltaTicks = (double) pixels * ticksPerPixel;
        const auto newOffset = juce::jmax (0.0, horizontalOffsetFor (view, TimelineView::arrangement)
                                                     + deltaTicks);

        if (newOffset != horizontalOffsetFor (view, TimelineView::arrangement))
        {
            setHorizontalOffsetFor (view, TimelineView::arrangement, newOffset);
            scrolled = true;
        }
    }

    if (verticalPixels != 0)
    {
        const auto newScroll = juce::jlimit (0, maxLaneScroll(), laneScroll + verticalPixels);

        if (newScroll != laneScroll)
        {
            laneScroll = newScroll;
            resized();   // the headers ride on laneScroll, so they have to move too
            scrolled = true;
        }
    }

    if (! scrolled)
        return;

    // Replay the drag with the pointer pinned inside the lane, so the clip keeps
    // following the mouse while the timeline and the lanes slide underneath it.
    auto clamped = dragLastPoint;

    if (pixels != 0)
        clamped.x = juce::jlimit (laneLeft + 1, laneRight - 1, clamped.x);

    if (verticalPixels != 0)
        clamped.y = juce::jlimit (lanesTopEdge + 1, juce::jmax (lanesTopEdge + 1, lanesBottomEdge - 1),
                                  clamped.y);

    updateDragFromPoint (clamped);

    if (auto* parent = getParentComponent())
        parent->repaint();

    repaint();
}

void TrackArea::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    autoScrolling = false;

    const auto finished = mode;
    mode = Mode::none;

    if (finished == Mode::pendingCreate)
        return;   // a plain click; mouseDown already handled it

    if (finished == Mode::creating)
    {
        commitCreation();
        return;
    }

    if (finished == Mode::movingClip || finished == Mode::resizingLeft
        || finished == Mode::resizingRight || finished == Mode::looping)
        commitClipDrag();
}

void TrackArea::commitCreation()
{
    const auto lane = pendingLane;
    pendingLane = -1;

    if (lane < 0 || lane >= laneCount())
        return;

    const auto trackId = document.getProject().tracks[(size_t) lane].id;
    const auto start = juce::jmin (creatingAnchorTick, creatingCurrentTick);
    const auto length = juce::jmax (kMinClipTicks,
                                    juce::jmax (creatingAnchorTick, creatingCurrentTick) - start);

    juce::int64 createdId = 0;

    document.applyEdit (NI_TXT ("新建片段"), [trackId, start, length, &createdId] (Project& project)
    {
        if (auto* track = project.findTrack (trackId))
        {
            auto clip = makeDefaultClip (project);
            clip.startTick = start;
            clip.lengthTicks = length;
            clip.loopLengthTicks = length;
            createdId = clip.id;
            track->clips.push_back (std::move (clip));
        }
    });

    if (createdId != 0)
    {
        appState.selectTrack (trackId);
        appState.selectClip (createdId);
        appState.setEditorOpen (true);
    }

    repaint();
}

void TrackArea::commitClipDrag()
{
    if (dragClipId == 0)
        return;

    const auto clipId = dragClipId;
    dragClipId = 0;

    const auto original = dragOriginal;
    const auto start = previewStartTick;
    const auto length = previewLengthTicks;
    const auto looped = previewLooped;
    const auto sourceTrackId = dragSourceLane >= 0 && dragSourceLane < laneCount()
                                   ? document.getProject().tracks[(size_t) dragSourceLane].id
                                   : (juce::int64) 0;
    const auto targetTrackId = dragTargetLane >= 0 && dragTargetLane < laneCount()
                                   ? document.getProject().tracks[(size_t) dragTargetLane].id
                                   : sourceTrackId;

    const auto movedToAnotherTrack = targetTrackId != sourceTrackId;

    document.applyEdit (movedToAnotherTrack ? NI_TXT ("移动到其他音轨") : NI_TXT ("移动片段"),
                        [clipId, original, start, length, looped, sourceTrackId,
                         targetTrackId, movedToAnotherTrack] (Project& project)
    {
        auto* sourceTrack = project.findTrack (sourceTrackId);
        auto* targetTrack = project.findTrack (targetTrackId);

        if (sourceTrack == nullptr || targetTrack == nullptr)
            return;

        Clip edited = original;
        const auto originalStartTick = original.startTick;
        edited.startTick = start;
        edited.lengthTicks = juce::jmax (kMinClipTicks, length);
        edited.looped = looped;

        // An audio clip stays anchored to the timeline: trimming its left edge has
        // to move the read point into the file by the same amount of time, or the
        // audio would slide along with the clip.
        if (edited.isAudio() && edited.startTick != originalStartTick)
        {
            const music::TempoMap tempoMap (project.tempoMarkers);
            const auto shiftSeconds = tempoMap.secondsAtTick (edited.startTick)
                                          - tempoMap.secondsAtTick (originalStartTick);
            edited.audioOffsetSeconds = juce::jmax (0.0, original.audioOffsetSeconds + shiftSeconds);
        }

        if (! looped)
            edited.loopLengthTicks = edited.lengthTicks;

        if (movedToAnotherTrack)
        {
            auto& clips = sourceTrack->clips;
            clips.erase (std::remove_if (clips.begin(), clips.end(),
                                         [clipId] (const Clip& c) { return c.id == clipId; }),
                         clips.end());
            targetTrack->clips.push_back (std::move (edited));
        }
        else if (auto* existing = sourceTrack->findClip (clipId))
        {
            *existing = std::move (edited);
        }

        // Clips may overlap now: it used to be trimmed here so a lane never had two
        // clips on top of each other, but overlapping is exactly what "record a new
        // take over the old one" needs - and the engine plays every clip of a track,
        // so both are heard.
    });

    repaint();
}

void TrackArea::drawSampleThumbnail (juce::Graphics& g, const Clip& clip,
                                     juce::Rectangle<int> clipRect,
                                     juce::Rectangle<int> inner) const
{
    juce::String error;
    auto sample = getCachedSample (clip.audioFilePath, error);

    if (sample == nullptr || sample->peaks.empty() || inner.getHeight() <= 4)
        return;

    const auto length = sample->lengthSeconds();

    if (length <= 0.0)
        return;

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto clipSeconds = tempoMap.secondsAtTick (clip.endTick())
                                 - tempoMap.secondsAtTick (clip.startTick);

    if (clipSeconds <= 0.0)
        return;

    const auto width = inner.getWidth();
    const auto centreY = (float) inner.getCentreY();
    const auto halfHeight = (float) inner.getHeight() * 0.5f;
    const auto bucketCount = (int) sample->peaks.size();

    g.setColour (juce::Colours::white.withAlpha (0.65f));

    for (int x = 0; x < width; ++x)
    {
        // Timeline position -> file position, honouring the clip's offset.
        const auto clipTime = ((double) x / (double) juce::jmax (1, width)) * clipSeconds;
        const auto fileTime = clip.audioOffsetSeconds + clipTime;
        const auto bucket = waveformBucketFor (fileTime, length, bucketCount);

        if (bucket < 0)
            continue;

        const auto peak = juce::jlimit (0.0f, 1.0f, sample->peaks[(size_t) bucket]);
        const auto height = juce::jmax (1.0f, peak * halfHeight);
        g.fillRect (juce::Rectangle<float> ((float) (inner.getX() + x), centreY - height,
                                            1.0f, height * 2.0f));
    }

    juce::ignoreUnused (clipRect);
}

void TrackArea::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (theme::windowBackground);
    g.fillRect (area);

    const auto& project = document.getProject();
    const auto playheadTick = music::TempoMap (project.tempoMarkers)
                                  .tickAtSeconds (audio.getPositionSeconds());

    // The lanes are clipped to their own area: with the lanes scrolled up, a lane
    // that hangs past the top would otherwise paint over the + bar, the tempo
    // markers and the ruler.
    const juce::Graphics::ScopedSaveState lanesClip (g);
    g.reduceClipRegion (juce::Rectangle<int> (0, lanesTop(), getWidth(), lanesHeight()));

    for (int lane = 0; lane < laneCount(); ++lane)
    {
        const auto& track = project.tracks[(size_t) lane];
        const auto bounds = laneBounds (lane);

        if (! bounds.intersects (g.getClipBounds()))
            continue;

        const auto selectedTrack = appState.selectedTrackId == track.id;
        const auto colour = theme::trackColour (track.colourIndex);

        // Only the content area: the header column belongs to the LaneHeader
        // children, which paint themselves on top.
        g.setColour (selectedTrack ? theme::panelRaised : theme::laneBackground);
        g.fillRect (bounds.reduced (0, 1).withTrimmedLeft (headerWidth));

        g.setColour (colour.withAlpha (selectedTrack ? 1.0f : 0.5f));
        g.fillRect (bounds.withHeight (1).withTrimmedLeft (headerWidth));

        const auto& trackSnap = document.viewState().trackSnap;

        if (trackSnap.enabled && trackSnap.clampedDenominator() > 1)
        {
            const music::TempoMap tempoMap (project.tempoMarkers);
            const auto firstTick = (juce::int64) std::floor (tickForX (0.0));
            const auto lastTick = (juce::int64) std::ceil (
                tickForX ((double) juce::jmax (0, getWidth() - headerWidth)));
            auto step = juce::jmax (
                1.0, tempoMap.ticksPerBeatAt (juce::jmax ((juce::int64) 0, firstTick))
                         / (double) trackSnap.clampedDenominator());
            const auto pixelsPerStep = step * pixelsPerQuarterFor (
                document.viewState(), TimelineView::arrangement)
                                       / (double) kTicksPerQuarter;

            if (pixelsPerStep >= 3.0)
            {
                const float dashes[] { 2.0f, 5.0f };
                auto tick = (juce::int64) std::floor (
                    (double) firstTick / step) * (juce::int64) std::llround (step);

                for (int guard = 0; guard < 4096 && tick <= lastTick; ++guard)
                {
                    const auto beatTicks = juce::jmax (1.0, tempoMap.ticksPerBeatAt (tick));

                    if (std::abs (std::fmod ((double) tick, beatTicks)) >= 0.5)
                    {
                        const auto x = (float) headerWidth + (float) xForTick ((double) tick);

                        if (x >= (float) headerWidth && x <= (float) getWidth())
                        {
                            g.setColour (theme::gridBeatLine.withAlpha (0.28f));
                            g.drawDashedLine ({ x, (float) bounds.getY(),
                                                x, (float) bounds.getBottom() },
                                              dashes, 2, 1.0f);
                        }
                    }

                    tick += (juce::int64) std::llround (step);
                }
            }
        }

        // Clips. Clipped to the lane's content area: a clip whose start has
        // scrolled off to the left would otherwise be positioned at
        // headerWidth + a negative offset and paint straight over the header.
        {
            const juce::Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (juce::Rectangle<int> (headerWidth, bounds.getY(),
                                                      juce::jmax (0, getWidth() - headerWidth),
                                                      bounds.getHeight()));

            for (const auto& storedClip : track.clips)
            {
                const auto dragging = storedClip.id == dragClipId && mode != Mode::none
                                      && mode != Mode::pendingCreate && mode != Mode::creating;

                if (dragging && dragTargetLane != lane)
                    continue;   // it is drawn in the lane it is being dragged to

                Clip clip = storedClip;

                if (dragging)
                {
                    clip.startTick = previewStartTick;
                    clip.lengthTicks = previewLengthTicks;
                    clip.looped = previewLooped;
                }

                const auto clipRect = clipBounds (clip, lane);

                if (clipRect.getRight() < headerWidth || clipRect.getX() > getWidth())
                    continue;

                const auto isOpen = appState.selectedClipId == clip.id;
                const auto clipColour = theme::trackColour (track.colourIndex);

                g.setColour (clipColour.withAlpha (isOpen ? 0.85f : 0.55f));
                g.fillRoundedRectangle (clipRect.toFloat(), 4.0f);

                g.setColour (isOpen ? theme::noteSelectedOutline : clipColour.darker (0.5f));
                g.drawRoundedRectangle (clipRect.toFloat(), 4.0f, isOpen ? 1.6f : 1.0f);

                auto inner = clipRect.reduced (5, 3);

                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (theme::font (10.5f, true));
                g.drawText (clip.name, inner.removeFromTop (13), juce::Justification::centredLeft, true);

                // Repeat dividers, so a looped clip is obvious.
                if (clip.looped && clip.loopLengthTicks > 0)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.45f));

                    for (auto tick = clip.startTick + clip.loopLengthTicks; tick < clip.endTick();
                         tick += clip.loopLengthTicks)
                    {
                        const auto x = (float) (headerWidth + (int) xForTick ((double) tick));
                        g.fillRect (x, (float) clipRect.getY() + 2.0f, 1.0f,
                                    (float) clipRect.getHeight() - 4.0f);
                    }
                }

                if (clip.isAudio())
                {
                    // Waveform thumbnail: the whole file, cropped to the clip window.
                    drawSampleThumbnail (g, clip, clipRect, inner);
                }
                else
                {
                    // Note thumbnail: what the clip plays, at the real tick scale, so
                    // it lines up with the repeat dividers above and does not change
                    // shape while the clip is being resized.
                    const juce::Graphics::ScopedSaveState thumbnailState (g);
                    g.reduceClipRegion (clipRect);
                    g.setColour (juce::Colours::white.withAlpha (0.75f));

                    for (const auto& bar : clipThumbnailBars (clip, lane))
                        g.fillRect (bar);
                }
            }
        }

        g.setColour (theme::outline);
        g.drawHorizontalLine (bounds.getBottom() - 1, 0.0f, (float) getWidth());
    }

    // Preview of the clip being dragged out on an empty lane.
    if (mode == Mode::creating && pendingLane >= 0 && pendingLane < laneCount())
    {
        Clip preview;
        preview.startTick = juce::jmin (creatingAnchorTick, creatingCurrentTick);
        preview.lengthTicks = juce::jmax (kMinClipTicks,
                                          juce::jmax (creatingAnchorTick, creatingCurrentTick)
                                              - preview.startTick);

        const auto rect = clipBounds (preview, pendingLane);

        // Same clipping rule as the clips themselves.
        const juce::Graphics::ScopedSaveState previewState (g);
        const auto lane = laneBounds (pendingLane);
        g.reduceClipRegion (juce::Rectangle<int> (headerWidth, lane.getY(),
                                                  juce::jmax (0, getWidth() - headerWidth),
                                                  lane.getHeight()));

        g.setColour (theme::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (rect.toFloat(), 4.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (rect.toFloat(), 4.0f, 1.5f);
    }

    // Playhead, aligned with the shared timeline.
    const auto x = (float) (headerWidth + (int) xForTick ((double) playheadTick));

    if (x >= (float) headerWidth && x <= (float) getWidth())
    {
        g.setColour (theme::playhead.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x, (float) lanesTop(), 1.5f,
                                            (float) (getHeight() - lanesTop())));
    }

    if (laneCount() == 0)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (13.0f));
        g.drawText (NI_TXT ("点左上角的「＋ 新建轨道」开始"),
                    area.withTrimmedTop (lanesTop()), juce::Justification::centred);
    }
}

} // namespace nidaw
