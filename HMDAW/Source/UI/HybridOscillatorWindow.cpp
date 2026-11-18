#include "HybridOscillatorWindow.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"
#include "../Core/WavetableTables.h"
#include "../Core/Wavetables.h"

#include <algorithm>
#include <cmath>

namespace hmdaw
{

HybridOscillatorThumbnail::HybridOscillatorThumbnail (ProjectDocument& d, AudioEngine& a)
    : document (d), audio (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HybridOscillatorThumbnail::setSource (juce::int64 newTrackId, int sourceIndex)
{
    trackId = newTrackId;
    source = juce::jlimit (0, 4, sourceIndex);
    repaint();
}

void HybridOscillatorThumbnail::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    lastActiveFrame = -1;
    lastThreeDExtent = {};
    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

    auto* track = document.findTrack (trackId);

    if (track == nullptr || track->kind != TrackKind::hybrid)
        return;

    auto plot = area.reduced (5, 4);
    g.setColour (theme::outline.withAlpha (0.4f));
    g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());

    if (source < 3)
    {
        const auto& l = track->hybrid.layers[(size_t) source];
        if (l.mode == HybridLayerMode::wavetable)
        {
            wavetable::ensureBuilt();
            const auto customId = l.wavetable.oscs[0].customTableId;
            auto custom = customId != 0
                              ? wavetabletables::playableTableFor (document.getProject(), customId)
                              : nullptr;
            const auto& table = custom != nullptr ? *custom
                                                  : wavetable::getTable (l.wavetable.oscs[0].tableIndex);
            const auto livePosition = audio.getHybridActiveVoices (trackId) > 0
                                          ? (double) audio.getHybridWavetablePosition (trackId,
                                                                                       source)
                                          : (double) l.wavetable.oscs[0].position;

            if (! threeD)
            {
                juce::Path path;
                const auto columns = juce::jmax (2, plot.getWidth());

                for (int x = 0; x < columns; ++x)
                {
                    const auto phase = (double) x / columns;
                    const auto v = WavetableSynth::sampleTable (
                        table, 0, phase, livePosition,
                        l.wavetable.oscs[0].positionDiscrete);
                    const auto px = (float) plot.getX() + (float) x;
                    const auto py = (float) plot.getCentreY()
                                        - (float) juce::jlimit (-1.0, 1.0, v)
                                              * (float) plot.getHeight() * 0.42f;
                    if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
                }

                g.setColour (theme::accent);
                g.strokePath (path, juce::PathStrokeType (1.6f));
            }
            else
            {
                // A compact stack: each slice keeps its own one-cycle waveform,
                // and the far ones step up and left so the whole depth axis stays
                // inside the thumbnail instead of running off its corners.
                const auto frames = juce::jlimit (1, 8, juce::jmax (1, table.frameCount));
                const auto sliceWidth = (float) plot.getWidth() * 0.42f;
                const auto sliceHeight = (float) plot.getHeight() * 0.38f;
                const auto amplitude = sliceHeight * 0.45f;
                const auto inset = 5.0f;
                const auto activeFrame = juce::jlimit (
                    0, frames - 1, (int) std::round (livePosition * (frames - 1)));
                lastActiveFrame = activeFrame;
                juce::Rectangle<float> extent;

                for (int frame = frames - 1; frame >= 0; --frame)
                {
                    const auto depth = frames > 1 ? (float) frame / (float) (frames - 1)
                                                  : 0.0f;
                    const auto originX = (float) plot.getRight() - inset - sliceWidth
                                             - depth * juce::jmax (
                                                   0.0f, (float) plot.getWidth() - sliceWidth
                                                             - inset * 2.0f);
                    const auto originY = (float) plot.getBottom() - inset - amplitude
                                             - depth * ((float) plot.getHeight() * 0.58f);
                    juce::Path path;
                    const auto columns = juce::jmax (2, (int) sliceWidth);
                    const auto framePosition = frames > 1
                                                   ? (double) frame / (double) (frames - 1)
                                                   : 0.0;

                    for (int x = 0; x < columns; ++x)
                    {
                        const auto phase = (double) x / (double) columns;
                        const auto v = WavetableSynth::sampleTable (
                            table, 0, phase, framePosition,
                            l.wavetable.oscs[0].positionDiscrete);
                        const auto px = originX + (float) x;
                        const auto py = originY - (float) juce::jlimit (-1.0, 1.0, v)
                                                     * amplitude;
                        if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
                    }

                    // Close down to the slice's own baseline, so every frame reads
                    // as one thin card rather than a floating scribble.
                    path.lineTo (originX + sliceWidth, originY);
                    path.closeSubPath();
                    const auto sliceBounds = path.getBounds();
                    extent = extent.isEmpty() ? sliceBounds : extent.getUnion (sliceBounds);

                    if (frame == activeFrame)
                    {
                        g.setColour (theme::accent.withAlpha (0.12f));
                        g.fillPath (path);
                        g.setColour (theme::accent);
                        g.strokePath (path, juce::PathStrokeType (1.8f));
                    }
                    else
                    {
                        g.setColour (theme::referenceLine.withAlpha (0.05f));
                        g.fillPath (path);
                        g.setColour (theme::referenceLine.withAlpha (0.36f));
                        g.strokePath (path, juce::PathStrokeType (1.0f));
                    }
                }

                lastThreeDExtent = extent;

                g.setColour (theme::accent);
                g.setFont (theme::font (8.5f, true));
                g.drawText (juce::String (juce::roundToInt (livePosition * 100.0)) + "%",
                            area.reduced (4, 1).removeFromBottom (10),
                            juce::Justification::centredLeft, false);
            }

            g.setColour (theme::dimText);
            g.setFont (theme::font (8.0f));
            g.drawText (threeD ? NI_TXT ("点击切回 2D") : NI_TXT ("点击看 3D"),
                        area.reduced (4, 1).removeFromBottom (10),
                        juce::Justification::centredRight, false);
        }
        else
        {
            const auto& path = l.mode == HybridLayerMode::sample ? l.sample.filePath
                                                                 : l.granular.filePath;
            juce::String error;
            auto sample = path.isNotEmpty() ? getCachedSample (path, error) : nullptr;
            if (sample != nullptr && ! sample->peaks.empty())
            {
                const auto columns = juce::jmax (1, plot.getWidth());
                for (int x = 0; x < columns; ++x)
                {
                    const auto index = juce::jlimit (0, (int) sample->peaks.size() - 1,
                                                     (int) ((juce::int64) x * sample->peaks.size() / columns));
                    const auto h = juce::jmax (1.0f, sample->peaks[(size_t) index]
                                                          * (float) plot.getHeight() * 0.45f);
                    g.setColour (theme::referenceLine);
                    g.fillRect ((float) plot.getX() + (float) x,
                                (float) plot.getCentreY() - h, 1.0f, h * 2.0f);
                }
            }
            else
            {
                g.setColour (theme::dimText);
                g.setFont (theme::font (9.0f));
                g.drawText (NI_TXT ("未载入音频"), plot, juce::Justification::centred);
            }
        }
    }
    else
    {
        juce::Path path;
        const auto columns = juce::jmax (2, plot.getWidth());
        for (int x = 0; x < columns; ++x)
        {
            const auto p = (double) x / columns;
            auto v = 0.0;
            if (source == 3)
                v = track->hybrid.sub.waveform == 0 ? std::sin (p * juce::MathConstants<double>::twoPi)
                  : track->hybrid.sub.waveform == 1 ? (4.0 * std::abs (p - 0.5) - 1.0)
                                                   : (p < 0.5 ? 1.0 : -1.0);
            else
                v = std::sin (p * 67.0) * 0.45 + std::sin (p * 191.0) * 0.25;
            const auto px = (float) plot.getX() + (float) x;
            const auto py = (float) plot.getCentreY()
                                - (float) v * (float) plot.getHeight() * 0.4f;
            if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }
        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }
}

