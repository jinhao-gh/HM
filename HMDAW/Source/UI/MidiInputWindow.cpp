#include "MidiInputWindow.h"
#include "Theme.h"

#include "../Audio/MidiInputManager.h"
#include "../Core/MusicMath.h"
#include "../Core/WindowPlacement.h"

#include <cmath>

namespace hmdaw
{

namespace
{
    constexpr int kMargin = 8;
    constexpr int kRowHeight = 24;
    constexpr int kDotRadius = 4;
    constexpr float kMagnetPixels = 14.0f;
    constexpr int kDeviceNoneId = 1;
    constexpr int kDeviceFirstId = 2;
    constexpr int kGroupNoneId = 1;
    constexpr int kGroupFirstEntryId = 2;

    constexpr int kMidiLiveIdBase = -1000;

    /** How far above / below the anchor the group's dots are offered: the octave
        below, its own, and the one above - so a key can be bound to the same scale
        degree at a lower or higher pitch. (The tile itself rides up and down with
        the keyboard, so this is about which octave's *frequency* gets stored.) */
    constexpr int kOctavesEitherSide = 1;

    /** How far left of the group column a drop still counts as "onto the column".
        The column lights up while a key is being dragged, so the target is never a
        secret; the slack is there because releasing one pixel short should not
        quietly do nothing. */
    constexpr int kDropSlackPixels = 40;
    /** A right-click this close to a cable means that cable. */
    constexpr float kCableHitPixels = 6.0f;
    /** The group column's own zoom: 1 = the whole group fits, and the wheel moves in
        this range (Ctrl+wheel steps by 1.15 per notch). */
    constexpr float kGroupMinZoom = 0.5f;
    constexpr float kGroupMaxZoom = 8.0f;
    constexpr float kGroupZoomStep = 1.15f;
    /** How far one wheel notch scrolls the group column. */
    constexpr float kGroupWheelPixels = 48.0f;
    /** "no candidate yet" for the nearest-something searches. */
    constexpr float kNoDistance = 1.0e9f;

