#include "ControllerLane.h"

#include "../Core/MusicMath.h"

#include <cmath>

namespace nidaw
{

namespace
{
    /** The picker's item ids: 1 is the note velocity, 100 + n is CC n, and the last
        one opens the number field. */
    constexpr int kVelocityItemId = 1;
    constexpr int kCustomItemId = 2;
    constexpr int kControllerBase = 100;

    /** How far (in pixels) the pointer may be from a knee and still grab it. */
    constexpr int kKneeGrabPixels = 6;
    /** The velocity bars are this wide, and this far above the bottom. */
    constexpr int kBarWidth = 5;
    /** Dragging a bar changes the velocity by one step every this many pixels. */
    constexpr int kPixelsPerVelocity = 3;

    const std::vector<ControllerLane::Signal> kSignals =
    {
        { 1,  "CC1 调制" },
        { 7,  "CC7 音量" },
        { 10, "CC10 声像" },
        { 11, "CC11 表情" },
        { 64, "CC64 延音踏板" },
        { 2,  "CC2 呼吸" },
        { 4,  "CC4 脚踏" },
        { 5,  "CC5 滑音时间" },
        { 65, "CC65 延音开关" },
        { 66, "CC66 弱音踏板" },
        { 67, "CC67 弱音" },
        { 68, "CC68 连奏" },
        { 71, "CC71 共鸣" },
        { 74, "CC74 亮度" },
        { 91, "CC91 混响" },
        { 93, "CC93 合唱" }
    };

    /** The first six are the everyday ones; the rest sit under 更多. */
    constexpr int kCommonSignalCount = 5;