void HybridOscillatorThumbnail::mouseUp (const juce::MouseEvent&)
{
    auto* track = document.findTrack (trackId);

    if (track != nullptr && track->kind == TrackKind::hybrid && source < 3
        && track->hybrid.layers[(size_t) source].mode == HybridLayerMode::wavetable)
    {
        threeD = ! threeD;
        repaint();
    }
}

HybridOscillatorPanel::HybridOscillatorPanel (ProjectDocument& d, AudioEngine& a)
    : document (d), audio (a)
{
    addAndMakeVisible (title);
    title.setFont (theme::font (13.0f, true));
    title.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (mode);
    mode.setFont (theme::font (11.0f));
    mode.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (back);
    back.setButtonText (NI_TXT ("返回"));
    back.onClick = [this] { if (onBack != nullptr) onBack(); };
    addAndMakeVisible (visual);

    const char* names[] = { "参数 1", "参数 2", "参数 3", "参数 4",
                            "参数 5", "参数 6", "参数 7" };
    for (int i = 0; i < (int) knobs.size(); ++i)
        configureKnob (knobs[(size_t) i], NI_TXT (names[i]), 0.0, 1.0);
}

InstrumentTrack* HybridOscillatorPanel::track()
{
    auto* t = document.findTrack (trackId);
    return t != nullptr && t->kind == TrackKind::hybrid ? t : nullptr;
}

