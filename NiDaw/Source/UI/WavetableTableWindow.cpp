#include "WavetableTableWindow.h"
#include "Theme.h"

#include "../Core/WavetableTables.h"
#include "../Core/WaveformPresets.h"
#include "../Core/WindowPlacement.h"

#include <cmath>
#include <algorithm>

namespace nidaw
{

namespace
{
    constexpr int kMargin        = 8;
    constexpr int kTopRowHeight  = 26;
    constexpr int kFrameStripWidth = 104;
    constexpr int kToolRowHeight = 24;
    constexpr int kHarmonicHeight = 180;
    constexpr int kStatusHeight  = 18;
    constexpr int kGap           = 6;

    constexpr int kBuiltInBaseId = 1;      // 1 .. 16
}

//==============================================================================
void WavetableCurveCanvas::setFrame (const CustomWavetable* newTable, int newFrame)
{
    source = newTable;
    frame = newFrame;
    repaint();
}

int WavetableCurveCanvas::indexAt (int x) const
{
    if (plotBounds.getWidth() <= 0)
        return 0;

    const auto t = (double) (x - plotBounds.getX()) / (double) plotBounds.getWidth();
    return juce::jlimit (0, kCustomWavetableFrameSize - 1,
                         (int) std::lround (t * (double) (kCustomWavetableFrameSize - 1)));
}

float WavetableCurveCanvas::valueAt (int y) const
{
    if (plotBounds.getHeight() <= 0)
        return 0.0f;

    const auto t = 1.0 - (double) (y - plotBounds.getY()) / (double) plotBounds.getHeight();
    return (float) juce::jlimit (-1.0, 1.0, t * 2.0 - 1.0);
}

float WavetableCurveCanvas::yFor (float value) const
{
    const auto t = (1.0 - (double) juce::jlimit (-1.0f, 1.0f, value)) * 0.5;
    return (float) plotBounds.getY() + (float) (t * (double) plotBounds.getHeight());
}

float WavetableCurveCanvas::getValueForTest (int index) const
{
    return wavetabletables::sampleAt (*source, frame, index);
}

void WavetableCurveCanvas::plot (juce::Point<int> position, bool straight)
{
    if (source == nullptr)
        return;

    const auto index = indexAt (position.x);
    const auto value = valueAt (position.y);

    if (straight && lineAnchorIndex >= 0)
    {
        // Shift drags a straight line from where the drag started to the pointer.
        const auto from = juce::jmin (lineAnchorIndex, index);
        const auto to   = juce::jmax (lineAnchorIndex, index);
        const auto span = juce::jmax (1, to - from);

        for (int i = from; i <= to; ++i)
        {
            const auto t = (float) (i - lineAnchorIndex) / (float) span;
            const auto interpolated = lineAnchorValue + (value - lineAnchorValue) * t;

            if (onPointDrawn != nullptr)
                onPointDrawn (frame, i, interpolated);
        }
    }
    else if (lastIndex >= 0 && std::abs (index - lastIndex) > 1)
    {
        // Pencil: fill the gap so a fast drag does not leave holes.
        const auto from = juce::jmin (lastIndex, index);
        const auto to   = juce::jmax (lastIndex, index);
        const auto span = juce::jmax (1, to - from);

        for (int i = from; i <= to; ++i)
        {
            const auto t = (float) std::abs (i - lastIndex) / (float) span;
            const auto interpolated = lastValue + (value - lastValue) * t;

            if (onPointDrawn != nullptr)
                onPointDrawn (frame, i, interpolated);
        }
    }
    else if (onPointDrawn != nullptr)
    {
        onPointDrawn (frame, index, value);
    }

    lastIndex = index;
    lastValue = value;
}

void WavetableCurveCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (source == nullptr || ! plotBounds.contains (e.getPosition()))
        return;

    drawing  = true;
    lineMode = e.mods.isShiftDown();
    lastIndex = -1;
    lineAnchorIndex = indexAt (e.getPosition().x);
    lineAnchorValue = valueAt (e.getPosition().y);

    if (onStrokeBegan != nullptr)
        onStrokeBegan();

    plot (e.getPosition(), lineMode);

    if (onStrokeChanged != nullptr)
        onStrokeChanged();

    repaint();
}

void WavetableCurveCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! drawing)
        return;

    plot (e.getPosition(), lineMode);

    if (onStrokeChanged != nullptr)
        onStrokeChanged();

    repaint();
}

void WavetableCurveCanvas::mouseUp (const juce::MouseEvent&)
{
    if (! drawing)
        return;

    drawing = false;
    lineMode = false;
    lastIndex = -1;

    if (onStrokeEnded != nullptr)
        onStrokeEnded();
}

void WavetableCurveCanvas::drawLineForTest (float value0, float value1)
{
    if (source == nullptr || onPointDrawn == nullptr)
        return;

    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
    {
        const auto t = (float) i / (float) (kCustomWavetableFrameSize - 1);
        onPointDrawn (frame, i, value0 + (value1 - value0) * t);
    }
}

