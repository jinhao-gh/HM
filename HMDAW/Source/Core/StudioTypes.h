#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

namespace hmdaw::studio
{

using Id = juce::int64;

struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

enum class SourceMode
{
    external = 0,
    speaker = 1,
    mixer = 2
};

enum class MicPattern
{
    omni = 0,
    cardioid = 1,
    supercardioid = 2,
    hypercardioid = 3,
    figure8 = 4
};

/** One arrangement instrument as a point in the studio. */
struct Source
{
    Id id = 0;
    Id trackId = 0;
    Vec2 position;
    double angleDeg = 0.0;
    SourceMode mode = SourceMode::external;
    double pan = 0.0;              // only speaker / mixer
    double directivityDeg = 360.0; // 360 = omni, no rear rejection
};

/** One or two speaker cabinets. `units == 2` is a labelled L/R pair. */
struct Speaker
{
    Id id = 0;
    juce::String name;
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
};

struct Microphone
{
    Id id = 0;
    juce::String name;
    Vec2 position;
    double angleDeg = 0.0;
    MicPattern pattern = MicPattern::cardioid;
    double inputGainDb = 0.0;
};

/** A named mixer input interface; channels select one of these. */
struct MixerInput
{
    Id id = 0;
    juce::String name;
    Vec2 position { 4.0, 3.0 };
};

struct Splitter
{
    Id id = 0;
    juce::String name;
    int outputs = 2;
    Vec2 position { -4.0, 2.0 };
};

struct Combiner
{
    Id id = 0;
    juce::String name;
    int inputs = 2;
    Vec2 position { -4.0, 0.0 };
};

struct Wall
{
    Id id = 0;
    Vec2 a, b;
    double reflection = 0.4;
    double occlusion = 0.2;
};

struct Connection
{
    Id id = 0;
    Id fromNode = 0;
    Id toNode = 0;
    int fromPort = 0;
    int toPort = 0;
};

struct MixerChannel
{
    Id id = 0;
    juce::String name;
    Id inputId = 0;
    double trimDb = 0.0;
    double hpfHz = 20.0;
    double lowDb = 0.0;
    double highDb = 0.0;
    double faderDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

struct Mixer
{
    std::vector<MixerChannel> channels;
    double masterGainDb = 0.0;
};

struct Scene
{
    std::vector<Source> sources;
    std::vector<Speaker> speakers;
    std::vector<Microphone> microphones;
    std::vector<MixerInput> mixerInputs;
    std::vector<Splitter> splitters;
    std::vector<Combiner> combiners;
    std::vector<Wall> walls;
    std::vector<Connection> connections;
    Mixer mixer;
    Id nextId = 1;
};

inline Id allocateId (Scene& scene)
{
    return scene.nextId++;
}

inline Source* findSourceForTrack (Scene& scene, Id trackId)
{
    for (auto& source : scene.sources)
        if (source.trackId == trackId)
            return &source;

    return nullptr;
}

inline const Source* findSourceForTrack (const Scene& scene, Id trackId)
{
    for (const auto& source : scene.sources)
        if (source.trackId == trackId)
            return &source;

    return nullptr;
}

inline Source* findSource (Scene& scene, Id sourceId)
{
    for (auto& source : scene.sources)
        if (source.id == sourceId)
            return &source;

    return nullptr;
}

inline Speaker* findSpeaker (Scene& scene, Id speakerId)
{
    for (auto& speaker : scene.speakers)
        if (speaker.id == speakerId)
            return &speaker;

    return nullptr;
}

inline Microphone* findMicrophone (Scene& scene, Id micId)
{
    for (auto& mic : scene.microphones)
        if (mic.id == micId)
            return &mic;

    return nullptr;
}

inline MixerInput* findMixerInput (Scene& scene, Id inputId)
{
    for (auto& input : scene.mixerInputs)
        if (input.id == inputId)
            return &input;

    return nullptr;
}

inline MixerChannel* findMixerChannel (Scene& scene, Id channelId)
{
    for (auto& channel : scene.mixer.channels)
        if (channel.id == channelId)
            return &channel;

    return nullptr;
}

/** The scene every new project starts with: two crossed cardioid microphones at
    the origin, each feeding its own mixer input and channel. */
inline Scene makeDefaultScene()
{
    Scene scene;

    const auto leftMicId = allocateId (scene);
    const auto rightMicId = allocateId (scene);
    const auto leftInputId = allocateId (scene);
    const auto rightInputId = allocateId (scene);

    Microphone left;
    left.id = leftMicId;
    left.name = juce::String::fromUTF8 ("麦克风 左");
    left.position = { 0.0, 0.0 };
    left.angleDeg = 135.0;
    left.pattern = MicPattern::cardioid;
    scene.microphones.push_back (left);

    Microphone right;
    right.id = rightMicId;
    right.name = juce::String::fromUTF8 ("麦克风 右");
    right.position = { 0.0, 0.0 };
    right.angleDeg = 45.0;
    right.pattern = MicPattern::cardioid;
    scene.microphones.push_back (right);

    MixerInput leftIn;
    leftIn.id = leftInputId;
    leftIn.name = juce::String::fromUTF8 ("左");
    scene.mixerInputs.push_back (leftIn);

    MixerInput rightIn;
    rightIn.id = rightInputId;
    rightIn.name = juce::String::fromUTF8 ("右");
    scene.mixerInputs.push_back (rightIn);

    MixerChannel leftChannel;
    leftChannel.id = allocateId (scene);
    leftChannel.name = juce::String::fromUTF8 ("左");
    leftChannel.inputId = leftInputId;
    leftChannel.pan = -1.0;
    scene.mixer.channels.push_back (leftChannel);

    MixerChannel rightChannel;
    rightChannel.id = allocateId (scene);
    rightChannel.name = juce::String::fromUTF8 ("右");
    rightChannel.inputId = rightInputId;
    rightChannel.pan = 1.0;
    scene.mixer.channels.push_back (rightChannel);

    auto connect = [&scene] (Id from, Id to)
    {
        Connection c;
        c.id = allocateId (scene);
        c.fromNode = from;
        c.toNode = to;
        scene.connections.push_back (c);
    };

    connect (leftMicId, leftInputId);
    connect (rightMicId, rightInputId);

    return scene;
}

/** Adds the default source for a track that does not have one yet. */
inline Source& ensureTrackSource (Scene& scene, Id trackId, int trackIndex)
{
    if (auto* existing = findSourceForTrack (scene, trackId))
        return *existing;

    Source source;
    source.id = allocateId (scene);
    source.trackId = trackId;
    source.mode = SourceMode::external;
    source.pan = 0.0;
    source.directivityDeg = 360.0;

    // In front of the microphones; stagger so several new instruments do not
    // land exactly on top of each other.
    source.position.x = (double) (trackIndex % 6) * 1.2 - 3.0;
    source.position.y = 2.0 + (double) (trackIndex / 6) * 1.2;

    scene.sources.push_back (source);
    return scene.sources.back();
}

inline void removeTrackSource (Scene& scene, Id trackId)
{
    const auto* source = findSourceForTrack (scene, trackId);

    if (source == nullptr)
        return;

    const auto sourceId = source->id;

    scene.sources.erase (std::remove_if (scene.sources.begin(), scene.sources.end(),
                                         [trackId] (const Source& s)
                                         { return s.trackId == trackId; }),
                         scene.sources.end());

    scene.connections.erase (std::remove_if (scene.connections.begin(),
                                             scene.connections.end(),
                                             [sourceId] (const Connection& c)
                                             {
                                                 return c.fromNode == sourceId
                                                        || c.toNode == sourceId;
                                             }),
                             scene.connections.end());
}

} // namespace hmdaw::studio
