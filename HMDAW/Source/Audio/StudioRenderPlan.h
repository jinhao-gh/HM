#pragma once

#include "../Core/Types.h"

#include <memory>
#include <vector>

namespace hmdaw::studio
{

struct RenderChannel
{
    Id id = 0;
    double trimDb = 0.0;
    double hpfHz = 20.0;
    double lowDb = 0.0;
    double highDb = 0.0;
    double faderDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

struct RenderMic
{
    Vec2 position;
    double angleDeg = 0.0;
    MicPattern pattern = MicPattern::cardioid;
    double gainDb = 0.0;
    int channelIndex = -1;
};

struct RenderSpeaker
{
    Id id = 0;
    Vec2 position;
    double angleDeg = 0.0;
    int units = 1;
    double spreadMeters = 0.6;
    double inputGainDb = 0.0;
    double volume = 0.8;
    double lfDb = 0.0;
    double hfDb = 0.0;
    double wooferInches = 8.0;
    double crossoverHz = 2200.0;
    double maxSplDb = 100.0;
    double directivityDeg = 90.0;
    int emitterIndex[2] { -1, -1 };
};

struct RenderSource
{
    Id trackId = 0;
    SourceMode mode = SourceMode::external;
    double volume = 0.8;
    double pan = 0.0;
    int directChannel = -1;
    int speakerIndex = -1;
    int emitterIndex = -1;
};

struct RenderPath
{
    int emitterIndex = 0;
    int micIndex = 0;
    int delaySamples = 0;
    float gain = 0.0f;
    float lp = 0.0f;
};

struct RenderPlan
{
    std::vector<RenderSource> sources;
    std::vector<RenderSpeaker> speakers;
    std::vector<RenderMic> mics;
    std::vector<RenderChannel> channels;
    std::vector<RenderPath> paths;
    float masterGain = 1.0f;
};

std::shared_ptr<const RenderPlan> buildRenderPlan (const Project& project, double sampleRate);

} // namespace hmdaw::studio
