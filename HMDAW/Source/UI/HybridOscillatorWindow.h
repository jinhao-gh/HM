#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectDocument.h"
#include "SynthControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

class HybridOscillatorThumbnail : public juce::Component
{
public:
    HybridOscillatorThumbnail (ProjectDocument& document, AudioEngine& audio);
    void setSource (juce::int64 trackId, int sourceIndex);
    void refresh() { repaint(); }
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isThreeDForTest() const noexcept { return threeD; }
    void toggleForTest() { threeD = ! threeD; repaint(); }
    int getActiveFrameForTest() const noexcept { return lastActiveFrame; }
    juce::Rectangle<float> getThreeDExtentForTest() const noexcept { return lastThreeDExtent; }
    std::function<void()> onOpen;

private:
    ProjectDocument& document;
    AudioEngine& audio;
    juce::int64 trackId = 0;
    int source = 0;
    bool threeD = false;
    int lastActiveFrame = -1;
    juce::Rectangle<float> lastThreeDExtent;
};

class HybridOscillatorPanel : public juce::Component
{
public:
    HybridOscillatorPanel (ProjectDocument& document, AudioEngine& audio);
    void setSource (juce::int64 trackId, int sourceIndex);
    std::function<void()> onBack;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh();

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
    };

    class Visual : public juce::Component
    {
    public:
        explicit Visual (HybridOscillatorPanel& owner) : panel (owner) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool threeD = false;
    private:
        enum class DragHandle { none, start, end, loopStart, loopEnd, crossfade };
        DragHandle handleAt (juce::Point<int> point) const;
        juce::Rectangle<int> samplePlot() const;
        HybridOscillatorPanel& panel;
        DragHandle dragging = DragHandle::none;
    };

    InstrumentTrack* track();
    void configureKnob (Knob& k, const juce::String& text, double lo, double hi);
    void push();
    void setModeLabel();

    ProjectDocument& document;
    AudioEngine& audio;
    juce::int64 trackId = 0;
    int source = 0;
    juce::Label title, mode;
    juce::TextButton back;
    Visual visual { *this };
    std::array<Knob, 9> knobs;
    bool updating = false;

    friend class Visual;
};

class HybridOscillatorWindow : public juce::DocumentWindow
{
public:
    HybridOscillatorWindow (ProjectDocument& document, AudioEngine& audio,
                            juce::Component* placementContext);
    void showFor (juce::int64 trackId, int sourceIndex);
    void closeButtonPressed() override { setVisible (false); }

private:
    HybridOscillatorPanel panel;
    juce::Component* context = nullptr;
};

} // namespace hmdaw