void WavetableCurveCanvas::paint (juce::Graphics& g)
{
    g.setColour (theme::gridBackground);
    g.fillRect (getLocalBounds());

    auto area = getLocalBounds().reduced (4);
    plotBounds = area;

    g.setColour (theme::laneBackground);
    g.fillRect (plotBounds);

    // Quarter lines and the zero line, so a drawn shape can be read off.
    g.setColour (theme::gridBeatLine);

    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (plotBounds.getX() + plotBounds.getWidth() * i / 4,
                            (float) plotBounds.getY(), (float) plotBounds.getBottom());

    g.setColour (theme::gridBarLine);
    g.drawHorizontalLine (plotBounds.getCentreY(), (float) plotBounds.getX(),
                          (float) plotBounds.getRight());

    g.setColour (theme::outline);
    g.drawRect (plotBounds);

    if (source == nullptr || frame >= source->effectiveFrameCount())
        return;

    // The wave itself: one point per column of the plot.
    juce::Path wave;
    const auto columns = juce::jmax (1, plotBounds.getWidth());

    for (int x = 0; x <= columns; ++x)
    {
        const auto index = juce::jlimit (0, kCustomWavetableFrameSize - 1,
                                         (int) std::lround ((double) x / (double) columns
                                                            * (double) (kCustomWavetableFrameSize - 1)));
        const auto value = wavetabletables::sampleAt (*source, frame, index);
        const auto point = juce::Point<float> ((float) (plotBounds.getX() + x), yFor (value));

        if (x == 0)
            wave.startNewSubPath (point);
        else
            wave.lineTo (point);
    }

    g.setColour (theme::accent);
    g.strokePath (wave, juce::PathStrokeType (1.6f));
}

//==============================================================================
void WavetableFrameStrip::setTable (const CustomWavetable* newTable, int newFrame)
{
    source = newTable;
    frame = newFrame;
    repaint();
}

int WavetableFrameStrip::getFrameAtForTest (juce::Point<int> point) const
{
    if (source == nullptr)
        return -1;

    const auto count = juce::jmax (1, source->effectiveFrameCount());
    const auto cell = juce::jmax (1, getWidth() / count);
    auto index = point.x / cell;
    index = juce::jlimit (0, count - 1, index);

    return getLocalBounds().contains (point) ? index : -1;
}

void WavetableFrameStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto selected = getFrameAtForTest (e.getPosition());

    if (selected >= 0 && onFrameSelected != nullptr)
        onFrameSelected (selected);
}

void WavetableFrameStrip::paint (juce::Graphics& g)
{
    g.setColour (theme::gridBackground);
    g.fillRect (getLocalBounds());

    if (source == nullptr)
        return;

    const auto count = juce::jmax (1, source->effectiveFrameCount());
    const auto cell = juce::jmax (1, getWidth() / count);

    for (int f = 0; f < count; ++f)
    {
        auto bounds = juce::Rectangle<int> (f * cell, 0, cell, getHeight()).reduced (2, 3);

        g.setColour (f == frame ? theme::accentDim : theme::laneBackground);
        g.fillRect (bounds);

        g.setColour (f == frame ? theme::accent : theme::outline);
        g.drawRect (bounds);

        // The frame's own shape, squeezed into the cell.
        juce::Path wave;

        for (int x = 0; x <= bounds.getWidth(); ++x)
        {
            const auto index = juce::jlimit (0, kCustomWavetableFrameSize - 1,
                                             (int) ((double) x / juce::jmax (1, bounds.getWidth())
                                                    * (double) (kCustomWavetableFrameSize - 1)));
            const auto value = wavetabletables::sampleAt (*source, f, index);
            const auto point = juce::Point<float> ((float) (bounds.getX() + x),
                                                   (float) bounds.getCentreY()
                                                       - value * (float) bounds.getHeight() * 0.45f);

            if (x == 0)
                wave.startNewSubPath (point);
            else
                wave.lineTo (point);
        }

        g.setColour (f == frame ? theme::text : theme::dimText);
        g.strokePath (wave, juce::PathStrokeType (1.0f));

        g.setColour (theme::dimText);
        g.setFont (theme::font (9.0f));
        g.drawText (juce::String (f + 1), bounds.reduced (3, 1), juce::Justification::topLeft, false);
    }
}

//==============================================================================
void WavetableHarmonicBars::setAmounts (std::vector<float> newAmounts)
{
    amounts = std::move (newAmounts);
    amounts.resize ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
    repaint();
}

int WavetableHarmonicBars::getBarAtForTest (juce::Point<int> point) const
{
    if (plotBounds.getWidth() <= 0 || ! plotBounds.contains (point))
        return -1;

    const auto index = (point.x - plotBounds.getX()) * wavetabletables::kMaxHarmonicBars
                           / juce::jmax (1, plotBounds.getWidth());

    return juce::jlimit (0, wavetabletables::kMaxHarmonicBars - 1, index);
}