void HybridOscillatorPanel::configureKnob (Knob& k, const juce::String& text,
                                           double lo, double hi)
{
    addAndMakeVisible (k.label);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setFont (theme::font (9.5f));
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (k.slider);
    configureSynthKnob (k.slider);
    k.slider.setRange (lo, hi, 0.001);
    k.slider.onDragStart = [this] { document.beginEdit (NI_TXT ("调整振荡器")); };
    k.slider.onValueChange = [this] { push(); };
}

void HybridOscillatorPanel::setSource (juce::int64 newTrackId, int sourceIndex)
{
    trackId = newTrackId;
    source = juce::jlimit (0, 4, sourceIndex);
    visual.threeD = false;
    refresh();
}

void HybridOscillatorPanel::setModeLabel()
{
    auto* t = track();

    if (t == nullptr)
        return;

    if (source < 3)
    {
        const auto& l = t->hybrid.layers[(size_t) source];
        title.setText (NI_TXT ("振荡器 ") + juce::String (source + 1), juce::dontSendNotification);
        mode.setText (l.mode == HybridLayerMode::wavetable ? NI_TXT ("波表模式")
                     : l.mode == HybridLayerMode::sample ? NI_TXT ("单采样模式")
                     : NI_TXT ("粒子模式"), juce::dontSendNotification);
    }
    else if (source == 3)
    {
        title.setText ("Sub", juce::dontSendNotification);
        mode.setText (NI_TXT ("独立低音振荡器"), juce::dontSendNotification);
    }
    else
    {
        title.setText (NI_TXT ("噪声"), juce::dontSendNotification);
        mode.setText (NI_TXT ("独立噪声源"), juce::dontSendNotification);
    }
}

