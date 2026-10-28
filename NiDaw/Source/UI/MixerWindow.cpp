#include "MixerWindow.h"

#include "../Core/EffectChain.h"
#include "../Core/MusicMath.h"
#include "../Core/WindowPlacement.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr int kTransportHeight = 30;
    constexpr int kOuterMargin = 2;
    constexpr int kGap = 3;
    constexpr int kNameHeight = 18;
    constexpr int kInstrumentHeight = 20;
    constexpr int kInsertRows = 4;
    constexpr int kInsertRowHeight = 17;
    constexpr int kAddRowHeight = 17;
    constexpr int kPanHeight = 14;
    constexpr int kMuteRowHeight = 20;
    /** The fader is deliberately narrow and the meters deliberately wide: the bars
        are what the eye has to read while playing. */
    constexpr int kFaderWidth = 15;
    constexpr int kMeterBarWidth = 18;
    constexpr int kMeterLabelWidth = 15;
    constexpr int kFaderScaleWidth = 22;

    /** The fader's range: the same one a track's volume has. */
    constexpr double kMinDb = -60.0, kMaxDb = 6.0;

    /** The fader's scale, top first: a tick and a number at each of these. */
    constexpr int kFaderScaleTicks[] = { 6, 0, -12, -24, -40, -60 };

    /** The meter's own ticks. It is a peak meter, i.e. linear in amplitude, so these
        land closer and closer together as they go down - which is exactly how a meter
        reads. Only three carry a number; the bars are 12 px wide. */
    constexpr int kMeterTicks[] = { 0, -6, -12, -24, -40, -60 };
    constexpr int kMeterLabels[] = { 0, -12, -40 };

    /** The meter's colour scale: green when quiet, yellow in the middle, red at the
        top. The colour belongs to the *scale* - a given dB always looks the same -
        so what changes as the level moves is how much of it the bar covers. */
    struct MeterStop { double db; juce::Colour colour; };

    const MeterStop kMeterStops[] =
    {
        { -60.0, juce::Colour (0xff3f9f6f) },
        { -24.0, juce::Colour (0xff6fcf92) },
        { -12.0, juce::Colour (0xffcfd464) },
        {  -6.0, juce::Colour (0xffe8b84a) },
        {  -3.0, juce::Colour (0xffe08a45) },
        {   0.0, theme::playhead }
    };

    /** Where a level sits in a 0..1 range, for the gradient. */
    float meterPositionForDb (double db) noexcept
    {
        return (float) juce::jlimit (0.0, 1.0, std::pow (10.0, db / 20.0));
    }

    juce::Colour meterColourForDb (double db) noexcept
    {
        const auto count = (int) (sizeof (kMeterStops) / sizeof (kMeterStops[0]));

        if (db <= kMeterStops[0].db)
            return kMeterStops[0].colour;

        if (db >= kMeterStops[count - 1].db)
            return kMeterStops[count - 1].colour;

        for (int i = 1; i < count; ++i)
            if (db <= kMeterStops[i].db)
            {
                const auto span = kMeterStops[i].db - kMeterStops[i - 1].db;
                const auto t = (float) ((db - kMeterStops[i - 1].db) / span);

                return kMeterStops[i - 1].colour.interpolatedWith (kMeterStops[i].colour, t);
            }

        return kMeterStops[count - 1].colour;
    }

    /** The gradient a bar is filled with: it runs from the quiet end (bottom, green)
        up to the loud end (top, red), so filling up to the level shows the right
        slice of the scale. */
    juce::ColourGradient makeMeterGradient (juce::Rectangle<int> bar)
    {
        juce::ColourGradient gradient (meterColourForDb (kMeterStops[0].db),
                                       (float) bar.getX(), (float) bar.getBottom(),
                                       meterColourForDb (kMeterStops[5].db),
                                       (float) bar.getX(), (float) bar.getY(), false);

        for (const auto& stop : kMeterStops)
            gradient.addColour (meterPositionForDb (stop.db), stop.colour);

        return gradient;
    }

    float dbForFader (juce::Rectangle<int> fader, int y) noexcept
    {
        const auto zeroToOne = juce::jlimit (0.0f, 1.0f,
                                             (float) (fader.getBottom() - y)
                                                 / (float) juce::jmax (1, fader.getHeight()));

        return (float) (kMinDb + zeroToOne * (kMaxDb - kMinDb));
    }

    int yForDb (juce::Rectangle<int> fader, double db) noexcept
    {
        const auto zeroToOne = juce::jlimit (0.0, 1.0, (db - kMinDb) / (kMaxDb - kMinDb));

        return fader.getBottom() - (int) (zeroToOne * (double) fader.getHeight());
    }

    /** Where a peak sits on a meter bar. The bar is linear in amplitude: 0 dB (= 1.0)
        is the top, half the amplitude (-6 dB) is half way down. */
    int yForLevel (juce::Rectangle<int> meter, double level) noexcept
    {
        return meter.getBottom()
                   - (int) (juce::jlimit (0.0, 1.0, level) * (double) meter.getHeight());
    }

    /** "+6" / "0" / "-12"…: the label the fader scale puts beside a tick. */
    juce::String dbLabel (int db)
    {
        return (db > 0 ? juce::String ("+") : juce::String()) + juce::String (db);
    }
}