void WavetableHarmonicBars::setBarFromPosition (juce::Point<int> point)
{
    const auto index = getBarAtForTest (point);

    if (index < 0)
        return;

    const auto t = 1.0 - (double) (point.y - plotBounds.getY()) / (double) plotBounds.getHeight();
    amounts[(size_t) index] = bipolar
                                  ? (float) juce::jlimit (-1.0, 1.0, t * 2.0 - 1.0)
                                  : (float) juce::jlimit (0.0, 1.0, t);

    if (onChanged != nullptr)
        onChanged();

    repaint();
}

void WavetableHarmonicBars::mouseDown (const juce::MouseEvent& e)
{
    if (onGestureBegin != nullptr)
        onGestureBegin();

    setBarFromPosition (e.getPosition());
}
void WavetableHarmonicBars::mouseDrag (const juce::MouseEvent& e) { setBarFromPosition (e.getPosition()); }
void WavetableHarmonicBars::mouseUp (const juce::MouseEvent&)
{
    if (onGestureEnd != nullptr)
        onGestureEnd();
}

void WavetableHarmonicBars::paint (juce::Graphics& g)
{
    g.setColour (theme::gridBackground);
    g.fillRect (getLocalBounds());

    plotBounds = getLocalBounds().reduced (4);

    g.setColour (theme::laneBackground);
    g.fillRect (plotBounds);

    if (amounts.empty())
        return;

    const auto bars = (int) amounts.size();
    const auto barWidth = juce::jmax (1, plotBounds.getWidth() / juce::jmax (1, bars));

    for (int i = 0; i < bars; ++i)
    {
        const auto x = plotBounds.getX() + i * barWidth;
        const auto centre = (float) plotBounds.getCentreY();
        const auto value = juce::jlimit (-1.0f, 1.0f, amounts[(size_t) i]);
        const auto height = (int) (std::abs ((double) value) * (double) plotBounds.getHeight()
                                   * (bipolar ? 0.5 : 1.0));
        const auto top = bipolar && value >= 0.0f ? (int) centre - height
                                                  : bipolar ? (int) centre
                                                            : plotBounds.getBottom() - height;

        g.setColour (i % 2 == 0 ? theme::accentDim : theme::accentDim.withAlpha (0.6f));
        g.fillRect (x, top, juce::jmax (1, barWidth - 1), height);

        g.setColour (theme::gridBeatLine);
        g.drawVerticalLine (x, (float) plotBounds.getY(), (float) plotBounds.getBottom());
    }

    if (bipolar)
    {
        g.setColour (theme::outline.withAlpha (0.7f));
        g.drawHorizontalLine (plotBounds.getCentreY(), (float) plotBounds.getX(),
                              (float) plotBounds.getRight());
    }

    g.setColour (theme::outline);
    g.drawRect (plotBounds);

    g.setColour (theme::dimText);
    g.setFont (theme::font (9.0f));
    g.drawText (NI_TXT ("第 1 次谐波"), juce::Rectangle<int> (plotBounds.getX() + 2, plotBounds.getY() + 1, 80, 12),
                juce::Justification::centredLeft, false);
}

