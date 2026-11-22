#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace hmdaw
{

class StudioCanvas;
class StudioInspector;

/** The studio page: a 2D room with instruments, speakers, microphones,
    interfaces and walls. */
class StudioPage : public juce::Component,
                   private juce::ChangeListener
{
public:
    StudioPage (ProjectDocument& document, AudioEngine& audioEngine, AppState& appState);
    ~StudioPage() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();

    void addSpeaker (int units);
    void addMicrophone();
    void addSplitter();
    void addCombiner();
    void addMixerInput();
    void setWallMode (bool on);
    bool isWallMode() const noexcept { return wallMode; }
    void setConnectMode (bool on);
    bool isConnectMode() const noexcept { return connectMode; }

    void openInspector (studio::Id nodeId);
    void deleteNode (studio::Id nodeId);
    void closeInspector (studio::Id nodeId);
    void refreshInspectors();

    // ---- test hooks -------------------------------------------------------
    int getSourceNodeCountForTest() const;
    int getSpeakerCountForTest() const;
    int getMicrophoneCountForTest() const;
    int getWallCountForTest() const;
    int getInspectorCountForTest() const;
    juce::Point<double> getNodePositionForTest (studio::Id nodeId) const;
    void dragNodeForTest (studio::Id nodeId, double x, double y);
    studio::Id getSourceNodeIdForTrackForTest (juce::int64 trackId) const;

    std::function<void (studio::Id)> onNodeSelected;

private:
    friend class StudioCanvas;
    friend class StudioInspector;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void createConnection (studio::Id fromNode, studio::Id toNode);
    void addNodeAndSelect (const juce::String& editName,
                           const std::function<studio::Id (studio::Scene&)>& create);

    ProjectDocument& document;
    AudioEngine& audio;
    AppState& appState;

    std::unique_ptr<StudioCanvas> canvas;
    juce::OwnedArray<StudioInspector> inspectors;

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::TextButton addSpeakerButton { NI_TXT ("添加音箱") };
    juce::TextButton addMicButton { NI_TXT ("添加麦克风") };
    juce::TextButton addSplitterButton { NI_TXT ("一分多") };
    juce::TextButton addCombinerButton { NI_TXT ("多合一") };
    juce::TextButton addInputButton { NI_TXT ("混音台接口") };
    juce::TextButton wallButton { NI_TXT ("墙壁") };
    juce::TextButton connectButton { NI_TXT ("连接") };
    bool wallMode = false;
    bool connectMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPage)
};

} // namespace hmdaw
