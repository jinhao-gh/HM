#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace hmdaw
{

class MixerChannelStrip;

/** The page-style SSL 4000E-inspired mixer. One strip per studio mixer channel. */
class MixerPage : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    MixerPage (ProjectDocument& document, AudioEngine& audioEngine, AppState& appState);
    ~MixerPage() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();
    void timerCallback() override;

    void addChannel();
    void removeChannel (studio::Id channelId);

    // Test hooks.
    int getChannelCountForTest() const;
    juce::int64 getChannelIdForTest (int index) const;
    juce::String getChannelInputNameForTest (int index) const;
    juce::Rectangle<int> getStripBoundsForTest (int index) const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildStrips();
    std::vector<studio::Id> currentChannelIds() const;

    ProjectDocument& document;
    AudioEngine& audio;
    AppState& appState;

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::TextButton addChannelButton { NI_TXT ("添加通道") };
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::Slider masterGain;
    juce::Label masterLabel;
    juce::OwnedArray<MixerChannelStrip> strips;
    std::vector<studio::Id> lastIds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPage)
};

} // namespace hmdaw