//==============================================================================
WavetableTablePanel::WavetableTablePanel (ProjectDocument& documentToUse,
                                          AudioEngine& audioToUse)
    : document (documentToUse), audio (audioToUse)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (12.5f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::accent);

    auto setupLabel = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::dimText);
    };

    setupLabel (nameLabel, NI_TXT ("名字"));
    setupLabel (framesLabel, NI_TXT ("帧数"));
    setupLabel (harmonicsLabel, NI_TXT ("泛音振幅 / 相位"));

    addAndMakeVisible (nameEditor);
    nameEditor.setFont (theme::font (12.0f));
    nameEditor.onFocusLost = [this]
    {
        if (updating)
            return;

        const auto text = nameEditor.getText().trim();

        if (text.isNotEmpty() && onRenameTable != nullptr)
            onRenameTable (text);
    };
    nameEditor.onReturnKey = [this] { nameEditor.onFocusLost(); };

    addAndMakeVisible (framesBox);

    for (int frames = 1; frames <= kMaxCustomWavetableFrames; ++frames)
        framesBox.addItem (juce::String (frames) + NI_TXT (" 帧"), frames);

    framesBox.onChange = [this]
    {
        if (updating)
            return;

        setFrameCount (framesBox.getSelectedId());
    };

    addAndMakeVisible (importButton);
    importButton.setButtonText (NI_TXT ("导入 WAV…"));
    importButton.onClick = [this] { if (onImportWav != nullptr) onImportWav(); };

    addAndMakeVisible (sampleButton);
    sampleButton.setButtonText (NI_TXT ("从片段取样…"));
    sampleButton.setTooltip (NI_TXT ("把当前打开在编辑栏里的音频片段切成帧，做成本工程的一张表"));
    sampleButton.onClick = [this] { if (onSampleFromClip != nullptr) onSampleFromClip(); };

    addAndMakeVisible (copyBuiltInButton);
    copyBuiltInButton.setButtonText (NI_TXT ("拷内置…"));
    copyBuiltInButton.setTooltip (NI_TXT ("从 16 张内置表里拿一张当起点，成为一张新的自定义表"));
    copyBuiltInButton.onClick = [this]
    {
        juce::PopupMenu menu;

        for (int i = 0; i < wavetable::builtInTableCount(); ++i)
            menu.addItem (i + 1, wavetable::tableName (i));

        juce::Component::SafePointer<WavetableTablePanel> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyBuiltInButton),
                            [safeThis] (int result)
                            {
                                if (result <= 0 || safeThis == nullptr || safeThis->onCopyBuiltIn == nullptr)
                                    return;

                                safeThis->onCopyBuiltIn (result - 1);
                            });
    };

    addAndMakeVisible (waveformPresetButton);
    waveformPresetButton.setButtonText (NI_TXT ("波形预设…"));
    waveformPresetButton.setTooltip (NI_TXT ("把内置单周期或多帧波形做成一张新的自定义表"));
    waveformPresetButton.onClick = [this]
    {
        juce::PopupMenu menu;
        juce::PopupMenu single, multi;

        for (int i = 0; i < waveformpresets::count(); ++i)
        {
            if (waveformpresets::isMultiFrame (i))
                multi.addItem (i + 1, waveformpresets::name (i));
            else
                single.addItem (i + 1, waveformpresets::name (i));
        }

        menu.addSubMenu (NI_TXT ("单周期"), single);
        menu.addSubMenu (NI_TXT ("多帧"), multi);

        juce::Component::SafePointer<WavetableTablePanel> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&waveformPresetButton),
                            [safeThis] (int result)
                            {
                                if (result <= 0 || safeThis == nullptr
                                    || safeThis->onWaveformPreset == nullptr)
                                    return;

                                safeThis->onWaveformPreset (result - 1);
                            });
    };

    addAndMakeVisible (auditionButton);
    auditionButton.setButtonText (NI_TXT ("试听 C4"));
    auditionButton.onClick = [this] { if (onAudition != nullptr) onAudition(); };

    addAndMakeVisible (deleteButton);
    deleteButton.setButtonText (NI_TXT ("删除这张表"));
    deleteButton.setTooltip (NI_TXT ("从工程里删掉这张自定义表（还在用它的振荡器会退回内置表）"));
    deleteButton.onClick = [this] { if (onDeleteTable != nullptr) onDeleteTable(); };

    auto setupTool = [this] (juce::TextButton& button, const juce::String& text,
                             const juce::String& tooltip)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
        button.setTooltip (tooltip);
    };

    setupTool (pencilButton, NI_TXT ("铅笔"), NI_TXT ("按住拖动就画出波形（横向是位置，竖直是幅度）"));
    pencilButton.onClick = [this] { repaint(); };

    setupTool (lineButton, NI_TXT ("直线"), NI_TXT ("从一点拖到另一点，画一条直线（也可以按住 Shift 拖）"));
    setupTool (smoothButton, NI_TXT ("平滑"), NI_TXT ("把这一帧抹圆一点"));
    smoothButton.onClick = [this] { smoothForTest(); };
    setupTool (normaliseButton, NI_TXT ("归一化"), NI_TXT ("整张表按最大幅度缩放到 0.98"));
    normaliseButton.onClick = [this] { normaliseForTest(); };
    setupTool (clearButton, NI_TXT ("清空"), NI_TXT ("这一帧清成 0"));
    clearButton.onClick = [this] { clearForTest(); };
    setupTool (addFrameButton, NI_TXT ("新建帧"), NI_TXT ("在当前帧后插入空白帧"));
    addFrameButton.onClick = [this] { addFrame (false); };
    setupTool (duplicateFrameButton, NI_TXT ("复制帧"), NI_TXT ("复制当前帧到下一帧"));
    duplicateFrameButton.onClick = [this] { addFrame (true); };
    setupTool (deleteFrameButton, NI_TXT ("删除帧"), NI_TXT ("删除当前帧（第一帧不可删）"));
    deleteFrameButton.onClick = [this] { deleteCurrentFrame(); };

    setupLabel (statusLabel, {});
    statusLabel.setColour (juce::Label::textColourId, theme::dimText);

    harmonicsLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (applyHarmonicsButton);
    applyHarmonicsButton.setButtonText (NI_TXT ("应用"));
    applyHarmonicsButton.setTooltip (NI_TXT ("把这一帧改写成这些谐波的和（加法合成）"));
    applyHarmonicsButton.onClick = [this] { applyHarmonicsForTest(); };

    addAndMakeVisible (canvas);
    addChildComponent (waterfall);
    addAndMakeVisible (frameViewport);
    frameViewport.setViewedComponent (&frameStrip, false);
    frameViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (harmonics);
    harmonics.setAmounts (std::vector<float> ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f));
    addAndMakeVisible (phaseHarmonics);
    phaseHarmonics.setBipolar (true);
    phaseHarmonics.setAmounts (std::vector<float> ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f));

    canvas.onStrokeBegan = [this]
    {
        // One gesture, one undo entry: snapshot once, then only mark dirty.
        document.beginEdit (NI_TXT ("编辑波表"));
        strokeOpen = true;
    };
    canvas.onStrokeChanged = [this]
    {
        document.notifyEdit();
        canvas.repaint();
        frameStrip.repaint();
        updateStatus();
    };
    canvas.onStrokeEnded = [this] { strokeOpen = false; };
    canvas.onPointDrawn = [this] (int targetFrame, int index, float value)
    {
        if (auto* current = table())
            wavetabletables::setSampleAt (*current, targetFrame, index, value);
    };

    frameStrip.onFrameSelected = [this] (int selected)
    {
        currentFrame = juce::jlimit (0, juce::jmax (0, getFrameCountForTest() - 1), selected);
        refreshFromTable();
        analyseCurrentFrame();
    };

    auto wireHarmonicBar = [this] (WavetableHarmonicBars& bars)
    {
        bars.onGestureBegin = [this]
        {
            if (! harmonicGestureOpen)
            {
                document.beginEdit (NI_TXT ("编辑泛音"));
                harmonicGestureOpen = true;
            }
        };
        bars.onChanged = [this] { applyHarmonicsLive (false); };
        bars.onGestureEnd = [this] { endHarmonicGesture(); };
    };
    wireHarmonicBar (harmonics);
    wireHarmonicBar (phaseHarmonics);

    setupTool (view3DButton, NI_TXT ("3D 视图"), NI_TXT ("在平面波形和多帧立体波表之间切换"));
    view3DButton.onClick = [this]
    {
        show3D = ! show3D;
        canvas.setVisible (! show3D);
        waterfall.setVisible (show3D);
        view3DButton.setButtonText (show3D ? NI_TXT ("2D 波形") : NI_TXT ("3D 视图"));
        refreshFromTable();
    };

    document.addChangeListener (this);
    refresh();
}