    bool kindCanPlayMidi (TrackKind kind) noexcept
    {
        return kind != TrackKind::drums && kind != TrackKind::audio;
    }
}

//==============================================================================
MidiInputCanvas::MidiInputCanvas (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse)
{
    setWantsKeyboardFocus (true);
}

MidiInputState& MidiInputCanvas::state()
{
    // The mapping is configuration rather than an edit: it is written through a
    // derived edit (no undo step) and the document is marked dirty by hand.
    return const_cast<Project&> (document.getProject()).midiInput;
}

const midimap::Map& MidiInputCanvas::map() const
{
    return document.getProject().midiInput.map;
}

const ReferenceLineGroup& MidiInputCanvas::group() const
{
    return document.getProject().midiInput.group;
}

std::vector<double> MidiInputCanvas::dotFrequencies() const
{
    std::vector<double> frequencies;

    for (const auto& dot : dots())
        frequencies.push_back (dot.hz);

    return frequencies;
}

std::vector<MidiInputCanvas::Dot> MidiInputCanvas::dots() const
{
    const auto& chosen = group();

    if (chosen.anchorHz <= 0.0)
        return {};

    // One octave first: the group's own dots, low to high, deduplicated. Their
    // order inside that octave is the "slot" the tile has to be long enough for.
    std::vector<Dot> oneOctave;

    for (const auto& dot : chosen.dots)
    {
        const auto hz = music::groupFrequencyAt (chosen.anchorHz, chosen.anchorPosition,
                                                 dot.position);

        if (hz >= kMinVisibleHz && hz <= kMaxVisibleHz)
            oneOctave.push_back ({ hz, dot.colourIndex, 0 });
    }

    std::sort (oneOctave.begin(), oneOctave.end(),
               [] (const Dot& a, const Dot& b) { return a.hz < b.hz; });
    oneOctave.erase (std::unique (oneOctave.begin(), oneOctave.end(),
                                  [] (const Dot& a, const Dot& b) { return std::abs (a.hz - b.hz) < 1.0e-6; }),
                     oneOctave.end());

    for (int i = 0; i < (int) oneOctave.size(); ++i)
        oneOctave[(size_t) i].slot = i;

    // ...and then the same pattern an octave down and an octave up. Octave-major
    // order keeps the whole list ascending in frequency.
    std::vector<Dot> result;

    for (int octave = -kOctavesEitherSide; octave <= kOctavesEitherSide; ++octave)
        for (const auto& dot : oneOctave)
        {
            const auto hz = dot.hz * std::pow (2.0, (double) octave);

            if (hz >= kMinVisibleHz && hz <= kMaxVisibleHz)
                result.push_back ({ hz, dot.colourIndex, dot.slot });
        }

    return result;
}

int MidiInputCanvas::dotSlotCount() const
{
    auto maxSlot = -1;

    for (const auto& dot : dots())
        maxSlot = juce::jmax (maxSlot, dot.slot);

    return maxSlot + 1;
}

int MidiInputCanvas::tileLength() const
{
    return juce::jlimit (midimap::Map::kMinLoopKeys, midimap::Map::kMaxLoopKeys,
                         map().loopKeys);
}

bool MidiInputCanvas::isDotBindable (int index) const
{
    const auto all = dots();

    return index >= 0 && index < (int) all.size()
           && all[(size_t) index].slot < tileLength();
}

juce::String MidiInputCanvas::keyCaption (int midiNote) const
{
    // Every key is named - the row is one tile, so the note names are what makes
    // "which key am I binding" answerable at a glance.
    return midimap::noteName (midiNote);
}

double MidiInputCanvas::lowestHz() const
{
    const auto frequencies = dotFrequencies();

    if (frequencies.empty())
        return 220.0;

    return frequencies.front() * 0.8;
}

double MidiInputCanvas::highestHz() const
{
    const auto frequencies = dotFrequencies();

    if (frequencies.empty())
        return 880.0;

    return frequencies.back() * 1.25;
}

float MidiInputCanvas::yForHz (double hz) const
{
    const auto low = std::log2 (lowestHz());
    const auto high = std::log2 (juce::jmax (highestHz(), lowestHz() * 1.01));
    const auto t = (std::log2 (juce::jmax (hz, kMinVisibleHz)) - low) / (high - low);
    const auto fitted = (1.0 - juce::jlimit (0.0, 1.0, t)) * (double) getHeight();

    // ...and then the column's own view on top of that: 1× is the fitted picture
    // (what this window has always drawn), and zoom / scroll move the dots and their
    // labels without touching the keyboard strip's note range.
    const auto centre = (double) getHeight() * 0.5;
    return (float) (centre + (fitted - centre) * (double) groupZoom + (double) groupScroll);
}

void MidiInputCanvas::clampGroupView()
{
    groupZoom = juce::jlimit (kGroupMinZoom, kGroupMaxZoom, groupZoom);

    // Zoomed in, the content is `reach` pixels past each edge; zoomed out there is
    // nothing to scroll at all, so the clamp collapses onto 0 by itself.
    const auto reach = juce::jmax (0.0f, (float) getHeight() * 0.5f * (groupZoom - 1.0f));
    groupScroll = juce::jlimit (-reach, reach, groupScroll);
}

void MidiInputCanvas::setGroupView (float zoom, float scroll)
{
    groupZoom = zoom;
    groupScroll = scroll;
    clampGroupView();
    repaint();
}

void MidiInputCanvas::scrollGroupBy (float pixels)
{
    groupScroll += pixels;
    clampGroupView();
    repaint();
}

void MidiInputCanvas::zoomGroupAt (float anchorY, float factor)
{
    const auto centre = (float) getHeight() * 0.5f;

    // Which un-zoomed height the pointer is really over...
    const auto fitted = centre + (anchorY - centre - groupScroll) / juce::jmax (0.01f, groupZoom);

    groupZoom = juce::jlimit (kGroupMinZoom, kGroupMaxZoom, groupZoom * factor);

    // ...so that same height can be put back under the pointer afterwards.
    groupScroll = anchorY - centre - (fitted - centre) * groupZoom;
    clampGroupView();
    repaint();
}

void MidiInputCanvas::scrollGroupForTest (float deltaY)
{
    scrollGroupBy (deltaY * kGroupWheelPixels);
}

void MidiInputCanvas::zoomGroupForTest (float factor, float anchorY)
{
    zoomGroupAt (anchorY, factor);
}

juce::Colour MidiInputCanvas::colourForDot (const Dot& dot) const
{
    // The reference canvas paints a group's lines with "the colour set on the
    // status entry if there is one, otherwise that dot's own colour"
    // (music::referenceLines fills line.colourIndex, PianoGrid draws it, and the
    // 参考线状态 颜色 button was removed in the 39th batch so the dot colour is
    // usually the one that shows). Doing the same here is what makes the dots in
    // this window look like the lines on the canvas.
    const auto& state = document.getProject().midiInput;
    auto entryColour = state.groupColourIndex;

    // While the source entry still exists, follow it live: a project saved before
    // this field existed would otherwise show dot colours where the canvas shows
    // the entry's. (The stored copy is what covers a deleted entry.)
    if (state.groupSource.kind == MidiGroupSource::Kind::entry)
        for (const auto& entry : document.getProject().referenceLines)
            if (entry.id == state.groupSource.entryId)
                entryColour = entry.colourIndex;

    return theme::trackColour (entryColour >= 0 ? entryColour : dot.colourIndex);
}

juce::Colour MidiInputCanvas::colourForHz (double hz) const
{
    const auto all = dots();

    if (all.empty())
        return theme::referenceLine;

    auto best = all.front();

    for (const auto& dot : all)
        if (std::abs (dot.hz - hz) < std::abs (best.hz - hz))
            best = dot;

    return colourForDot (best);
}

std::vector<int> MidiInputCanvas::getVisibleNotesForTest() const
{
    return map().visibleNotes();
}

juce::Rectangle<int> MidiInputCanvas::getKeyBoundsForTest (int midiNote) const
{
    return keyRect (midiNote);
}

int MidiInputCanvas::getDotCountForTest() const
{
    return (int) dotFrequencies().size();
}

juce::Rectangle<int> MidiInputCanvas::getDotBoundsForTest (int index) const
{
    const auto frequencies = dotFrequencies();

    if (index < 0 || index >= (int) frequencies.size())
        return {};

    const auto lineX = getWidth() - kGroupStripWidth + 24;
    const auto y = (int) yForHz (frequencies[(size_t) index]);

    return { lineX - (int) kDotRadius, y - (int) kDotRadius,
             (int) kDotRadius * 2, (int) kDotRadius * 2 };
}

double MidiInputCanvas::getDotFrequencyForTest (int index) const
{
    const auto frequencies = dotFrequencies();

    return index >= 0 && index < (int) frequencies.size() ? frequencies[(size_t) index] : 0.0;
}

juce::Colour MidiInputCanvas::getDotColourForTest (int index) const
{
    const auto all = dots();

    if (index < 0 || index >= (int) all.size())
        return theme::referenceLine;

    // Exactly what the paint pass draws: a dot the tile is too short for is grey
    // ("装不下就绑不了"), the rest keep the colour the canvas uses for that line.
    return isDotBindable (index) ? colourForDot (all[(size_t) index]) : theme::outline;
}

int MidiInputCanvas::getDropDotForTest (juce::Point<int> point) const
{
    return dropDotAt (point);
}

juce::String MidiInputCanvas::getKeyCaptionForTest (int midiNote) const
{
    return keyCaption (midiNote);
}

bool MidiInputCanvas::isStepEmptyForTest (int midiNote) const
{
    return ! map().isBound (midiNote);
}

int MidiInputCanvas::getBindableDotCountForTest() const
{
    auto count = 0;

    for (int i = 0; i < (int) dots().size(); ++i)
        if (isDotBindable (i))
            ++count;

    return count;
}

bool MidiInputCanvas::isDotBindableForTest (int index) const
{
    return isDotBindable (index);
}

std::vector<int> MidiInputCanvas::getKeysBoundToDotForTest (int dotIndex) const
{
    std::vector<int> notes;
    const auto all = dots();

    if (dotIndex < 0 || dotIndex >= (int) all.size())
        return notes;

    const auto hz = all[(size_t) dotIndex].hz;

    for (const auto note : getVisibleNotesForTest())
        for (const auto bound : map().frequenciesFor (note))
            if (std::abs (bound - hz) < 1.0e-6)
            {
                notes.push_back (note);
                break;
            }

    return notes;
}

bool MidiInputCanvas::isKeyBoundForTest (int midiNote) const
{
    return map().isBound (midiNote);
}

int MidiInputCanvas::getCableCountForTest (int midiNote) const
{
    return map().bindingCount (midiNote);
}

juce::String MidiInputCanvas::getKeyFrequencyTextForTest (int midiNote) const
{
    return keyFrequencyText (midiNote);
}

int MidiInputCanvas::getCableNoteAtForTest (juce::Point<int> point) const
{
    return cableHitAt (point).midiNote;
}

void MidiInputCanvas::bindKeyForTest (int midiNote, int dotIndex)
{
    const auto frequencies = dotFrequencies();

    if (dotIndex < 0 || dotIndex >= (int) frequencies.size())
        return;

    const auto hz = frequencies[(size_t) dotIndex];
    commit ([midiNote, hz] (midimap::Map& m) { m.addFrequency (midiNote, hz); });
}

void MidiInputCanvas::unbindCableForTest (int midiNote, int dotIndex)
{
    const auto frequencies = dotFrequencies();

    if (dotIndex < 0 || dotIndex >= (int) frequencies.size())
        return;

    const auto hz = frequencies[(size_t) dotIndex];
    commit ([midiNote, hz] (midimap::Map& m) { m.removeFrequency (midiNote, hz); });
}

void MidiInputCanvas::unbindKeyForTest (int midiNote)
{
    commit ([midiNote] (midimap::Map& m) { m.clearFrequency (midiNote); });
}

juce::String MidiInputCanvas::keyFrequencyText (int midiNote) const
{
    juce::String text;

    for (const auto hz : map().frequenciesFor (midiNote))
        text += text.isEmpty() ? formatHz (hz) : NI_TXT (" · ") + formatHz (hz);

    return text;
}

juce::Rectangle<int> MidiInputCanvas::keyRect (int midiNote) const
{
    const auto notes = getVisibleNotesForTest();

    auto index = -1;

    for (int i = 0; i < (int) notes.size(); ++i)
        if (notes[(size_t) i] == midiNote)
        {
            index = i;
            break;
        }

    if (index < 0)
        return {};

    auto top = 0;

    if (map().mode == midimap::Map::Mode::looping)
    {
        // One tile, drawn **low at the bottom, high at the top** - the way the pitch
        // axis on the right (and the piano window's own keyboard) runs, so a key and
        // the dot it belongs to are at the same height and a drag straight across
        // stays on the same degree. (It used to be the other way round, which made
        // the strip read upside down against the dots.)
        const auto total = (int) notes.size() * kKeyHeight;
        const auto first = juce::jmax (0, (getHeight() - total) / 2);
        top = first + ((int) notes.size() - 1 - index) * kKeyHeight;
    }
    else
    {
        const auto visible = juce::jmax (1, getHeight() / kKeyHeight);
        const auto lastVisible = firstVisibleNote + visible - 1;
        top = (lastVisible - midiNote) * kKeyHeight;
    }

    const auto black = music::piano::isBlackKey (midiNote);
    const auto width = black ? (int) (kKeyStripWidth * 0.62f) : kKeyStripWidth;
    return { 0, top, width, juce::jmax (1, kKeyHeight - 1) };
}

int MidiInputCanvas::noteAt (juce::Point<int> point) const
{
    if (point.x < 0 || point.x > kKeyStripWidth)
        return -1;

    for (const auto note : getVisibleNotesForTest())
        if (keyRect (note).contains (point))
            return note;

    return -1;
}

int MidiInputCanvas::dotAt (juce::Point<int> point) const
{
    const auto frequencies = dotFrequencies();

    if (frequencies.empty() || point.x < getWidth() - kGroupStripWidth)
        return -1;

    auto best = -1;
    auto bestDistance = kMagnetPixels;

    for (int i = 0; i < (int) frequencies.size(); ++i)
    {
        if (! isDotBindable (i))
            continue;

        const auto distance = std::abs (yForHz (frequencies[(size_t) i]) - (float) point.y);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

int MidiInputCanvas::dropDotAt (juce::Point<int> point) const
{
    const auto frequencies = dotFrequencies();

    if (frequencies.empty() || point.x < getWidth() - kGroupStripWidth - kDropSlackPixels)
        return -1;

    // Inside the column every y means the dot nearest to it: the drop only has to
    // be roughly right, and the drag preview shows which dot that is before the
    // mouse is let go. (The 14 px magnet still makes the dots themselves the most
    // precise place to aim at.)
    auto best = 0;
    auto bestDistance = kNoDistance;

    for (int i = 0; i < (int) frequencies.size(); ++i)
    {
        // A dot the tile cannot hold is not a target: dragging onto it must not
        // silently bind some step to a frequency it was never meant to have.
        if (! isDotBindable (i))
            continue;

        const auto distance = std::abs (yForHz (frequencies[(size_t) i]) - (float) point.y);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return bestDistance < kNoDistance ? best : -1;
}

int MidiInputCanvas::dropKeyAt (juce::Point<int> point) const
{
    if (point.x < 0 || point.x > kKeyStripWidth)
        return -1;

    // Same idea in the other direction: anywhere along the key strip means the row
    // the pointer is closest to.
    auto best = -1;
    auto bestDistance = kNoDistance;

    for (const auto note : getVisibleNotesForTest())
    {
        const auto rect = keyRect (note);

        if (rect.getBottom() < 0 || rect.getY() > getHeight())
            continue;

        const auto distance = std::abs ((float) rect.getCentreY() - (float) point.y);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = note;
        }
    }

    return best;
}

MidiInputCanvas::CableHit MidiInputCanvas::cableHitAt (juce::Point<int> point) const
{
    CableHit hit;

    const auto lineX = (float) (getWidth() - kGroupStripWidth + 24);
    const auto x1 = (float) kKeyStripWidth;
    const auto x2 = lineX - 6.0f;
    const auto px = (float) point.x;
    const auto py = (float) point.y;

    if (px <= x1 || px >= x2 || point.x >= getWidth() - kGroupStripWidth)
        return hit;

    auto best = kCableHitPixels;
    const auto dx = x2 - x1;

    for (const auto note : getVisibleNotesForTest())
    {
        const auto frequencies = map().frequenciesFor (note);
        const auto rect = keyRect (note);

        if (frequencies.empty() || rect.getBottom() < 0 || rect.getY() > getHeight())
            continue;

        const auto y1 = (float) rect.getCentreY();

        for (int i = 0; i < (int) frequencies.size(); ++i)
        {
            const auto y2 = yForHz (frequencies[(size_t) i]);
            const auto dy = y2 - y1;

            // Distance to the *segment* (not the endless line through it): a cable
            // that leaves the column far above the pointer must not steal the click.
            const auto t = juce::jlimit (0.0f, 1.0f,
                                         ((px - x1) * dx + (py - y1) * dy)
                                             / juce::jmax (1.0e-6f, dx * dx + dy * dy));
            const auto cx = x1 + t * dx;
            const auto cy = y1 + t * dy;
            const auto distance = std::sqrt ((px - cx) * (px - cx) + (py - cy) * (py - cy));

            if (distance < best)
            {
                best = distance;
                hit = { note, i };
            }
        }
    }

    return hit;
}

void MidiInputCanvas::commit (const std::function<void (midimap::Map&)>& mutation)
{
    document.applyDerivedEdit ([&mutation] (Project& project) { mutation (project.midiInput.map); });
    // A real change (the project is now unsaved) that is not an undoable edit.
    document.notifyEdit();

    if (onMapChanged != nullptr)
        onMapChanged();

    repaint();
}

void MidiInputCanvas::setLearning (bool shouldLearn)
{
    learning = shouldLearn;
    learningKey = -1;
    repaint();
}

void MidiInputCanvas::showPlayedNote (int midiNote)
{
    if (map().mode != midimap::Map::Mode::free || midiNote < 0)
        return;

    // The strip runs low-to-high upwards, so "off screen" is above the top or below
    // the bottom; either way, bring it into the middle of the view.
    const auto rect = keyRect (midiNote);

    if (rect.getY() >= 0 && rect.getBottom() <= getHeight())
        return;

    const auto visible = juce::jmax (1, getHeight() / kKeyHeight);
    firstVisibleNote = juce::jlimit (0, juce::jmax (0, 128 - visible), midiNote - visible / 2);
    repaint();
}

void MidiInputCanvas::refresh()
{
    const auto visible = juce::jmax (1, getHeight() / kKeyHeight);
    firstVisibleNote = juce::jlimit (0, juce::jmax (0, 128 - visible), firstVisibleNote);
    repaint();
}

void MidiInputCanvas::resized()
{
    // The column's scroll range is a function of the canvas height, so a resize can
    // leave the view somewhere it could no longer be reached.
    clampGroupView();
    refresh();
}

void MidiInputCanvas::paint (juce::Graphics& g)
{
    const auto width = getWidth();
    const auto height = getHeight();
    const auto groupX = width - kGroupStripWidth;
    const auto lineX = groupX + 24;

    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    // ---- the keys ---------------------------------------------------------
    g.setColour (theme::gridBackground);
    g.fillRect (0, 0, kKeyStripWidth, height);

    g.setFont (theme::font (9.0f));

    for (const auto note : getVisibleNotesForTest())
    {
        const auto rect = keyRect (note);

        if (rect.getBottom() < 0 || rect.getY() > height)
            continue;

        const auto black = music::piano::isBlackKey (note);
        const auto bound = map().isBound (note);
        const auto picked = learning && note == learningKey;

        g.setColour (black ? juce::Colour (0xff15151b) : juce::Colour (0xffe6e6ee));
        g.fillRect (rect);

        if (bound)
        {
            // The key's colour is the colour of the dot it is bound to, so a key
            // bound to a 1:3 dot is the same colour as that dot on the canvas.
            g.setColour (colourForHz (map().frequencyFor (note)).withAlpha (0.85f));
            g.fillRect (rect.withWidth (4));
        }

        if (picked)
        {
            g.setColour (theme::accent);
            g.drawRect (rect, 2);
        }

        // Every key says what it is, and an unbound step says "空" - the two things
        // that make "按白键出音阶、按黑键不发声" visible instead of a guess.
        g.setColour (black ? juce::Colour (0xffb9b9c6) : juce::Colour (0xff3a3a45));
        g.drawText (keyCaption (note),
                    rect.withTrimmedLeft (5).withTrimmedRight (bound ? 4 : 22),
                    juce::Justification::centredLeft, false);

        if (! bound)
        {
            g.setColour (theme::dimText.withAlpha (0.75f));
            g.drawText (NI_TXT ("空"),
                        rect.withTrimmedRight (5),
                        juce::Justification::centredRight, false);
        }
    }

    g.setColour (theme::outline);
    g.drawVerticalLine (kKeyStripWidth, 0.0f, (float) height);

    // ---- the reference group's own line and dots ---------------------------
    g.setColour (theme::gridBackground);
    g.fillRect (groupX, 0, kGroupStripWidth, height);

    // The stand-in for the group's own line. On the canvas a group is a set of
    // horizontal lines (one per dot, each in its dot's colour); here it is a single
    // vertical axis, so it stays neutral and lets the dots carry the colours.
    g.setColour (theme::outline);
    g.fillRect (lineX - 1, 0, 2, height);

    const auto allDots = dots();

    g.setFont (theme::font (10.0f));

    for (int i = 0; i < (int) allDots.size(); ++i)
    {
        const auto& dot = allDots[(size_t) i];
        const auto y = yForHz (dot.hz);
        const auto hovered = i == hoverDot;
        const auto fits = isDotBindable (i);
        // A dot this tile is too short for is drawn grey and cannot be dropped on:
        // "装不下就绑不了", said out loud in the hint below.
        const auto colour = fits ? colourForDot (dot) : theme::outline;

        g.setColour (colour);
        g.fillEllipse ((float) lineX - (hovered ? 6.0f : kDotRadius),
                       y - (hovered ? 6.0f : kDotRadius),
                       (hovered ? 12.0f : kDotRadius * 2.0f),
                       (hovered ? 12.0f : kDotRadius * 2.0f));

        // The magnet ring: the dot the pointer is about to land on gets a ring, so
        // the drop is never a guess.
        if (hovered && fits)
        {
            g.setColour (colour.brighter (0.4f));
            g.drawEllipse ((float) lineX - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);
        }

        g.setColour (hovered && fits ? theme::accent
                                     : (fits ? theme::dimText : theme::outline));
        g.drawText (formatHz (dot.hz),
                    juce::Rectangle<int> (lineX + 12, (int) y - 7, kGroupStripWidth - 20, 14),
                    juce::Justification::centredLeft, false);
    }

    // Nothing to bind to yet: say what to do instead of leaving a bare line.
    if (allDots.empty())
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f));
        g.drawFittedText (NI_TXT ("上面选一组参考线组，\n这里的圆点就是能绑的频率"),
                          juce::Rectangle<int> (groupX + 6, kMargin, kGroupStripWidth - 12, 40),
                          juce::Justification::centredLeft, 2);
    }
    else
    {
        if (dotSlotCount() > tileLength())
        {
            g.setColour (theme::accentDim.brighter (0.5f));
            g.setFont (theme::font (10.0f));
            g.drawFittedText (NI_TXT ("这一格只有 ")
                                  + juce::String (tileLength()) + NI_TXT (" 个半音，这一组的 ")
                                  + juce::String (dotSlotCount()) + NI_TXT (" 个圆点装不下")
                                  + NI_TXT ("（把「循环键数」调大就能绑）"),
                              juce::Rectangle<int> (groupX + 6, kMargin, kGroupStripWidth - 12, 34),
                              juce::Justification::topLeft, 2);
        }

        g.setColour (theme::dimText.withAlpha (0.75f));
        g.setFont (theme::font (10.0f));
        g.drawFittedText (NI_TXT ("把琴键拖到圆点上即可绑定（一个键可以连好几个圆点）；\n"
                                  "右键琴键断开这个键的全部连线、右键一条连线只断开那一条；\n"
                                  "滚轮上下滑动、Ctrl+滚轮缩放，双击回到全览"),
                          juce::Rectangle<int> (groupX + 6, height - 56, kGroupStripWidth - 12, 50),
                          juce::Justification::bottomLeft, 2);
    }

