#include "StudioRenderer.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace hmdaw
{

namespace
{
constexpr int kMaxPaths = 512;

float panLeft (double pan)
{
    return (float) juce::jlimit (0.0, 1.0, 1.0 - juce::jmax (0.0, pan));
}

float panRight (double pan)
{
    return (float) juce::jlimit (0.0, 1.0, 1.0 + juce::jmin (0.0, pan));
}
} // namespace

void StudioRenderer::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock = juce::jlimit (32, 16384, maxBlockSize);

    channelInputs.assign ((size_t) kMaxChannels, std::vector<float> ((size_t) maxBlock * 2, 0.0f));
    channelStates.assign ((size_t) kMaxChannels, ChannelState {});
    const auto maxDelay = juce::jmax (1, (int) std::ceil (sampleRate * 0.25) + 2);
    delays.clear();
    delays.resize ((size_t) kMaxPaths);

    for (auto& delay : delays)
    {
        delay.buffer.assign ((size_t) maxDelay, 0.0f);
        delay.write = 0;
        delay.gain = 0.0f;
        delay.micChannel = -1;
    }

    activePaths = 0;
}

void StudioRenderer::setPlan (std::shared_ptr<const studio::RenderPlan> newPlan)
{
    plan = std::move (newPlan);
    activePaths = plan != nullptr ? (int) plan->paths.size() : 0;

    if (plan == nullptr)
        return;

    for (int i = 0; i < (int) delays.size(); ++i)
    {
        auto& delay = delays[(size_t) i];
        std::fill (delay.buffer.begin(), delay.buffer.end(), 0.0f);
        delay.write = 0;
        delay.state = 0.0f;

        if (i < (int) plan->paths.size())
        {
            const auto& path = plan->paths[(size_t) i];
            delay.delaySamples = juce::jlimit (0, (int) delay.buffer.size() - 1,
                                                path.delaySamples);
            delay.gain = path.gain;
            delay.lp = path.lp;
            delay.micChannel = path.micIndex < (int) plan->mics.size()
                                   ? plan->mics[(size_t) path.micIndex].channelIndex
                                   : -1;
        }
        else
        {
            delay.gain = 0.0f;
            delay.micChannel = -1;
        }
    }
}

void StudioRenderer::reset()
{
    for (auto& delay : delays)
    {
        std::fill (delay.buffer.begin(), delay.buffer.end(), 0.0f);
        delay.write = 0;
        delay.state = 0.0f;
    }
}

void StudioRenderer::beginChunk (int numSamples)
{
    for (auto& channel : channelInputs)
        std::fill (channel.begin(), channel.begin() + (std::ptrdiff_t) numSamples * 2, 0.0f);
}

const studio::RenderSource* StudioRenderer::routeForTrack (juce::int64 trackId) const
{
    if (plan == nullptr)
        return nullptr;

    for (const auto& route : plan->sources)
        if (route.trackId == trackId)
            return &route;

    return nullptr;
}

