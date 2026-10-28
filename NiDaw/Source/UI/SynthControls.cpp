#include "SynthControls.h"
#include "Theme.h"

#include <cmath>

namespace nidaw
{

void configureSynthKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 15);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
    slider.setDoubleClickReturnValue (true, slider.getMinimum());
}

void configureVolumeBar (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
}

//==============================================================================
EnvelopeEditorView::EnvelopeEditorView()
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void EnvelopeEditorView::setValues (Values newValues)
{
    values.attack  = juce::jlimit (0.001f, 10.0f, newValues.attack);
    values.decay   = juce::jlimit (0.001f, 10.0f, newValues.decay);
    values.sustain = juce::jlimit (0.0f, 1.0f, newValues.sustain);
    values.release = juce::jlimit (0.001f, 10.0f, newValues.release);
    repaint();
}

void EnvelopeEditorView::setPlayhead (float phase, float value)
{
    playheadPhase = juce::jlimit (0.0f, 1.0f, phase);
    playheadValue = juce::jlimit (0.0f, 1.0f, value);
    repaint();
}

void EnvelopeEditorView::layoutNodes()
{
    auto area = getLocalBounds();
    plot = area.reduced (8, 16).withTrimmedBottom (4);

    if (plot.getWidth() < 8 || plot.getHeight() < 8)
        return;

    const auto a = std::sqrt (juce::jmax (0.0001f, values.attack));
    const auto d = std::sqrt (juce::jmax (0.0001f, values.decay));
    const auto r = std::sqrt (juce::jmax (0.0001f, values.release));
    const auto hold = juce::jmax (0.08f, (a + d + r) * 0.12f);
    const auto total = juce::jmax (0.001f, a + d + hold + r);

    const auto x0 = (float) plot.getX();
    const auto x1 = x0 + (float) plot.getWidth() * a / total;
    const auto x2 = x1 + (float) plot.getWidth() * d / total;
    const auto x3 = x2 + (float) plot.getWidth() * hold / total;
    const auto x4 = (float) plot.getRight();
    const auto yBottom = (float) plot.getBottom();
    const auto yTop = (float) plot.getY();
    const auto ySustain = yBottom - values.sustain * (float) plot.getHeight();

    node0 = { (int) std::round (x0), (int) std::round (yBottom) };
    node1 = { (int) std::round (x1), (int) std::round (yTop) };
    node2 = { (int) std::round (x2), (int) std::round (ySustain) };
    node3 = { (int) std::round (x3), (int) std::round (ySustain) };
    juce::ignoreUnused (x4);
}

void EnvelopeEditorView::paint (juce::Graphics& g)
{
    layoutNodes();
    auto area = getLocalBounds();

    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

    if (plot.getWidth() < 8 || plot.getHeight() < 8)
        return;

    g.setColour (theme::outline.withAlpha (0.45f));

    for (int i = 0; i <= 4; ++i)
    {
        const auto x = plot.getX() + plot.getWidth() * i / 4;
        const auto y = plot.getY() + plot.getHeight() * i / 4;
        g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
        g.drawHorizontalLine (y, (float) plot.getX(), (float) plot.getRight());
    }

    juce::Path path;
    path.startNewSubPath ((float) node0.x, (float) node0.y);
    path.lineTo ((float) node1.x, (float) node1.y);
    path.lineTo ((float) node2.x, (float) node2.y);
    path.lineTo ((float) node3.x, (float) node3.y);
    path.lineTo ((float) plot.getRight(), (float) plot.getBottom());

    juce::Path filled (path);
    filled.lineTo ((float) plot.getRight(), (float) plot.getBottom());
    filled.lineTo ((float) plot.getX(), (float) plot.getBottom());
    filled.closeSubPath();
    g.setColour (theme::accent.withAlpha (0.10f));
    g.fillPath (filled);

    g.setColour (theme::accent);
    g.strokePath (path, juce::PathStrokeType (1.8f));

    for (auto* p : { &node1, &node2, &node3 })
    {
        g.setColour (theme::accent.brighter (0.25f));
        g.fillEllipse ((float) p->x - 4.0f, (float) p->y - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::panelBackground);
        g.drawEllipse ((float) p->x - 4.0f, (float) p->y - 4.0f, 8.0f, 8.0f, 1.0f);
    }

    const auto cursorX = (float) plot.getX() + playheadPhase * (float) plot.getWidth();
    const auto cursorY = (float) plot.getBottom()
                             - playheadValue * (float) plot.getHeight();
    g.setColour (theme::accent.withAlpha (0.55f));
    g.drawVerticalLine ((int) cursorX, (float) plot.getY(), (float) plot.getBottom());
    g.fillEllipse (cursorX - 3.5f, cursorY - 3.5f, 7.0f, 7.0f);

    g.setColour (theme::dimText);
    g.setFont (theme::font (9.5f));
    g.drawText (title.isNotEmpty() ? title : NI_TXT ("音量包络"),
                area.reduced (6, 2).removeFromTop (13),
                juce::Justification::topLeft, false);
}

