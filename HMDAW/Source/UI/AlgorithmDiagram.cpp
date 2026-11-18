#include "AlgorithmDiagram.h"
#include "Theme.h"

namespace hmdaw
{

namespace
{
    constexpr juce::uint8 opBit (int index) noexcept { return (juce::uint8) (1u << index); }

    /** Draws an arrow head pointing at `tip`, coming from `from`. */
    void drawArrowHead (juce::Graphics& g, juce::Point<float> tip, juce::Point<float> from,
                        float size)
    {
        const auto direction = (tip - from);
        const auto length = direction.getDistanceFromOrigin();

        if (length < 1.0e-3f)
            return;

        const auto unit = direction / length;
        const auto normal = juce::Point<float> (-unit.y, unit.x);
        const auto base = tip - unit * size;

        juce::Path head;
        head.addTriangle (tip, base + normal * (size * 0.5f), base - normal * (size * 0.5f));
        g.fillPath (head);
    }
}

AlgorithmDiagram::AlgorithmDiagram()
{
    setInterceptsMouseClicks (true, false);
}

void AlgorithmDiagram::setAlgorithm (int newAlgorithm)
{
    const auto clamped = juce::jlimit (1, kFmAlgorithmCount, newAlgorithm);

    if (clamped == algorithm)
        return;

    algorithm = clamped;
    layoutBoxes();
    repaint();
}

void AlgorithmDiagram::setPatch (const FmPatch& newPatch)
{
    patch = newPatch;
    repaint();
}

void AlgorithmDiagram::setThumbnail (bool shouldBeThumbnail)
{
    if (thumbnail == shouldBeThumbnail)
        return;

    thumbnail = shouldBeThumbnail;
    layoutBoxes();
    repaint();
}

void AlgorithmDiagram::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void AlgorithmDiagram::setHighlightedOperator (int index)
{
    const auto clamped = index >= 0 && index < kFmOperators ? index : -1;

    if (highlighted == clamped)
        return;

    highlighted = clamped;
    repaint();
}

std::array<int, kFmOperators> AlgorithmDiagram::computeDepths() const
{
    const auto& alg = getFmAlgorithm (algorithm);
    std::array<int, kFmOperators> depths {};

    // Relaxation rather than a single ordered pass: the algorithm tables always
    // feed downwards, but a few extra sweeps cost nothing and keep the layout
    // right even if a future table breaks that convention.
    for (int pass = 0; pass < kFmOperators; ++pass)
    {
        for (int i = 0; i < kFmOperators; ++i)
        {
            for (int target = 0; target < kFmOperators; ++target)
                if ((alg.modulators[target] & opBit (i)) != 0)
                    depths[(size_t) i] = juce::jmax (depths[(size_t) i],
                                                     depths[(size_t) target] + 1);
        }
    }

    return depths;
}

void AlgorithmDiagram::layoutBoxes()
{
    for (auto& box : boxes)
        box = {};

    outputBar = {};

    const auto depths = computeDepths();
    const auto rows = *std::max_element (depths.begin(), depths.end()) + 1;

    auto area = getLocalBounds().reduced (thumbnail ? 3 : 8);

    outputBar = area.removeFromBottom (thumbnail ? 7 : 18);

    if (! thumbnail)
        area.removeFromBottom (10);   // room for the carrier arrows
    else
        area.removeFromBottom (3);

    if (area.getHeight() <= 0 || area.getWidth() <= 0)
        return;

    const auto rowHeight = juce::jmax (1, area.getHeight() / rows);

    for (int row = 0; row < rows; ++row)
    {
        std::vector<int> operatorsInRow;

        for (int i = 0; i < kFmOperators; ++i)
            if (depths[(size_t) i] == row)
                operatorsInRow.push_back (i);

        if (operatorsInRow.empty())
            continue;

        const auto count = (int) operatorsInRow.size();
        const auto slot = juce::jmax (1, area.getWidth() / count);
        const auto boxWidth = juce::jmax (thumbnail ? 14 : 46,
                                          juce::jmin (thumbnail ? 30 : 96, slot - (thumbnail ? 4 : 12)));
        const auto boxHeight = juce::jmax (thumbnail ? 7 : 30,
                                           juce::jmin (thumbnail ? 20 : 54, rowHeight - (thumbnail ? 3 : 10)));
        const auto y = area.getBottom() - (row + 1) * rowHeight
                           + (rowHeight - boxHeight) / 2;

        for (int slotIndex = 0; slotIndex < count; ++slotIndex)
        {
            const auto centreX = area.getX() + slot * slotIndex + slot / 2;
            boxes[(size_t) operatorsInRow[(size_t) slotIndex]] =
                juce::Rectangle<int> (centreX - boxWidth / 2, y, boxWidth, boxHeight);
        }
    }
}

juce::Rectangle<int> AlgorithmDiagram::getOperatorBox (int index) const
{
    // The boxes are only meaningful once the component has a real size; the
    // library builds its 32 pictures before the parent has placed them, so
    // layoutBoxes() also runs from resized().
    if (index < 0 || index >= kFmOperators)
        return {};

    return boxes[(size_t) index];
}

void AlgorithmDiagram::resized()
{
    layoutBoxes();
}

juce::Rectangle<int> AlgorithmDiagram::getOperatorSwitch (int index) const
{
    if (thumbnail)
        return {};

    const auto box = getOperatorBox (index);

    if (box.isEmpty())
        return {};

    const auto size = juce::jmin (12, box.getHeight() / 3);
    return juce::Rectangle<int> (box.getRight() - size - 3, box.getY() + 3, size, size);
}

void AlgorithmDiagram::paint (juce::Graphics& g)
{
    const auto& alg = getFmAlgorithm (algorithm);

    // The same dark plate as the big diagram: the operator boxes are drawn in
    // panelRaised, so a thumbnail keeps the same contrast as the full picture.
    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), thumbnail ? 3.0f : 5.0f);

    g.setColour (selected ? theme::accent : theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f),
                            thumbnail ? 3.0f : 5.0f, selected ? 2.0f : 1.0f);

    const auto lineColour = thumbnail ? theme::dimText.withAlpha (0.7f) : theme::referenceLineDim;

    // ---- modulation arrows -------------------------------------------------
    for (int target = 0; target < kFmOperators; ++target)
    {
        const auto to = boxes[(size_t) target];

        if (to.isEmpty())
            continue;

        for (int source = 0; source < kFmOperators; ++source)
        {
            if ((alg.modulators[target] & opBit (source)) == 0)
                continue;

            const auto from = boxes[(size_t) source];

            if (from.isEmpty())
                continue;

            const auto start = juce::Point<float> ((float) from.getCentreX(), (float) from.getBottom());
            const auto end   = juce::Point<float> ((float) to.getCentreX(), (float) to.getY());

            g.setColour (source == highlighted || target == highlighted ? theme::accent : lineColour);
            g.drawLine ({ start, end }, thumbnail ? 1.0f : 1.4f);
            drawArrowHead (g, end, start, thumbnail ? 3.0f : 5.0f);
        }
    }

    // ---- carriers into the output bar --------------------------------------
    for (int i = 0; i < kFmOperators; ++i)
    {
        if ((alg.carriers & opBit (i)) == 0 || boxes[(size_t) i].isEmpty())
            continue;

        const auto from = boxes[(size_t) i];
        const auto start = juce::Point<float> ((float) from.getCentreX(), (float) from.getBottom());
        const auto end = juce::Point<float> ((float) from.getCentreX(),
                                             (float) outputBar.getY());

        g.setColour (i == highlighted ? theme::accent : lineColour);
        g.drawLine ({ start, end }, thumbnail ? 1.0f : 1.4f);
        drawArrowHead (g, end, start, thumbnail ? 3.0f : 5.0f);
    }

    // ---- the output bar ----------------------------------------------------
    g.setColour (theme::outline);
    g.fillRoundedRectangle (outputBar.toFloat(), 2.0f);
    g.setColour (theme::dimText);
    g.setFont (theme::font (thumbnail ? 7.0f : 11.0f));
    g.drawText (thumbnail ? juce::String() : NI_TXT ("输出"),
                outputBar, juce::Justification::centred);

    // ---- the operator boxes ------------------------------------------------
    for (int i = 0; i < kFmOperators; ++i)
    {
        const auto box = boxes[(size_t) i];

        if (box.isEmpty())
            continue;

        const auto& op = patch.ops[(size_t) i];
        const auto isHighlighted = i == highlighted;

        auto fill = theme::panelRaised;

        if (! op.enabled)
            fill = theme::panelBackground;
        else if (isHighlighted)
            fill = theme::accentDim;

        g.setColour (fill);
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (isHighlighted ? theme::accent
                                   : (op.enabled ? theme::outline : theme::dimText.withAlpha (0.5f)));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);

        if (thumbnail)
        {
            g.setColour (op.enabled ? theme::text : theme::dimText.withAlpha (0.6f));
            g.setFont (theme::font (juce::jmin (9.5f, (float) box.getHeight() - 1.0f), true));
            g.drawText (juce::String (i + 1), box, juce::Justification::centred);
            continue;
        }

        auto textArea = box.reduced (4, 2);
        juce::String title = "OP" + juce::String (i + 1);

        if (i == kFmOperators - 1 && patch.feedback > 0)
            title += "  " + NI_TXT ("反馈") + juce::String (patch.feedback);

        g.setColour (op.enabled ? theme::text : theme::dimText);
        g.setFont (theme::font (11.0f, true));
        g.drawText (title, textArea.removeFromTop (14), juce::Justification::centredLeft);

        g.setColour (theme::dimText);
        g.setFont (theme::font (9.5f));
        juce::String details = "x" + juce::String (op.ratio, 2)
                                   + "  L" + juce::String (op.outputLevel);

        if (! op.enabled)
            details = NI_TXT ("已关闭") + "  " + details;

        g.drawText (details, textArea.removeFromTop (12), juce::Justification::centredLeft);

        // The switch at the top right of the box.
        const auto switchArea = getOperatorSwitch (i);
        g.setColour (op.enabled ? theme::accent : theme::outline);
        g.fillRoundedRectangle (switchArea.toFloat(), 2.0f);
        g.setColour (theme::windowBackground);

        if (op.enabled)
        {
            const auto knob = switchArea.reduced (2).toFloat();
            g.fillRoundedRectangle (knob, 1.5f);
        }
    }
}

void AlgorithmDiagram::mouseDown (const juce::MouseEvent& e)
{
    if (onDiagramClicked != nullptr)
    {
        onDiagramClicked();
        return;
    }

    for (int i = 0; i < kFmOperators; ++i)
    {
        const auto box = boxes[(size_t) i];

        if (box.isEmpty() || ! box.contains (e.getPosition()))
            continue;

        if (! thumbnail && getOperatorSwitch (i).contains (e.getPosition()))
        {
            if (onOperatorToggled != nullptr)
                onOperatorToggled (i);
        }
        else if (onOperatorClicked != nullptr)
        {
            onOperatorClicked (i);
        }

        return;
    }
}

void AlgorithmDiagram::mouseMove (const juce::MouseEvent& e)
{
    if (thumbnail)
        return;

    for (int i = 0; i < kFmOperators; ++i)
    {
        const auto box = boxes[(size_t) i];

        if (! box.isEmpty() && box.contains (e.getPosition()))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

} // namespace hmdaw