    juce::String signalLabelFor (int controller)
    {
        for (const auto& signal : kSignals)
            if (signal.controller == controller)
                return NI_TXT (signal.label);

        return "CC " + juce::String (controller);
    }
}

const std::vector<ControllerLane::Signal>& ControllerLane::namedSignals()
{
    return kSignals;
}

//==============================================================================
ControllerLane::ControllerLane (ProjectDocument& documentToUse, AppState& appStateToUse,
                                AudioEngine& audioEngine)
    : document (documentToUse), appState (appStateToUse), audio (audioEngine),
      snapControl (documentToUse, appStateToUse, SnapArea::controller)
{
    rebuildSignalList();
    addAndMakeVisible (snapControl);

    signalCombo.onChange = [this] { applyLaneSignalFromCombo(); };
    addAndMakeVisible (signalCombo);

    customControllerEditor.setFont (theme::font (12.0f));
    customControllerEditor.setJustification (juce::Justification::centred);
    customControllerEditor.setInputRestrictions (3, "0123456789");
    customControllerEditor.setTooltip (NI_TXT ("输入 1–127 的 CC 号"));
    customControllerEditor.onReturnKey = [this]
    {
        commitCustomController (customControllerEditor.getText());
    };
    customControllerEditor.onFocusLost = [this]
    {
        commitCustomController (customControllerEditor.getText());
    };
    addChildComponent (customControllerEditor);

    readoutLabel.setFont (theme::font (11.0f));
    readoutLabel.setColour (juce::Label::textColourId, theme::dimText);
    readoutLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (readoutLabel);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    refresh();
}

ControllerLane::~ControllerLane()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void ControllerLane::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void ControllerLane::rebuildSignalList()
{
    signalCombo.clear (juce::dontSendNotification);
    signalCombo.addSectionHeading (NI_TXT ("常用"));
    signalCombo.addItem (NI_TXT ("力度"), kVelocityItemId);

    for (int i = 0; i < (int) kSignals.size(); ++i)
    {
        if (i == kCommonSignalCount)
        {
            signalCombo.addSeparator();
            signalCombo.addSectionHeading (NI_TXT ("更多"));
        }

        signalCombo.addItem (signalLabelFor (kSignals[(size_t) i].controller),
                             kControllerBase + kSignals[(size_t) i].controller);
    }

    signalCombo.addSeparator();
    signalCombo.addItem (NI_TXT ("自定义…"), kCustomItemId);
}

void ControllerLane::applyLaneSignalFromCombo()
{
    const auto id = signalCombo.getSelectedId();

    if (id == kVelocityItemId)
    {
        appState.setLaneSignal (true, appState.laneController);
        return;
    }

    if (id == kCustomItemId)
    {
        // The number field is what actually picks the controller; the lane keeps
        // showing whatever it showed until a number is typed.
        customControllerEditor.setVisible (true);
        customControllerEditor.setText (juce::String (appState.laneController),
                                        juce::dontSendNotification);
        customControllerEditor.grabKeyboardFocus();
        resized();
        repaint();
        return;
    }

    if (id >= kControllerBase)
        appState.setLaneSignal (false, id - kControllerBase);
}

void ControllerLane::commitCustomController (const juce::String& text)
{
    const auto number = text.getIntValue();

    // The numbers the host sends for itself (pitch bend range, all notes off) are
    // not on offer: a curve drawn on them would fight the host.
    if (number < 1 || number > 127 || controllers::isReservedController (number))
    {
        customControllerEditor.setText (juce::String (appState.laneController),
                                        juce::dontSendNotification);
        setReadout (NI_TXT ("CC 号要在 1–127 之间，而且不能用 6 / 38 / 100 / 101 / 123（宿主自己发这些）"));
        return;
    }

    appState.setLaneSignal (false, number);
    refresh();
}

void ControllerLane::selectSignalForTest (bool showsVelocity, int controller)
{
    appState.setLaneSignal (showsVelocity, controller);
    refresh();
}

void ControllerLane::setCustomControllerForTest (int controller)
{
    commitCustomController (juce::String (controller));
}

//==============================================================================
const EditorViewState& ControllerLane::view() const
{
    return document.viewState();
}

const Clip* ControllerLane::openClip() const
{
    return document.getProject().findClip (appState.selectedClipId);
}

const InstrumentTrack* ControllerLane::openTrack() const
{
    return document.getProject().findTrack (appState.selectedTrackId);
}

bool ControllerLane::isAudioClip() const
{
    const auto* clip = openClip();
    return clip != nullptr && clip->isAudio();
}

bool ControllerLane::isDrumClip() const
{
    const auto* track = openTrack();
    return track != nullptr && track->kind == TrackKind::drums;
}

juce::int64 ControllerLane::getClipIdForTest() const
{
    const auto* clip = openClip();
    return clip != nullptr ? clip->id : 0;
}

juce::Rectangle<int> ControllerLane::getHeaderBoundsForTest() const
{
    return getLocalBounds().removeFromTop (headerHeight);
}

juce::Rectangle<int> ControllerLane::plotBounds() const
{
    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    return area;
}

juce::Rectangle<int> ControllerLane::getPlotBoundsForTest() const
{
    return plotBounds();
}

//==============================================================================
int ControllerLane::yForValue (double value) const
{
    const auto plot = plotBounds().reduced (0, 3);
    const auto unit = juce::jlimit (0.0, 1.0, value / 127.0);

    return plot.getBottom() - (int) std::lround (unit * (double) plot.getHeight());
}

double ControllerLane::valueForY (double y) const
{
    const auto plot = plotBounds().reduced (0, 3);
    const auto unit = juce::jlimit (0.0, 1.0,
                                    (double) (plot.getBottom() - y) / (double) juce::jmax (1, plot.getHeight()));

    return unit * 127.0;
}

double ControllerLane::xForTick (double tick) const
{
    // The very mapping the canvas and the ruler use. The panel trims this component
    // by the piano window's key column, exactly as it trims the ruler, so "tick to x"
    // needs no fudge here at all - and a fudge was what used to put this lane's
    // playhead line (and its bars) 88 px to the left of the canvas's.
    return xForTimelineTick (view(), TimelineView::editor, tick);
}

double ControllerLane::tickForX (double x) const
{
    return timelineTickForX (view(), TimelineView::editor, x);
}

double ControllerLane::snapGridTicksAt (juce::int64 tick) const
{
    const auto& project = document.getProject();
    const music::TempoMap tempoMap (project.tempoMarkers);
    const auto divisor = view().controllerSnap.clampedDenominator();

    return tempoMap.ticksPerBeatAt (tick) / (double) divisor;
}

juce::int64 ControllerLane::snappedTickForX (double x) const
{
    const auto rounded = (juce::int64) std::llround (tickForX (x));

    if (! view().controllerSnap.enabled)
        return rounded;

    return music::TempoMap::snapToGrid (rounded, snapGridTicksAt (rounded));
}

int ControllerLane::getXForTickForTest (double tick) const
{
    return (int) std::lround (xForTick (tick));
}

int ControllerLane::getYForValueForTest (double value) const
{
    return yForValue (value);
}

double ControllerLane::getTickForXForTest (double x) const
{
    return tickForX (x);
}

int ControllerLane::getValueForYForTest (double y) const
{
    return juce::jlimit (0, 127, (int) std::lround (valueForY (y)));
}

//==============================================================================
std::vector<ControllerLane::BarHit> ControllerLane::velocityBars() const
{
    std::vector<BarHit> bars;
    const auto* clip = openClip();

    if (clip == nullptr || clip->isAudio())
        return bars;

    const auto plot = plotBounds();
    const auto bottom = plot.getBottom() - 3;

    for (const auto& placement : music::expandClip (*clip))
    {
        if (placement.note == nullptr)
            continue;

        const auto x = (int) std::lround (xForTick ((double) placement.startTick));

        if (x < plot.getX() - 8 || x > plot.getRight() + 8)
            continue;

        const auto top = yForValue ((double) juce::jlimit (0, 127, placement.note->velocity));

        bars.push_back ({ placement.note->id,
                          { x - kBarWidth / 2, top, kBarWidth, juce::jmax (1, bottom - top) } });
    }

    return bars;
}

std::vector<juce::Rectangle<int>> ControllerLane::getVelocityBarsForTest() const
{
    std::vector<juce::Rectangle<int>> rects;

    for (const auto& bar : velocityBars())
        rects.push_back (bar.bounds);

    return rects;
}

std::vector<juce::Point<int>> ControllerLane::curvePoints() const
{
    std::vector<juce::Point<int>> points;
    const auto* clip = openClip();

    if (clip == nullptr || clip->isAudio() || appState.laneShowsVelocity)
        return points;

    const auto* lane = controllers::findLane (*clip, appState.laneController);

    if (lane == nullptr)
        return points;

    for (const auto& value : controllers::expandControllerPoints (*clip, *lane))
        points.push_back ({ (int) std::lround (xForTick ((double) value.tick)),
                            yForValue ((double) value.value) });

    return points;
}

std::vector<juce::Point<int>> ControllerLane::getCurvePointsForTest() const
{
    return curvePoints();
}

std::vector<juce::Rectangle<int>> ControllerLane::getCurveKneesForTest() const
{
    std::vector<juce::Rectangle<int>> rects;

    for (const auto& point : curvePoints())
        rects.push_back ({ point.x - 3, point.y - 3, 7, 7 });

    return rects;
}

ControllerPoint* ControllerLane::kneeNear (juce::Point<int> point, juce::int64& tickOut)
{
    tickOut = 0;

    const auto* clip = openClip();

    if (clip == nullptr || appState.laneShowsVelocity)
        return nullptr;

    const auto* lane = controllers::findLane (*clip, appState.laneController);

    if (lane == nullptr)
        return nullptr;

    // The curve is drawn tiled (a looping clip repeats it), so a knee can be grabbed
    // on any of the repeats: the point it edits is the one in the clip's own period.
    // The curve is drawn tiled (a looping clip repeats it), so a knee can be grabbed
    // on any of the repeats - but the point it edits is the one in the clip's own
    // period, which is why the tick that comes back is the clip-relative one.
    // The curve is read-only here: the mouse edits it through the document, and this
    // only has to answer "is there a knee under the pointer, and which tick is it".
    const ControllerPoint* best = nullptr;
    auto bestDistance = (double) kKneeGrabPixels;
    const auto period = clip->looped ? juce::jmax ((juce::int64) 1, clip->loopLengthTicks)
                                     : juce::jmax (kMinClipTicks, clip->lengthTicks);
    const auto repeats = music::clipRepeatCount (*clip);
    auto bestTick = (juce::int64) 0;

    for (int repeat = 0; repeat < repeats; ++repeat)
        for (const auto& knee : lane->points)
        {
            if (knee.tick >= period)
                continue;   // outside one period: not drawn, not grabbable

            const auto absolute = clip->startTick + (juce::int64) repeat * period + knee.tick;

            if (absolute >= clip->endTick())
                continue;

            const juce::Point<int> where { (int) std::lround (xForTick ((double) absolute)),
                                           yForValue ((double) knee.value) };
            const auto distance = where.getDistanceFrom (point);

            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = &knee;
                bestTick = knee.tick;
            }
        }