EnvelopeEditorView::Node EnvelopeEditorView::nodeAt (juce::Point<int> point) const
{
    const auto near = [] (juce::Point<int> a, juce::Point<int> b)
    {
        return juce::Point<int> (a.x - b.x, a.y - b.y).getDistanceFromOrigin() <= 11.0f;
    };

    if (near (point, node1))
        return Node::attack;
    if (near (point, node2))
        return Node::sustain;
    if (near (point, node3))
        return Node::release;
    return Node::none;
}

void EnvelopeEditorView::beginGesture (Node node, juce::Point<int> point)
{
    dragging = node;
    dragStart = point;
    gestureStartValues = values;
    gestureOpen = true;

    if (onGesture)
        onGesture (true, values);
}

void EnvelopeEditorView::updateGesture (juce::Point<int> point)
{
    if (dragging == Node::none)
        return;

    const auto dx = (float) (point.x - dragStart.x) / juce::jmax (1.0f, (float) plot.getWidth());
    const auto dy = (float) (point.y - dragStart.y) / juce::jmax (1.0f, (float) plot.getHeight());

    switch (dragging)
    {
        case Node::attack:
            values.attack = juce::jlimit (0.001f, 10.0f,
                                          gestureStartValues.attack * std::exp (dx * 3.5f));
            break;
        case Node::sustain:
            values.decay = juce::jlimit (0.001f, 10.0f,
                                         gestureStartValues.decay * std::exp (dx * 3.5f));
            values.sustain = juce::jlimit (0.0f, 1.0f, gestureStartValues.sustain - dy);
            break;
        case Node::release:
            values.release = juce::jlimit (0.001f, 10.0f,
                                           gestureStartValues.release * std::exp (dx * 3.5f));
            break;
        case Node::none:
            break;
    }

    repaint();

    if (onGesture)
        onGesture (false, values);
}

void EnvelopeEditorView::mouseDown (const juce::MouseEvent& e)
{
    layoutNodes();
    const auto node = nodeAt (e.getPosition());

    if (node != Node::none)
        beginGesture (node, e.getPosition());
}

void EnvelopeEditorView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging != Node::none)
        updateGesture (e.getPosition());
}

void EnvelopeEditorView::mouseUp (const juce::MouseEvent&)
{
    dragging = Node::none;
    gestureOpen = false;
}

void EnvelopeEditorView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onGesture)
        onGesture (true, values);

    values = {};
    repaint();

    if (onGesture)
        onGesture (false, values);
}

//==============================================================================
FilterResponseView::FilterResponseView()
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void FilterResponseView::setValues (Values newValues)
{
    values.mode      = newValues.mode;
    values.slopeDb   = newValues.slopeDb >= 24 ? 24 : 12;
    values.cutoffHz  = juce::jlimit (20.0f, 20000.0f, newValues.cutoffHz);
    values.resonance = juce::jlimit (0.1f, 20.0f, newValues.resonance);
    values.envAmount = juce::jlimit (-1.0f, 1.0f, newValues.envAmount);
    values.keyTrack  = juce::jlimit (0.0f, 1.0f, newValues.keyTrack);
    repaint();
}

double FilterResponseView::magnitudeAtHz (const Values& v, double hz) noexcept
{
    const auto cutoff = juce::jlimit (20.0, 20000.0, (double) v.cutoffHz);
    const auto q = juce::jlimit (0.1, 20.0, (double) v.resonance);
    const auto r = hz / cutoff;
    const auto denom = std::sqrt (std::pow (1.0 - r * r, 2.0) + std::pow (r / q, 2.0));
    auto mag = 1.0 / juce::jmax (1.0e-6, denom);

    switch (v.mode)
    {
        case FilterMode::bandPass: mag = (r / q) / juce::jmax (1.0e-6, denom); break;
        case FilterMode::highPass: mag = (r * r) / juce::jmax (1.0e-6, denom); break;
        case FilterMode::notch:
            mag = std::abs (1.0 - r * r) / juce::jmax (1.0e-6, denom);
            break;
        case FilterMode::comb:
            mag = 0.35 + 0.65 * std::abs (std::cos (juce::MathConstants<double>::pi * r));
            break;
        case FilterMode::lowPass:  break;
    }

    return v.slopeDb >= 24 ? mag * mag : mag;
}