void HybridOscillatorPanel::refresh()
{
    auto* t = track();

    if (t == nullptr)
        return;

    updating = true;
    setModeLabel();
    for (auto& k : knobs)
    {
        k.label.setVisible (false);
        k.slider.setVisible (false);
    }

    auto show = [this] (int i, const juce::String& label, double value,
                        double lo, double hi)
    {
        auto& k = knobs[(size_t) i];
        k.label.setText (label, juce::dontSendNotification);
        k.slider.setRange (lo, hi, 0.001);
        k.slider.setValue (value, juce::dontSendNotification);
        k.label.setVisible (true);
        k.slider.setVisible (true);
    };

    if (source < 3)
    {
        const auto& l = t->hybrid.layers[(size_t) source];
        if (l.mode == HybridLayerMode::wavetable)
        {
            show (0, NI_TXT ("齐奏"), l.wavetable.oscs[0].unisonVoices, 1.0,
                  kMaxWavetableUnisonVoices);
            show (1, NI_TXT ("齐奏失谐"), l.wavetable.oscs[0].unisonDetuneCents,
                  0.0, 100.0);
            show (2, NI_TXT ("位置"), l.wavetable.oscs[0].position, 0.0, 1.0);
            show (3, NI_TXT ("八度"), l.wavetable.oscs[0].octave, -2, 2);
            show (4, NI_TXT ("半音"), l.wavetable.oscs[0].semitone, -12, 12);
            show (5, NI_TXT ("失谐"), l.wavetable.oscs[0].detuneCents, -50, 50);
            show (6, NI_TXT ("电平"), l.wavetable.oscs[0].level, 0.0, 1.0);
        }
        else if (l.mode == HybridLayerMode::sample)
        {
            show (0, NI_TXT ("根音"), l.sample.rootHz, 20.0, 8000.0);
            show (1, NI_TXT ("起点"), l.sample.sampleStart, 0.0, 1.0);
            show (2, NI_TXT ("终点"), l.sample.sampleEnd, 0.0, 1.0);
            show (3, NI_TXT ("循环起点"), l.sample.loopStart, 0.0, 1.0);
            show (4, NI_TXT ("循环终点"), l.sample.loopEnd, 0.0, 1.0);
            show (5, NI_TXT ("交叉淡化"), l.sample.crossfade, 0.0, 1.0);
            show (6, NI_TXT ("增益"), l.sample.gain, 0.0, 1.0);
            show (7, NI_TXT ("截止"), l.sample.filterCutoffHz, 20.0, 20000.0);
            show (8, NI_TXT ("共振"), l.sample.filterResonance, 0.1, 20.0);
        }
        else
        {
            show (0, NI_TXT ("位置"), l.granular.position, 0.0, 1.0);
            show (1, NI_TXT ("扫描"), l.granular.scanSpeed, -2.0, 2.0);
            show (2, NI_TXT ("粒度"), l.granular.grainMs, 5.0, 500.0);
            show (3, NI_TXT ("密度"), l.granular.densityHz, 1.0, 100.0);
            show (4, NI_TXT ("抖动"), l.granular.spray, 0.0, 1.0);
            show (5, NI_TXT ("增益"), l.granular.gain, 0.0, 1.0);
        }
    }
    else if (source == 3)
    {
        show (0, NI_TXT ("八度"), t->hybrid.sub.octave, -2, -1);
        show (1, NI_TXT ("音量"), t->hybrid.sub.level, 0.0, 1.0);
        show (2, NI_TXT ("声相"), t->hybrid.sub.pan, -1.0, 1.0);
    }
    else
    {
        show (0, NI_TXT ("音量"), t->hybrid.noise.level, 0.0, 1.0);
        show (1, NI_TXT ("声相"), t->hybrid.noise.pan, -1.0, 1.0);
        show (2, NI_TXT ("Tone"), t->hybrid.noise.toneHz, 20.0, 20000.0);
    }

    updating = false;
    resized();
    visual.repaint();
}