    // Zoomed or scrolled: say so, because the picture alone cannot tell "the group
    // is small" from "I am zoomed in".
    if (std::abs (groupZoom - 1.0f) > 0.001f)
    {
        g.setColour (theme::accentDim.brighter (0.5f));
        g.setFont (theme::font (10.0f));
        g.drawFittedText (NI_TXT ("×") + juce::String (groupZoom, 2)
                              + NI_TXT ("　双击回到全览"),
                          juce::Rectangle<int> (groupX + 6, kMargin, kGroupStripWidth - 12, 16),
                          juce::Justification::topRight, 1);
    }

    // ---- the patch cables --------------------------------------------------
    g.setFont (theme::font (9.0f));

    for (const auto note : getVisibleNotesForTest())
    {
        const auto frequencies = map().frequenciesFor (note);

        if (frequencies.empty())
            continue;

        const auto rect = keyRect (note);

        if (rect.getBottom() < 0 || rect.getY() > height)
            continue;

        const auto y1 = (float) rect.getCentreY();

        // One cable per dot the key is tied to: a key can hold a whole stack, and
        // every one of them sounds at once.
        for (const auto hz : frequencies)
        {
            const auto y2 = yForHz (hz);
            const auto colour = colourForHz (hz);

            juce::Path cable;
            cable.startNewSubPath ((float) kKeyStripWidth, y1);
            cable.lineTo ((float) lineX - 6.0f, y2);

            g.setColour (colour.withAlpha (0.7f));
            g.strokePath (cable, juce::PathStrokeType (1.4f));

            g.setColour (colour.withAlpha (0.45f));
            g.fillEllipse ((float) lineX - 9.0f, y2 - 3.0f, 6.0f, 6.0f);
        }

        g.setColour (theme::dimText.withAlpha (0.9f));
        g.drawFittedText (keyFrequencyText (note),
                          juce::Rectangle<int> (kKeyStripWidth + 4, rect.getY() - 1,
                                                groupX - kKeyStripWidth - 12, rect.getHeight()),
                          juce::Justification::centredLeft, 1);
    }

