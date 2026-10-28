#include "TimelineRuler.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    constexpr int kMaxRulerLines = 4000;
}

TimelineRuler::TimelineRuler (ProjectDocument& documentToUse, AppState& appStateToUse,
                              AudioEngine& audioToUse, TimelineView timelineToUse)
    : document (documentToUse),
      appState (appStateToUse),
      audio (audioToUse),
      view (timelineToUse)
{
    document.addChangeListener (this);
    appState.addChangeListener (this);

    setMouseCursor (juce::MouseCursor::IBeamCursor);
    setTooltip (NI_TXT ("左键点击或拖动：设置开始播放的位置；Shift+拖动：设置循环区间，Shift+点击：清除"));
}

TimelineRuler::~TimelineRuler()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void TimelineRuler::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void TimelineRuler::refresh()
{
    repaint();
}

void TimelineRuler::refreshPlayhead()
{
    // Short strip, so a full repaint is cheaper than tracking dirty columns.
    repaint();
}

double TimelineRuler::xForTick (double tick) const
{
    return xForTimelineTick (document.viewState(), view, tick);
}

double TimelineRuler::tickForX (double x) const
{
    return timelineTickForX (document.viewState(), view, x);
}

juce::int64 TimelineRuler::snappedTick (double tick) const
{
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto rounded = (juce::int64) std::llround (juce::jmax (0.0, tick));

    // 不吸附: the playhead follows the pointer exactly, like the canvases do.
    const auto& snap = view == TimelineView::arrangement
                           ? document.viewState().trackSnap
                           : document.viewState().editorSnap;

    if (! snap.enabled)
        return rounded;

    const auto divisor = snap.clampedDenominator();
    const auto gridTicks = juce::jmax (1.0, tempoMap.ticksPerBeatAt (rounded) / (double) divisor);

    return juce::jmax ((juce::int64) 0, music::TempoMap::snapToGrid (rounded, gridTicks));
}

void TimelineRuler::setPositionFromMouse (int x)
{
    const auto tick = snappedTick (tickForX ((double) x));
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);

    // Playback continues from the new position without stopping.
    audio.setPositionSeconds (tempoMap.secondsAtTick (tick));
    repaint();
}

void TimelineRuler::mouseDown (const juce::MouseEvent& e)
{
    // Shift drags out the loop region instead of moving the playhead.
    if (e.mods.isShiftDown())
    {
        loopDragging   = true;
        loopAnchorTick = snappedTick (tickForX ((double) e.x));
        loopDragTick   = loopAnchorTick;
        repaint();
        return;
    }

    setPositionFromMouse (e.x);
}

void TimelineRuler::mouseDrag (const juce::MouseEvent& e)
{
    if (loopDragging)
    {
        loopDragTick = snappedTick (tickForX ((double) e.x));
        repaint();
        return;
    }

    setPositionFromMouse (e.x);
}

void TimelineRuler::mouseUp (const juce::MouseEvent&)
{
    if (! loopDragging)
        return;

    loopDragging = false;
    commitLoopDrag();
    repaint();
}

LoopRegion TimelineRuler::displayedLoop() const
{
    if (! loopDragging)
        return document.getProject().loop;

    LoopRegion region;
    region.startTick = juce::jmin (loopAnchorTick, loopDragTick);
    region.endTick   = juce::jmax (loopAnchorTick, loopDragTick);
    region.enabled   = true;
    return region;
}

void TimelineRuler::commitLoopDrag()
{
    const auto start = juce::jmin (loopAnchorTick, loopDragTick);
    const auto end   = juce::jmax (loopAnchorTick, loopDragTick);

    if (end <= start)
    {
        // A shift click with no drag clears the cycle.
        document.applyEdit (NI_TXT ("清除循环区间"), [] (Project& project)
        {
            project.loop = {};
        });
    }
    else
    {
        // Dragging out a region is an explicit "loop this", so it turns the loop on.
        document.applyEdit (NI_TXT ("设置循环区间"), [start, end] (Project& project)
        {
            project.loop.startTick = start;
            project.loop.endTick   = end;
            project.loop.enabled   = true;
        });
    }

    audio.refreshTransportAids();
}

