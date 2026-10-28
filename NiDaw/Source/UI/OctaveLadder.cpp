#include "OctaveLadder.h"
#include "Theme.h"

namespace nidaw
{

namespace
{
    constexpr float kHitRadius      = 7.0f;
    constexpr float kDotRadius      = 5.0f;
    constexpr float kCrossHalfSize  = 4.0f;
    constexpr float kFrameRadius    = 5.5f;
    constexpr int   kMaxVisibleOctaves = 8;

    juce::String formatDotRatio (double position)
    {
        return juce::String (music::positionToRatio (position), 3);
    }
}

OctaveLadder::OctaveLadder (ProjectDocument& documentToUse, AppState& appStateToUse)
    : document (documentToUse), appState (appStateToUse)
{
    document.addChangeListener (this);
    appState.addChangeListener (this);

    // Clicking the segment puts the 1..5 ratio keys on this ladder (see keyPressed).
    setWantsKeyboardFocus (true);
}

OctaveLadder::~OctaveLadder()
{
    document.removeChangeListener (this);
    appState.removeChangeListener (this);
}

const ReferenceGroup& OctaveLadder::group() const
{
    return document.getProject().referenceGroup;
}

void OctaveLadder::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Only a change of SELECTION clears the preview crosses. A document edit
    // (promoting one cross into a dot, for instance) must leave the rest of the
    // batch alone - they are cleared when the next batch is generated.
    // In the reference flow the crosses are a live projection, so they never need
    // clearing either.
    if (source == &appState)
    {
        if (appState.referencePick != AppState::ReferencePick::reference)
            transientCrosses.clear();
    }
    else
    {
        // A dot may have appeared where a cross was showing (promoted here, or
        // created another way). Drop only those, so no cross ever sits on a dot.
        transientCrosses.erase (std::remove_if (transientCrosses.begin(), transientCrosses.end(),
                                                [this] (const Cross& cross) { return isDotAt (cross.position); }),
                                transientCrosses.end());
    }

    repaint();
}

void OctaveLadder::refresh()
{
    repaint();
}

void OctaveLadder::setLook (Look newLook)
{
    if (newLook.plainLine == look.plainLine
        && newLook.frequencyLabels == look.frequencyLabels
        && std::abs (newLook.anchorHz - look.anchorHz) < 1.0e-9
        && std::abs (newLook.anchorPosition - look.anchorPosition) < 1.0e-9)
        return;

    look = newLook;
    repaint();
}

std::vector<double> OctaveLadder::getCrossPositions() const
{
    std::vector<double> positions;

    for (const auto& cross : currentCrosses())
        positions.push_back (cross.position);

    return positions;
}

juce::StringArray OctaveLadder::getCrossLabelsForTest() const
{
    juce::StringArray labels;

    for (const auto& cross : currentCrosses())
        labels.add (cross.label);

    return labels;
}

void OctaveLadder::commitGroupChange (const juce::String& undoName,
                                      const std::function<void (ReferenceGroup&)>& mutation)
{
    // While a group's editor is open the project's reference group is that editor's
    // working copy, so a change to it is a preview: it must not push an undo step
    // (保存 is what turns the work into a real edit).
    if (appState.groupEditor.isOpen())
    {
        document.applyDerivedEdit ([&mutation] (Project& project) { mutation (project.referenceGroup); });
        return;
    }

    document.applyEdit (undoName, [&mutation] (Project& project) { mutation (project.referenceGroup); });
}

//==============================================================================
double OctaveLadder::pixelsPerOctave() const
{
    return (double) juce::jmax (1, getHeight()) / juce::jmax (0.05, visibleOctaves);
}

double OctaveLadder::yForPosition (double ladderPosition) const
{
    return (double) getHeight() * 0.5 - (ladderPosition - viewCentre) * pixelsPerOctave();
}

double OctaveLadder::positionForY (double y) const
{
    return viewCentre + ((double) getHeight() * 0.5 - y) / pixelsPerOctave();
}

juce::Point<float> OctaveLadder::pointForPosition (double ladderPosition) const
{
    return { (float) getWidth() * 0.5f, (float) yForPosition (ladderPosition) };
}

bool OctaveLadder::isDotAt (double position) const
{
    const auto folded = music::foldPosition (position);

    if (folded < 1.0e-9 || folded > 1.0 - 1.0e-9)
        return true;   // the frame dots

    return music::findDotNear (group(), folded, 1.0e-6) >= 0;
}

