#include "StudioScene.h"

#include <algorithm>
#include <cmath>

namespace hmdaw
{

namespace
{
int readInt (const juce::var& v, const char* name, int fallback)
{
    if (v.getDynamicObject() == nullptr)
        return fallback;

    const auto value = v.getDynamicObject()->getProperty (name);
    return value.isVoid() ? fallback : (int) value;
}

juce::int64 readInt64 (const juce::var& v, const char* name, juce::int64 fallback)
{
    if (v.getDynamicObject() == nullptr)
        return fallback;

    const auto value = v.getDynamicObject()->getProperty (name);
    return value.isVoid() ? fallback : (juce::int64) value;
}

double readDouble (const juce::var& v, const char* name, double fallback)
{
    if (v.getDynamicObject() == nullptr)
        return fallback;

    const auto value = v.getDynamicObject()->getProperty (name);
    return value.isVoid() ? fallback : (double) value;
}

bool readBool (const juce::var& v, const char* name, bool fallback)
{
    if (v.getDynamicObject() == nullptr)
        return fallback;

    const auto value = v.getDynamicObject()->getProperty (name);
    return value.isVoid() ? fallback : (bool) value;
}

juce::String readString (const juce::var& v, const char* name, const juce::String& fallback)
{
    if (v.getDynamicObject() == nullptr)
        return fallback;

    const auto value = v.getDynamicObject()->getProperty (name);
    return value.isVoid() ? fallback : value.toString();
}

juce::var vecToVar (const studio::Vec2& v)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("x", v.x);
    o->setProperty ("y", v.y);
    return juce::var (o);
}

studio::Vec2 vecFromVar (const juce::var& v)
{
    return { readDouble (v, "x", 0.0), readDouble (v, "y", 0.0) };
}

juce::var wallToVar (const studio::Wall& w)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", w.id);
    o->setProperty ("a", vecToVar (w.a));
    o->setProperty ("b", vecToVar (w.b));
    o->setProperty ("reflection", w.reflection);
    o->setProperty ("occlusion", w.occlusion);
    return juce::var (o);
}

studio::Wall wallFromVar (const juce::var& v)
{
    studio::Wall w;
    w.id = readInt64 (v, "id", 0);
    w.a = vecFromVar (v.getDynamicObject() != nullptr
                          ? v.getDynamicObject()->getProperty ("a") : juce::var());
    w.b = vecFromVar (v.getDynamicObject() != nullptr
                          ? v.getDynamicObject()->getProperty ("b") : juce::var());
    w.reflection = juce::jlimit (0.0, 1.0, readDouble (v, "reflection", 0.4));
    w.occlusion = juce::jlimit (0.0, 1.0, readDouble (v, "occlusion", 0.2));
    return w;
}

juce::var connectionToVar (const studio::Connection& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("from", c.fromNode);
    o->setProperty ("to", c.toNode);
    o->setProperty ("fromPort", c.fromPort);
    o->setProperty ("toPort", c.toPort);
    return juce::var (o);
}

studio::Connection connectionFromVar (const juce::var& v)
{
    studio::Connection c;
    c.id = readInt64 (v, "id", 0);
    c.fromNode = readInt64 (v, "from", 0);
    c.toNode = readInt64 (v, "to", 0);
    c.fromPort = readInt (v, "fromPort", 0);
    c.toPort = readInt (v, "toPort", 0);
    return c;
}

juce::var mixerChannelToVar (const studio::MixerChannel& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("name", c.name);
    o->setProperty ("inputId", c.inputId);
    o->setProperty ("trimDb", c.trimDb);
    o->setProperty ("hpfHz", c.hpfHz);
    o->setProperty ("lowDb", c.lowDb);
    o->setProperty ("highDb", c.highDb);
    o->setProperty ("faderDb", c.faderDb);
    o->setProperty ("pan", c.pan);
    o->setProperty ("mute", c.mute);
    o->setProperty ("solo", c.solo);
    return juce::var (o);
}