    // ---- the cable being dragged -------------------------------------------
    // A dashed line from where the drag started to where it would land, so "which
    // dot is this going to be" is answered before the mouse button comes up.
    if (dragNote >= 0 && hoverDot >= 0 && hoverDot < (int) allDots.size())
    {
        const auto rect = keyRect (dragNote);

        if (! rect.isEmpty())
        {
            const auto& dot = allDots[(size_t) hoverDot];
            juce::Path preview;
            preview.startNewSubPath ((float) kKeyStripWidth, (float) rect.getCentreY());
            preview.lineTo ((float) lineX - 6.0f, yForHz (dot.hz));

            g.setColour (colourForDot (dot).withAlpha (0.9f));
            g.strokePath (preview, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }
    }
    else if (dragDot >= 0 && hoverNote >= 0 && dragDot < (int) allDots.size())
    {
        const auto rect = keyRect (hoverNote);

        if (! rect.isEmpty())
        {
            const auto& dot = allDots[(size_t) dragDot];
            juce::Path preview;
            preview.startNewSubPath ((float) kKeyStripWidth, (float) rect.getCentreY());
            preview.lineTo ((float) lineX - 6.0f, yForHz (dot.hz));

            g.setColour (colourForDot (dot).withAlpha (0.9f));
            g.strokePath (preview, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }
    }

    // ---- the hint while the anchor is being taught -------------------------
    if (learning)
    {
        const auto hint = learningKey < 0 ? NI_TXT ("点一个左侧琴键…")
                                          : NI_TXT ("…再按 MIDI 键盘上一个键");

        g.setColour (theme::accent);
        g.setFont (theme::font (12.0f));
        g.drawText (hint, juce::Rectangle<int> (kKeyStripWidth + 6, kMargin, groupX - kKeyStripWidth - 12, 18),
                    juce::Justification::centred, false);
    }
}

void MidiInputCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto note = noteAt (e.getPosition());

    if (note < 0)
    {
        if (e.mods.isPopupMenu())
        {
            // A right click on a cable lets go of just that one line...
            const auto hit = cableHitAt (e.getPosition());

            if (hit.midiNote >= 0)
            {
                const auto frequencies = map().frequenciesFor (hit.midiNote);

                if (hit.frequencyIndex >= 0 && hit.frequencyIndex < (int) frequencies.size())
                {
                    const auto hz = frequencies[(size_t) hit.frequencyIndex];
                    commit ([midiNote = hit.midiNote, hz] (midimap::Map& m)
                            { m.removeFrequency (midiNote, hz); });
                    return;
                }
            }

            // ...one on a dot lets go of every cable that ends there...
            const auto rightDot = dotAt (e.getPosition());

            if (rightDot >= 0)
            {
                const auto hz = dotFrequencies()[(size_t) rightDot];

                commit ([hz] (midimap::Map& m)
                {
                    for (const auto candidate : m.visibleNotes())
                        m.removeFrequency (candidate, hz);
                });
                return;
            }

            // ...and one on a key row lets go of that key - anywhere along the strip,
            // not only over the part a black key's own body covers.
            const auto pressed = dropKeyAt (e.getPosition());

            if (pressed >= 0)
                unbindKeyForTest (pressed);

            return;
        }

        // Not on a key: the other half of the gesture. Pressing a dot and dragging
        // onto a key binds that key - the same thing as the key -> dot drag, just
        // started from the other end (which is what a lot of people try first).
        const auto dot = dotAt (e.getPosition());

        if (dot < 0)
            return;

        dragDot = dot;
        hoverNote = -1;
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        unbindKeyForTest (note);
        return;
    }

    dragNote = note;
    hoverDot = -1;

    if (learning)
    {
        learningKey = note;

        if (onKeyPicked != nullptr)
            onKeyPicked (note);
    }

    // A key can hold a stack: clicking it auditions every cable it has.
    if (onAudition != nullptr)
        for (const auto hz : map().frequenciesFor (note))
            onAudition (hz);

    repaint();
}

void MidiInputCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDot >= 0)
    {
        hoverNote = dropKeyAt (e.getPosition());
        repaint();
        return;
    }

    if (dragNote < 0)
        return;

    // The exact magnet first (that is what the ring in the paint pass shows), and
    // the forgiving "nearest dot in the column" otherwise - so a drag that ends a
    // little above or below a dot still lands on the one it was aiming at.
    hoverDot = dropDotAt (e.getPosition());
    repaint();
}

void MidiInputCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (dragDot >= 0)
    {
        // Dot -> key: bind whichever key was let go over.
        const auto frequencies = dotFrequencies();
        const auto dot = dragDot;
        const auto note = hoverNote >= 0 ? hoverNote : dropKeyAt (e.getPosition());

        dragDot = -1;
        hoverNote = -1;
        hoverDot = -1;

        if (note >= 0 && dot >= 0 && dot < (int) frequencies.size())
        {
            const auto hz = frequencies[(size_t) dot];
            // Connecting adds a cable; right-click is what takes one away.
            commit ([note, hz] (midimap::Map& m) { m.addFrequency (note, hz); });
            return;
        }

        repaint();
        return;
    }

    if (dragNote < 0)
        return;

    const auto dot = dropDotAt (e.getPosition());
    const auto note = dragNote;

    dragNote = -1;
    hoverDot = -1;
    hoverNote = -1;

    if (dot >= 0 && dot < (int) dotFrequencies().size())
    {
        const auto hz = dotFrequencies()[(size_t) dot];
        commit ([note, hz] (midimap::Map& m) { m.addFrequency (note, hz); });
        return;
    }

    repaint();
}

void MidiInputCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double click on the group's column = "back to the whole group": the one
    // gesture that has to exist once there is a zoom to get lost in.
    if (e.getPosition().x >= getWidth() - kGroupStripWidth)
    {
        setGroupView (1.0f, 0.0f);
        return;
    }

    juce::Component::mouseDoubleClick (e);
}

void MidiInputCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY == 0.0f)
        return;

    // Over the group's own column the wheel belongs to that column: scroll, and zoom
    // when Ctrl is held down.
    if (e.getPosition().x >= getWidth() - kGroupStripWidth)
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            zoomGroupAt ((float) e.getPosition().y,
                         wheel.deltaY > 0.0f ? kGroupZoomStep : 1.0f / kGroupZoomStep);
        else
            scrollGroupBy (wheel.deltaY * kGroupWheelPixels);

        return;
    }

    if (map().mode != midimap::Map::Mode::free)
        return;

    // The strip runs low-at-the-bottom, so scrolling up moves towards the higher
    // notes - the same way the pitch axis and every other keyboard here behave.
    const auto step = wheel.deltaY > 0.0f ? 3 : -3;

    const auto visible = juce::jmax (1, getHeight() / kKeyHeight);
    firstVisibleNote = juce::jlimit (0, juce::jmax (0, 128 - visible), firstVisibleNote + step);
    repaint();
}

