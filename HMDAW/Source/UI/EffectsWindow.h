#pragma once

#include "EffectsBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The track's insert chain in a window of its own.

    It used to be the lower half of a column down the right of the arrangement; the
    column is gone, so the same bar (the same instance - MainComponent owns it) is
    shown here, opened from the 「效果」 button in a lane header. The window is reused
    and only hidden when it is closed, like the instrument windows.
*/
class EffectsWindow : public juce::DocumentWindow
{
public:
    EffectsWindow (EffectsBar& barToShow, juce::Component* context);
    ~EffectsWindow() override;

    void resized() override;
    void closeButtonPressed() override;

    /** Points the window (and the bar inside it) at one track and shows it. */
    void showForTrack (juce::int64 trackId, const juce::String& trackName);

    EffectsBar& getBar() noexcept { return bar; }

    /** The size the window lays its content out at, for the self test. */
    juce::Point<int> getContentSizeForTest() const;

    static constexpr int kDesignWidth = EffectsBar::panelWidth + 12;

private:
    /** Holds the unowned bar and keeps it at the width the window is designed for. */
    class Content : public juce::Component
    {
    public:
        explicit Content (EffectsBar& barToShow);
        void resized() override;

        EffectsBar& bar;
    };

    EffectsBar& bar;
    Content* content = nullptr;
    juce::Component* placementContext = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsWindow)
};

} // namespace hmdaw