std::vector<double> OctaveLadder::projectedPositions() const
{
    std::vector<double> positions;

    if (! appState.referenceAnchorValid || appState.referenceAnchorHz <= 0.0)
        return positions;

    const auto* clip = document.getProject().findClip (appState.selectedClipId);

    if (clip == nullptr)
        return positions;

    for (const auto& note : clip->notes)
    {
        if (appState.selectedNoteIds.find (note.id) == appState.selectedNoteIds.end())
            continue;

        const auto offset = std::log2 (note.frequencyHz / appState.referenceAnchorHz);
        const auto position = music::foldPosition (appState.referenceAnchorPosition + offset);

        // A class that already has a dot needs no cross.
        if (isDotAt (position))
            continue;

        const bool duplicate = std::any_of (positions.begin(), positions.end(),
                                            [position] (double existing)
                                            {
                                                const auto distance = std::abs (existing - position);
                                                return juce::jmin (distance, 1.0 - distance) < 1.0e-6;
                                            });

        if (! duplicate)
            positions.push_back (position);
    }

    return positions;
}

std::vector<OctaveLadder::Cross> OctaveLadder::currentCrosses() const
{
    // While a reference group is being picked from a note selection the crosses are
    // a live projection rather than a proposal, so they carry no caption.
    if (appState.referencePick == AppState::ReferencePick::reference && appState.referenceAnchorValid)
    {
        std::vector<Cross> crosses;

        for (const auto position : projectedPositions())
            crosses.push_back ({ position, juce::String() });

        return crosses;
    }

    return transientCrosses;
}

void OctaveLadder::showRatioCrosses (double ratio)
{
    transientCrosses.clear();

    const auto selected = getSelectedDotPosition();

    if (selected < 0.0 || ratio <= 0.0 || std::abs (ratio - 1.0) < 1.0e-9)
    {
        repaint();
        return;
    }

    const auto offset = std::log2 (ratio);

    // The two crosses are the same interval the other way round: the one above is
    // the dot times the ratio (正), the one below is the dot divided by it (负).
    // The caption is carried with the cross so the pointer can be told which it is.
    // It is written the way the canvas writes its own ratio arrow ("正 1:3"), so the
    // two read the same.
    const auto caption = music::formatRatioText (ratio);

    for (const auto& [delta, sign] : { std::pair<double, const char*> { offset, "正" },
                                       std::pair<double, const char*> { -offset, "负" } })
    {
        const auto position = music::foldPosition (selected + delta);

        if (isDotAt (position))
            continue;

        const bool duplicate = std::any_of (transientCrosses.begin(), transientCrosses.end(),
                                            [position] (const Cross& existing)
                                            {
                                                const auto distance = std::abs (existing.position - position);
                                                return juce::jmin (distance, 1.0 - distance) < 1.0e-6;
                                            });

        if (! duplicate)
            transientCrosses.push_back ({ position, NI_TXT (sign) + " " + caption });
    }

    repaint();
}

void OctaveLadder::showDivisionCrosses (int parts)
{
    transientCrosses.clear();

    if (parts > 1 && selectedDots.size() == 2)
    {
        // Counted from the lower of the two dots upwards: the lowest cross is 1/N.
        const auto positions = music::dividePositions (selectedDots[0], selectedDots[1], parts);

        for (int i = 0; i < (int) positions.size(); ++i)
            if (! isDotAt (positions[(size_t) i]))
                transientCrosses.push_back ({ positions[(size_t) i],
                                              juce::String (i + 1) + "/" + juce::String (parts) });
    }

    repaint();
}

void OctaveLadder::clearTransientCrosses()
{
    if (transientCrosses.empty())
        return;

    transientCrosses.clear();
    repaint();
}

double OctaveLadder::getSelectedDotPosition() const
{
    return selectedDots.size() == 1 ? selectedDots.front() : -1.0;
}

bool OctaveLadder::isFrameDotSelected() const
{
    if (selectedDots.size() != 1)
        return false;

    const auto position = selectedDots.front();
    return position <= 1.0e-9 || position >= 1.0 - 1.0e-9;
}

void OctaveLadder::selectDot (double position, bool addToSelection)
{
    // Positions are kept unfolded so the frame dots at 0 and 1 stay distinct:
    // folding them would collapse the two ends of the octave into one entry.
    if (addToSelection)
    {
        music::toggleSelectedPosition (selectedDots, position);
    }
    else
    {
        selectedDots.clear();
        selectedDots.push_back (position);
    }
}