    if (best == nullptr)
        return nullptr;

    tickOut = bestTick;
    return const_cast<ControllerPoint*> (best);
}

int ControllerLane::velocityBarNear (juce::Point<int> point) const
{
    const auto bars = velocityBars();

    for (int i = 0; i < (int) bars.size(); ++i)
        if (bars[(size_t) i].bounds.expanded (kKneeGrabPixels, 0).contains (point))
            return i;

    return -1;
}

//==============================================================================
void ControllerLane::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight);

    // The panel's fold arrow occupies the left end of this row (it is a sibling, so
    // it stays clickable while this component is on screen).
    header.removeFromLeft (18);
    header = header.reduced (4, 2);

    signalCombo.setBounds (header.removeFromLeft (juce::jmin (160, juce::jmax (86, getWidth() / 4))));
    header.removeFromLeft (6);
    snapControl.setBounds (header.removeFromLeft (108));
    header.removeFromLeft (6);

    if (customControllerEditor.isVisible())
    {
        customControllerEditor.setBounds (header.removeFromLeft (46));
        header.removeFromLeft (6);
    }

    readoutLabel.setBounds (header);
}

void ControllerLane::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    snapControl.refresh();

    // The number field is the "type a controller by hand" mode; it stays on screen
    // while that is what the box is set to, so a second number can be typed.
    auto wantsCustomField = false;

    if (appState.laneShowsVelocity)
    {
        signalCombo.setSelectedId (kVelocityItemId, juce::dontSendNotification);
    }
    else
    {
        const auto wanted = kControllerBase + appState.laneController;
        auto named = false;

        for (int i = 0; i < signalCombo.getNumItems(); ++i)
            if (signalCombo.getItemId (i) == wanted)
                named = true;

        // The field shows itself when the box is on 自定义…, and also while the
        // current controller is one that has no name in the list: that is where a
        // number like "CC 20" comes from, so the place to change it must be there.
        wantsCustomField = ! named || signalCombo.getSelectedId() == kCustomItemId;

        if (named && signalCombo.getSelectedId() != kCustomItemId)
        {
            signalCombo.setSelectedId (wanted, juce::dontSendNotification);
        }
        else
        {
            if (! named)
            {
                // A controller with no item of its own: the box shows it as text.
                signalCombo.setSelectedId (0, juce::dontSendNotification);
                signalCombo.setText ("CC " + juce::String (appState.laneController),
                                     juce::dontSendNotification);
            }
        }
    }

    customControllerEditor.setVisible (wantsCustomField);

    if (wantsCustomField && ! customControllerEditor.hasKeyboardFocus (true))
        customControllerEditor.setText (juce::String (appState.laneController),
                                        juce::dontSendNotification);

    resized();
    setReadout (readoutText());
    repaint();
}