void FilterResponseView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme::panelRaised.withAlpha (0.7f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

    plot = area.reduced (8, 16).withTrimmedBottom (2);

    if (plot.getWidth() < 8 || plot.getHeight() < 8)
        return;

    g.setColour (theme::outline.withAlpha (0.4f));

    for (int i = 0; i <= 4; ++i)
    {
        const auto x = plot.getX() + plot.getWidth() * i / 4;
        g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
    }

    g.drawHorizontalLine (plot.getBottom(), (float) plot.getX(), (float) plot.getRight());

    juce::Path path;
    const auto columns = juce::jmax (2, plot.getWidth());

    for (int x = 0; x < columns; ++x)
    {
        const auto hz = 20.0 * std::pow (1000.0, (double) x / (double) (columns - 1));
        const auto mag = juce::jlimit (0.0, 1.0, magnitudeAtHz (values, hz));
        const auto px = (float) plot.getX() + (float) x;
        const auto py = (float) plot.getBottom() - (float) mag * (float) plot.getHeight();

        if (x == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }

    g.setColour (theme::accent);
    g.strokePath (path, juce::PathStrokeType (1.8f));

    const auto peakHz = juce::jlimit (20.0f, 20000.0f,
                                      values.cutoffHz * std::pow (2.0f, values.envAmount * 8.0f));
    const auto peakX = (float) plot.getX()
                           + (float) (std::log ((double) peakHz / 20.0) / std::log (1000.0))
                                 * (float) plot.getWidth();
    g.setColour (theme::accent.withAlpha (0.5f));
    g.drawVerticalLine ((int) peakX, (float) plot.getY(), (float) plot.getBottom());

    g.setColour (theme::dimText);
    g.setFont (theme::font (9.5f));
    g.drawText (NI_TXT ("滤波器 · 拖动截止/共振 · Shift 包络量"),
                area.reduced (6, 2).removeFromTop (13),
                juce::Justification::topLeft, false);
}

void FilterResponseView::beginGesture (juce::Point<int> point, bool shift)
{
    dragging = true;

    if (onGesture)
        onGesture (true, values);

    updateFromPoint (point, shift);
}

void FilterResponseView::updateFromPoint (juce::Point<int> point, bool shift)
{
    if (plot.getWidth() < 8 || plot.getHeight() < 8)
        return;

    const auto x = juce::jlimit (0.0f, 1.0f,
                                 (float) (point.x - plot.getX()) / (float) plot.getWidth());
    const auto y = juce::jlimit (0.0f, 1.0f,
                                 (float) (plot.getBottom() - point.y) / (float) plot.getHeight());

    if (shift)
    {
        values.envAmount = juce::jlimit (-1.0f, 1.0f, y * 2.0f - 1.0f);
    }
    else
    {
        values.cutoffHz = (float) juce::jlimit (20.0, 20000.0,
                                                20.0 * std::pow (1000.0, (double) x));
        values.resonance = (float) juce::jlimit (0.1, 20.0,
                                                 0.1 * std::pow (200.0, (double) y));
    }

    repaint();

    if (onGesture)
        onGesture (false, values);
}

void FilterResponseView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick != nullptr)
            onRightClick();
        return;
    }

    if (plot.contains (e.getPosition()))
        beginGesture (e.getPosition(), e.mods.isShiftDown());
}

void FilterResponseView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        updateFromPoint (e.getPosition(), e.mods.isShiftDown());
}

void FilterResponseView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void FilterResponseView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onGesture)
        onGesture (true, values);

    values.cutoffHz = 2000.0f;
    values.resonance = 0.707f;
    values.envAmount = 0.0f;
    repaint();

    if (onGesture)
        onGesture (false, values);
}

void FilterResponseView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! onGesture)
        return;

    onGesture (true, values);
    values.cutoffHz = juce::jlimit (20.0f, 20000.0f,
                                    values.cutoffHz * std::pow (2.0f, wheel.deltaY * 0.25f));
    repaint();
    onGesture (false, values);
}

LfoEditorView::LfoEditorView()
{
    setInterceptsMouseClicks (false, false);
}

void LfoEditorView::setValues (int newWaveform, float newPhase, float newValue)
{
    waveform = juce::jlimit (0, 4, newWaveform);
    phase = juce::jlimit (0.0f, 1.0f, newPhase);
    value = juce::jlimit (-1.0f, 1.0f, newValue);
    repaint();
}

void LfoEditorView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.toFloat(), 5.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);

    auto plot = area.reduced (8, 10);

    if (plot.getWidth() < 4 || plot.getHeight() < 4)
        return;

    g.setColour (theme::outline.withAlpha (0.4f));
    g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());

    juce::Path path;
    const auto columns = juce::jmax (2, plot.getWidth());

    for (int x = 0; x < columns; ++x)
    {
        const auto p = (double) x / (double) (columns - 1);
        auto v = 0.0;

        switch (waveform)
        {
            case 1: v = std::sin (juce::MathConstants<double>::twoPi * p); break;
            case 2: v = p < 0.5 ? 1.0 : -1.0; break;
            case 3: v = 2.0 * p - 1.0; break;
            case 4: v = std::sin (p * 173.0) > 0.0 ? 1.0 : -1.0; break;
            default: v = 4.0 * std::abs (p - 0.5) - 1.0; break;
        }

        const auto px = (float) plot.getX() + (float) x;
        const auto py = (float) plot.getCentreY()
                            - (float) v * (float) plot.getHeight() * 0.42f;
        if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
    }

    g.setColour (theme::accent);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    const auto cursorX = (float) plot.getX() + phase * (float) plot.getWidth();
    const auto cursorY = (float) plot.getCentreY()
                             - value * (float) plot.getHeight() * 0.42f;
    g.setColour (theme::accent.withAlpha (0.7f));
    g.drawVerticalLine ((int) cursorX, (float) plot.getY(), (float) plot.getBottom());
    g.fillEllipse (cursorX - 3.0f, cursorY - 3.0f, 6.0f, 6.0f);
}

} // namespace nidaw