void TimelineRuler::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    const auto width = area.getWidth();

    g.setColour (theme::gridTempoStrip);
    g.fillRect (area);
    g.setColour (theme::outline);
    g.drawHorizontalLine (area.getBottom() - 1, 0.0f, (float) width);

    loopBandBounds = {};

    // Loop (cycle) band. It is drawn under the ticks so it reads as a background
    // wash, with a brighter block on the two ends.
    if (const auto loop = displayedLoop(); loop.isValid())
    {
        const auto left  = (float) xForTick ((double) loop.startTick);
        const auto right = (float) xForTick ((double) loop.endTick);

        if (right >= 0.0f && left <= (float) width)
        {
            const auto from = juce::jmax (0.0f, left);
            const auto to   = juce::jmin ((float) width, right);
            const auto bandColour = loop.enabled ? theme::accent : theme::dimText;

            g.setColour (bandColour.withAlpha (loop.enabled ? 0.28f : 0.12f));
            g.fillRect (from, 0.0f, to - from, (float) area.getHeight());

            g.setColour (bandColour.withAlpha (loop.enabled ? 0.95f : 0.5f));
            g.fillRect (from, 0.0f, 1.6f, (float) area.getHeight());
            g.fillRect (to - 1.6f, 0.0f, 1.6f, (float) area.getHeight());

            loopBandBounds = juce::Rectangle<int> ((int) std::floor (from), 0,
                                                   juce::jmax (1, (int) std::ceil (to - from)),
                                                   area.getHeight());
        }
    }

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);

    const auto firstTick = tickForX (0.0);
    const auto lastTick  = tickForX ((double) width);

    // Beat ticks.
    g.setColour (theme::dimText.withAlpha (0.5f));

    auto ticksPerBeat = juce::jmax (1.0, tempoMap.ticksPerBeatAt ((juce::int64) juce::jmax (0.0, firstTick)));
    auto beatTick = (juce::int64) std::floor (juce::jmax (0.0, firstTick) / ticksPerBeat + 1.0e-9)
                    * (juce::int64) std::llround (ticksPerBeat);
    const auto pixelsPerBeat = ticksPerBeat * pixelsPerQuarterFor (document.viewState(), view)
                               / (double) kTicksPerQuarter;

    if (pixelsPerBeat >= 10.0)
    {
        for (int guard = 0; guard < kMaxRulerLines && (double) beatTick <= lastTick; ++guard)
        {
            const auto x = (float) xForTick ((double) beatTick);

            if (x >= 0.0f && x <= (float) width)
                g.fillRect (x, (float) area.getBottom() - 6.0f, 1.0f, 5.0f);

            beatTick = tempoMap.nextBeatTick (beatTick);
            ticksPerBeat = juce::jmax (1.0, tempoMap.ticksPerBeatAt (beatTick));
        }
    }

    // Bar lines and numbers.
    g.setFont (theme::font (10.5f));

    auto bar = juce::jmax (1, tempoMap.barAtTick ((juce::int64) juce::jmax (0.0, firstTick)));

    for (int guard = 0; guard < kMaxRulerLines; ++guard)
    {
        const auto barTick = tempoMap.barStartTick (bar);

        if ((double) barTick > lastTick)
            break;

        const auto x = (float) xForTick ((double) barTick);

        if (x >= -40.0f && x <= (float) width)
        {
            g.setColour (theme::gridBarLine);
            g.fillRect (x, 0.0f, 1.4f, (float) area.getHeight());
            g.setColour (theme::dimText);
            g.drawText (juce::String (bar),
                        juce::Rectangle<int> ((int) x + 3, 0, 48, area.getHeight() - 2),
                        juce::Justification::centredLeft);
        }

        ++bar;
    }

    // Transport position marker.
    const auto positionTick = tempoMap.tickAtSeconds (audio.getPositionSeconds());
    const auto markerX = (float) xForTick ((double) positionTick);

    if (markerX >= -8.0f && markerX <= (float) width + 8.0f)
    {
        g.setColour (theme::playhead);

        juce::Path flag;
        flag.startNewSubPath (markerX, (float) area.getBottom() - 1.0f);
        flag.lineTo (markerX - 5.0f, 3.0f);
        flag.lineTo (markerX + 5.0f, 3.0f);
        flag.closeSubPath();
        g.fillPath (flag);

        g.fillRect (markerX, 0.0f, 1.4f, (float) area.getHeight());
    }
}

} // namespace nidaw