juce::String ControllerLane::readoutText() const
{
    const auto* clip = openClip();

    if (clip == nullptr)
        return NI_TXT ("打开一个片段后，可以在这里画力度与 CC");

    if (clip->isAudio())
        return NI_TXT ("音频片段没有音符与 CC");

    if (appState.laneShowsVelocity)
        return NI_TXT ("力度：上下拖竖条改音符力度，多选时一起走");

    return signalLabelFor (appState.laneController)
               + NI_TXT ("：空白按住拖动 = 画线，Shift+拖动 = 直线，右键折点 = 删除");
}

void ControllerLane::setReadout (const juce::String& text)
{
    if (readoutLabel.getText() != text)
        readoutLabel.setText (text, juce::dontSendNotification);
}

void ControllerLane::simplifyCurveGesture (juce::int64 fromTick, juce::int64 toTick)
{
    const auto* clip = openClip();

    if (clip == nullptr)
        return;

    const auto cc = appState.laneController;
    const auto clipId = clip->id;
    const auto from = juce::jmin (fromTick, toTick);
    const auto to = juce::jmax (fromTick, toTick);

    document.applyDerivedEdit ([this, clipId, cc, from, to] (Project& project)
    {
        auto* target = project.findClip (clipId);
        auto* lane = target != nullptr ? controllers::findLane (*target, cc) : nullptr;

        if (lane == nullptr || lane->points.size() < 3)
            return;

        std::vector<ControllerPoint> segment;

        for (const auto& point : lane->points)
            if (point.tick >= from && point.tick <= to)
                segment.push_back (point);

        if (segment.size() < 3)
            return;

        std::vector<juce::Point<float>> screen;
        screen.reserve (segment.size());

        for (const auto& point : segment)
            screen.emplace_back ((float) xForTick ((double) point.tick),
                                 (float) yForValue ((double) point.value));

        std::vector<bool> keep (screen.size(), false);
        keep.front() = true;
        keep.back() = true;

        std::function<void (size_t, size_t)> simplify = [&] (size_t first, size_t last)
        {
            if (last <= first + 1)
                return;

            const auto a = screen[first];
            const auto b = screen[last];
            const auto ab = b - a;
            const auto length = ab.getDistanceFromOrigin();
            auto best = 0.0f;
            size_t bestIndex = first + 1;

            for (auto i = first + 1; i < last; ++i)
            {
                auto distance = 0.0f;

                if (length <= 1.0e-6f)
                {
                    distance = screen[i].getDistanceFrom (a);
                }
                else
                {
                    const auto ap = screen[i] - a;
                    distance = std::abs (ap.x * ab.y - ap.y * ab.x) / length;
                }

                if (distance > best)
                {
                    best = distance;
                    bestIndex = i;
                }
            }

            if (best > 2.0f)
            {
                keep[bestIndex] = true;
                simplify (first, bestIndex);
                simplify (bestIndex, last);
            }
        };
        simplify (0, screen.size() - 1);

        lane->points.erase (
            std::remove_if (lane->points.begin(), lane->points.end(),
                            [from, to] (const ControllerPoint& point)
                            { return point.tick >= from && point.tick <= to; }),
            lane->points.end());

        for (size_t i = 0; i < segment.size(); ++i)
            if (keep[i])
                lane->points.push_back (segment[i]);

        std::sort (lane->points.begin(), lane->points.end(),
                   [] (const ControllerPoint& a, const ControllerPoint& b)
                   { return a.tick < b.tick; });
    });
}

