#include "SynthWindow.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"

namespace hmdaw
{

SynthWindow::Content::Content (ProjectDocument& document, AudioEngine& audioEngine,
                               PresetStore& presets, AppState* appState)
{
    fm = new SynthEditor (document, audioEngine, presets);
    sampler = new SamplerEditor (document, audioEngine, presets);
    subtractive = new SubtractiveEditor (document, audioEngine, presets);
    minimoog = new MinimoogEditor (document, audioEngine, presets);
    wavetable = new WavetableEditor (document, audioEngine, presets);
    drums = new DrumEditor (document, audioEngine, presets);
    granular = new GranularEditor (document, audioEngine, presets);
    hybrid = new HybridEditor (document, audioEngine, presets, appState);

    addAndMakeVisible (fm);
    addChildComponent (sampler);
    addChildComponent (subtractive);
    addChildComponent (minimoog);
    addChildComponent (wavetable);
    addChildComponent (drums);
    addChildComponent (granular);
    addChildComponent (hybrid);
}

void SynthWindow::Content::resized()
{
    fm->setBounds (getLocalBounds());
    sampler->setBounds (getLocalBounds());
    subtractive->setBounds (getLocalBounds());
    minimoog->setBounds (getLocalBounds());
    wavetable->setBounds (getLocalBounds());
    drums->setBounds (getLocalBounds());
    granular->setBounds (getLocalBounds());
    hybrid->setBounds (getLocalBounds());
}

void SynthWindow::Content::showFor (TrackKind kind)
{
    // One switch, no default: a newly added TrackKind makes the compiler ask which
    // page it belongs to instead of silently showing an empty window.
    juce::Component* page = nullptr;

    switch (kind)
    {
        case TrackKind::synth:       page = fm; break;
        case TrackKind::sampler:     page = sampler; break;
        case TrackKind::subtractive: page = subtractive; break;
        case TrackKind::minimoog:    page = minimoog; break;
        case TrackKind::wavetable:   page = wavetable; break;
        case TrackKind::drums:       page = drums; break;
        case TrackKind::granular:    page = granular; break;
        case TrackKind::hybrid:      page = hybrid; break;
        case TrackKind::audio:
        case TrackKind::vst:         break;   // no instrument page
    }

    for (auto* pageEditor : { static_cast<juce::Component*> (fm),
                              static_cast<juce::Component*> (sampler),
                              static_cast<juce::Component*> (subtractive),
                              static_cast<juce::Component*> (minimoog),
                              static_cast<juce::Component*> (wavetable),
                              static_cast<juce::Component*> (drums),
                              static_cast<juce::Component*> (granular),
                              static_cast<juce::Component*> (hybrid) })
        pageEditor->setVisible (pageEditor == page);
}

SynthWindow::SynthWindow (ProjectDocument& document, AudioEngine& audioEngine,
                          PresetStore& presets, bool showDialogs, juce::Component* context,
                          AppState* appState)
    : juce::DocumentWindow (NI_TXT ("音色"), theme::panelBackground,
                            juce::DocumentWindow::closeButton)
{
    placementContext = context;

    setUsingNativeTitleBar (true);
    setResizable (false, false);

    content = new Content (document, audioEngine, presets, appState);
    editor = content->fm;
    samplerEditor = content->sampler;
    subtractiveEditor = content->subtractive;
    minimoogEditor = content->minimoog;
    wavetableEditor = content->wavetable;
    drumEditor = content->drums;
    granularEditor = content->granular;
    hybridEditor = content->hybrid;

    // The panel is laid out at its design size and the window scrolls around it
    // when the screen is smaller than that. Without this the bottom of the editor
    // (the audition keyboard, the drum pads) simply falls off a small display.
    viewport.setViewedComponent (content, false);
    viewport.setScrollBarsShown (true, true);
    setContentNonOwned (&viewport, false);

    setBounds (placement::placeWindow ({ kDesignWidth, kDesignHeight },
                                       placement::displayAreaFor (placementContext)));

    setShowDialogs (showDialogs);
}

SynthWindow::~SynthWindow()
{
    editor = nullptr;
    samplerEditor = nullptr;
    subtractiveEditor = nullptr;
    minimoogEditor = nullptr;
    wavetableEditor = nullptr;
    drumEditor = nullptr;
    granularEditor = nullptr;
    hybridEditor = nullptr;

    // The viewport does not own the panel, so the window deletes it - after the
    // editor pointers above have been dropped, so nothing is left dangling.
    viewport.setViewedComponent (nullptr, false);
    delete content;
    content = nullptr;
}

void SynthWindow::resized()
{
    juce::DocumentWindow::resized();

    if (content == nullptr)
        return;

    // Never smaller than the size the editors are designed for, never smaller than
    // the window: a smaller window then scrolls rather than squeezing the panel.
    content->setSize (juce::jmax (currentPageWidth, viewport.getMaximumVisibleWidth()),
                      juce::jmax (currentPageHeight, viewport.getMaximumVisibleHeight()));
}

juce::Point<int> SynthWindow::getPanelSizeForTest() const
{
    return content != nullptr ? juce::Point<int> (content->getWidth(), content->getHeight())
                              : juce::Point<int>();
}

bool SynthWindow::isPanelScrollableForTest() const
{
    return content != nullptr
               && (content->getWidth() > viewport.getMaximumVisibleWidth()
                       || content->getHeight() > viewport.getMaximumVisibleHeight());
}

void SynthWindow::setShowDialogs (bool shouldShow)
{
    if (editor != nullptr)
        editor->setShowDialogs (shouldShow);

    if (samplerEditor != nullptr)
        samplerEditor->setShowDialogs (shouldShow);

    if (subtractiveEditor != nullptr)
        subtractiveEditor->setShowDialogs (shouldShow);

    if (minimoogEditor != nullptr)
        minimoogEditor->setShowDialogs (shouldShow);

    if (wavetableEditor != nullptr)
        wavetableEditor->setShowDialogs (shouldShow);

    if (drumEditor != nullptr)
        drumEditor->setShowDialogs (shouldShow);

    if (granularEditor != nullptr)
        granularEditor->setShowDialogs (shouldShow);
}

void SynthWindow::showForTrack (juce::int64 trackId, const juce::String& trackName, TrackKind kind)
{
    content->showFor (kind);

    // The wavetable page carries the waterfall view and is taller than the others:
    // the window follows the page it is showing (and still scrolls when the display
    // cannot give that much).
    const auto wantedWidth = pageWidthFor (kind);
    const auto wantedHeight = pageHeightFor (kind);

    if (wantedWidth != currentPageWidth || wantedHeight != currentPageHeight)
    {
        currentPageWidth = wantedWidth;
        currentPageHeight = wantedHeight;
        setBounds (placement::placeWindow ({ currentPageWidth, currentPageHeight },
                                           placement::displayAreaFor (placementContext)));
        resized();
    }

    // A switch with no default, so a new TrackKind cannot fall through to the FM
    // editor by accident.
    switch (kind)
    {
        case TrackKind::sampler:     samplerEditor->setTrack (trackId); break;
        case TrackKind::subtractive: subtractiveEditor->setTrack (trackId); break;
        case TrackKind::minimoog:    minimoogEditor->setTrack (trackId); break;
        case TrackKind::wavetable:   wavetableEditor->setTrack (trackId); break;
        case TrackKind::drums:       drumEditor->setTrack (trackId); break;
        case TrackKind::granular:    granularEditor->setTrack (trackId); break;
        case TrackKind::hybrid:      hybridEditor->setTrack (trackId); break;
        case TrackKind::synth:
        case TrackKind::audio:
        case TrackKind::vst:
            editor->setTrack (trackId);
            break;
    }

    setName (NI_TXT ("音色 · ") + trackName);

    setVisible (true);
    toFront (true);
}

void SynthWindow::closeButtonPressed()
{
    // The window is reused, so closing just hides it.
    setVisible (false);
}

} // namespace hmdaw