WavetableTablePanel::~WavetableTablePanel()
{
    document.removeChangeListener (this);
}

CustomWavetable* WavetableTablePanel::table() const
{
    return const_cast<Project&> (document.getProject()).findCustomWavetable (tableId);
}

void WavetableTablePanel::setTableId (juce::int64 newTableId)
{
    tableId = newTableId;
    currentFrame = 0;
    refresh();
}

void WavetableTablePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void WavetableTablePanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    refreshFromTable();
    analyseCurrentFrame();
}

void WavetableTablePanel::refreshFromTable()
{
    auto* current = table();
    const auto frames = getFrameCountForTest();

    notice.clear();
    currentFrame = juce::jlimit (0, juce::jmax (0, frames - 1), currentFrame);

    titleLabel.setText (current != nullptr ? NI_TXT ("波表编辑器 · ") + current->name
                                           : NI_TXT ("波表编辑器"),
                        juce::dontSendNotification);
    nameEditor.setText (current != nullptr ? current->name : juce::String(), juce::dontSendNotification);
    framesBox.setSelectedId (frames, juce::dontSendNotification);

    canvas.setFrame (current, currentFrame);
    if (current != nullptr)
        waterfall.setContent (wavetabletables::playableTable (*current), 0, 0, 0.0, -1.0);
    frameStrip.setTable (current, currentFrame);
    frameStrip.setSize (juce::jmax (frameViewport.getWidth(), juce::jmax (1, frames) * 52), 64);

    const auto enabled = current != nullptr;
    const std::initializer_list<juce::Component*> parts { &framesBox, &nameEditor, &canvas,
                                                          &frameViewport, &frameStrip,
                                                          &harmonics, &phaseHarmonics,
                                                          &view3DButton,
                                                          &applyHarmonicsButton, &smoothButton,
                                                          &normaliseButton, &clearButton,
                                                          &addFrameButton, &duplicateFrameButton,
                                                          &deleteFrameButton,
                                                          &auditionButton };

    for (auto* component : parts)
        component->setEnabled (enabled);

    updateStatus();
}

void WavetableTablePanel::analyseCurrentFrame()
{
    auto* current = table();

    if (current == nullptr)
    {
        spectrum = {};
        spectrum.amplitudes.assign ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
        spectrum.phases.assign ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);
        harmonics.setAmounts (spectrum.amplitudes);
        phaseHarmonics.setAmounts (spectrum.phases);
        return;
    }

    spectrum = wavetabletables::analyseHarmonicsWithPhase (*current, currentFrame,
                                                           wavetabletables::kMaxHarmonicBars);
    harmonics.setAmounts (spectrum.amplitudes);

    auto phases = spectrum.phases;
    for (auto& p : phases)
        p /= juce::MathConstants<float>::pi;
    phaseHarmonics.setAmounts (phases);
}

