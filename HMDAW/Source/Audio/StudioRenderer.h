#pragma once

#include "StudioRenderPlan.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace hmdaw
{

/** Real-time studio router: source buffers -> speakers/mics -> mixer -> master. */
class StudioRenderer
{
public:
    StudioRenderer() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void setPlan (std::shared_ptr<const studio::RenderPlan> newPlan);
    void reset();

    void beginChunk (int numSamples);
    void addSource (juce::int64 trackId, const juce::AudioBuffer<float>& buffer,
                    int numSamples, float expression = 1.0f);
    void render (juce::AudioBuffer<float>& output, int offset, int numSamples);

    int getActivePathCount() const noexcept { return activePaths; }

private:
    struct DelayState
    {
        std::vector<float> buffer;
        int write = 0;
        int delaySamples = 0;
        float gain = 0.0f;
        float lp = 0.0f;
        float state = 0.0f;
        int micChannel = -1;
    };

    const studio::RenderSource* routeForTrack (juce::int64 trackId) const;

    std::shared_ptr<const studio::RenderPlan> plan;
    struct ChannelState { float hp[2] {}; float low[2] {}; };
    std::vector<std::vector<float>> channelInputs;
    std::vector<ChannelState> channelStates;
    std::vector<DelayState> delays;
    double sampleRate = 48000.0;
    int maxBlock = 1024;
    int activePaths = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioRenderer)
};

} // namespace hmdaw