//==============================================================================
void ControllerLane::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);

    g.setColour (theme::panelRaised);
    g.fillRect (header);
    g.setColour (theme::outline);
    g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);

    const auto plot = area;

    g.setColour (theme::gridBackground);
    g.fillRect (plot);

    // ---- the vertical grid: the same bars and beats the canvas shows ----------
    {
        const music::TempoMap tempoMap (document.getProject().tempoMarkers);
        const auto firstTick = (juce::int64) std::floor (tickForX ((double) plot.getX()));
        const auto lastTick = (juce::int64) std::ceil (tickForX ((double) plot.getRight()));
        auto bar = juce::jmax (1, tempoMap.barAtTick (juce::jmax ((juce::int64) 0, firstTick)));

        for (int guard = 0; guard < 4096; ++guard)
        {
            const auto barStart = tempoMap.barStartTick (bar);

            if (barStart > lastTick)
                break;

            const auto barX = xForTick ((double) barStart);

            g.setColour (theme::gridBarLine);

            if (barX >= (double) plot.getX() - 1.0)
                g.fillRect ((float) barX, (float) plot.getY(), 1.0f, (float) plot.getHeight());

            for (auto tick = tempoMap.nextBeatTick (barStart);
                 tick < tempoMap.barStartTick (bar + 1) && tick <= lastTick;
                 tick = tempoMap.nextBeatTick (tick))
            {
                const auto beatX = xForTick ((double) tick);

                if (beatX >= (double) plot.getX())
                {
                    g.setColour (theme::gridBeatLine);
                    g.fillRect ((float) beatX, (float) plot.getY(), 1.0f, (float) plot.getHeight());
                }
            }

            ++bar;
        }

        const auto& ccSnap = document.viewState().controllerSnap;

        if (ccSnap.enabled && ccSnap.clampedDenominator() > 1)
        {
            auto step = juce::jmax (
                1.0, tempoMap.ticksPerBeatAt (juce::jmax ((juce::int64) 0, firstTick))
                         / (double) ccSnap.clampedDenominator());
            const auto pixelsPerStep = step * view().pixelsPerQuarter
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
                        const auto x = (float) xForTick ((double) tick);

                        if (x >= (float) plot.getX() - 1.0f && x <= (float) plot.getRight())
                        {
                            g.setColour (theme::gridBeatLine.withAlpha (0.36f));
                            g.drawDashedLine ({ x, (float) plot.getY(),
                                                x, (float) plot.getBottom() },
                                              dashes, 2, 1.0f);
                        }
                    }

                    tick += (juce::int64) std::llround (step);
                }
            }
        }
    }

    // ---- the value scale: drawn over the plot's left edge, so the lane's pixels
    //      line up with the canvas above it -------------------------------------
    {
        static constexpr int ticks[] = { 127, 96, 64, 32, 0 };

        g.setFont (theme::font (9.0f));

        for (const auto value : ticks)
        {
            const auto y = yForValue ((double) value);

            g.setColour (theme::outline.withAlpha (0.7f));
            g.fillRect (plot.getX(), y, plot.getWidth(), 1);

            g.setColour (theme::panelBackground.withAlpha (0.75f));
            g.fillRect (plot.getX(), y - 6, scaleWidth, 12);

            g.setColour (theme::dimText);
            g.drawText (juce::String (value),
                        juce::Rectangle<int> (plot.getX() + 3, y - 6, scaleWidth - 6, 12),
                        juce::Justification::centredLeft, false);
        }
    }

    const auto* clip = openClip();

    if (clip == nullptr || clip->isAudio())
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.5f));
        g.drawText (readoutText(), plot.reduced (10, 0), juce::Justification::centredLeft, true);
    }
    else if (appState.laneShowsVelocity)
    {
        for (const auto& bar : velocityBars())
        {
            const auto selected = appState.isNoteSelected (bar.noteId);
            const auto* track = openTrack();
            const auto base = track != nullptr ? theme::trackColour (track->colourIndex)
                                               : theme::accent;

            g.setColour (selected ? base.brighter (0.4f) : base.withAlpha (0.75f));
            g.fillRect (bar.bounds);
            g.setColour (theme::outline);
            g.drawRect (bar.bounds, 1);
        }
    }
    else
    {
        const auto points = curvePoints();

        if (points.size() >= 2)
        {
            juce::Path path;
            path.startNewSubPath ((float) points.front().x, (float) points.front().y);

            for (size_t i = 1; i < points.size(); ++i)
                path.lineTo ((float) points[i].x, (float) points[i].y);

            // The fill under the line makes the shape readable at a glance; the line
            // itself is what the user draws.
            juce::Path filled (path);
            filled.lineTo ((float) points.back().x, (float) plot.getBottom());
            filled.lineTo ((float) points.front().x, (float) plot.getBottom());
            filled.closeSubPath();

            g.setColour (theme::accent.withAlpha (0.14f));
            g.fillPath (filled);

            g.setColour (theme::accent);
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
        else if (points.size() == 1)
        {
            g.setColour (theme::dimText);
            g.setFont (theme::font (11.0f));
            g.drawText (NI_TXT ("按住拖动就能画这条 CC 曲线"),
                        plot.reduced (10, 0), juce::Justification::centredLeft, true);
        }

        for (const auto& point : points)
        {
            const juce::Rectangle<int> knee { point.x - 3, point.y - 3, 7, 7 };

            g.setColour (theme::panelRaised);
            g.fillRect (knee);
            g.setColour (theme::accent.brighter (0.3f));
            g.drawRect (knee, 1);
        }
    }

    // ---- the transport's own line -------------------------------------------
    {
        const music::TempoMap tempoMap (document.getProject().tempoMarkers);
        const auto x = (float) xForTick ((double) tempoMap.tickAtSeconds (audio.getPositionSeconds()));

        if (x >= (float) plot.getX() - 2.0f && x <= (float) plot.getRight() + 2.0f)
        {
            g.setColour (theme::playhead);
            g.fillRect (x, (float) plot.getY(), 1.6f, (float) plot.getHeight());
        }
    }
}

