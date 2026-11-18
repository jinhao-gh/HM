#include "EnvelopeCurve.h"
#include "../Core/FmMath.h"
#include "Theme.h"

#include <array>

namespace hmdaw
{

namespace
{
    /** Matches the engine's level-to-amplitude curve. */
    double levelToAmplitude (double level) noexcept
    {
        const auto n = juce::jlimit (0.0, 99.0, level) / 99.0;
        return n * n;
    }
}

EnvelopeCurve::EnvelopeCurve()
{
    setInterceptsMouseClicks (false, false);
}

void EnvelopeCurve::setOperator (const FmOperator& newOperator)
{
    op = newOperator;
    repaint();
}

void EnvelopeCurve::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

    auto plot = area.reduced (10, 8);
    plot.removeFromBottom (12);   // room for the stage letters

    if (plot.getWidth() < 20 || plot.getHeight() < 20)
        return;

    // Horizontal: the four segments, their widths taken from the rates but
    // squeezed so nothing disappears when one segment is far slower than the rest.
    std::array<double, 4> shares {};
    auto total = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        shares[(size_t) i] = std::sqrt (juce::jmax (0.0005, fm::rateToSeconds (op.rate[i])));
        total += shares[(size_t) i];
    }

    if (total <= 0.0)
        return;

    std::array<float, 5> xs {};
    std::array<double, 4> levels { (double) op.level[0], (double) op.level[1],
                                   (double) op.level[2], (double) op.level[3] };

    xs[0] = (float) plot.getX();

    auto x = (double) plot.getX();

    for (int i = 0; i < 4; ++i)
    {
        x += (double) plot.getWidth() * shares[(size_t) i] / total;
        xs[(size_t) (i + 1)] = (float) x;
    }

    // The envelope starts at silence, rises to L1, and walks down to L4.
    std::array<float, 5> ys {};
    ys[0] = (float) plot.getBottom();

    for (int i = 0; i < 4; ++i)
    {
        const auto amplitude = levelToAmplitude (levels[(size_t) i]);
        ys[(size_t) (i + 1)] = (float) (plot.getBottom() - amplitude * plot.getHeight());
    }

    // ---- the grid ----------------------------------------------------------
    g.setColour (theme::outline.withAlpha (0.5f));

    for (int i = 0; i <= 4; ++i)
        g.drawVerticalLine ((int) xs[(size_t) i], (float) plot.getY(), (float) plot.getBottom());

    for (int i = 0; i <= 4; ++i)
    {
        const auto y = (float) (plot.getBottom() - levelToAmplitude (i * 25.0) * plot.getHeight());
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
    }

    // ---- the curve ---------------------------------------------------------
    juce::Path path;
    path.startNewSubPath (xs[0], ys[0]);

    for (int i = 1; i < 5; ++i)
        path.lineTo (xs[(size_t) i], ys[(size_t) i]);

    g.setColour (op.enabled ? theme::accent : theme::dimText);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    g.setColour (theme::referenceLine.withAlpha (0.35f));

    for (int i = 1; i < 5; ++i)
        g.fillEllipse (xs[(size_t) i] - 3.0f, ys[(size_t) i] - 3.0f, 6.0f, 6.0f);

    // ---- the stage letters -------------------------------------------------
    juce::StringArray names { "R1", "R2", "R3", "R4" };
    g.setFont (theme::font (9.5f));

    for (int i = 0; i < 4; ++i)
    {
        juce::Rectangle<float> label ((float) xs[(size_t) i], (float) plot.getBottom() + 1.0f,
                                      (float) (xs[(size_t) (i + 1)] - xs[(size_t) i]), 12.0f);
        g.setColour (theme::dimText);
        g.drawText (names[i], label, juce::Justification::centred);
    }

    g.setFont (theme::font (9.0f));

    for (int i = 1; i < 5; ++i)
    {
        const auto text = "L" + juce::String (i) + " " + juce::String (op.level[i - 1]);
        g.setColour (theme::text.withAlpha (0.75f));
        g.drawText (text, juce::Rectangle<float> (xs[(size_t) i] - 34.0f,
                                                  ys[(size_t) i] - 13.0f, 32.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    if (! op.enabled)
    {
        g.setColour (theme::dimText);
        g.setFont (theme::font (11.0f, true));
        g.drawText (NI_TXT ("运算器已关闭"), getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace hmdaw