void HybridOscillatorPanel::push()
{
    if (updating)
        return;

    auto* t = track();

    if (t == nullptr)
        return;

    if (source < 3)
    {
        auto& l = t->hybrid.layers[(size_t) source];
        if (l.mode == HybridLayerMode::wavetable)
        {
            l.wavetable.oscs[0].unisonVoices = juce::jlimit (
                1, kMaxWavetableUnisonVoices, (int) std::round (knobs[0].slider.getValue()));
            l.wavetable.oscs[0].unisonDetuneCents = (float) knobs[1].slider.getValue();
            l.wavetable.oscs[0].position = (float) knobs[2].slider.getValue();
            l.wavetable.oscs[0].octave = (int) knobs[3].slider.getValue();
            l.wavetable.oscs[0].semitone = (int) knobs[4].slider.getValue();
            l.wavetable.oscs[0].detuneCents = (float) knobs[5].slider.getValue();
            l.wavetable.oscs[0].level = (float) knobs[6].slider.getValue();
            l.wavetable.oscs[1].enabled = false;
        }
        else if (l.mode == HybridLayerMode::sample)
        {
            l.sample.rootHz = knobs[0].slider.getValue();
            l.sample.sampleStart = juce::jlimit (0.0, 0.999,
                                                 knobs[1].slider.getValue());
            l.sample.sampleEnd = juce::jlimit (l.sample.sampleStart + 0.001, 1.0,
                                               knobs[2].slider.getValue());
            l.sample.loopStart = juce::jlimit (l.sample.sampleStart,
                                               l.sample.sampleEnd - 0.001,
                                               knobs[3].slider.getValue());
            l.sample.loopEnd = juce::jlimit (l.sample.loopStart + 0.001,
                                             l.sample.sampleEnd,
                                             knobs[4].slider.getValue());
            l.sample.crossfade = juce::jlimit (0.0, 1.0, knobs[5].slider.getValue());
            l.sample.gain = (float) knobs[6].slider.getValue();
            l.sample.filterCutoffHz = (float) knobs[7].slider.getValue();
            l.sample.filterResonance = (float) knobs[8].slider.getValue();
        }
        else
        {
            l.granular.position = (float) knobs[0].slider.getValue();
            l.granular.scanSpeed = (float) knobs[1].slider.getValue();
            l.granular.grainMs = (float) knobs[2].slider.getValue();
            l.granular.densityHz = (float) knobs[3].slider.getValue();
            l.granular.spray = (float) knobs[4].slider.getValue();
            l.granular.gain = (float) knobs[5].slider.getValue();
        }
    }
    else if (source == 3)
    {
        t->hybrid.sub.octave = (int) knobs[0].slider.getValue();
        t->hybrid.sub.level = (float) knobs[1].slider.getValue();
        t->hybrid.sub.pan = (float) knobs[2].slider.getValue();
    }
    else
    {
        t->hybrid.noise.level = (float) knobs[0].slider.getValue();
        t->hybrid.noise.pan = (float) knobs[1].slider.getValue();
        t->hybrid.noise.toneHz = (float) knobs[2].slider.getValue();
    }

    document.notifyEdit();
    visual.repaint();
}

void HybridOscillatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);
}

void HybridOscillatorPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto header = area.removeFromTop (30);
    back.setBounds (header.removeFromLeft (58));
    header.removeFromLeft (6);
    title.setBounds (header.removeFromLeft (220));
    mode.setBounds (header);
    visual.setBounds (area.removeFromTop (300));
    area.removeFromTop (8);

    const auto count = (int) std::count_if (knobs.begin(), knobs.end(),
                                            [] (const Knob& k) { return k.slider.isVisible(); });
    const auto w = count > 0 ? area.getWidth() / count : area.getWidth();

    for (auto& k : knobs)
    {
        if (! k.slider.isVisible())
            continue;

        auto cell = area.removeFromLeft (w).reduced (3);
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    }
}