//==============================================================================
MixerWindow::Strip::Strip (MixerWindow& ownerToUse, ProjectDocument& documentToUse,
                           AppState& appStateToUse, juce::int64 trackIdToUse)
    : owner (ownerToUse), document (documentToUse), appState (appStateToUse),
      trackId (trackIdToUse), master (trackIdToUse == 0)
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MixerWindow::Strip::setTrackId (juce::int64 newTrackId)
{
    if (newTrackId == trackId)
        return;

    trackId = newTrackId;
    master = newTrackId == 0;
    levelLeft = levelRight = 0.0f;
    holdLeft = holdRight = 1.0f;
    holdSecondsLeft = holdSecondsRight = 0.0;
    clippingLeft = clippingRight = false;
    repaint();
}

MixerWindow::Strip::Layout MixerWindow::Strip::layout() const
{
    Layout area;
    auto bounds = getLocalBounds().reduced (kOuterMargin, kOuterMargin);

    area.name = bounds.removeFromTop (kNameHeight);

    if (! master)
    {
        bounds.removeFromTop (kGap);
        area.instrument = bounds.removeFromTop (kInstrumentHeight);
        bounds.removeFromTop (kGap);
        area.inserts = bounds.removeFromTop (kInsertRows * kInsertRowHeight);
        area.addRow = bounds.removeFromTop (kAddRowHeight);
        bounds.removeFromTop (kGap);
        area.pan = bounds.removeFromTop (kPanHeight);
        bounds.removeFromTop (kGap);
    }

    // The mute / solo row is reserved on both kinds of strip, so the faders and the
    // meters line up across the whole desk.
    auto muteRow = bounds.removeFromBottom (kMuteRowHeight);
    area.mute = muteRow.removeFromLeft (muteRow.getWidth() / 2).reduced (2, 1);
    area.solo = muteRow.reduced (2, 1);
    bounds.removeFromBottom (kGap);

    area.meterLabels = bounds.removeFromRight (kMeterLabelWidth);
    bounds.removeFromRight (kGap);
    area.meterRight = bounds.removeFromRight (kMeterBarWidth);
    bounds.removeFromRight (2);
    area.meterLeft = bounds.removeFromRight (kMeterBarWidth);
    bounds.removeFromRight (kGap);
    area.faderScale = bounds.removeFromLeft (kFaderScaleWidth);
    area.fader = bounds.withSizeKeepingCentre (juce::jmin (kFaderWidth, bounds.getWidth()),
                                               bounds.getHeight());
    return area;
}

juce::Rectangle<int> MixerWindow::Strip::insertRowBounds (int row) const
{
    const auto area = layout();

    if (row < 0 || row >= kInsertRows)
        return {};

    return { area.inserts.getX(), area.inserts.getY() + row * kInsertRowHeight,
             area.inserts.getWidth(), kInsertRowHeight };
}

juce::Rectangle<int> MixerWindow::Strip::insertBypassBounds (int row) const
{
    auto bounds = insertRowBounds (row);

    if (bounds.isEmpty())
        return {};

    // The whole right end of the row is the button; the box drawn inside it is
    // smaller, so the thing that is visible is comfortably clickable.
    return bounds.removeFromRight (18);
}

MixerWindow::Strip::Part MixerWindow::Strip::partAt (juce::Point<int> point, int& rowIndex) const
{
    rowIndex = -1;

    const auto area = layout();

    if (area.meterLeft.contains (point) || area.meterRight.contains (point))
        return Part::meter;

    // The scale is part of the fader's hit area: clicking beside the scale jumps the
    // fader there, the way every desk does it.
    if (area.faderScale.getUnion (area.fader).expanded (0, 1).contains (point))
        return Part::fader;

    if (master)
        return Part::none;      // the master strip has no M / S, no pan, no chain

    if (area.pan.contains (point))
        return Part::pan;

    if (area.mute.contains (point))
        return Part::mute;

    if (area.solo.contains (point))
        return Part::solo;

    if (area.addRow.contains (point))
        return Part::addRow;

    if (area.inserts.contains (point))
    {
        rowIndex = (point.y - area.inserts.getY()) / kInsertRowHeight;

        return insertBypassBounds (rowIndex).contains (point) ? Part::insertBypass
                                                              : Part::insertRow;
    }

    if (area.instrument.contains (point))
        return Part::instrument;

    return Part::none;          // anywhere else: the strip itself (which selects)
}