//==============================================================================
MidiInputPanel::MidiInputPanel (ProjectDocument& documentToUse, AppState& appStateToUse,
                                PresetStore& presetsToUse, AudioEngine& audioToUse,
                                MidiInputManager& midiToUse)
    : document (documentToUse), appState (appStateToUse), presets (presetsToUse),
      audio (audioToUse), midi (midiToUse), canvas (documentToUse, appStateToUse),
      arpControls (documentToUse)
{
    auto setupLabel = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);
    };

    setupLabel (deviceLabel, NI_TXT ("MIDI设备"));
    setupLabel (trackLabel, NI_TXT ("轨道"));
    setupLabel (keysLabel, NI_TXT ("循环键数"));
    setupLabel (groupLabel, NI_TXT ("参考线组"));

    addAndMakeVisible (deviceBox);
    deviceBox.onChange = [this]
    {
        if (updating)
            return;

        const auto id = deviceBox.getSelectedId();
        const auto& list = midi.getDeviceList();
        const auto index = id - kDeviceFirstId;

        // Closing (「不使用」) is just as valid a choice as picking a device.
        midi.openDevice (id >= kDeviceFirstId && index >= 0 && index < (int) list.size()
                             ? list[(size_t) index].identifier
                             : juce::String());
        refresh();
    };

    addAndMakeVisible (trackLabel);
    trackLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (loopModeButton);
    loopModeButton.setButtonText (NI_TXT ("循环"));
    loopModeButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    loopModeButton.setTooltip (NI_TXT ("一格 N 个半音，在 MIDI 键盘上上下循环平铺"));
    loopModeButton.onClick = [this] { applyMode (true); };

    addAndMakeVisible (freeModeButton);
    freeModeButton.setButtonText (NI_TXT ("非循环"));
    freeModeButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    freeModeButton.setTooltip (NI_TXT ("每个琴键单独指定频率（MIDI 0–127）"));
    freeModeButton.onClick = [this] { applyMode (false); };

    addAndMakeVisible (loopKeysBox);

    for (int keys = midimap::Map::kMinLoopKeys; keys <= midimap::Map::kMaxLoopKeys; ++keys)
        loopKeysBox.addItem (juce::String (keys) + NI_TXT (" 个半音"), keys);

    loopKeysBox.onChange = [this]
    {
        if (updating)
            return;

        const auto keys = loopKeysBox.getSelectedId();
        document.applyDerivedEdit ([keys] (Project& project)
        {
            midimap::setLoopKeys (project.midiInput.map, keys);
        });
        document.notifyEdit();
        refresh();
    };

    addAndMakeVisible (groupBox);
    groupBox.onChange = [this]
    {
        if (updating)
            return;

        const auto id = groupBox.getSelectedId();
        auto& project = const_cast<Project&> (document.getProject());

        if (id == kGroupNoneId)
        {
            project.midiInput.groupSource = {};
            project.midiInput.group = {};
            project.midiInput.groupColourIndex = -1;
        }
        else
        {
            // The ids are laid out in rebuildGroupBox: the project's own entries
            // first, then the library presets.
            const auto& lines = document.getProject().referenceLines;
            auto index = id - kGroupFirstEntryId;

            std::vector<const ReferenceLineEntry*> groups;

            for (const auto& entry : lines)
                if (entry.kind == ReferenceLineKind::group)
                    groups.push_back (&entry);

            if (index >= 0 && index < (int) groups.size())
            {
                const auto* entry = groups[(size_t) index];
                project.midiInput.groupSource.kind = MidiGroupSource::Kind::entry;
                project.midiInput.groupSource.entryId = entry->id;
                project.midiInput.group.name = entry->name;
                project.midiInput.group.anchorHz = entry->anchorHz;
                project.midiInput.group.anchorPosition = entry->anchorPosition;
                project.midiInput.group.dots = entry->dots;
                // The canvas paints a group's lines with the entry's colour when it
                // has one, so the dots here have to follow the same rule.
                project.midiInput.groupColourIndex = entry->colourIndex;
            }
            else
            {
                index -= (int) groups.size();
                const auto& list = presets.getReferenceLineGroups();

                if (index >= 0 && index < (int) list.size())
                {
                    project.midiInput.groupSource.kind = MidiGroupSource::Kind::preset;
                    project.midiInput.groupSource.presetIndex = index;
                    project.midiInput.group = list[(size_t) index];
                    project.midiInput.groupColourIndex = -1;
                }
            }
        }

        document.notifyEdit();
        refresh();
    };

    addAndMakeVisible (applyButton);
    applyButton.setButtonText (NI_TXT ("应用"));
    applyButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    applyButton.setTooltip (NI_TXT ("循环模式：点一个琴键、再按 MIDI 键盘一个键，就能定下这一格从哪里开始"));
    applyButton.onClick = [this] { triggerApplyForTest(); };

    addAndMakeVisible (saveButton);
    saveButton.setButtonText (NI_TXT ("保存"));
    saveButton.setTooltip (NI_TXT ("覆盖这张映射原来存的那条预设"));
    saveButton.onClick = [this] { savePresetForTest(); };

    addAndMakeVisible (saveAsButton);
    saveAsButton.setButtonText (NI_TXT ("另存为…"));
    saveAsButton.onClick = [this]
    {
        if (! showDialogs)
            return;

        auto* window = new juce::AlertWindow (NI_TXT ("另存为"),
                                              NI_TXT ("给这张映射起个名字："),
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", document.getProject().midiInput.presetName);
        window->addButton (NI_TXT ("确定"), 1);
        window->addButton (NI_TXT ("取消"), 0);

        juce::Component::SafePointer<MidiInputPanel> safeThis (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safeThis, window] (int result)
            {
                const auto name = window->getTextEditorContents ("name").trim();
                std::unique_ptr<juce::AlertWindow> owned (window);

                if (result == 1 && safeThis != nullptr && name.isNotEmpty())
                    safeThis->saveAsForTest (name);
            }), false);
    };

    addAndMakeVisible (presetsButton);
    presetsButton.setButtonText (NI_TXT ("预设…"));
    presetsButton.onClick = [this]
    {
        juce::PopupMenu menu;

        const auto names = presetNames();

        for (int i = 0; i < (int) names.size(); ++i)
            menu.addItem (i + 1, names[(size_t) i], true,
                          names[(size_t) i] == document.getProject().midiInput.presetName);

        if (names.empty())
            menu.addItem (1, NI_TXT ("还没有存过映射"), false, false);

        juce::Component::SafePointer<MidiInputPanel> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton),
                            [safeThis] (int result)
                            {
                                if (result > 0 && safeThis != nullptr)
                                    safeThis->loadPresetForTest (result - 1);
                            });
    };

    addAndMakeVisible (renameButton);
    renameButton.setButtonText (NI_TXT ("改名…"));
    renameButton.setTooltip (NI_TXT ("给这张映射存的那条预设改个名字（工程里记的名字也跟着改）"));
    renameButton.onClick = [this] { showRenameDialog(); };

    addAndMakeVisible (deleteButton);
    deleteButton.setButtonText (NI_TXT ("删除"));
    deleteButton.setTooltip (NI_TXT ("删掉这张映射存的那条预设"));
    deleteButton.onClick = [this]
    {
        const auto name = document.getProject().midiInput.presetName;

        for (int i = 0; i < (int) presets.getMidiMaps().size(); ++i)
            if (presets.getMidiMaps()[(size_t) i].presetName == name)
            {
                presets.removeMidiMap (i);
                break;
            }

        document.applyDerivedEdit ([] (Project& project) { project.midiInput.presetName.clear(); });
        document.notifyEdit();
        refresh();
    };

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (theme::font (10.5f));
    statusLabel.setColour (juce::Label::textColourId, theme::dimText);
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (canvas);
    canvas.onAudition = [this] (double hz) { audition (hz); };
    canvas.onMapChanged = [this] { refresh(); };
    canvas.onKeyPicked = [this] (int)
    {
        learningNeedsKey = false;
        updateStatus();
        repaint();
    };

    // 琶音器: the same controls the virtual keyboard window has, working on the same
    // project state, so the two windows always agree.
    addAndMakeVisible (arpControls);
    arpControls.onChanged = [this]
    {
        updateStatus();

        if (onArpSettingsChanged != nullptr)
            onArpSettingsChanged();
    };

    document.addChangeListener (this);
    appState.addChangeListener (this);

    rebuildDeviceBox();
    refresh();
}

MidiInputPanel::~MidiInputPanel()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

void MidiInputPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

std::vector<juce::String> MidiInputPanel::presetNames() const
{
    std::vector<juce::String> names;

    for (const auto& map : presets.getMidiMaps())
        names.push_back (map.presetName);

    return names;
}

void MidiInputPanel::rebuildDeviceBox()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& list = midi.getDeviceList();

    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItem (NI_TXT ("（不使用）"), kDeviceNoneId);

    for (int i = 0; i < (int) list.size(); ++i)
        deviceBox.addItem (list[(size_t) i].name, kDeviceFirstId + i);

    const auto open = midi.getOpenIdentifier();

    if (open.isEmpty())
    {
        deviceBox.setSelectedId (kDeviceNoneId, juce::dontSendNotification);
        return;
    }

    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].identifier == open)
        {
            deviceBox.setSelectedId (kDeviceFirstId + i, juce::dontSendNotification);
            return;
        }

    // The remembered device is not here (unplugged): say so rather than pretend.
    deviceBox.setText (midi.getOpenName() + NI_TXT ("（未连接）"), juce::dontSendNotification);
}