void HybridOscillatorPanel::Visual::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme::gridBackground);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);

    auto* t = panel.track();

    if (t == nullptr)
        return;

    auto plot = area.reduced (14);
    g.setColour (theme::outline.withAlpha (0.45f));
    g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());

    if (panel.source < 3)
    {
        const auto& l = t->hybrid.layers[(size_t) panel.source];
        if (l.mode == HybridLayerMode::wavetable)
        {
            wavetable::ensureBuilt();
            const auto customId = l.wavetable.oscs[0].customTableId;
            auto custom = customId != 0
                              ? wavetabletables::playableTableFor (panel.document.getProject(), customId)
                              : nullptr;
            const auto& table = custom != nullptr ? *custom
                                                  : wavetable::getTable (l.wavetable.oscs[0].tableIndex);
            const auto position = l.wavetable.oscs[0].position;
            const auto frames = threeD ? juce::jmax (1, table.frameCount) : 1;
            const auto columns = juce::jmax (2, plot.getWidth());
            for (int f = frames - 1; f >= 0; --f)
            {
                juce::Path path;
                const auto yc = (float) plot.getCentreY() - (float) f * 7.0f;
                for (int x = 0; x < columns; ++x)
                {
                    const auto phase = (double) x / columns;
                    const auto value = WavetableSynth::sampleTable (table, 0, phase,
                                                                    threeD ? (double) f / juce::jmax (1, frames - 1)
                                                                           : position,
                                                                    l.wavetable.oscs[0].positionDiscrete);
                    const auto px = (float) plot.getX() + (float) x;
                    const auto py = yc - (float) value * (float) plot.getHeight() * 0.28f;
                    if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
                }
                g.setColour (f == 0 ? theme::accent : theme::referenceLine.withAlpha (0.6f));
                g.strokePath (path, juce::PathStrokeType (f == 0 ? 2.0f : 1.2f));
            }
        }
        else if (l.mode == HybridLayerMode::sample)
        {
            juce::String error;
            auto sample = l.sample.filePath.isNotEmpty()
                              ? getCachedSample (l.sample.filePath, error) : nullptr;

            if (sample == nullptr || sample->peaks.empty())
            {
                g.setColour (theme::dimText);
                g.setFont (theme::font (13.0f));
                g.drawText (NI_TXT ("还没有载入音频文件"), plot,
                            juce::Justification::centred);
            }
            else
            {
                const auto columns = juce::jmax (2, plot.getWidth());

                for (int x = 0; x < columns; ++x)
                {
                    const auto fraction = (double) x / (double) (columns - 1);
                    const auto bucket = waveformBucketFor (
                        fraction * sample->lengthSeconds(), sample->lengthSeconds(),
                        (int) sample->peaks.size());

                    if (bucket < 0)
                        continue;

                    const auto peak = juce::jlimit (0.0f, 1.0f, sample->peaks[(size_t) bucket]);
                    const auto px = (float) plot.getX() + (float) x;
                    const auto half = peak * (float) plot.getHeight() * 0.42f;
                    g.setColour (theme::referenceLine.withAlpha (0.65f));
                    g.drawVerticalLine ((int) px,
                                        (float) plot.getCentreY() - half,
                                        (float) plot.getCentreY() + half);
                }

                auto xFor = [&] (double position)
                {
                    return (float) plot.getX()
                               + (float) juce::jlimit (0.0, 1.0, position) * (float) plot.getWidth();
                };
                const auto startX = xFor (l.sample.sampleStart);
                const auto endX = xFor (l.sample.sampleEnd);
                const auto loopStartX = xFor (l.sample.loopStart);
                const auto loopEndX = xFor (l.sample.loopEnd);
                const auto crossX = loopEndX - (loopEndX - loopStartX)
                                                  * (float) l.sample.crossfade;

                g.setColour (theme::outline);
                g.drawLine (startX, (float) plot.getY(), startX, (float) plot.getBottom(), 1.0f);
                g.drawLine (endX, (float) plot.getY(), endX, (float) plot.getBottom(), 1.0f);
                g.setColour (theme::accent.withAlpha (0.12f));
                g.fillRect (juce::Rectangle<float> (crossX, (float) plot.getY(),
                                                    loopEndX - crossX, (float) plot.getHeight()));
                g.setColour (theme::accent);
                g.drawLine (loopStartX, (float) plot.getY(), loopStartX,
                            (float) plot.getBottom(), 1.5f);
                g.drawLine (loopEndX, (float) plot.getY(), loopEndX,
                            (float) plot.getBottom(), 1.5f);
            }
        }
        else
        {
            juce::String error;
            auto sample = l.granular.filePath.isNotEmpty()
                              ? getCachedSample (l.granular.filePath, error) : nullptr;

            if (sample == nullptr || sample->peaks.empty())
            {
                g.setColour (theme::dimText);
                g.setFont (theme::font (13.0f));
                g.drawText (NI_TXT ("还没有载入音频文件"), plot,
                            juce::Justification::centred);
            }
            else
            {
                const auto columns = juce::jmax (2, plot.getWidth());

                for (int x = 0; x < columns; ++x)
                {
                    const auto fraction = (double) x / (double) (columns - 1);
                    const auto bucket = waveformBucketFor (
                        fraction * sample->lengthSeconds(), sample->lengthSeconds(),
                        (int) sample->peaks.size());

                    if (bucket < 0)
                        continue;

                    const auto peak = juce::jlimit (0.0f, 1.0f, sample->peaks[(size_t) bucket]);
                    const auto px = (float) plot.getX() + (float) x;
                    const auto half = peak * (float) plot.getHeight() * 0.42f;
                    g.setColour (theme::referenceLine.withAlpha (0.35f));
                    g.drawVerticalLine ((int) px,
                                        (float) plot.getCentreY() - half,
                                        (float) plot.getCentreY() + half);
                }

                const auto grains = panel.audio.getHybridGranularGrainCount (panel.trackId,
                                                                             panel.source);
                g.setColour (theme::accent);

                for (int i = 0; i < grains; ++i)
                {
                    const auto position = panel.audio.getHybridGranularGrainPosition (
                        panel.trackId, panel.source, i);
                    const auto age = panel.audio.getHybridGranularGrainAge (
                        panel.trackId, panel.source, i);
                    const auto x = (float) plot.getX()
                                       + juce::jlimit (0.0f, 1.0f, position)
                                             * (float) plot.getWidth();
                    const auto y = (float) plot.getBottom()
                                       - juce::jlimit (0.0f, 1.0f, age)
                                             * (float) plot.getHeight();
                    g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
                }
            }
        }
    }
    else
    {
        juce::Path path;
        const auto columns = juce::jmax (2, plot.getWidth());
        for (int x = 0; x < columns; ++x)
        {
            const auto p = (double) x / columns;
            auto v = panel.source == 3
                         ? (t->hybrid.sub.waveform == 0 ? std::sin (p * juce::MathConstants<double>::pi)
                            : t->hybrid.sub.waveform == 1 ? (4.0 * std::abs (p - 0.5) - 1.0)
                                                         : (p < 0.5 ? 1.0 : -1.0))
                         : (std::sin (p * 71.0) * 0.4 + std::sin (p * 193.0) * 0.3);
            const auto px = (float) plot.getX() + (float) x;
            const auto py = (float) plot.getCentreY() - (float) v * (float) plot.getHeight() * 0.35f;
            if (x == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }
        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.0f));
    g.drawText (threeD ? NI_TXT ("多帧立体波表 · 点击切回平面波形")
                       : NI_TXT ("当前位置波形 · 点击切换立体波表"),
                area.reduced (8, 4).removeFromBottom (16),
                juce::Justification::centredLeft, false);
}