void OctaveLadder::clearDotSelection()
{
    if (selectedDots.empty())
        return;

    selectedDots.clear();
}

int OctaveLadder::findCrossAt (float y, double* positionOut) const
{
    const auto crosses = currentCrosses();

    for (int i = 0; i < (int) crosses.size(); ++i)
    {
        if (std::abs ((float) yForPosition (crosses[(size_t) i].position) - y) <= kHitRadius)
        {
            if (positionOut != nullptr)
                *positionOut = crosses[(size_t) i].position;

            return i;
        }
    }

    return -1;
}

int OctaveLadder::findDotAt (float y, double* positionOut) const
{
    const auto& current = group();

    for (int i = 0; i < (int) current.dots.size(); ++i)
    {
        if (std::abs ((float) yForPosition (current.dots[(size_t) i].position) - y) <= kHitRadius)
        {
            if (positionOut != nullptr)
                *positionOut = current.dots[(size_t) i].position;

            return i;
        }
    }

    for (const auto framePosition : { 0.0, 1.0 })
    {
        if (std::abs ((float) yForPosition (framePosition) - y) <= kHitRadius)
        {
            if (positionOut != nullptr)
                *positionOut = framePosition;

            return -2;   // frame dot; not an index into the stored dots
        }
    }

    return -1;
}

//==============================================================================
void OctaveLadder::promoteCross (double position)
{
    const auto wasProjection = (appState.referencePick == AppState::ReferencePick::reference);

    commitGroupChange (NI_TXT ("建立圆点"), [position, this] (ReferenceGroup& target)
    {
        const auto colour = (int) target.dots.size() % kReferencePaletteSize;
        music::addDotAt (target, position, colour);
    });

    if (! wasProjection)
    {
        // Only the cross that was promoted goes: the rest of the batch stays, so a
        // division can be confirmed one cross at a time.
        transientCrosses.erase (std::remove_if (transientCrosses.begin(), transientCrosses.end(),
                                                [position] (const Cross& existing)
                                                { return std::abs (existing.position - position) < 1.0e-6; }),
                                transientCrosses.end());
    }
}

void OctaveLadder::mouseDown (const juce::MouseEvent& e)
{
    const auto y = (float) e.position.y;

    // Clicking the segment puts the 1..5 ratio keys on this ladder.
    grabKeyboardFocus();

    marqueeActive = false;
    bandArmedAtPress = false;
    bandKeepsPressedDot = false;
    pendingCrossValid = false;
    hoveredCross = {};

    double crossPosition = 0.0;
    double dotPosition = 0.0;

    if (e.mods.isPopupMenu())
    {
        const auto dotIndex = findDotAt (y, &dotPosition);

        if (dotIndex >= 0)
        {
            // A right click on a dot that is part of a multiple selection removes
            // the whole selection, as one edit - "select several dots, right click
            // one of them, they all go". A dot outside the selection goes on its
            // own, and a single selected dot behaves exactly as it always has.
            auto doomed = (selectedDots.size() > 1
                               && music::isPositionSelected (selectedDots, dotPosition))
                              ? selectedDots
                              : std::vector<double> { dotPosition };

            // The two frame dots are always there and store nothing, so they can
            // never be part of the batch (nor can they be deleted at all).
            doomed.erase (std::remove_if (doomed.begin(), doomed.end(),
                                          [] (double position)
                                          {
                                              return position <= 1.0e-9
                                                     || position >= 1.0 - 1.0e-9;
                                          }),
                          doomed.end());

            if (! doomed.empty())
                commitGroupChange (NI_TXT ("删除圆点"), [&doomed] (ReferenceGroup& target)
                {
                    for (const auto position : doomed)
                        music::removeDotAt (target, position, 1.0e-6);
                });

            selectedDots.clear();
            transientCrosses.clear();
            repaint();
        }

        return;
    }

    // Crosses sit on top of dots: clicking one promotes it to a dot.
    if (findCrossAt (y, &crossPosition) >= 0)
    {
        // Only promote it if this turns out to be a click: a press that becomes a
        // drag is the start of a band, and turning the cross into a dot on the way
        // would be an edit nobody asked for. See mouseUp().
        pendingCrossValid = true;
        pendingCrossPosition = crossPosition;
        bandArmedAtPress = true;
        marqueeStart = e.position;
        marqueeCurrent = e.position;

        repaint();
        return;
    }

    const auto dotIndex = findDotAt (y, &dotPosition);

    if (dotIndex != -1)
    {
        selectDot (dotPosition, e.mods.isCommandDown());
        transientCrosses.clear();

        if (appState.referencePick != AppState::ReferencePick::none)
            appState.armDot (music::foldPosition (dotPosition));
        else
            sendChangeMessage();   // keep listeners in step with the selection

        // A drag from here bands the segment too - the dot stays selected, because
        // the press was aimed at it.
        bandArmedAtPress = true;
        bandKeepsPressedDot = true;
        marqueeStart = e.position;
        marqueeCurrent = e.position;

        repaint();
        return;
    }

    // Empty space starts a rubber band. A plain press that never moves still clears
    // the selection and the preview crosses, exactly as it used to.
    if (! e.mods.isCommandDown())
        clearDotSelection();

    marqueeActive = true;
    marqueeStart = e.position;
    marqueeCurrent = e.position;
    repaint();
}