void ControllerLane::refreshPlayhead()
{
    // The whole lane is redrawn, not just the playhead's own stripe: it follows the
    // editor's view, so *any* sideways movement - the scroll bar, the wheel, the
    // drag's edge scrolling - has to be redrawn with it. (The ruler and the marker
    // track above do exactly the same thing for the same reason; the big canvas can
    // afford to repaint a stripe because it is the thing that moves the view.)
    ++viewRefreshes;
    repaint();
}

//==============================================================================
void ControllerLane::mouseDown (const juce::MouseEvent& e)
{
    const auto point = e.getPosition();

    if (! plotBounds().contains (point))
        return;

    const auto* clip = openClip();

    if (clip == nullptr || clip->isAudio())
        return;

    if (appState.laneShowsVelocity)
    {
        const auto index = velocityBarNear (point);

        if (index < 0)
            return;

        const auto bars = velocityBars();
        const auto noteId = bars[(size_t) index].noteId;

        // Dragging a bar picks its note (Ctrl keeps the rest of the selection, the
        // same as clicking a note in the canvas).
        if (! appState.isNoteSelected (noteId))
            appState.selectNote (noteId, e.mods.isCommandDown());

        // One gesture, one undo step: remember where every selected note started.
        velocityStartValues.clear();

        for (const auto& note : clip->notes)
            if (appState.isNoteSelected (note.id))
                velocityStartValues.emplace_back (note.id, note.velocity);

        velocityDragStartY = point.y;
        drag = Drag::velocity;
        document.beginEdit (NI_TXT ("调整力度"));
        const auto initialVelocity = juce::jlimit (
            0, 127, (int) std::lround (valueForY ((double) point.y)));
        const auto initialClipId = clip->id;
        document.applyDerivedEdit ([this, initialClipId, initialVelocity] (Project& project)
        {
            if (auto* target = project.findClip (initialClipId))
                for (const auto& [noteId, original] : velocityStartValues)
                {
                    juce::ignoreUnused (original);
                    for (auto& note : target->notes)
                        if (note.id == noteId)
                            note.velocity = initialVelocity;
                }
        });
        repaint();
        return;
    }

    // Right click: on a knee it deletes it, on empty space it offers to clear the
    // whole curve.
    if (e.mods.isPopupMenu())
    {
        juce::int64 tick = 0;

        if (kneeNear (point, tick) != nullptr)
        {
            const auto cc = appState.laneController;
            const auto clipId = clip->id;

            document.applyEdit (NI_TXT ("删除 CC 折点"), [clipId, cc, tick] (Project& project)
            {
                if (auto* target = project.findClip (clipId))
                    if (auto* found = controllers::findLane (*target, cc))
                    {
                        controllers::removeControllerPoint (*found, tick,
                                                            controllers::kDefaultStepTicks);
                        controllers::pruneEmptyCurves (*target);
                    }
            });
        }
        else
        {
            const auto clipId = clip->id;
            const auto cc = appState.laneController;
            auto* menu = new juce::PopupMenu();
            auto options = juce::PopupMenu::Options().withTargetComponent (this);

            menu->addItem (1, NI_TXT ("清空这条 CC 曲线"));
            menu->showMenuAsync (options, [this, menu, clipId, cc] (int result)
            {
                std::unique_ptr<juce::PopupMenu> owner (menu);

                if (result != 1)
                    return;

                document.applyEdit (NI_TXT ("清空 CC 曲线"), [clipId, cc] (Project& project)
                {
                    if (auto* target = project.findClip (clipId))
                    {
                        if (auto* found = controllers::findLane (*target, cc))
                            found->points.clear();

                        controllers::pruneEmptyCurves (*target);
                    }
                });
            });
        }

        return;
    }

    juce::int64 kneeTick = 0;

    if (kneeNear (point, kneeTick) != nullptr)
    {
        dragKneeTick = kneeTick;
        dragStartPoint = point;
        drag = Drag::moveKnee;
        document.beginEdit (NI_TXT ("移动 CC 折点"));
        return;
    }

    // Empty space: Shift draws one straight line between press and release, plain
    // dragging is the pencil.
    const auto tick = snappedTickForX ((double) point.x);
    const auto value = juce::jlimit (0, 127, (int) std::lround (valueForY ((double) point.y)));

    if (e.mods.isShiftDown())
    {
        lineStartTick = tick;
        lineStartValue = value;
        drag = Drag::drawLine;
        setReadout (NI_TXT ("直线：拖到终点松手（起点 tick ") + juce::String (tick)
                        + NI_TXT ("，值 ") + juce::String (value) + "）");
        repaint();
        return;
    }

    drag = Drag::drawCurve;
    lastDrawnTick = -1;
    lastDrawnPixelX = -1;
    curveGestureStartTick = tick;
    curveGestureEndTick = tick;
    document.beginEdit (NI_TXT ("画 CC 曲线"));

    {
        const auto clipId = clip->id;
        const auto cc = appState.laneController;

        document.applyDerivedEdit ([clipId, cc, tick, value] (Project& project)
        {
            if (auto* target = project.findClip (clipId))
                controllers::upsertControllerPoint (controllers::laneFor (*target, cc),
                                                    tick, value);
        });
    }

    lastDrawnTick = tick;
    repaint();
}