void HybridOscillatorPanel::Visual::mouseUp (const juce::MouseEvent&)
{
    if (dragging != DragHandle::none)
    {
        dragging = DragHandle::none;
        panel.refresh();
        return;
    }

    if (panel.source < 3
        && panel.track() != nullptr
        && panel.track()->hybrid.layers[(size_t) panel.source].mode == HybridLayerMode::wavetable)
    {
        threeD = ! threeD;
        repaint();
    }
}

juce::Rectangle<int> HybridOscillatorPanel::Visual::samplePlot() const
{
    return getLocalBounds().reduced (14);
}

HybridOscillatorPanel::Visual::DragHandle
HybridOscillatorPanel::Visual::handleAt (juce::Point<int> point) const
{
    auto* t = panel.track();

    if (t == nullptr || panel.source >= 3)
        return DragHandle::none;

    const auto& l = t->hybrid.layers[(size_t) panel.source];

    if (l.mode != HybridLayerMode::sample)
        return DragHandle::none;

    const auto plot = samplePlot();

    if (! plot.contains (point))
        return DragHandle::none;

    auto xFor = [&] (double position)
    {
        return (float) plot.getX()
                   + (float) juce::jlimit (0.0, 1.0, position) * (float) plot.getWidth();
    };

    struct Candidate { DragHandle handle; float x; };
    const Candidate candidates[] = {
        { DragHandle::start, xFor (l.sample.sampleStart) },
        { DragHandle::end, xFor (l.sample.sampleEnd) },
        { DragHandle::loopStart, xFor (l.sample.loopStart) },
        { DragHandle::loopEnd, xFor (l.sample.loopEnd) },
        { DragHandle::crossfade,
          xFor (l.sample.loopEnd - (l.sample.loopEnd - l.sample.loopStart)
                                       * l.sample.crossfade) }
    };

    auto best = DragHandle::none;
    auto bestDistance = 12.0f;

    for (const auto& candidate : candidates)
    {
        const auto distance = std::abs ((float) point.x - candidate.x);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate.handle;
        }
    }

    return best;
}