void OctaveLadder::mouseDrag (const juce::MouseEvent& e)
{
    // Pressing right beside a dot or a cross used to be a dead end: the press was
    // read as a click and the drag did nothing. A press only counts as a click if
    // the pointer stays put, so a real movement turns it into a band.
    if (! marqueeActive && bandArmedAtPress && (e.position - marqueeStart).getDistanceFromOrigin() > 3.0f)
    {
        marqueeActive = true;
        pendingCrossValid = false;   // that press was a drag, not a click
    }

    if (! marqueeActive)
        return;

    marqueeCurrent = e.position;
    repaint();
}

void OctaveLadder::mouseUp (const juce::MouseEvent& e)
{
    // The press never moved: now it is a click, and a click on a cross turns it
    // into a dot (the only thing that was ever deferred).
    if (pendingCrossValid)
    {
        const auto position = pendingCrossPosition;

        pendingCrossValid = false;
        bandArmedAtPress = false;
        bandKeepsPressedDot = false;
        promoteCross (position);
        repaint();
        return;
    }

    if (! marqueeActive)
    {
        bandArmedAtPress = false;
        bandKeepsPressedDot = false;
        return;
    }

    marqueeActive = false;
    marqueeCurrent = e.position;

    const auto band = juce::Rectangle<float> (marqueeStart, marqueeCurrent);

    // The segment is a vertical strip, so "inside the band" is a question about y.
    auto covered = [&band] (double y) { return (float) y >= band.getY() && (float) y <= band.getBottom(); };

    // A band that never left the press is just a click on empty space: the selection
    // was already cleared above and there is nothing to promote.
    if (band.getWidth() < 3.0f && band.getHeight() < 3.0f)
    {
        repaint();
        return;
    }

    // Ctrl unions the band into what was already selected; a band that started on a
    // dot keeps that dot too, since a press aimed at a dot is not aimed at nothing.
    auto selected = (e.mods.isCommandDown() || bandKeepsPressedDot)
                        ? selectedDots
                        : std::vector<double>();

    bandArmedAtPress = false;
    bandKeepsPressedDot = false;
    std::vector<double> promoted;

    for (const auto& cross : currentCrosses())
    {
        const auto y = (float) yForPosition (cross.position);

        if (! covered (y))
            continue;

        promoted.push_back (cross.position);
    }

    // Every dot the band covers is selected as well - the ones that were already
    // there, plus the crosses that are about to become dots.
    for (const auto& dot : group().dots)
    {
        const auto y = (float) yForPosition (dot.position);

        if (covered (y))
            if (std::find (selected.begin(), selected.end(), dot.position) == selected.end())
                selected.push_back (dot.position);
    }

    for (const auto position : promoted)
    {
        const auto folded = music::foldPosition (position);

        if (std::find (selected.begin(), selected.end(), folded) == selected.end())
            selected.push_back (folded);
    }

    selectedDots = std::move (selected);
    marqueeStart = marqueeCurrent = {};

    if (! promoted.empty())
    {
        // Promoting a batch of crosses is one edit, like every other gesture here.
        commitGroupChange (NI_TXT ("建立圆点"), [&promoted] (ReferenceGroup& target)
        {
            for (const auto position : promoted)
                music::addDotAt (target, position, (int) target.dots.size() % kReferencePaletteSize);
        });

        transientCrosses.clear();
    }

    // The band rewrote the selection, so whatever is watching it (the colour row,
    // the ratio keys, the status line) has to be told.
    sendChangeMessage();
    repaint();
}

