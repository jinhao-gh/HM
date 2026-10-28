#include "EffectsWindow.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"

namespace nidaw
{

EffectsWindow::Content::Content (EffectsBar& barToShow) : bar (barToShow)
{
    addAndMakeVisible (bar);
}

void EffectsWindow::Content::resized()
{
    bar.setBounds (getLocalBounds().reduced (6, 4));
}

EffectsWindow::EffectsWindow (EffectsBar& barToShow, juce::Component* context)
    : juce::DocumentWindow (NI_TXT ("效果器"), theme::panelBackground,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      bar (barToShow),
      placementContext (context)
{
    setUsingNativeTitleBar (true);
    setResizable (false, false);

    content = new Content (bar);
    setContentOwned (content, false);
}

EffectsWindow::~EffectsWindow()
{
    // The bar belongs to MainComponent, so it must not go with the window.
    content = nullptr;
}

juce::Point<int> EffectsWindow::getContentSizeForTest() const
{
    return content != nullptr ? juce::Point<int> (content->getWidth(), content->getHeight())
                              : juce::Point<int>();
}

void EffectsWindow::resized()
{
    juce::DocumentWindow::resized();

    if (content == nullptr)
        return;

    // The content fills the window; the window's own size is worked out in
    // showForTrack from how tall the chain is.
    content->setBounds (getLocalBounds());
}

void EffectsWindow::showForTrack (juce::int64 trackId, const juce::String& trackName)
{
    bar.setTrack (trackId);
    setName (NI_TXT ("效果器 · ") + trackName);

    const auto wanted = juce::Point<int> (kDesignWidth, juce::jmax (70, bar.getPreferredHeight()));

    if (! isVisible())
        setBounds (placement::placeWindow (wanted, placement::displayAreaFor (placementContext)));
    else
        setSize (wanted.x, wanted.y);

    setVisible (true);
    toFront (true);
}

void EffectsWindow::closeButtonPressed()
{
    setVisible (false);
}

} // namespace nidaw