void MidiInputPanel::rebuildGroupBox()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    groupBox.clear (juce::dontSendNotification);
    groupBox.addItem (NI_TXT ("（不显示）"), kGroupNoneId);

    const auto& lines = document.getProject().referenceLines;
    auto id = kGroupFirstEntryId;
    auto selected = kGroupNoneId;

    for (const auto& entry : lines)
    {
        if (entry.kind != ReferenceLineKind::group)
            continue;

        groupBox.addItem (NI_TXT ("状态栏 · ") + entry.name, id);

        if (document.getProject().midiInput.groupSource.kind == MidiGroupSource::Kind::entry
            && document.getProject().midiInput.groupSource.entryId == entry.id)
            selected = id;

        ++id;
    }

    const auto& list = presets.getReferenceLineGroups();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        groupBox.addItem (NI_TXT ("预设 · ") + list[(size_t) i].name, id);

        if (document.getProject().midiInput.groupSource.kind == MidiGroupSource::Kind::preset
            && document.getProject().midiInput.groupSource.presetIndex == i)
            selected = id;

        ++id;
    }

    groupBox.setSelectedId (selected, juce::dontSendNotification);
}

void MidiInputPanel::applyMode (bool looping)
{
    if (updating)
        return;

    document.applyDerivedEdit ([looping] (Project& project)
    {
        project.midiInput.map.mode = looping ? midimap::Map::Mode::looping
                                             : midimap::Map::Mode::free;
    });
    document.notifyEdit();

    learningAnchor = false;
    learningNeedsKey = false;
    canvas.setLearning (false);
    refresh();
}

void MidiInputPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& project = document.getProject();
    const auto& map = project.midiInput.map;
    const auto* track = project.findTrack (appState.selectedTrackId);

    rebuildDeviceBox();
    rebuildGroupBox();

    trackLabel.setText (track != nullptr ? NI_TXT ("轨道：") + track->name
                                         : NI_TXT ("轨道：—"),
                        juce::dontSendNotification);

    const auto looping = map.mode == midimap::Map::Mode::looping;
    loopModeButton.setToggleState (looping, juce::dontSendNotification);
    freeModeButton.setToggleState (! looping, juce::dontSendNotification);
    loopKeysBox.setSelectedId (juce::jlimit (midimap::Map::kMinLoopKeys, midimap::Map::kMaxLoopKeys,
                                             map.loopKeys),
                               juce::dontSendNotification);
    loopKeysBox.setEnabled (looping);
    keysLabel.setEnabled (looping);

    applyButton.setToggleState (learningAnchor, juce::dontSendNotification);

    canvas.refresh();
    arpControls.refresh();
    updateStatus();
}

void MidiInputPanel::refreshArpControls()
{
    arpControls.refresh();
    updateStatus();
}

void MidiInputPanel::updateStatus()
{
    const auto& project = document.getProject();
    const auto& map = project.midiInput.map;
    const auto* track = project.findTrack (appState.selectedTrackId);

    juce::String text;

    if (track == nullptr)
        text = NI_TXT ("先选一条轨道");
    else if (! kindCanPlayMidi (track->kind))
        text = NI_TXT ("这条轨没有音高（鼓机 / 音频），MIDI 输入不发声也不记录");
    else if (learningAnchor)
        text = learningNeedsKey
                   ? NI_TXT ("先点一个左侧琴键（Esc 取消）")
                   : NI_TXT ("请点一个左侧琴键，再按 MIDI 键盘上一个键（Esc 取消）");
    else if (document.viewState().editorMode == EditorMode::piano)
        text = NI_TXT ("钢琴窗模式：MIDI 按十二平均律发声，这张映射暂时不生效");
    else
    {
        const auto bound = map.boundKeyCount();
        const auto cables = map.cableCount();
        const auto steps = juce::jlimit (midimap::Map::kMinLoopKeys, midimap::Map::kMaxLoopKeys,
                                         map.loopKeys);

        if (map.mode == midimap::Map::Mode::looping)
        {
            // Everything the user needs to answer "现在按出来是什么" in one line:
            // where the tile starts, how long it is, and how many of its steps make
            // a sound at all (the empty ones are the silent black keys).
            text = NI_TXT ("锚点 ") + midimap::noteName (map.anchorNote)
                   + NI_TXT ("（MIDI ") + juce::String (map.anchorNote) + NI_TXT ("）")
                   + NI_TXT ("　·　一格 ") + juce::String (map.loopKeys) + NI_TXT (" 个半音")
                   + NI_TXT ("　·　已绑 ") + juce::String (bound) + NI_TXT (" 个")
                   + NI_TXT ("　·　空 ") + juce::String (juce::jmax (0, steps - bound))
                   + NI_TXT (" 个（空的不发声）");
        }
        else
        {
            text = NI_TXT ("非循环：每个琴键单独指定")
                   + NI_TXT ("　·　已绑 ") + juce::String (bound) + NI_TXT (" 个键");
        }

        // Only worth saying when some key is a stack - otherwise it is the same
        // number twice.
        if (cables != bound)
            text += NI_TXT ("　·　一共 ") + juce::String (cables) + NI_TXT (" 条连线");

        if (project.midiInput.group.dots.empty())
            text += NI_TXT ("　右侧还没有参考线组：从上面选一组");
        else if (bound == 0)
            text += NI_TXT ("　把左边的琴键拖到右边的一个圆点上就绑好了");
    }

    const auto device = midi.isOpen() ? NI_TXT ("　设备：") + midi.getOpenName()
                                      : NI_TXT ("　设备：未选择");
    statusLabel.setText (text + device, juce::dontSendNotification);
}

void MidiInputPanel::audition (double frequencyHz)
{
    const auto* track = document.getProject().findTrack (appState.selectedTrackId);

    if (track == nullptr || ! kindCanPlayMidi (track->kind) || frequencyHz <= 0.0)
        return;

    const auto trackId = track->id;
    const auto noteId = (juce::int64) kMidiLiveIdBase - (juce::int64) ++auditionSerial;

    audio.noteOnTrack (trackId, noteId, frequencyHz, 100.0f);

    juce::Component::SafePointer<MidiInputPanel> safeThis (this);
    juce::Timer::callAfterDelay (260, [safeThis, trackId, noteId]
    {
        if (safeThis != nullptr)
            safeThis->audio.noteOffTrack (trackId, noteId);
    });
}

bool MidiInputPanel::handleMidiMessage (const juce::MidiMessage& message)
{
    if (! learningAnchor || ! message.isNoteOn())
        return false;

    const auto key = canvas.getLearningKey();

    if (key < 0)
    {
        // A MIDI key arrived before the left-hand one: say which half of the gesture
        // is still missing instead of quietly doing nothing.
        learningNeedsKey = true;
        updateStatus();
        return false;
    }

    const auto offset = document.getProject().midiInput.map.tileOffsetOf (key);
    const auto anchor = message.getNoteNumber() - offset;

    document.applyDerivedEdit ([anchor] (Project& project)
    {
        project.midiInput.map.anchorNote = juce::jlimit (midimap::Map::kLowestNote,
                                                         midimap::Map::kHighestNote, anchor);
    });
    document.notifyEdit();

    learningAnchor = false;
    learningNeedsKey = false;
    canvas.setLearning (false);
    refresh();
    return true;
}

void MidiInputPanel::triggerApplyForTest()
{
    if (updating)
        return;

    // Non-looping mode has no tile to anchor, so 应用 simply leaves the edit.
    if (document.getProject().midiInput.map.mode != midimap::Map::Mode::looping)
    {
        updateStatus();
        return;
    }

    learningAnchor = ! learningAnchor;
    learningNeedsKey = false;
    canvas.setLearning (learningAnchor);
    refresh();
}