void HybridOscillatorPanel::Visual::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.getPosition());

    if (dragging != DragHandle::none)
        panel.document.beginEdit (NI_TXT ("拖动采样区间"));
}

void HybridOscillatorPanel::Visual::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == DragHandle::none)
        return;

    auto* t = panel.track();

    if (t == nullptr || panel.source >= 3)
        return;

    auto& s = t->hybrid.layers[(size_t) panel.source].sample;
    const auto plot = samplePlot();
    const auto fraction = juce::jlimit (
        0.0, 1.0,
        (double) (e.getPosition().x - plot.getX()) / juce::jmax (1, plot.getWidth()));

    switch (dragging)
    {
        case DragHandle::start:
            s.sampleStart = juce::jlimit (0.0, s.sampleEnd - 0.001, fraction);
            s.loopStart = juce::jmax (s.sampleStart, s.loopStart);
            break;
        case DragHandle::end:
            s.sampleEnd = juce::jlimit (s.sampleStart + 0.001, 1.0, fraction);
            s.loopEnd = juce::jmin (s.sampleEnd, s.loopEnd);
            break;
        case DragHandle::loopStart:
            s.loopStart = juce::jlimit (s.sampleStart, s.loopEnd - 0.001, fraction);
            break;
        case DragHandle::loopEnd:
            s.loopEnd = juce::jlimit (s.loopStart + 0.001, s.sampleEnd, fraction);
            break;
        case DragHandle::crossfade:
        {
            const auto width = juce::jmax (0.001, s.loopEnd - s.loopStart);
            s.crossfade = juce::jlimit (0.0, 1.0,
                                        (s.loopEnd - fraction) / width);
            break;
        }
        case DragHandle::none:
            break;
    }

    panel.document.notifyEdit();
    panel.refresh();
}

HybridOscillatorWindow::HybridOscillatorWindow (ProjectDocument& document,
                                                AudioEngine& audio,
                                                juce::Component* placementContext)
    : DocumentWindow (NI_TXT ("振荡器"), theme::panelBackground,
                      juce::DocumentWindow::closeButton),
      panel (document, audio),
      context (placementContext)
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (&panel, false);
    setResizable (false, false);
}

void HybridOscillatorWindow::showFor (juce::int64 trackId, int sourceIndex)
{
    panel.setSource (trackId, sourceIndex);
    setBounds (placement::placeWindow ({ 1000, 640 },
                                       placement::displayAreaFor (context)));
    setVisible (true);
    toFront (true);
}

} // namespace hmdaw