void ControllerLane::mouseDrag (const juce::MouseEvent& e)
{
    const auto point = e.getPosition();

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::velocity:
        {
            // Up is louder. Three pixels per velocity step, like the drum grid.
            const auto value = juce::jlimit (
                0, 127, (int) std::lround (valueForY ((double) point.y)));
            const auto delta = 0;
            const auto clipId = appState.selectedClipId;

            document.applyDerivedEdit ([this, clipId, value] (Project& project)
            {
                if (auto* clip = project.findClip (clipId))
                    for (const auto& [noteId, original] : velocityStartValues)
                    {
                        juce::ignoreUnused (original);
                        for (auto& note : clip->notes)
                            if (note.id == noteId)
                                note.velocity = value;
                    }
            });

            if (! velocityStartValues.empty())
                velocityStartValues.front().second = value;

            setReadout (NI_TXT ("力度 ") + juce::String (juce::jlimit (
                            0, 127, (velocityStartValues.empty()
                                         ? 0 : velocityStartValues.front().second) + delta))
                            + NI_TXT ("（改了 ") + juce::String ((int) velocityStartValues.size())
                            + NI_TXT (" 个音符）"));
            repaint();
            return;
        }

        case Drag::moveKnee:
        {
            const auto* clip = openClip();

            if (clip == nullptr)
                return;

            const auto cc = appState.laneController;
            const auto clipId = clip->id;
            const auto from = dragKneeTick;
            const auto to = juce::jmax ((juce::int64) 0, snappedTickForX ((double) point.x));
            const auto value = juce::jlimit (0, 127, (int) std::lround (valueForY ((double) point.y)));

            document.applyDerivedEdit ([clipId, cc, from, to, value] (Project& project)
            {
                if (auto* target = project.findClip (clipId))
                    if (auto* lane = controllers::findLane (*target, cc))
                    {
                        controllers::removeControllerPoint (*lane, from, 0);
                        controllers::upsertControllerPoint (*lane, to, value);
                    }
            });

            dragKneeTick = to;
            setReadout (NI_TXT ("tick ") + juce::String (to) + NI_TXT (" · 值 ")
                            + juce::String (value));
            repaint();
            return;
        }

        case Drag::drawCurve:
        {
            const auto* clip = openClip();

            if (clip == nullptr)
                return;

            const auto tick = snappedTickForX ((double) point.x);

            if (tick == lastDrawnTick && view().controllerSnap.enabled)
                return;

            if (! view().controllerSnap.enabled && tick != lastDrawnTick
                && lastDrawnPixelX >= 0
                && std::abs (point.x - lastDrawnPixelX) < 3)
                return;

            const auto value = juce::jlimit (0, 127, (int) std::lround (valueForY ((double) point.y)));
            const auto cc = appState.laneController;
            const auto clipId = clip->id;

            document.applyDerivedEdit ([clipId, cc, tick, value] (Project& project)
            {
                if (auto* target = project.findClip (clipId))
                    controllers::upsertControllerPoint (controllers::laneFor (*target, cc),
                                                        tick, value);
            });

            lastDrawnTick = tick;
            lastDrawnPixelX = point.x;
            curveGestureStartTick = juce::jmin (curveGestureStartTick, tick);
            curveGestureEndTick = juce::jmax (curveGestureEndTick, tick);
            setReadout (NI_TXT ("tick ") + juce::String (tick) + NI_TXT (" · 值 ")
                            + juce::String (value));
            repaint();
            return;
        }

        case Drag::drawLine:
        {
            const auto tick = snappedTickForX ((double) point.x);
            const auto value = juce::jlimit (0, 127, (int) std::lround (valueForY ((double) point.y)));

            setReadout (NI_TXT ("直线到 tick ") + juce::String (tick) + NI_TXT (" · 值 ")
                            + juce::String (value));
            repaint();
            return;
        }
    }
}