void StudioRenderer::addSource (juce::int64 trackId, const juce::AudioBuffer<float>& buffer,
                                int numSamples, float expression)
{
    const auto* route = routeForTrack (trackId);

    if (route == nullptr || plan == nullptr)
        return;

    const auto gain = (float) route->volume * juce::jlimit (0.0f, 1.0f, expression);
    const auto leftGain = gain * panLeft (route->pan);
    const auto rightGain = gain * panRight (route->pan);

    if (route->mode == studio::SourceMode::mixer)
    {
        auto addToChannel = [&] (int channelIndex)
        {
            if (channelIndex < 0 || channelIndex >= kMaxChannels)
                return;

            auto& channel = channelInputs[(size_t) channelIndex];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto l = buffer.getSample (0, i);
                const auto r = buffer.getNumChannels() > 1 ? buffer.getSample (1, i) : l;
                channel[(size_t) i * 2] += l * leftGain;
                channel[(size_t) i * 2 + 1] += r * rightGain;
            }
        };

        if (! route->directChannels.empty())
        {
            for (const auto channel : route->directChannels)
                addToChannel (channel);
        }
        else
        {
            addToChannel (route->directChannel);
        }

        return;
    }

    struct EmitterSignal
    {
        int emitterIndex = -1;
        std::function<float (int)> sample;
    };

    std::vector<EmitterSignal> emitters;

    if (route->mode == studio::SourceMode::external)
    {
        emitters.push_back ({ route->emitterIndex, [&buffer] (int i)
        {
            const auto l = buffer.getSample (0, i);
            const auto r = buffer.getNumChannels() > 1 ? buffer.getSample (1, i) : l;
            return 0.5f * (l + r);
        } });
    }
    else if (route->mode == studio::SourceMode::speaker)
    {
        auto appendSpeaker = [&] (int speakerIndex)
        {
            if (speakerIndex < 0 || speakerIndex >= (int) plan->speakers.size())
                return;

            const auto& speaker = plan->speakers[(size_t) speakerIndex];
            const auto sizeFactor = 0.55 + 0.45 * juce::jlimit (3.0, 18.0, speaker.wooferInches) / 8.0;
            const auto limiter = std::pow (10.0, juce::jlimit (-18.0, 18.0,
                                                               speaker.maxSplDb - 100.0) / 40.0);
            const auto speakerGain = (float) (std::pow (10.0, speaker.inputGainDb / 20.0)
                                              * juce::jlimit (0.0, 2.0, speaker.volume)
                                              * sizeFactor * limiter
                                              * std::pow (10.0, (speaker.lfDb + speaker.hfDb) / 40.0));

            if (speaker.units == 2)
            {
                emitters.push_back ({ speaker.emitterIndex[0], [&buffer, leftGain, speakerGain] (int i)
                {
                    return buffer.getSample (0, i) * leftGain * speakerGain;
                } });
                emitters.push_back ({ speaker.emitterIndex[1], [&buffer, rightGain, speakerGain] (int i)
                {
                    const auto r = buffer.getNumChannels() > 1 ? buffer.getSample (1, i)
                                                                 : buffer.getSample (0, i);
                    return r * rightGain * speakerGain;
                } });
            }
            else
            {
                emitters.push_back ({ speaker.emitterIndex[0], [&buffer, gain, speakerGain] (int i)
                {
                    const auto l = buffer.getSample (0, i);
                    const auto r = buffer.getNumChannels() > 1 ? buffer.getSample (1, i) : l;
                    return 0.5f * (l + r) * gain * speakerGain;
                } });
            }
        };

        if (! route->speakerIndices.empty())
        {
            for (const auto speakerIndex : route->speakerIndices)
                appendSpeaker (speakerIndex);
        }
        else
        {
            appendSpeaker (route->speakerIndex);
        }
    }

    for (const auto& emitter : emitters)
    {
        const auto emitterIndex = emitter.emitterIndex;

        for (int p = 0; p < (int) plan->paths.size(); ++p)
        {
            const auto& path = plan->paths[(size_t) p];

            if (path.emitterIndex != emitterIndex)
                continue;

            auto& delay = delays[(size_t) p];

            if (delay.buffer.empty())
                continue;

            for (int i = 0; i < numSamples; ++i)
            {
                delay.buffer[(size_t) delay.write] = emitter.sample (i);
                delay.write = (delay.write + 1) % (int) delay.buffer.size();
                int read = delay.write - delay.delaySamples;

                if (read < 0)
                    read += (int) delay.buffer.size();

                const auto raw = delay.buffer[(size_t) read];
                delay.state += delay.lp * (raw - delay.state);

                if (path.micIndex < (int) plan->mics.size())
                {
                    const auto micChannel = plan->mics[(size_t) path.micIndex].channelIndex;

                    if (micChannel >= 0)
                    {
                        auto& channel = channelInputs[(size_t) juce::jlimit (0, kMaxChannels - 1,
                                                                              micChannel)];
                        const auto value = delay.state * delay.gain;
                        channel[(size_t) i * 2] += value;
                        channel[(size_t) i * 2 + 1] += value;
                    }
                }
            }
        }
    }
}

void StudioRenderer::render (juce::AudioBuffer<float>& output, int offset, int numSamples)
{
    if (plan == nullptr)
        return;

    const auto channels = juce::jmin ((int) plan->channels.size(), kMaxChannels);
    bool anySolo = false;

    for (int i = 0; i < channels; ++i)
        anySolo = anySolo || plan->channels[(size_t) i].solo;

    for (int c = 0; c < channels; ++c)
    {
        const auto& channelPlan = plan->channels[(size_t) c];

        if (channelPlan.mute || (anySolo && ! channelPlan.solo))
            continue;

        const auto trim = (float) std::pow (10.0, channelPlan.trimDb / 20.0);
        const auto fader = (float) std::pow (10.0, channelPlan.faderDb / 20.0);
        const auto left = fader * trim * panLeft (channelPlan.pan);
        const auto right = fader * trim * panRight (channelPlan.pan);
        const auto& channel = channelInputs[(size_t) c];
        auto& state = channelStates[(size_t) c];

        const auto hpCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                       * juce::jlimit (20.0, 500.0, channelPlan.hpfHz)
                                                       / sampleRate));
        const auto lowCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                        * 250.0 / sampleRate));
        const auto lowGain = (float) std::pow (10.0, channelPlan.lowDb / 20.0);
        float channelPeak = 0.0f;
        const auto highGain = (float) std::pow (10.0, channelPlan.highDb / 20.0);

        for (int i = 0; i < numSamples; ++i)
        {
            float l = channel[(size_t) i * 2];
            float r = channel[(size_t) i * 2 + 1];

            // One-pole DC/rumble blocker plus a very simple low/high split.
            for (int side = 0; side < 2; ++side)
            {
                auto& hp = state.hp[side];
                auto& low = state.low[side];
                const auto in = side == 0 ? l : r;
                hp += hpCoeff * (in - hp);
                const auto highPassed = channelPlan.hpfHz <= 20.0 ? in : in - hp;
                low += lowCoeff * (highPassed - low);
                const auto band = low * lowGain + (highPassed - low) * highGain;
                (side == 0 ? l : r) = band;
            }

            auto outL = l * plan->masterGain * left;
            auto outR = r * plan->masterGain * right;

            if (std::abs (outL) < 1.0e-24f) outL = 0.0f;
            if (std::abs (outR) < 1.0e-24f) outR = 0.0f;
            channelPeak = juce::jmax (channelPeak, std::abs (outL), std::abs (outR));

            output.addSample (0, offset + i, outL);

            if (output.getNumChannels() > 1)
                output.addSample (1, offset + i, outR);
        }

        channelPeaks[(size_t) c].store (channelPeak, std::memory_order_relaxed);
        masterPeak.store (juce::jmax (masterPeak.load (std::memory_order_relaxed), channelPeak),
                          std::memory_order_relaxed);
    }
}

} // namespace hmdaw