studio::MixerChannel mixerChannelFromVar (const juce::var& v)
{
    studio::MixerChannel c;
    c.id = readInt64 (v, "id", 0);
    c.name = readString (v, "name", "通道");
    c.inputId = readInt64 (v, "inputId", 0);
    c.trimDb = juce::jlimit (-24.0, 24.0, readDouble (v, "trimDb", 0.0));
    c.hpfHz = juce::jlimit (20.0, 500.0, readDouble (v, "hpfHz", 20.0));
    c.lowDb = juce::jlimit (-15.0, 15.0, readDouble (v, "lowDb", 0.0));
    c.highDb = juce::jlimit (-15.0, 15.0, readDouble (v, "highDb", 0.0));
    c.faderDb = juce::jlimit (-60.0, 6.0, readDouble (v, "faderDb", 0.0));
    c.pan = juce::jlimit (-1.0, 1.0, readDouble (v, "pan", 0.0));
    c.mute = readBool (v, "mute", false);
    c.solo = readBool (v, "solo", false);
    return c;
}
} // namespace

juce::var studioSceneToVar (const studio::Scene& scene)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("nextId", scene.nextId);

    juce::Array<juce::var> sources;

    for (const auto& s : scene.sources)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", s.id);
        vo->setProperty ("trackId", s.trackId);
        vo->setProperty ("position", vecToVar (s.position));
        vo->setProperty ("angleDeg", s.angleDeg);
        vo->setProperty ("mode", (int) s.mode);
        vo->setProperty ("pan", s.pan);
        vo->setProperty ("directivityDeg", s.directivityDeg);
        sources.add (juce::var (vo));
    }

    o->setProperty ("sources", sources);

    juce::Array<juce::var> speakers;

    for (const auto& s : scene.speakers)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", s.id);
        vo->setProperty ("name", s.name);
        vo->setProperty ("position", vecToVar (s.position));
        vo->setProperty ("angleDeg", s.angleDeg);
        vo->setProperty ("units", s.units);
        vo->setProperty ("spreadMeters", s.spreadMeters);
        vo->setProperty ("inputGainDb", s.inputGainDb);
        vo->setProperty ("volume", s.volume);
        vo->setProperty ("lfDb", s.lfDb);
        vo->setProperty ("hfDb", s.hfDb);
        vo->setProperty ("wooferInches", s.wooferInches);
        vo->setProperty ("crossoverHz", s.crossoverHz);
        vo->setProperty ("maxSplDb", s.maxSplDb);
        vo->setProperty ("directivityDeg", s.directivityDeg);
        speakers.add (juce::var (vo));
    }

    o->setProperty ("speakers", speakers);

    juce::Array<juce::var> mics;

    for (const auto& m : scene.microphones)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", m.id);
        vo->setProperty ("name", m.name);
        vo->setProperty ("position", vecToVar (m.position));
        vo->setProperty ("angleDeg", m.angleDeg);
        vo->setProperty ("pattern", (int) m.pattern);
        vo->setProperty ("inputGainDb", m.inputGainDb);
        mics.add (juce::var (vo));
    }

    o->setProperty ("microphones", mics);

    juce::Array<juce::var> inputs;

    for (const auto& i : scene.mixerInputs)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", i.id);
        vo->setProperty ("name", i.name);
        vo->setProperty ("position", vecToVar (i.position));
        inputs.add (juce::var (vo));
    }

    o->setProperty ("mixerInputs", inputs);

    juce::Array<juce::var> splitters;

    for (const auto& s : scene.splitters)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", s.id);
        vo->setProperty ("name", s.name);
        vo->setProperty ("outputs", s.outputs);
        vo->setProperty ("position", vecToVar (s.position));
        splitters.add (juce::var (vo));
    }

    o->setProperty ("splitters", splitters);

    juce::Array<juce::var> combiners;

    for (const auto& c : scene.combiners)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", c.id);
        vo->setProperty ("name", c.name);
        vo->setProperty ("inputs", c.inputs);
        vo->setProperty ("position", vecToVar (c.position));
        combiners.add (juce::var (vo));
    }

    o->setProperty ("combiners", combiners);

    juce::Array<juce::var> walls;

    for (const auto& w : scene.walls)
        walls.add (wallToVar (w));

    o->setProperty ("walls", walls);

    juce::Array<juce::var> connections;

    for (const auto& c : scene.connections)
        connections.add (connectionToVar (c));

    o->setProperty ("connections", connections);

    auto* mixer = new juce::DynamicObject();
    mixer->setProperty ("masterGainDb", scene.mixer.masterGainDb);

    juce::Array<juce::var> channels;

    for (const auto& c : scene.mixer.channels)
        channels.add (mixerChannelToVar (c));

    mixer->setProperty ("channels", channels);
    o->setProperty ("mixer", juce::var (mixer));

    return juce::var (o);
}