void WavetableTablePanel::applyHarmonicsLive (bool begin)
{
    if (begin && ! harmonicGestureOpen)
    {
        document.beginEdit (NI_TXT ("编辑泛音"));
        harmonicGestureOpen = true;
    }

    auto amounts = harmonics.getAmounts();
    auto phases = phaseHarmonics.getAmounts();
    for (auto& p : phases)
        p *= juce::MathConstants<float>::pi;

    if (auto* current = table())
    {
        wavetabletables::synthesiseFromHarmonics (*current, currentFrame, amounts, phases);
        document.notifyEdit();
        canvas.repaint();
        frameStrip.repaint();
        updateStatus();
    }
}

void WavetableTablePanel::endHarmonicGesture()
{
    harmonicGestureOpen = false;
}

void WavetableTablePanel::updateStatus()
{
    auto* current = table();

    if (notice.isNotEmpty())
    {
        statusLabel.setText (notice, juce::dontSendNotification);
        return;
    }

    if (current == nullptr)
    {
        statusLabel.setText (NI_TXT ("这一张表已经不在工程里了"), juce::dontSendNotification);
        return;
    }

    const auto bytes = wavetabletables::storedBytes (*current);

    statusLabel.setText (juce::String (current->effectiveFrameCount()) + NI_TXT (" 帧 × ")
                             + juce::String (kCustomWavetableFrameSize) + NI_TXT (" 点 · 约 ")
                             + juce::String (juce::jmax (1, bytes / 1024)) + NI_TXT (" KB · 第 ")
                             + juce::String (currentFrame + 1) + NI_TXT (" 帧 · 一次手势一条撤销"),
                         juce::dontSendNotification);
}

void WavetableTablePanel::setNotice (const juce::String& text)
{
    notice = text;
    updateStatus();
}

void WavetableTablePanel::commit (const juce::String& editName,
                                  const std::function<void (CustomWavetable&)>& mutation)
{
    const auto id = tableId;

    document.applyEdit (editName, [id, &mutation] (Project& project)
    {
        if (auto* current = project.findCustomWavetable (id))
            mutation (*current);
    });

    refresh();
}

int WavetableTablePanel::getFrameCountForTest() const
{
    const auto* current = const_cast<WavetableTablePanel*> (this)->table();

    return current != nullptr ? current->effectiveFrameCount() : 0;
}

float WavetableTablePanel::getSampleForTest (int frame, int index) const
{
    const auto* current = const_cast<WavetableTablePanel*> (this)->table();

    return current != nullptr ? wavetabletables::sampleAt (*current, frame, index) : 0.0f;
}

void WavetableTablePanel::selectFrameForTest (int frame)
{
    currentFrame = juce::jlimit (0, juce::jmax (0, getFrameCountForTest() - 1), frame);
    refreshFromTable();
    analyseCurrentFrame();
}

void WavetableTablePanel::drawLineForTest (float value0, float value1)
{
    if (table() == nullptr)
        return;

    document.beginEdit (NI_TXT ("编辑波表"));
    canvas.drawLineForTest (value0, value1);
    document.notifyEdit();
    refresh();
}

void WavetableTablePanel::setHarmonicForTest (int index, float value)
{
    auto amounts = harmonics.getAmounts();
    amounts.resize ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);

    if (index < 0 || index >= (int) amounts.size())
        return;

    amounts[(size_t) index] = juce::jlimit (0.0f, 1.0f, value);
    harmonics.setAmounts (amounts);
}

void WavetableTablePanel::setPhaseForTest (int index, float value)
{
    auto phases = phaseHarmonics.getAmounts();
    phases.resize ((size_t) wavetabletables::kMaxHarmonicBars, 0.0f);

    if (index < 0 || index >= (int) phases.size())
        return;

    phases[(size_t) index] = juce::jlimit (-1.0f, 1.0f, value);
    phaseHarmonics.setAmounts (phases);
}

void WavetableTablePanel::applyHarmonicsForTest()
{
    const auto amounts = harmonics.getAmounts();
    auto phases = phaseHarmonics.getAmounts();
    for (auto& p : phases)
        p *= juce::MathConstants<float>::pi;
    const auto frame = currentFrame;

    commit (NI_TXT ("按谐波生成波表"), [frame, &amounts, &phases] (CustomWavetable& target)
    {
        wavetabletables::synthesiseFromHarmonics (target, frame, amounts, phases);
    });

    analyseCurrentFrame();
}

void WavetableTablePanel::smoothForTest()
{
    const auto frame = currentFrame;

    commit (NI_TXT ("平滑波表"), [frame] (CustomWavetable& target)
    {
        wavetabletables::smoothFrame (target, frame);
    });

    analyseCurrentFrame();
}

void WavetableTablePanel::normaliseForTest()
{
    commit (NI_TXT ("归一化波表"), [] (CustomWavetable& target)
    {
        wavetabletables::normalise (target);
    });

    analyseCurrentFrame();
}

