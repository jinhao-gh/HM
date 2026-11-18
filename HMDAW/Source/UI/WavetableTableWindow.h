#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "../Core/WavetableTables.h"
#include "AppState.h"
#include "WavetableEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hmdaw
{

/** The big editable picture of one frame: 2048 samples left to right, -1..+1 top
    to bottom. Dragging draws; Shift turns the drag into a straight line. */
class WavetableCurveCanvas : public juce::Component
{
public:
    WavetableCurveCanvas() = default;

    /** Which table/frame to show, and the data to draw (the panel owns both). */
    void setFrame (const CustomWavetable* table, int frame);

    /** One gesture: started / moved / finished - the panel turns that into
        beginEdit + notifyEdit, i.e. one undo entry per stroke. */
    std::function<void()> onStrokeBegan;
    std::function<void()> onStrokeChanged;
    std::function<void()> onStrokeEnded;
    /** A point was drawn: (frame, index, value). */
    std::function<void (int, int, float)> onPointDrawn;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** Self test: draw a straight line across one frame the way a Shift drag does. */
    void drawLineForTest (float value0, float value1);

    juce::Rectangle<int> getPlotBoundsForTest() const { return plotBounds; }
    float getValueForTest (int index) const;

private:
    void plot (juce::Point<int> position, bool straight);
    int   indexAt (int x) const;
    float valueAt (int y) const;
    float yFor (float value) const;

    const CustomWavetable* source = nullptr;
    int   frame = 0;
    int   lastIndex = -1;
    float lastValue = 0.0f;
    bool  drawing = false;
    bool  lineMode = false;
    int   lineAnchorIndex = 0;
    float lineAnchorValue = 0.0f;
    juce::Rectangle<int> plotBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableCurveCanvas)
};

//==============================================================================
/** The 1..8 frame thumbnails down the side; clicking one selects it. */
class WavetableFrameStrip : public juce::Component
{
public:
    WavetableFrameStrip() = default;

    void setTable (const CustomWavetable* table, int currentFrame);

    std::function<void (int)> onFrameSelected;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    int getFrameAtForTest (juce::Point<int> point) const;

private:
    const CustomWavetable* source = nullptr;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableFrameStrip)
};

//==============================================================================
/** The harmonic strip: one bar per harmonic, dragged to set how much of it the
    frame should hold. 应用 rewrites the frame as the sum of those harmonics. */
class WavetableHarmonicBars : public juce::Component
{
public:
    WavetableHarmonicBars() = default;

    void setAmounts (std::vector<float> newAmounts);
    const std::vector<float>& getAmounts() const noexcept { return amounts; }
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; }

    std::function<void()> onChanged;
    std::function<void()> onGestureBegin;
    std::function<void()> onGestureEnd;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    int getBarAtForTest (juce::Point<int> point) const;

private:
    void setBarFromPosition (juce::Point<int> point);

    std::vector<float> amounts;
    juce::Rectangle<int> plotBounds;
    bool bipolar = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableHarmonicBars)
};