bool studioSceneFromVar (const juce::var& value, studio::Scene& scene, juce::String& error)
{
    if (value.getDynamicObject() == nullptr)
    {
        error = "工作室数据无法识别";
        return false;
    }

    scene = studio::makeDefaultScene();
    scene.sources.clear();
    scene.speakers.clear();
    scene.microphones.clear();
    scene.mixerInputs.clear();
    scene.splitters.clear();
    scene.combiners.clear();
    scene.walls.clear();
    scene.connections.clear();
    scene.mixer.channels.clear();
    scene.nextId = juce::jmax ((studio::Id) 1, readInt64 (value, "nextId", 1));

    if (auto* a = value.getDynamicObject()->getProperty ("sources").getArray())
        for (const auto& v : *a)
        {
            studio::Source s;
            s.id = readInt64 (v, "id", 0);
            s.trackId = readInt64 (v, "trackId", 0);
            s.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            s.angleDeg = readDouble (v, "angleDeg", 0.0);
            s.mode = (studio::SourceMode) juce::jlimit (0, 2, readInt (v, "mode", 0));
            s.pan = juce::jlimit (-1.0, 1.0, readDouble (v, "pan", 0.0));
            s.directivityDeg = juce::jlimit (0.0, 360.0, readDouble (v, "directivityDeg", 360.0));
            scene.sources.push_back (s);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("speakers").getArray())
        for (const auto& v : *a)
        {
            studio::Speaker s;
            s.id = readInt64 (v, "id", 0);
            s.name = readString (v, "name", "音箱");
            s.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            s.angleDeg = readDouble (v, "angleDeg", 0.0);
            s.units = juce::jlimit (1, 2, readInt (v, "units", 1));
            s.spreadMeters = juce::jlimit (0.0, 10.0, readDouble (v, "spreadMeters", 0.6));
            s.inputGainDb = juce::jlimit (-24.0, 24.0, readDouble (v, "inputGainDb", 0.0));
            s.volume = juce::jlimit (0.0, 2.0, readDouble (v, "volume", 0.8));
            s.lfDb = juce::jlimit (-18.0, 18.0, readDouble (v, "lfDb", 0.0));
            s.hfDb = juce::jlimit (-18.0, 18.0, readDouble (v, "hfDb", 0.0));
            s.wooferInches = juce::jlimit (3.0, 18.0, readDouble (v, "wooferInches", 8.0));
            s.crossoverHz = juce::jlimit (300.0, 8000.0, readDouble (v, "crossoverHz", 2200.0));
            s.maxSplDb = juce::jlimit (70.0, 140.0, readDouble (v, "maxSplDb", 100.0));
            s.directivityDeg = juce::jlimit (30.0, 360.0, readDouble (v, "directivityDeg", 90.0));
            scene.speakers.push_back (s);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("microphones").getArray())
        for (const auto& v : *a)
        {
            studio::Microphone m;
            m.id = readInt64 (v, "id", 0);
            m.name = readString (v, "name", "麦克风");
            m.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            m.angleDeg = readDouble (v, "angleDeg", 0.0);
            m.pattern = (studio::MicPattern) juce::jlimit (0, 4, readInt (v, "pattern", 1));
            m.inputGainDb = juce::jlimit (-24.0, 24.0, readDouble (v, "inputGainDb", 0.0));
            scene.microphones.push_back (m);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("mixerInputs").getArray())
        for (const auto& v : *a)
        {
            studio::MixerInput i;
            i.id = readInt64 (v, "id", 0);
            i.name = readString (v, "name", "输入");
            i.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            scene.mixerInputs.push_back (i);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("splitters").getArray())
        for (const auto& v : *a)
        {
            studio::Splitter s;
            s.id = readInt64 (v, "id", 0);
            s.name = readString (v, "name", "一分多");
            s.outputs = juce::jlimit (2, 16, readInt (v, "outputs", 2));
            s.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            scene.splitters.push_back (s);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("combiners").getArray())
        for (const auto& v : *a)
        {
            studio::Combiner c;
            c.id = readInt64 (v, "id", 0);
            c.name = readString (v, "name", "多合一");
            c.inputs = juce::jlimit (2, 16, readInt (v, "inputs", 2));
            c.position = vecFromVar (v.getDynamicObject() != nullptr
                                         ? v.getDynamicObject()->getProperty ("position") : juce::var());
            scene.combiners.push_back (c);
        }

    if (auto* a = value.getDynamicObject()->getProperty ("walls").getArray())
        for (const auto& v : *a)
            scene.walls.push_back (wallFromVar (v));

    if (auto* a = value.getDynamicObject()->getProperty ("connections").getArray())
        for (const auto& v : *a)
            scene.connections.push_back (connectionFromVar (v));

    if (const auto mixerVar = value.getDynamicObject()->getProperty ("mixer");
        mixerVar.getDynamicObject() != nullptr)
    {
        scene.mixer.masterGainDb = juce::jlimit (-60.0, 6.0,
                                                 readDouble (mixerVar, "masterGainDb", 0.0));

        if (auto* a = mixerVar.getDynamicObject()->getProperty ("channels").getArray())
            for (const auto& v : *a)
                scene.mixer.channels.push_back (mixerChannelFromVar (v));
    }

    // Stable next id also when a hand-edited file forgot it.
    auto bump = [&scene] (studio::Id id)
    {
        if (id >= scene.nextId)
            scene.nextId = id + 1;
    };

    for (const auto& x : scene.sources) bump (x.id);
    for (const auto& x : scene.speakers) bump (x.id);
    for (const auto& x : scene.microphones) bump (x.id);
    for (const auto& x : scene.mixerInputs) bump (x.id);
    for (const auto& x : scene.splitters) bump (x.id);
    for (const auto& x : scene.combiners) bump (x.id);
    for (const auto& x : scene.walls) bump (x.id);
    for (const auto& x : scene.connections) bump (x.id);
    for (const auto& x : scene.mixer.channels) bump (x.id);

    error.clear();
    return true;
}

