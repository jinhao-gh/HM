#pragma once

#include "SamplerEditor.h"
#include "SubtractiveEditor.h"
#include "MinimoogEditor.h"
#include "SynthEditor.h"
#include "WavetableEditor.h"
#include "DrumEditor.h"
#include "GranularEditor.h"
#include "HybridEditor.h"

#include "../Core/PresetStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** A single reusable, non modal window that hosts the instrument editor.

    It carries both editors - the FM one and the sampler one - and shows whichever
    matches the track being edited. Opening it for another track reuses the window.

    The editors are fixed-layout panels, so the window opens at their design size
    (never bigger than the screen) and scrolls when the display cannot give that
    much: on a small or heavily scaled screen the bottom row of controls stays
    reachable instead of being cut off.
*/
class SynthWindow : public juce::DocumentWindow
{
public:
    SynthWindow (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets,
                 bool showDialogs = true, juce::Component* context = nullptr,
                 AppState* appState = nullptr);
    ~SynthWindow() override;

    void resized() override;

    void showForTrack (juce::int64 trackId, const juce::String& trackName, TrackKind kind);
    void setShowDialogs (bool shouldShow);

    SynthEditor& getEditor() noexcept { return *editor; }
    SamplerEditor& getSamplerEditor() noexcept { return *samplerEditor; }
    SubtractiveEditor& getSubtractiveEditor() noexcept { return *subtractiveEditor; }
    MinimoogEditor& getMinimoogEditor() noexcept { return *minimoogEditor; }
    WavetableEditor& getWavetableEditor() noexcept { return *wavetableEditor; }
    DrumEditor& getDrumEditor() noexcept { return *drumEditor; }
    GranularEditor& getGranularEditor() noexcept { return *granularEditor; }
    HybridEditor& getHybridEditor() noexcept { return *hybridEditor; }

    /** The size the panel inside is laid out at: the design size, or the window
        when the window is bigger (self test). */
    juce::Point<int> getPanelSizeForTest() const;
    /** True while the window is scrolling because the panel does not fit. */
    bool isPanelScrollableForTest() const;
    /** The height of the page on screen (self test: the wavetable page is taller). */
    int getPageHeightForTest() const noexcept { return currentPageHeight; }

    void closeButtonPressed() override;

    /** The size the editors are designed for. */
    static constexpr int kDesignWidth  = 780;
    static constexpr int kDesignHeight = 560;

    /** How tall the page for this kind wants to be: the wavetable page carries the
        3D waterfall view and is 80 px taller than the rest. */
    static int pageHeightFor (TrackKind kind) noexcept
    {
        return kind == TrackKind::wavetable ? WavetableEditor::kDesignHeightWithWaterfall
               : kind == TrackKind::granular ? GranularEditor::kDesignHeightWithCloud
               : kind == TrackKind::hybrid ? HybridEditor::kDesignHeight
               : kind == TrackKind::minimoog ? MinimoogEditor::kDesignHeight
                                             : kDesignHeight;
    }

    static int pageWidthFor (TrackKind kind) noexcept
    {
        return kind == TrackKind::hybrid ? HybridEditor::kDesignWidth
               : kind == TrackKind::minimoog ? MinimoogEditor::kDesignWidth
                                             : kDesignWidth;
    }

private:
    /** Holds both editors and shows the one for the current track. */
    class Content : public juce::Component
    {
    public:
        Content (ProjectDocument& document, AudioEngine& audioEngine, PresetStore& presets,
                 AppState* appState);

        void resized() override;
        void showFor (TrackKind kind);

        SynthEditor* fm = nullptr;
        SamplerEditor* sampler = nullptr;
        SubtractiveEditor* subtractive = nullptr;
        MinimoogEditor* minimoog = nullptr;
        WavetableEditor* wavetable = nullptr;
        DrumEditor* drums = nullptr;
        GranularEditor* granular = nullptr;
        HybridEditor* hybrid = nullptr;
    };

    SynthEditor* editor = nullptr;
    SamplerEditor* samplerEditor = nullptr;
    SubtractiveEditor* subtractiveEditor = nullptr;
    MinimoogEditor* minimoogEditor = nullptr;
    WavetableEditor* wavetableEditor = nullptr;
    DrumEditor* drumEditor = nullptr;
    GranularEditor* granularEditor = nullptr;
    HybridEditor* hybridEditor = nullptr;
    Content* content = nullptr;
    juce::Viewport viewport;
    /** The component whose screen the window opens on (the main window). */
    juce::Component* placementContext = nullptr;
    /** The page height the window is currently sized for (see `pageHeightFor`). */
    int currentPageHeight = kDesignHeight;
    int currentPageWidth = kDesignWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};

} // namespace nidaw