void MidiInputPanel::savePresetForTest()
{
    const auto state = document.getProject().midiInput;

    if (state.presetName.isEmpty())
    {
        saveAsForTest (NI_TXT ("MIDI 映射 ") + juce::String ((int) presets.getMidiMaps().size() + 1));
        return;
    }

    for (int i = 0; i < (int) presets.getMidiMaps().size(); ++i)
        if (presets.getMidiMaps()[(size_t) i].presetName == state.presetName)
        {
            presets.replaceMidiMap (i, state);
            refresh();
            return;
        }

    presets.addMidiMap (state);
    refresh();
}

void MidiInputPanel::saveAsForTest (const juce::String& name)
{
    auto state = document.getProject().midiInput;
    state.presetName = name.isNotEmpty() ? name
                                         : NI_TXT ("MIDI 映射 ")
                                               + juce::String ((int) presets.getMidiMaps().size() + 1);

    presets.addMidiMap (state);

    document.applyDerivedEdit ([&state] (Project& project) { project.midiInput.presetName = state.presetName; });
    document.notifyEdit();
    refresh();
}

void MidiInputPanel::renamePresetForTest (const juce::String& newName)
{
    const auto name = newName.trim();
    const auto current = document.getProject().midiInput.presetName;

    if (name.isEmpty() || current.isEmpty())
        return;   // nothing saved yet: 另存为… is the way in

    for (int i = 0; i < (int) presets.getMidiMaps().size(); ++i)
        if (presets.getMidiMaps()[(size_t) i].presetName == current)
        {
            presets.renameMidiMap (i, name);
            break;
        }

    // The project remembers which library entry it came from: that follows too.
    document.applyDerivedEdit ([&name] (Project& project) { project.midiInput.presetName = name; });
    document.notifyEdit();
    refresh();
}

void MidiInputPanel::showRenameDialog()
{
    if (! showDialogs)
        return;

    const auto current = document.getProject().midiInput.presetName;

    if (current.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, NI_TXT ("改名"),
                                                NI_TXT ("这张映射还没有存成预设：先用「另存为…」存一次。"));
        return;
    }

    auto* window = new juce::AlertWindow (NI_TXT ("改名"),
                                          NI_TXT ("给这张映射起个新名字："),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", current);
    window->addButton (NI_TXT ("确定"), 1);
    window->addButton (NI_TXT ("取消"), 0);

    juce::Component::SafePointer<MidiInputPanel> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis, window] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            std::unique_ptr<juce::AlertWindow> owned (window);

            if (result == 1 && safeThis != nullptr && name.isNotEmpty())
                safeThis->renamePresetForTest (name);
        }), false);
}

void MidiInputPanel::loadPresetForTest (int index)
{
    const auto& list = presets.getMidiMaps();

    if (index < 0 || index >= (int) list.size())
        return;

    const auto state = list[(size_t) index];
    document.applyDerivedEdit ([&state] (Project& project) { project.midiInput = state; });
    document.notifyEdit();

    learningAnchor = false;
    learningNeedsKey = false;
    canvas.setLearning (false);
    refresh();
}

void MidiInputPanel::deletePresetForTest()
{
    deleteButton.onClick();
}

int MidiInputPanel::getPresetCountForTest() const
{
    return (int) presets.getMidiMaps().size();
}

juce::String MidiInputPanel::getPresetNameForTest() const
{
    return document.getProject().midiInput.presetName;
}

void MidiInputPanel::setDeviceForTest (int index)
{
    deviceBox.setSelectedId (kDeviceFirstId + index, juce::dontSendNotification);

    if (deviceBox.onChange != nullptr)
        deviceBox.onChange();
}

void MidiInputPanel::setModeForTest (bool looping)
{
    applyMode (looping);
}

void MidiInputPanel::setLoopKeysForTest (int keys)
{
    loopKeysBox.setSelectedId (keys, juce::dontSendNotification);

    if (loopKeysBox.onChange != nullptr)
        loopKeysBox.onChange();
}

void MidiInputPanel::setGroupForTest (int comboId)
{
    groupBox.setSelectedId (comboId, juce::dontSendNotification);

    if (groupBox.onChange != nullptr)
        groupBox.onChange();
}

bool MidiInputPanel::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey && learningAnchor)
    {
        triggerApplyForTest();
        return true;
    }

    return false;
}

void MidiInputPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
}

void MidiInputPanel::resized()
{
    auto area = getLocalBounds().reduced (kMargin, 6);

    auto firstRow = area.removeFromTop (kRowHeight);
    deviceLabel.setBounds (firstRow.removeFromLeft (66));
    deviceBox.setBounds (firstRow.removeFromLeft (160).reduced (0, 1));
    firstRow.removeFromLeft (10);
    trackLabel.setBounds (firstRow.removeFromLeft (150));
    firstRow.removeFromLeft (8);
    loopModeButton.setBounds (firstRow.removeFromLeft (72).reduced (0, 1));
    firstRow.removeFromLeft (4);
    freeModeButton.setBounds (firstRow.removeFromLeft (80).reduced (0, 1));
    firstRow.removeFromLeft (10);
    keysLabel.setBounds (firstRow.removeFromLeft (60));
    loopKeysBox.setBounds (firstRow.removeFromLeft (96).reduced (0, 1));
    firstRow.removeFromLeft (8);
    applyButton.setBounds (firstRow.removeFromLeft (72).reduced (0, 1));

    area.removeFromTop (4);

    auto secondRow = area.removeFromTop (kRowHeight);
    groupLabel.setBounds (secondRow.removeFromLeft (66));
    groupBox.setBounds (secondRow.removeFromLeft (240).reduced (0, 1));
    secondRow.removeFromLeft (10);
    deleteButton.setBounds (secondRow.removeFromRight (60).reduced (0, 1));
    secondRow.removeFromRight (4);
    presetsButton.setBounds (secondRow.removeFromRight (74).reduced (0, 1));
    secondRow.removeFromRight (4);
    renameButton.setBounds (secondRow.removeFromRight (60).reduced (0, 1));
    secondRow.removeFromRight (4);
    saveAsButton.setBounds (secondRow.removeFromRight (84).reduced (0, 1));
    secondRow.removeFromRight (4);
    saveButton.setBounds (secondRow.removeFromRight (60).reduced (0, 1));
    secondRow.removeFromRight (8);
    statusLabel.setBounds (secondRow);

    area.removeFromTop (4);

    // The 琶音 row sits between the preset row and the canvas: it belongs to the
    // MIDI input as a whole, not to the mapping picture below it.
    arpControls.setBounds (area.removeFromTop (ArpControls::kDesignHeight));
    area.removeFromTop (4);

    canvas.setBounds (area);
}

//==============================================================================
MidiInputWindow::MidiInputWindow (ProjectDocument& document, AppState& appState,
                                  PresetStore& presets, AudioEngine& audio,
                                  MidiInputManager& midi, juce::Component* context)
    : juce::DocumentWindow (NI_TXT ("MIDI 输入"), theme::panelBackground,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      placementContext (context)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (760, 460, 2000, 1400);

    panel = new MidiInputPanel (document, appState, presets, audio, midi);
    setContentOwned (panel, false);
}

MidiInputWindow::~MidiInputWindow()
{
    panel = nullptr;
}

void MidiInputWindow::closeButtonPressed()
{
    setVisible (false);
}

void MidiInputWindow::show()
{
    if (! isVisible())
        setBounds (placement::placeWindow ({ kDesignWidth, kDesignHeight },
                                           placement::displayAreaFor (placementContext)));

    setVisible (true);
    toFront (true);

    if (panel != nullptr)
        panel->refresh();
}

void MidiInputWindow::refresh()
{
    if (panel != nullptr)
        panel->refresh();
}

} // namespace hmdaw