void ensureStudioForProject (Project& project)
{
    if (project.studio.microphones.empty()
        || project.studio.mixerInputs.empty()
        || project.studio.mixer.channels.empty())
        project.studio = studio::makeDefaultScene();

    int index = 0;

    for (const auto& track : project.tracks)
        studio::ensureTrackSource (project.studio, track.id, index++);
}

void migrateLegacyStudio (Project& project)
{
    project.studio = studio::makeDefaultScene();
    project.studio.mixer.masterGainDb = juce::jlimit (-60.0, 6.0,
                                                      (double) project.masterVolumeDb);

    int index = 0;

    for (const auto& track : project.tracks)
    {
        auto& source = studio::ensureTrackSource (project.studio, track.id, index++);
        source.mode = studio::SourceMode::mixer;
        source.pan = track.pan;

        studio::MixerInput input;
        input.id = studio::allocateId (project.studio);
        input.name = track.name.isNotEmpty() ? track.name
                                             : "轨道 " + juce::String ((int) project.studio.mixerInputs.size() + 1);
        project.studio.mixerInputs.push_back (input);

        studio::MixerChannel channel;
        channel.id = studio::allocateId (project.studio);
        channel.name = input.name;
        channel.inputId = input.id;
        channel.pan = track.pan;
        project.studio.mixer.channels.push_back (channel);

        studio::Connection connection;
        connection.id = studio::allocateId (project.studio);
        connection.fromNode = source.id;
        connection.toNode = input.id;
        project.studio.connections.push_back (connection);
    }
}

} // namespace hmdaw
