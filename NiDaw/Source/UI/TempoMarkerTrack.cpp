#include "TempoMarkerTrack.h"
#include "Theme.h"

namespace nidaw
{

TempoMarkerTrack::TempoMarkerTrack (ProjectDocument& documentToUse, AppState& appStateToUse,
                                    TimelineView timelineToUse)
    : document (documentToUse),
      appState (appStateToUse),
      view (timelineToUse)
{
    document.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

TempoMarkerTrack::~TempoMarkerTrack()
{
    document.removeChangeListener (this);
}

double TempoMarkerTrack::xForTick (double tick) const
{
    return xForTimelineTick (document.viewState(), view, tick);
}

void TempoMarkerTrack::refresh()
{
    repaint();
}

void TempoMarkerTrack::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void TempoMarkerTrack::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (theme::gridTempoStrip);
    g.fillRect (area);
    g.setColour (theme::outline);
    g.drawHorizontalLine (area.getBottom() - 1, 0.0f, (float) area.getWidth());

    const auto& project = document.getProject();

    g.setFont (theme::font (11.0f));

    for (const auto& marker : project.tempoMarkers)
    {
        const auto x = (float) xForTick ((double) marker.tick);

        if (x < -160.0f || x > (float) area.getWidth() + 4.0f)
            continue;

        g.setColour (theme::accent);
        juce::Path flag;
        flag.startNewSubPath (x, (float) area.getBottom() - 2.0f);
        flag.lineTo (x, 4.0f);
        flag.lineTo (x + 9.0f, 8.0f);
        flag.lineTo (x, 12.0f);
        flag.closeSubPath();
        g.fillPath (flag);

        g.setColour (theme::text);
        g.drawText (juce::String (marker.numerator) + ":" + juce::String (marker.denominator)
                        + "   " + juce::String (marker.bpm, 2),
                    juce::Rectangle<int> ((int) x + 12, 1, 150, area.getHeight() - 2),
                    juce::Justification::centredLeft);
    }

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.0f));
    g.drawText (NI_TXT ("节拍标记轨 · 左键新建 / 编辑，右键删除"),
                area.reduced (6, 0), juce::Justification::centredRight);
}

int TempoMarkerTrack::markerIndexAtX (int x) const
{
    const auto& markers = document.getProject().tempoMarkers;
    int best = -1;
    auto bestDistance = 14.0;

    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const auto distance = std::abs (xForTick ((double) markers[(size_t) i].tick) - (double) x);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void TempoMarkerTrack::mouseDown (const juce::MouseEvent& e)
{
    const auto index = markerIndexAtX (e.x);
    const auto& markers = document.getProject().tempoMarkers;

    if (e.mods.isPopupMenu())
    {
        if (index > 0)
        {
            const auto tick = markers[(size_t) index].tick;

            document.applyEdit (NI_TXT ("删除节拍标记"), [tick] (Project& project)
            {
                auto& all = project.tempoMarkers;
                all.erase (std::remove_if (all.begin(), all.end(),
                                           [tick] (const TempoMarker& m) { return m.tick == tick; }),
                           all.end());

                if (all.empty())
                    all.push_back ({ 0, 4, 4, 120.0 });
            });
        }

        return;
    }

    if (index >= 0)
    {
        editMarker (index, markers[(size_t) index].tick, false);
        return;
    }

    const auto rawTick = timelineTickForX (document.viewState(), view, (double) e.x);
    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    const auto gridTicks = juce::jmax (1.0, tempoMap.ticksPerBeatAt ((juce::int64) rawTick));
    const auto tick = juce::jmax ((juce::int64) 0,
                                  music::TempoMap::snapToGrid ((juce::int64) std::llround (rawTick), gridTicks));

    editMarker (-1, tick, true);
}

void TempoMarkerTrack::editMarker (int index, juce::int64 tick, bool isNew)
{
    TempoMarker existing;

    if (isNew)
    {
        const auto& markers = document.getProject().tempoMarkers;
        const music::TempoMap tempoMap (markers);

        // New markers inherit whatever is currently in force.
        existing.tick = tick;
        existing.numerator = tempoMap.numeratorAt (tick);
        existing.denominator = tempoMap.denominatorAt (tick);
        existing.bpm = tempoMap.bpmAt (tick);
    }
    else
    {
        const auto& markers = document.getProject().tempoMarkers;

        if (index < 0 || index >= (int) markers.size())
            return;

        existing = markers[(size_t) index];
    }

    juce::AlertWindow window (isNew ? NI_TXT ("新建节拍标记") : NI_TXT ("编辑节拍标记"),
                              NI_TXT ("拍型（每小节拍数 : 拍的单位）与速度，横向生效到下一个标记之前。"),
                              juce::MessageBoxIconType::NoIcon,
                              this);

    window.addTextEditor ("numerator", juce::String (existing.numerator), NI_TXT ("每小节拍数"));
    window.addTextEditor ("denominator", juce::String (existing.denominator), NI_TXT ("拍的单位（分母）"));
    window.addTextEditor ("bpm", juce::String (existing.bpm, 2), NI_TXT ("速度 BPM"));
    window.addButton (NI_TXT ("确定"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (NI_TXT ("取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (window.runModalLoop() != 1)
        return;

    TempoMarker marker;
    marker.tick        = tick;
    marker.numerator   = juce::jlimit (1, 32, window.getTextEditorContents ("numerator").getIntValue());
    marker.denominator = juce::jlimit (1, 32, window.getTextEditorContents ("denominator").getIntValue());
    marker.bpm         = juce::jlimit (10.0, 400.0, window.getTextEditorContents ("bpm").getDoubleValue());

    if (marker.numerator <= 0 || marker.denominator <= 0)
        return;

    document.applyEdit (isNew ? NI_TXT ("新建节拍标记") : NI_TXT ("编辑节拍标记"),
                        [marker, isNew] (Project& project)
    {
        auto& markers = project.tempoMarkers;

        if (isNew)
        {
            markers.erase (std::remove_if (markers.begin(), markers.end(),
                                           [&marker] (const TempoMarker& m) { return m.tick == marker.tick; }),
                           markers.end());
            markers.push_back (marker);
        }
        else
        {
            for (auto& m : markers)
                if (m.tick == marker.tick)
                    m = marker;
        }

        std::sort (markers.begin(), markers.end(),
                   [] (const TempoMarker& a, const TempoMarker& b) { return a.tick < b.tick; });

        if (markers.empty() || markers.front().tick != 0)
            markers.insert (markers.begin(), TempoMarker { 0, 4, 4, 120.0 });
    });
}

} // namespace nidaw