//==============================================================================
/** The window's content: the name, the frame count, the three sources (import a
    WAV / sample the open clip / start from a built-in table), the drawing tools
    and the harmonic strip.

    Every change goes through the document: one gesture is one undo entry
    (`beginEdit` on the first point, `notifyEdit` while dragging), and the discrete
    commands are single `applyEdit`s.
*/
class WavetableTablePanel : public juce::Component,
                            private juce::ChangeListener
{
public:
    WavetableTablePanel (ProjectDocument& document, AudioEngine& audio);
    ~WavetableTablePanel() override;

    /** Which of the project's tables this window edits (0 = nothing yet). */
    void setTableId (juce::int64 newTableId);
    juce::int64 getTableId() const noexcept { return tableId; }

    /** Set by MainComponent: the three sources and the "listen" button. */
    std::function<void()> onImportWav;
    std::function<void()> onSampleFromClip;
    std::function<void (int builtInIndex)> onCopyBuiltIn;
    std::function<void (int waveformPresetIndex)> onWaveformPreset;
    std::function<void (const juce::String&)> onRenameTable;
    std::function<void()> onAudition;
    std::function<void()> onDeleteTable;

    /** A one-line note (an import that failed, "that clip has no audio", …) that
        the window shows in its status line. */
    void setNotice (const juce::String& text);

    /** False in the offscreen self test, where a dialog would block the run. */
    bool showDialogs = true;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();

    // ---- diagnostics ---------------------------------------------------------
    juce::Rectangle<int> getCanvasBoundsForTest() const { return canvas.getBounds(); }
    juce::Rectangle<int> getFrameStripBoundsForTest() const { return frameStrip.getBounds(); }
    juce::Rectangle<int> getHarmonicBoundsForTest() const { return harmonics.getBounds(); }
    juce::Rectangle<int> getToolBoundsForTest (int which) const;
    /** Toolbar buttons: 0 = 导入 WAV, 1 = 从片段取样, 2 = 拷内置, 3 = 试听, 4 = 删除. */
    juce::Rectangle<int> getToolbarBoundsForTest (int which) const;
    bool isDeleteEnabledForTest() const { return deleteButton.isEnabled(); }
    void triggerDeleteForTest();
    int  getFrameCountForTest() const;
    int  getCurrentFrameForTest() const noexcept { return currentFrame; }
    void selectFrameForTest (int frame);
    float getSampleForTest (int frame, int index) const;
    void drawLineForTest (float value0, float value1);
    std::vector<float> getHarmonicsForTest() const { return harmonics.getAmounts(); }
    std::vector<float> getPhasesForTest() const { return phaseHarmonics.getAmounts(); }
    void setHarmonicForTest (int index, float value);
    void setPhaseForTest (int index, float value);
    void applyHarmonicsForTest();
    void smoothForTest();
    void normaliseForTest();
    void clearForTest();
    void setFrameCountForTest (int count);
    void setNameForTest (const juce::String& name);
    void triggerCopyBuiltInForTest (int builtInIndex);
    void triggerWaveformPresetForTest (int presetIndex);
    juce::String getStatusTextForTest() const { return statusLabel.getText(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    CustomWavetable* table() const;
    void refreshFromTable();
    void analyseCurrentFrame();
    void applyHarmonicsLive (bool begin);
    void endHarmonicGesture();
    void addFrame (bool duplicateCurrent);
    void deleteCurrentFrame();
    void setFrameCount (int count);
    void commit (const juce::String& editName, const std::function<void (CustomWavetable&)>& mutation);
    void updateStatus();

    ProjectDocument& document;
    AudioEngine& audio;

    juce::int64 tableId = 0;
    int currentFrame = 0;

    juce::Label titleLabel, nameLabel, framesLabel, harmonicsLabel, statusLabel;
    juce::TextEditor nameEditor;
    juce::ComboBox framesBox, builtInBox;
    juce::TextButton importButton, sampleButton, copyBuiltInButton, auditionButton;
    juce::TextButton waveformPresetButton;
    juce::TextButton deleteButton;
    juce::TextButton pencilButton, lineButton, smoothButton, normaliseButton, clearButton;
    juce::TextButton view3DButton;
    juce::TextButton addFrameButton, duplicateFrameButton, deleteFrameButton;
    juce::TextButton applyHarmonicsButton;
    WavetableCurveCanvas canvas;
    WavetableWaterfallView waterfall;
    juce::Viewport frameViewport;
    WavetableFrameStrip frameStrip;
    WavetableHarmonicBars harmonics;
    WavetableHarmonicBars phaseHarmonics;
    wavetabletables::HarmonicSpectrum spectrum;
    bool harmonicGestureOpen = false;

    bool updating = false;
    bool show3D = false;
    /** A note from the owner (import problems and such), shown instead of the size
        line until the next refresh shows something else. */
    juce::String notice;
    /** True while a stroke is in flight, so the document gets one undo entry. */
    bool strokeOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableTablePanel)
};

//==============================================================================
/** The wavetable editor window: reused, hidden when closed, placed like the rest. */
class WavetableTableWindow : public juce::DocumentWindow
{
public:
    WavetableTableWindow (ProjectDocument& document, AppState& appState, AudioEngine& audio,
                          juce::Component* context);
    ~WavetableTableWindow() override;

    void closeButtonPressed() override;
    void resized() override;

    /** Shows the window on one of the project's tables. */
    void show (juce::int64 customTableId);
    void refresh();

    WavetableTablePanel& getPanel() noexcept { return *panel; }

    static constexpr int kDesignWidth  = 1000;
    static constexpr int kDesignHeight = 700;

private:
    WavetableTablePanel* panel = nullptr;
    /** The panel is laid out at its design size and the window scrolls around it,
        so a narrow window never clips the toolbar's right-hand buttons. */
    juce::Viewport viewport;
    juce::Component* placementContext = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableTableWindow)
};

} // namespace hmdaw