void ControllerLane::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::none)
        return;

    const auto finished = drag;
    drag = Drag::none;

    if (finished == Drag::drawLine)
    {
        const auto* clip = openClip();

        if (clip != nullptr)
        {
            const auto endTick = juce::jmax ((juce::int64) 0, snappedTickForX ((double) e.getPosition().x));
            const auto endValue = juce::jlimit (0, 127,
                                                (int) std::lround (valueForY ((double) e.getPosition().y)));
            const auto clipId = clip->id;
            const auto cc = appState.laneController;

            document.beginEdit (NI_TXT ("画 CC 直线"));
            document.applyDerivedEdit ([clipId, cc, endTick, endValue, this] (Project& project)
            {
                if (auto* target = project.findClip (clipId))
                {
                    auto& lane = controllers::laneFor (*target, cc);
                    const auto from = juce::jmin (lineStartTick, endTick);
                    const auto to = juce::jmax (lineStartTick, endTick);

                    // The line replaces whatever was drawn between the two ends.
                    lane.points.erase (std::remove_if (lane.points.begin(), lane.points.end(),
                                                       [from, to] (const ControllerPoint& point)
                                                       { return point.tick >= from && point.tick <= to; }),
                                       lane.points.end());

                    controllers::upsertControllerPoint (lane, from,
                                                        lineStartTick <= endTick ? lineStartValue
                                                                                 : endValue);
                    controllers::upsertControllerPoint (lane, to,
                                                        lineStartTick <= endTick ? endValue
                                                                                 : lineStartValue);
                }
            });
            document.notifyEdit();
        }
    }
    else if (finished == Drag::velocity || finished == Drag::drawCurve
             || finished == Drag::moveKnee)
    {
        if (finished == Drag::drawCurve
            && ! document.viewState().controllerSnap.enabled)
            simplifyCurveGesture (curveGestureStartTick, curveGestureEndTick);

        document.notifyEdit();
    }

    velocityStartValues.clear();
    lastDrawnTick = -1;
    lastDrawnPixelX = -1;
    setReadout (readoutText());
    repaint();
}

void ControllerLane::mouseMove (const juce::MouseEvent& e)
{
    const auto point = e.getPosition();

    if (! plotBounds().contains (point))
        return;

    if (appState.laneShowsVelocity)
    {
        const auto index = velocityBarNear (point);

        if (index >= 0)
        {
            const auto bars = velocityBars();
    const auto* clip = openClip();

            if (clip != nullptr)
                for (const auto& note : clip->notes)
                    if (note.id == bars[(size_t) index].noteId)
                    {
                        setReadout (NI_TXT ("力度 ") + juce::String (note.velocity));
                        return;
                    }
        }

        setReadout (readoutText());
        return;
    }

    setReadout (NI_TXT ("tick ") + juce::String ((juce::int64) std::llround (tickForX ((double) point.x)))
                    + NI_TXT (" · 值 ")
                    + juce::String (juce::jlimit (0, 127, (int) std::lround (valueForY ((double) point.y)))));
}

void ControllerLane::mouseExit (const juce::MouseEvent&)
{
    if (drag == Drag::none)
        setReadout (readoutText());
}

} // namespace nidaw