void OctaveLadder::mouseMove (const juce::MouseEvent& e)
{
    double position = 0.0;
    Cross found;

    if (findCrossAt ((float) e.position.y, &position) >= 0)
        for (const auto& cross : currentCrosses())
            if (std::abs (cross.position - position) < 1.0e-9)
                found = cross;

    if (found.position == hoveredCross.position && found.label == hoveredCross.label)
        return;

    hoveredCross = found;
    repaint();
}

void OctaveLadder::mouseExit (const juce::MouseEvent&)
{
    if (hoveredCross.label.isEmpty())
        return;

    hoveredCross = {};
    repaint();
}

bool OctaveLadder::keyPressed (const juce::KeyPress& key)
{
    // 1..5 pick a ratio (the same five the buttons offer). The panel decides whether
    // they are live - with several dots, or a cross, selected there is no single
    // origin for a ratio lens, so it simply does nothing.
    const auto code = key.getKeyCode();

    if (key.getModifiers().isAnyModifierKeyDown() || code < '1' || code > '5')
        return false;

    if (onRatioKey != nullptr)
        onRatioKey (code - '1');

    return true;
}

void OctaveLadder::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (delta == 0.0f)
        return;

    if (e.mods.isAltDown())
    {
        // Zoom around the position under the mouse so it stays put.
        const auto anchor = positionForY ((double) e.position.y);
        visibleOctaves = juce::jlimit (0.5, (double) kMaxVisibleOctaves,
                                       visibleOctaves / std::pow (2.0, (double) delta * 0.75));
        viewCentre = anchor - ((double) getHeight() * 0.5 - (double) e.position.y) / pixelsPerOctave();
    }
    else
    {
        viewCentre += (double) delta * visibleOctaves * 0.5;
    }

    repaint();
}