juce::Rectangle<int> MixerWindow::Strip::boundsFor (Part part) const
{
    const auto area = layout();

    switch (part)
    {
        case Part::name:       return area.name;
        case Part::instrument: return area.instrument;
        case Part::insertRow:  return area.inserts;
        case Part::insertBypass: return insertBypassBounds (0);
        case Part::addRow:     return area.addRow;
        case Part::pan:        return area.pan;
        case Part::fader:      return area.fader;
        case Part::meter:      return area.meterLeft.getUnion (area.meterRight);
        case Part::mute:       return area.mute;
        case Part::solo:       return area.solo;
        case Part::none:       break;
    }

    return {};
}

juce::StringArray MixerWindow::Strip::scaleLabels() const
{
    juce::StringArray labels;

    for (const auto tick : kFaderScaleTicks)
        labels.add (dbLabel (tick));

    return labels;
}

//==============================================================================
void MixerWindow::Strip::paint (juce::Graphics& g)
{
    const auto area = layout();
    const auto* track = master ? nullptr : document.getProject().findTrack (trackId);
    const auto selected = track != nullptr && appState.selectedTrackId == trackId;
    const auto colour = master ? theme::accent
                               : theme::trackColour (track != nullptr ? track->colourIndex : 0);

    g.setColour (selected ? theme::panelRaised : theme::panelBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (colour.withAlpha (selected ? 1.0f : 0.55f));
    g.fillRect (2, 3, 3, juce::jmax (0, getHeight() - 6));

    if (master)
    {
        paintMaster (g, area);
        return;
    }

    if (track == nullptr)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f));
        g.drawText (NI_TXT ("轨道不见了"), area.name, juce::Justification::centred, false);
        return;
    }

    paintTrack (g, area, *track);
}

void MixerWindow::Strip::paintTrack (juce::Graphics& g, const Layout& area,
                                     const InstrumentTrack& track)
{
    const auto colour = theme::trackColour (track.colourIndex);

    // ---- name ----
    g.setColour (theme::text);
    g.setFont (theme::font (11.5f, true));
    g.drawText (track.name, area.name.reduced (5, 0), juce::Justification::centredLeft, true);

    // ---- instrument ----
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (area.instrument.toFloat(), 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.instrument.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (10.5f));
    g.drawText (track.kind == TrackKind::vst ? NI_TXT ("插件界面") : NI_TXT ("音色"),
                area.instrument, juce::Justification::centred, false);

    // ---- insert chain: four rows, then the add row ----
    for (int row = 0; row < kInsertRows; ++row)
    {
        const auto rowBounds = insertRowBounds (row);

        const auto filled = row < (int) track.effects.size();
        const auto bypassed = filled && track.effects[(size_t) row].bypass;

        if (filled)
        {
            const auto& slot = track.effects[(size_t) row];

            g.setColour (bypassed ? theme::panelBackground : theme::accentDim);
            g.fillRoundedRectangle (rowBounds.reduced (1).toFloat(), 2.0f);
            g.setColour (bypassed ? theme::dimText : theme::text);
            g.setFont (theme::font (9.5f));
            g.drawText (slot.name.isNotEmpty() ? slot.name : NI_TXT ("插件缺失"),
                        rowBounds.reduced (4, 0).withTrimmedRight (20),
                        juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (theme::outline.withAlpha (0.45f));
            g.drawRoundedRectangle (rowBounds.reduced (1).toFloat(), 2.0f, 1.0f);
        }

        // The bypass button: filled in when the plugin is in circuit, dim with a
        // stroke through it when it is out of the way. Clicking this box is what
        // toggles - the rest of the row opens the plugin.
        const auto box = insertBypassBounds (row).reduced (4, 3);

        g.setColour (filled && ! bypassed ? theme::referenceLineTarget.withAlpha (0.85f)
                                          : theme::panelBackground);
        g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);

        if (bypassed)
        {
            g.setColour (theme::dimText);
            g.drawLine ((float) box.getX() + 2.0f, (float) box.getY() + 2.0f,
                        (float) box.getRight() - 2.0f, (float) box.getBottom() - 2.0f, 1.4f);
        }
    }

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.0f));
    g.drawText (track.effects.empty() ? NI_TXT ("＋ 加效果器") : NI_TXT ("＋ 再加一个"),
                area.addRow, juce::Justification::centred, false);

    // ---- pan ----
    {
        const auto bar = area.pan.reduced (1, 4);

        g.setColour (theme::gridBackground);
        g.fillRoundedRectangle (bar.toFloat(), 2.0f);
        g.setColour (theme::outline);
        g.fillRect (bar.getCentreX() - 1, bar.getY(), 2, bar.getHeight());

        const auto x = bar.getX() + (int) ((juce::jlimit (-1.0f, 1.0f, track.pan) + 1.0f)
                                               * 0.5f * (float) bar.getWidth());

        g.setColour (theme::accent);
        g.fillRect (juce::jlimit (bar.getX(), bar.getRight() - 3, x - 1),
                    bar.getY() - 2, 3, bar.getHeight() + 4);
    }

    paintFader (g, area, track.volumeDb, colour);
    paintMeter (g, area);

    // ---- mute / solo ----
    g.setColour (track.muted ? theme::playhead.withAlpha (0.85f) : theme::panelRaised);
    g.fillRoundedRectangle (area.mute.toFloat(), 3.0f);
    g.setColour (track.soloed ? theme::referenceLineTarget.withAlpha (0.85f) : theme::panelRaised);
    g.fillRoundedRectangle (area.solo.toFloat(), 3.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("M", area.mute, juce::Justification::centred, false);
    g.drawText ("S", area.solo, juce::Justification::centred, false);
}