void WavetableTablePanel::clearForTest()
{
    const auto frame = currentFrame;

    commit (NI_TXT ("清空波表帧"), [frame] (CustomWavetable& target)
    {
        wavetabletables::clearFrame (target, frame);
    });

    analyseCurrentFrame();
}

void WavetableTablePanel::setFrameCountForTest (int count)
{
    setFrameCount (count);
}

void WavetableTablePanel::addFrame (bool duplicateCurrent)
{
    auto* current = table();

    if (current == nullptr || current->frameCount >= kMaxCustomWavetableFrames)
        return;

    const auto insertAt = juce::jlimit (0, current->frameCount, currentFrame + 1);
    const auto sourceFrame = currentFrame;
    commit (NI_TXT ("新建波表帧"), [insertAt, duplicateCurrent, sourceFrame] (CustomWavetable& target)
    {
        const auto oldCount = target.frameCount;
        const auto oldSamples = target.samples;
        target.frameCount = oldCount + 1;
        target.samples.assign ((size_t) target.frameCount * (size_t) kCustomWavetableFrameSize, 0);

        for (int f = 0; f < oldCount; ++f)
        {
            const auto destination = f < insertAt ? f : f + 1;
            const auto* source = oldSamples.data()
                                     + (size_t) (duplicateCurrent && f == sourceFrame ? sourceFrame : f)
                                           * (size_t) kCustomWavetableFrameSize;
            std::copy (source, source + kCustomWavetableFrameSize,
                       target.samples.data() + (size_t) destination * (size_t) kCustomWavetableFrameSize);
        }
    });

    currentFrame = juce::jlimit (0, current->frameCount - 1, insertAt);
    refresh();
}

void WavetableTablePanel::deleteCurrentFrame()
{
    auto* current = table();

    if (current == nullptr || current->frameCount <= 1)
        return;

    const auto frame = currentFrame;
    commit (NI_TXT ("删除波表帧"), [frame] (CustomWavetable& target)
    {
        if (target.frameCount <= 1 || frame < 0 || frame >= target.frameCount)
            return;

        for (int f = frame + 1; f < target.frameCount; ++f)
        {
            const auto* source = target.samples.data()
                                     + (size_t) f * (size_t) kCustomWavetableFrameSize;
            std::copy (source, source + kCustomWavetableFrameSize,
                       target.samples.data() + (size_t) (f - 1) * (size_t) kCustomWavetableFrameSize);
        }

        --target.frameCount;
        target.samples.resize ((size_t) target.frameCount * (size_t) kCustomWavetableFrameSize);
    });

    currentFrame = juce::jlimit (0, current->frameCount - 1, currentFrame);
    refresh();
}

void WavetableTablePanel::setFrameCount (int count)
{
    const auto wanted = juce::jlimit (1, kMaxCustomWavetableFrames, count);

    commit (NI_TXT ("改波表帧数"), [wanted] (CustomWavetable& target)
    {
        if (wanted > target.frameCount)
        {
            // Growing keeps the frames that are already there and appends silence.
            target.frameCount = wanted;
            wavetabletables::clampToShape (target);
        }
        else
        {
            target.frameCount = wanted;
            target.samples.resize ((size_t) wanted * (size_t) kCustomWavetableFrameSize);
        }
    });

    currentFrame = juce::jlimit (0, juce::jmax (0, wanted - 1), currentFrame);
    refresh();
}

void WavetableTablePanel::setNameForTest (const juce::String& name)
{
    nameEditor.setText (name, juce::dontSendNotification);
    nameEditor.onFocusLost();
}

void WavetableTablePanel::triggerCopyBuiltInForTest (int builtInIndex)
{
    if (onCopyBuiltIn != nullptr)
        onCopyBuiltIn (builtInIndex);
}

void WavetableTablePanel::triggerWaveformPresetForTest (int presetIndex)
{
    if (onWaveformPreset != nullptr)
        onWaveformPreset (presetIndex);
}

void WavetableTablePanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
}

juce::Rectangle<int> WavetableTablePanel::getToolBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return pencilButton.getBounds();
        case 1: return lineButton.getBounds();
        case 2: return smoothButton.getBounds();
        case 3: return normaliseButton.getBounds();
        case 4: return clearButton.getBounds();
        default: return {};
    }
}

juce::Rectangle<int> WavetableTablePanel::getToolbarBoundsForTest (int which) const
{
    switch (which)
    {
        case 0: return importButton.getBounds();
        case 1: return sampleButton.getBounds();
        case 2: return copyBuiltInButton.getBounds();
        case 3: return auditionButton.getBounds();
        case 4: return deleteButton.getBounds();
        case 5: return waveformPresetButton.getBounds();
        default: return {};
    }
}

void WavetableTablePanel::triggerDeleteForTest()
{
    deleteButton.onClick();
}