//==============================================================================
void OctaveLadder::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (theme::gridBackground);
    g.fillRect (area);

    const auto centreX = (float) area.getWidth() * 0.5f;
    const auto top = 0.0f;
    const auto bottom = (float) area.getHeight();

    // The frame octave (ladder positions 0 to 1) is drawn brighter than the rest.
    const auto frameTop = (float) yForPosition (1.0);
    const auto frameBottom = (float) yForPosition (0.0);

    g.setColour (theme::outline);
    g.fillRect (centreX - 0.5f, top, 1.0f, bottom - top);

    // The line itself: the reference group column brightens the frame octave, the
    // group editor draws one plain straight line from top to bottom.
    if (look.plainLine)
    {
        g.setColour (theme::accent.withAlpha (0.55f));
        g.fillRect (centreX - 1.0f, top, 2.0f, bottom - top);
    }
    else
    {
        g.setColour (theme::accent.withAlpha (0.55f));
        g.fillRect (centreX - 1.0f, juce::jmax (top, frameTop), 2.0f,
                    juce::jmin (bottom, frameBottom) - juce::jmax (top, frameTop));
    }

    // Octave boundaries.
    const auto firstOctave = (int) std::floor (positionForY (bottom));
    const auto lastOctave  = (int) std::ceil (positionForY (top));

    const auto dotColour = [] (int index) { return theme::trackColour (index); };

    for (int octave = firstOctave; octave <= lastOctave; ++octave)
    {
        for (const auto& dot : group().dots)
        {
            const auto ladderPosition = (double) octave + dot.position;
            const auto y = (float) yForPosition (ladderPosition);

            if (y < -8.0f || y > bottom + 8.0f)
                continue;

            const auto selected = music::isPositionSelected (selectedDots, dot.position);

            g.setColour (dotColour (dot.colourIndex));
            g.fillEllipse (centreX - kDotRadius, y - kDotRadius, kDotRadius * 2.0f, kDotRadius * 2.0f);

            if (selected)
            {
                g.setColour (theme::noteSelectedOutline);
                g.drawEllipse (centreX - kDotRadius - 2.0f, y - kDotRadius - 2.0f,
                               (kDotRadius + 2.0f) * 2.0f, (kDotRadius + 2.0f) * 2.0f, 1.5f);
            }
        }
    }

    // Frame dots, always present.
    for (const auto framePosition : { 0.0, 1.0 })
    {
        const auto y = (float) yForPosition (framePosition);

        if (y < -8.0f || y > bottom + 8.0f)
            continue;

        g.setColour (dotColour (group().frameColourIndex));
        g.fillEllipse (centreX - kFrameRadius, y - kFrameRadius, kFrameRadius * 2.0f, kFrameRadius * 2.0f);
        g.setColour (theme::windowBackground);
        g.fillEllipse (centreX - 2.0f, y - 2.0f, 4.0f, 4.0f);

        if (music::isPositionSelected (selectedDots, framePosition))
        {
            g.setColour (theme::noteSelectedOutline);
            g.drawEllipse (centreX - kFrameRadius - 2.0f, y - kFrameRadius - 2.0f,
                           (kFrameRadius + 2.0f) * 2.0f, (kFrameRadius + 2.0f) * 2.0f, 1.5f);
        }
    }

    // Preview crosses, each with the caption that explains it (a ratio's 正 / 负, or
    // the fraction an equal division produced).
    const auto crosses = currentCrosses();

    g.setColour (theme::accent);

    for (const auto& cross : crosses)
    {
        const auto y = (float) yForPosition (cross.position);

        if (y < -8.0f || y > bottom + 8.0f)
            continue;

        g.drawLine (centreX - kCrossHalfSize, y - kCrossHalfSize,
                    centreX + kCrossHalfSize, y + kCrossHalfSize, 1.8f);
        g.drawLine (centreX - kCrossHalfSize, y + kCrossHalfSize,
                    centreX + kCrossHalfSize, y - kCrossHalfSize, 1.8f);

        // The caption sits to the *left* of the cross, opposite the dots' own
        // labels, so the two never overlap.
        if (! cross.label.isEmpty() && std::abs (cross.position - hoveredCross.position) < 1.0e-9
            && ! hoveredCross.label.isEmpty())
        {
            // A fixed plate wide enough for "正 1:3" / "2/3": the captions are short
            // and fixed in shape, so measuring them would only add an API to trust.
            const auto plate = juce::Rectangle<int> ((int) centreX - (int) kCrossHalfSize - 50,
                                                     (int) y - 8, 46, 16);

            g.setColour (theme::panelBackground.withAlpha (0.92f));
            g.fillRoundedRectangle (plate.toFloat(), 3.0f);
            g.setColour (theme::outline);
            g.drawRoundedRectangle (plate.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (theme::accent);
            g.setFont (theme::font (10.0f));
            g.drawText (cross.label, plate, juce::Justification::centred, false);
        }
    }

    // The rubber band, while it is being dragged.
    if (marqueeActive)
    {
        const juce::Rectangle<float> band (marqueeStart, marqueeCurrent);

        g.setColour (theme::accent.withAlpha (0.16f));
        g.fillRect (band);
        g.setColour (theme::accent);
        g.drawRect (band, 1.0f);
    }

    // Labels: only for the frame octave, to avoid clutter when zoomed out.
    if (visibleOctaves <= 2.5)
    {
        g.setFont (theme::font (10.0f));
        g.setColour (theme::dimText);

        // The reference LINE group editor works on the line itself, so a dot is
        // labelled with the frequency that line puts there; everywhere else a dot
        // is labelled with its ratio inside the octave.
        const auto labelForPosition = [this] (double position)
        {
            if (look.frequencyLabels)
                return formatHz (music::groupFrequencyAt (look.anchorHz, look.anchorPosition, position));

            return formatDotRatio (position);
        };

        for (const auto& dot : group().dots)
        {
            const auto y = (float) yForPosition (dot.position);

            if (y < 8.0f || y > bottom - 4.0f)
                continue;

            g.drawText (labelForPosition (dot.position),
                        juce::Rectangle<int> ((int) centreX + 10, (int) y - 7, 78, 14),
                        juce::Justification::centredLeft);
        }

        // The two frame dots are the same pitch class at two different positions,
        // so they are labelled explicitly to keep that visible.
        for (const auto framePosition : { 0.0, 1.0 })
        {
            const auto y = (float) yForPosition (framePosition);

            if (y < 8.0f || y > bottom - 4.0f)
                continue;

            g.drawText (labelForPosition (framePosition),
                        juce::Rectangle<int> ((int) centreX + 10, (int) y - 7, 78, 14),
                        juce::Justification::centredLeft);
        }
    }

    if (appState.referencePick != AppState::ReferencePick::none)
    {
        g.setColour (theme::accent.withAlpha (0.8f));
        g.drawRect (area, 2);
    }
}

} // namespace nidaw