void MixerWindow::Strip::paintMaster (juce::Graphics& g, const Layout& area)
{
    g.setColour (theme::text);
    g.setFont (theme::font (12.0f, true));
    g.drawText (NI_TXT ("总输出"), area.name, juce::Justification::centred, false);

    // The mute row is where the master strip explains its clip dot.
    g.setColour (theme::dimText);
    g.setFont (theme::font (9.5f));
    g.drawText (NI_TXT ("点电平表清削顶"), area.mute.getUnion (area.solo),
                juce::Justification::centred, true);

    paintFader (g, area, document.getProject().masterVolumeDb, theme::accent);
    paintMeter (g, area);
}

void MixerWindow::Strip::paintFader (juce::Graphics& g, const Layout& area, float db,
                                     juce::Colour colour)
{
    // ---- the scale: a tick and a number at each step ----
    g.setFont (theme::font (8.5f));

    for (const auto tick : kFaderScaleTicks)
    {
        const auto y = yForDb (area.fader, tick);

        g.setColour (theme::dimText.withAlpha (0.65f));
        g.fillRect (area.faderScale.getRight() - 4, y, 4, 1);
        g.setColour (theme::dimText);
        g.drawText (dbLabel (tick),
                    area.faderScale.withTrimmedRight (5).withY (y - 6).withHeight (12),
                    juce::Justification::centredRight, false);
    }

    // ---- the track, the knob and the read-out ----
    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.fader.toFloat(), 3.0f);
    g.setColour (theme::outline.withAlpha (0.7f));
    g.drawRoundedRectangle (area.fader.toFloat().reduced (0.5f), 3.0f, 1.0f);

    // The unity mark, so "0 dB" is findable at a glance.
    {
        const auto y = yForDb (area.fader, 0.0);
        g.setColour (theme::text.withAlpha (0.35f));
        g.fillRect (area.fader.getX() + 1, y, area.fader.getWidth() - 2, 1);
    }

    const auto knob = juce::Rectangle<int> (area.fader.getX() - 3,
                                            yForDb (area.fader, db) - 5,
                                            area.fader.getWidth() + 4, 10);
    g.setColour (colour);
    g.fillRoundedRectangle (knob.toFloat(), 2.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (knob.toFloat().reduced (0.5f), 2.0f, 1.0f);

    g.setColour (theme::dimText);
    g.setFont (theme::font (9.5f));
    g.drawText (juce::String (db, 1),
                area.fader.withY (area.fader.getBottom() - 12).withHeight (12),
                juce::Justification::centred, false);
}