void WavetableTablePanel::resized()
{
    auto area = getLocalBounds().reduced (kMargin, kGap);

    auto top = area.removeFromTop (kTopRowHeight);
    nameLabel.setBounds (top.removeFromLeft (34));
    nameEditor.setBounds (top.removeFromLeft (150).reduced (0, 3));
    top.removeFromLeft (kGap);
    framesLabel.setBounds (top.removeFromLeft (34));
    framesBox.setBounds (top.removeFromLeft (76).reduced (0, 3));
    top.removeFromLeft (kGap);
    importButton.setBounds (top.removeFromLeft (86).reduced (0, 3));
    top.removeFromLeft (4);
    sampleButton.setBounds (top.removeFromLeft (96).reduced (0, 3));
    top.removeFromLeft (4);
    copyBuiltInButton.setBounds (top.removeFromLeft (78).reduced (0, 3));
    top.removeFromLeft (4);
    waveformPresetButton.setBounds (top.removeFromLeft (86).reduced (0, 3));

    auto right = top;
    deleteButton.setBounds (right.removeFromRight (94).reduced (0, 3));
    right.removeFromRight (4);
    auditionButton.setBounds (right.removeFromRight (78).reduced (0, 3));

    area.removeFromTop (kGap);
    statusLabel.setBounds (area.removeFromBottom (kStatusHeight));
    area.removeFromBottom (4);

    // Serum-style order: harmonics at the top, waveform in the middle, frame strip at the bottom.
    harmonicsLabel.setBounds (area.removeFromTop (16));
    auto harmonicArea = area.removeFromTop (kHarmonicHeight - 16);
    applyHarmonicsButton.setBounds (harmonicArea.removeFromRight (72).reduced (0, 4));
    auto amplitudeArea = harmonicArea.removeFromTop (harmonicArea.getHeight() / 2);
    harmonics.setBounds (amplitudeArea.reduced (0, 1));
    phaseHarmonics.setBounds (harmonicArea.reduced (0, 1));

    area.removeFromTop (kGap);

    auto toolRow = area.removeFromTop (kToolRowHeight);
    const std::initializer_list<juce::TextButton*> toolButtons { &pencilButton, &lineButton,
                                                                 &smoothButton, &normaliseButton,
                                                                 &clearButton, &addFrameButton,
                                                                 &duplicateFrameButton,
                                                                 &deleteFrameButton,
                                                                 &view3DButton };

    for (auto* button : toolButtons)
    {
        button->setBounds (toolRow.removeFromLeft (62).reduced (1, 2));
        toolRow.removeFromLeft (4);
    }

    area.removeFromTop (kGap);

    auto frameRow = area.removeFromBottom (68);
    frameViewport.setBounds (frameRow);
    frameStrip.setSize (juce::jmax (frameViewport.getWidth(), juce::jmax (1, getFrameCountForTest()) * 52),
                        juce::jmax (1, frameRow.getHeight() - 12));
    area.removeFromBottom (kGap);
    canvas.setBounds (area);
    waterfall.setBounds (area);

    titleLabel.setBounds (getLocalBounds().removeFromTop (0));
}

//==============================================================================
WavetableTableWindow::WavetableTableWindow (ProjectDocument& document, AppState& appState,
                                            AudioEngine& audio, juce::Component* context)
    : juce::DocumentWindow (NI_TXT ("波表编辑器"), theme::panelBackground,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      placementContext (context)
{
    juce::ignoreUnused (appState);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (640, 360, 1600, 1200);

    panel = new WavetableTablePanel (document, audio);

    // The panel is laid out at its design size and the window scrolls around it:
    // without this a narrow window clips the toolbar's right-hand buttons
    // (试听 C4 / 删除这张表), which then look like they do not exist at all.
    viewport.setViewedComponent (panel, false);
    viewport.setScrollBarsShown (true, true);
    setContentOwned (&viewport, false);
}

WavetableTableWindow::~WavetableTableWindow()
{
    // The viewport does not own the panel, so the window deletes it.
    viewport.setViewedComponent (nullptr, false);
    delete panel;
    panel = nullptr;
}

void WavetableTableWindow::resized()
{
    juce::DocumentWindow::resized();

    if (panel != nullptr)
        panel->setSize (juce::jmax (kDesignWidth, viewport.getMaximumVisibleWidth()),
                        juce::jmax (kDesignHeight, viewport.getMaximumVisibleHeight()));
}

void WavetableTableWindow::closeButtonPressed()
{
    setVisible (false);
}

void WavetableTableWindow::show (juce::int64 customTableId)
{
    if (panel != nullptr)
        panel->setTableId (customTableId);

    if (! isVisible())
        setBounds (placement::placeWindow ({ kDesignWidth, kDesignHeight },
                                           placement::displayAreaFor (placementContext)));

    setVisible (true);
    toFront (true);
}

void WavetableTableWindow::refresh()
{
    if (panel != nullptr)
        panel->refresh();
}

} // namespace nidaw