void MixerWindow::Strip::paintMeter (juce::Graphics& g, const Layout& area)
{
    auto paintBar = [&g] (juce::Rectangle<int> bar, float level, float hold, bool clip)
    {
        g.setColour (theme::gridBackground);
        g.fillRoundedRectangle (bar.toFloat(), 2.0f);
        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (bar.toFloat().reduced (0.5f), 2.0f, 1.0f);

        const auto filled = juce::jlimit (0.0f, 1.0f, level);

        if (filled > 0.002f)
        {
            const auto top = yForLevel (bar, filled);

            // The colour comes from the scale, not from the level: quiet stays green
            // however loud the track is, and the top of the bar is where it goes red.
            g.setGradientFill (makeMeterGradient (bar));
            g.fillRect (bar.getX() + 1, top, bar.getWidth() - 2,
                        juce::jmax (1, bar.getBottom() - top - 1));
        }

        // The hold line: where the loudest peak of the last 1.5 s was.
        const auto holdY = yForLevel (bar, hold);

        g.setColour (theme::text.withAlpha (0.85f));
        g.fillRect (bar.getX() + 1, holdY - 1, bar.getWidth() - 2, 2);

        if (clip)
        {
            g.setColour (theme::playhead);
            g.fillRect (bar.getX() + 1, bar.getY() + 1, bar.getWidth() - 2, 4);
        }
    };

    paintBar (area.meterLeft, levelLeft, holdLeft, clippingLeft);
    paintBar (area.meterRight, levelRight, holdRight, clippingRight);

    // ---- the meter's own scale ----
    g.setFont (theme::font (8.0f));

    for (const auto tick : kMeterTicks)
    {
        const auto level = std::pow (10.0, tick / 20.0);
        const auto y = yForLevel (area.meterLeft, level);

        for (const auto x : { area.meterLeft.getX(), area.meterRight.getX() })
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (x, y, kMeterBarWidth, 1);
        }
    }

    for (const auto tick : kMeterLabels)
    {
        const auto level = std::pow (10.0, tick / 20.0);
        const auto y = yForLevel (area.meterLeft, level);

        g.setColour (theme::dimText);
        g.drawText (dbLabel (tick),
                    area.meterLabels.withY (y - 5).withHeight (11),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void MixerWindow::Strip::mouseDown (const juce::MouseEvent& e)
{
    auto rowIndex = -1;
    const auto part = partAt (e.getPosition(), rowIndex);

    const auto* track = master ? nullptr : document.getProject().findTrack (trackId);

    switch (part)
    {
        case Part::instrument:
            if (track != nullptr && owner.onOpenInstrument != nullptr)
                owner.onOpenInstrument (trackId);
            return;

        case Part::addRow:
            if (track != nullptr && owner.onAddEffect != nullptr)
                owner.onAddEffect (trackId);
            return;

        case Part::insertRow:
            if (track == nullptr || rowIndex < 0 || rowIndex >= (int) track->effects.size())
                return;

            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addItem (1, NI_TXT ("上移"));
                menu.addItem (2, NI_TXT ("下移"));
                menu.addItem (4, NI_TXT ("旁路"), true,
                              track->effects[(size_t) rowIndex].bypass);
                menu.addSeparator();
                menu.addItem (3, NI_TXT ("删除"));

                const auto id = trackId;
                const auto row = rowIndex;

                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                    [&owner = owner, id, row] (int result)
                                    {
                                        if (result > 0)
                                            owner.performInsertMenuCommand (id, row, result);
                                    });
            }
            else if (owner.onOpenEffect != nullptr)
            {
                owner.onOpenEffect (trackId, rowIndex);
            }

            return;

        case Part::insertBypass:
            if (track == nullptr || rowIndex < 0 || rowIndex >= (int) track->effects.size())
                return;

            owner.toggleEffectBypass (trackId, rowIndex);
            return;

        case Part::mute:
            owner.toggleMute (trackId);
            return;

        case Part::solo:
            owner.toggleSolo (trackId);
            return;

        case Part::pan:
            draggingPan = true;
            owner.beginEdit (NI_TXT ("声相"));
            setPanFromX (e.getPosition().x);
            return;

        case Part::fader:
            draggingFader = true;
            owner.beginEdit (master ? NI_TXT ("总输出音量") : NI_TXT ("轨道音量"));
            setFaderFromY (e.getPosition().y);
            return;

        case Part::meter:
            resetClip();
            return;

        case Part::name:
        case Part::none:
            break;
    }

    if (! master)
        appState.selectTrack (trackId);

    repaint();
}

void MixerWindow::Strip::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingFader)
        setFaderFromY (e.getPosition().y);
    else if (draggingPan)
        setPanFromX (e.getPosition().x);
}

void MixerWindow::Strip::mouseUp (const juce::MouseEvent&)
{
    if (! draggingFader && ! draggingPan)
        return;

    draggingFader = false;
    draggingPan = false;
    owner.endEdit();
}

void MixerWindow::Strip::mouseWheelMove (const juce::MouseEvent&,
                                         const juce::MouseWheelDetails& wheel)
{
    if (owner.content != nullptr)
        owner.content->scrollStripsBy (wheel.deltaX, wheel.deltaY);
}

void MixerWindow::Strip::setFaderFromY (int y)
{
    owner.setFaderDb (trackId, dbForFader (layout().fader, y));
    repaint();
}

void MixerWindow::Strip::setPanFromX (int x)
{
    const auto bar = layout().pan.reduced (1, 4);
    const auto zeroToOne = juce::jlimit (0.0f, 1.0f,
                                         (float) (x - bar.getX())
                                             / (float) juce::jmax (1, bar.getWidth()));

    owner.setPan (trackId, zeroToOne * 2.0f - 1.0f);
    repaint();
}

void MixerWindow::Strip::updateMeters (double dtSeconds)
{
    const auto reading = master ? owner.content->audio.readMasterMeter()
                                : owner.content->audio.readTrackMeter (trackId);

    levelLeft = meter::nextMeterLevel (levelLeft, reading.peakLeft, dtSeconds);
    levelRight = meter::nextMeterLevel (levelRight, reading.peakRight, dtSeconds);
    holdLeft = meter::nextPeakHold (holdLeft, levelLeft, holdSecondsLeft, dtSeconds);
    holdRight = meter::nextPeakHold (holdRight, levelRight, holdSecondsRight, dtSeconds);
    clippingLeft = clippingLeft || reading.clipLeft;
    clippingRight = clippingRight || reading.clipRight;

    repaint (boundsFor (Part::meter).getUnion (boundsFor (Part::fader)));
}

void MixerWindow::Strip::resetClip()
{
    clippingLeft = false;
    clippingRight = false;
    repaint();
}

//==============================================================================
MixerWindow::MixerWindow (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine,
                          juce::Component* context)
    : juce::DocumentWindow (NI_TXT ("混音台"), theme::panelBackground,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      placementContext (context)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);

    content = new Content (*this, document, appState, audioEngine);
    setContentOwned (content, false);

    const auto minimum = placement::fitMinimum ({ 760, 420 }, placement::displayAreaFor (context));
    setResizeLimits (juce::jmax (760, minimum.x), juce::jmax (420, minimum.y), 4000, 3000);
    setBounds (placement::placeWindow ({ kDesignWidth, kDesignHeight },
                                       placement::displayAreaFor (context)));
}

MixerWindow::~MixerWindow()
{
    content = nullptr;
}

void MixerWindow::resized()
{
    juce::DocumentWindow::resized();

    if (content != nullptr)
        content->setBounds (getLocalBounds());
}

void MixerWindow::closeButtonPressed()
{
    setVisible (false);
}

void MixerWindow::showForProject()
{
    if (content != nullptr)
    {
        content->rebuildRows();
        content->updateMeters();
    }

    setVisible (true);
    toFront (true);
}

void MixerWindow::beginEdit (const juce::String& description)
{
    if (content != nullptr)
        content->document.beginEdit (description);
}

void MixerWindow::endEdit()
{
    if (content != nullptr)
        content->document.notifyEdit();
}

void MixerWindow::setFaderDb (juce::int64 trackId, float db)
{
    if (content == nullptr)
        return;

    const auto clamped = juce::jlimit (-60.0f, 6.0f, db);

    if (trackId == 0)
    {
        content->document.applyDerivedEdit ([clamped] (Project& project)
                                            { project.masterVolumeDb = clamped; });
        return;
    }

    content->document.applyDerivedEdit ([trackId, clamped] (Project& project)
    {
        if (auto* track = project.findTrack (trackId))
            track->volumeDb = clamped;
    });
}

void MixerWindow::setPan (juce::int64 trackId, float pan)
{
    if (content == nullptr || trackId == 0)
        return;

    const auto clamped = juce::jlimit (-1.0f, 1.0f, pan);

    content->document.applyDerivedEdit ([trackId, clamped] (Project& project)
    {
        if (auto* track = project.findTrack (trackId))
            track->pan = clamped;
    });
}

void MixerWindow::toggleMute (juce::int64 trackId)
{
    if (content == nullptr || trackId == 0)
        return;

    const auto* track = content->document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    const auto value = ! track->muted;

    content->document.applyEdit (NI_TXT ("静音"), [trackId, value] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->muted = value;
    });
}

void MixerWindow::toggleSolo (juce::int64 trackId)
{
    if (content == nullptr || trackId == 0)
        return;

    const auto* track = content->document.getProject().findTrack (trackId);

    if (track == nullptr)
        return;

    const auto value = ! track->soloed;

    content->document.applyEdit (NI_TXT ("独奏"), [trackId, value] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            target->soloed = value;
    });
}

void MixerWindow::performInsertMenuCommand (juce::int64 trackId, int row, int menuId)
{
    if (content == nullptr)
        return;

    const auto* track = content->document.getProject().findTrack (trackId);

    if (track == nullptr || row < 0 || row >= (int) track->effects.size())
        return;

    const auto description = menuId == 4 ? NI_TXT ("旁路效果器")
                               : menuId == 3 ? NI_TXT ("移除效果器")
                                             : NI_TXT ("移动效果器");

    content->document.applyEdit (description,
                                 [trackId, row, menuId] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
        {
            if (menuId == 1)
                effects::moveUp (target->effects, row);
            else if (menuId == 2)
                effects::moveDown (target->effects, row);
            else if (menuId == 4)
                effects::toggleBypass (target->effects, row);
            else
                effects::remove (target->effects, row);
        }
    });
}

void MixerWindow::toggleEffectBypass (juce::int64 trackId, int slotIndex)
{
    if (content == nullptr || trackId == 0)
        return;

    const auto* track = content->document.getProject().findTrack (trackId);

    if (track == nullptr || slotIndex < 0 || slotIndex >= (int) track->effects.size())
        return;

    content->document.applyEdit (NI_TXT ("旁路效果器"), [trackId, slotIndex] (Project& project)
    {
        if (auto* target = project.findTrack (trackId))
            effects::toggleBypass (target->effects, slotIndex);
    });
}

//==============================================================================
MixerWindow::Content::Content (MixerWindow& ownerToUse, ProjectDocument& documentToUse,
                               AppState& appStateToUse, AudioEngine& audioEngine)
    : owner (ownerToUse), document (documentToUse), appState (appStateToUse), audio (audioEngine)
{
    addAndMakeVisible (stripViewport);
    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (true, true);

    auto setup = [this] (juce::TextButton& button, const juce::String& text)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
    };

    setup (playButton, NI_TXT ("▶ 播放"));
    setup (stopButton, NI_TXT ("■ 停止"));
    setup (returnButton, NI_TXT ("|◀ 回到开头"));

    playButton.onClick = [this] { if (owner.onPlayStop != nullptr) owner.onPlayStop(); };
    stopButton.onClick = [this] { if (owner.onPlayStop != nullptr) owner.onPlayStop(); };
    returnButton.onClick = [this] { if (owner.onReturnToStart != nullptr) owner.onReturnToStart(); };

    addAndMakeVisible (positionLabel);
    positionLabel.setFont (theme::font (12.0f));
    positionLabel.setJustificationType (juce::Justification::centredLeft);
    positionLabel.setColour (juce::Label::textColourId, theme::text);

    masterStrip = std::make_unique<Strip> (owner, document, appState, 0);
    addAndMakeVisible (*masterStrip);

    document.addChangeListener (this);
    appState.addChangeListener (this);

    rebuildRows();
    startTimerHz (30);
}

MixerWindow::Content::~Content()
{
    stopTimer();
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void MixerWindow::Content::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (strips.size() != document.getProject().tracks.size())
    {
        rebuildRows();
        return;
    }

    repaint();
}

void MixerWindow::Content::rebuildRows()
{
    const auto& tracks = document.getProject().tracks;

    while (strips.size() > tracks.size())
        strips.pop_back();

    while (strips.size() < tracks.size())
        strips.push_back (std::make_unique<Strip> (owner, document, appState, 0));

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        strips[i]->setTrackId (tracks[i].id);
        stripHolder.addAndMakeVisible (*strips[i]);
    }

    resized();
    repaint();
}

void MixerWindow::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBackground);
}

void MixerWindow::Content::resized()
{
    auto area = getLocalBounds();

    auto transport = area.removeFromBottom (kTransportHeight).reduced (6, 3);
    playButton.setBounds (transport.removeFromLeft (76));
    transport.removeFromLeft (4);
    stopButton.setBounds (transport.removeFromLeft (70));
    transport.removeFromLeft (4);
    returnButton.setBounds (transport.removeFromLeft (96));
    transport.removeFromLeft (10);
    positionLabel.setBounds (transport.removeFromLeft (160));

    // The master strip is outside the viewport, so it never scrolls away.
    masterStrip->setBounds (area.removeFromRight (kMasterWidth).reduced (2, 4));
    stripViewport.setBounds (area);

    // Short window: the strips keep their shape and the viewport scrolls, instead of
    // the faders being squeezed into a few pixels.
    const auto height = juce::jmax (kMinStripHeight, stripViewport.getMaximumVisibleHeight());

    stripHolder.setSize (juce::jmax (1, (int) strips.size() * kStripWidth), height);

    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds ((int) i * kStripWidth, 0, kStripWidth, height);
}

void MixerWindow::Content::timerCallback()
{
    updateMeters();
}

void MixerWindow::Content::updateMeters()
{
    for (auto& strip : strips)
        strip->updateMeters (lastTickSeconds);

    masterStrip->updateMeters (lastTickSeconds);

    const music::TempoMap tempoMap (document.getProject().tempoMarkers);
    int bar = 1, beat = 1, tick = 0;
    tempoMap.tickToBarBeat (tempoMap.tickAtSeconds (audio.getPositionSeconds()), bar, beat, tick);
    positionLabel.setText (juce::String (bar) + ":" + juce::String (beat) + ":"
                               + juce::String (tick),
                           juce::dontSendNotification);

    playButton.setButtonText (audio.isPlaying() ? NI_TXT ("▶ 播放中") : NI_TXT ("▶ 播放"));
}

void MixerWindow::Content::scrollStripsBy (float deltaX, float deltaY)
{
    auto position = stripViewport.getViewPosition();

    // The same shape as JUCE's own viewport wheel handling: rolling up (deltaY > 0)
    // reveals what is above, rolling right (deltaX > 0) reveals what is to the right.
    position.y -= (int) (deltaY * 90.0f);
    position.x += (int) (deltaX * 90.0f);
    stripViewport.setViewPosition (position);
}

//==============================================================================
int MixerWindow::getStripCountForTest() const
{
    return content != nullptr ? (int) content->strips.size() + 1 : 0;
}

juce::Component* MixerWindow::getStripComponentForTest (int index) const
{
    if (content == nullptr || index < 0)
        return nullptr;

    if (index < (int) content->strips.size())
        return content->strips[(size_t) index].get();

    if (index == (int) content->strips.size())
        return content->masterStrip.get();

    return nullptr;
}

namespace
{
    MixerWindow::Strip* stripFor (const MixerWindow& mixer, int index)
    {
        return dynamic_cast<MixerWindow::Strip*> (mixer.getStripComponentForTest (index));
    }
}

juce::int64 MixerWindow::getStripTrackIdForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr ? strip->getTrackId() : 0;
}

bool MixerWindow::isMasterStripForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr && strip->isMaster();
}

juce::Rectangle<int> MixerWindow::getPartBoundsForTest (int index, Strip::Part part) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr ? strip->boundsFor (part) : juce::Rectangle<int>();
}

juce::Rectangle<int> MixerWindow::getInsertRowBoundsForTest (int index, int row) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr ? strip->insertRowBounds (row) : juce::Rectangle<int>();
}

juce::Rectangle<int> MixerWindow::getInsertBypassBoundsForTest (int index, int row) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr ? strip->insertBypassBounds (row) : juce::Rectangle<int>();
}

juce::Colour MixerWindow::getMeterColourForTest (double db)
{
    return meterColourForDb (db);
}

juce::StringArray MixerWindow::getFaderScaleLabelsForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr ? strip->scaleLabels() : juce::StringArray();
}

int MixerWindow::getFaderTickYForTest (int index, double db) const
{
    const auto fader = getPartBoundsForTest (index, Strip::Part::fader);

    return fader.isEmpty() ? 0 : yForDb (fader, db);
}

int MixerWindow::getMeterTickYForTest (int index, double db) const
{
    const auto meter = getPartBoundsForTest (index, Strip::Part::meter);

    return meter.isEmpty() ? 0 : yForLevel (meter, std::pow (10.0, db / 20.0));
}

float MixerWindow::getFaderValueDbForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    if (strip == nullptr || content == nullptr)
        return 0.0f;

    if (strip->isMaster())
        return content->document.getProject().masterVolumeDb;

    if (const auto* track = content->document.getProject().findTrack (strip->getTrackId()))
        return track->volumeDb;

    return 0.0f;
}

int MixerWindow::getInsertRowCountForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    if (strip == nullptr || content == nullptr || strip->isMaster())
        return 0;

    if (const auto* track = content->document.getProject().findTrack (strip->getTrackId()))
        return juce::jmin (kInsertRows, (int) track->effects.size());

    return 0;
}

juce::String MixerWindow::getInsertRowTextForTest (int index, int row) const
{
    auto* strip = stripFor (*this, index);

    if (strip == nullptr || content == nullptr || strip->isMaster())
        return {};

    if (const auto* track = content->document.getProject().findTrack (strip->getTrackId()))
        if (row >= 0 && row < (int) track->effects.size())
            return track->effects[(size_t) row].name;

    return {};
}

juce::Point<float> MixerWindow::getMeterLevelsForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    if (strip == nullptr)
        return {};

    return { strip->getLevelLeft(), strip->getLevelRight() };
}

bool MixerWindow::isClippingForTest (int index) const
{
    auto* strip = stripFor (*this, index);

    return strip != nullptr && strip->isClipping();
}

void MixerWindow::tickMetersForTest()
{
    if (content != nullptr)
        content->updateMeters();
}

void MixerWindow::pressPlayStopForTest()
{
    if (content != nullptr)
        content->playButton.onClick();
}

void MixerWindow::pressReturnToStartForTest()
{
    if (content != nullptr)
        content->returnButton.onClick();
}

juce::String MixerWindow::getPositionTextForTest() const
{
    return content != nullptr ? content->positionLabel.getText() : juce::String();
}

} // namespace nidaw
