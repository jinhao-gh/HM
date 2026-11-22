#include "StudioRenderPlan.h"

#include <algorithm>
#include <cmath>

namespace hmdaw::studio
{

namespace
{
constexpr double kSpeedOfSound = 343.0;
constexpr double kMaxDelaySeconds = 0.25;
constexpr int kMaxPaths = 512;
constexpr int kMaxPathsPerPair = 8;

struct Emitter
{
    Vec2 position;
    double angleDeg = 0.0;
    double directivityDeg = 180.0;
    int speakerIndex = -1;
    int unit = 0;
};

double coneGain (double sourceAngleDeg, double directivityDeg, const Vec2& from,
                 const Vec2& to)
{
    if (directivityDeg >= 359.0)
        return 1.0;

    const auto dx = to.x - from.x, dy = to.y - from.y;
    const auto distance = std::sqrt (dx * dx + dy * dy);

    if (distance < 1.0e-9)
        return 1.0;

    const auto angle = juce::radiansToDegrees (std::atan2 (dy, dx));
    auto delta = std::abs (angle - sourceAngleDeg);
    delta = std::fmod (delta + 180.0, 360.0) - 180.0;

    if (std::abs (delta) >= directivityDeg * 0.5)
        return 0.0;

    const auto normalised = std::abs (delta) / (directivityDeg * 0.5);
    return std::pow (std::cos (normalised * juce::MathConstants<double>::pi * 0.5), 1.5);
}

double micGain (MicPattern pattern, double micAngleDeg, const Vec2& mic,
                const Vec2& emitter)
{
    const auto dx = emitter.x - mic.x, dy = emitter.y - mic.y;
    const auto distance = std::sqrt (dx * dx + dy * dy);

    if (distance < 1.0e-9)
        return 1.0;

    const auto angle = juce::radiansToDegrees (std::atan2 (dy, dx));
    auto delta = juce::degreesToRadians (std::abs (angle - micAngleDeg));
    delta = std::fmod (delta + juce::MathConstants<double>::pi,
                       juce::MathConstants<double>::twoPi)
            - juce::MathConstants<double>::pi;

    switch (pattern)
    {
        case MicPattern::omni: return 1.0;
        case MicPattern::cardioid: return juce::jmax (0.0, 0.5 + 0.5 * std::cos (delta));
        case MicPattern::supercardioid: return juce::jmax (0.0, 0.37 + 0.63 * std::cos (delta));
        case MicPattern::hypercardioid: return juce::jmax (0.0, 0.25 + 0.75 * std::cos (delta));
        case MicPattern::figure8: return std::abs (std::cos (delta));
    }

    return 1.0;
}

bool segmentIntersection (const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d)
{
    const auto rX = b.x - a.x, rY = b.y - a.y;
    const auto sX = d.x - c.x, sY = d.y - c.y;
    const auto denom = rX * sY - rY * sX;

    if (std::abs (denom) < 1.0e-9)
        return false;

    const auto qpx = c.x - a.x, qpy = c.y - a.y;
    const auto t = (qpx * sY - qpy * sX) / denom;
    const auto u = (qpx * rY - qpy * rX) / denom;
    return t > 0.001 && t < 0.999 && u > 0.001 && u < 0.999;
}

double occlusionFor (const Scene& scene, const Vec2& a, const Vec2& b)
{
    double transmission = 1.0;

    for (const auto& wall : scene.walls)
        if (segmentIntersection (a, b, wall.a, wall.b))
            transmission *= 1.0 - juce::jlimit (0.0, 1.0, wall.occlusion);

    return transmission;
}

bool reflectPoint (const Vec2& emitter, const Wall& wall, const Vec2& mic, Vec2& point)
{
    const auto wx = wall.b.x - wall.a.x, wy = wall.b.y - wall.a.y;
    const auto len2 = wx * wx + wy * wy;

    if (len2 < 1.0e-9)
        return false;

    // Mirror the emitter across the wall line.
    const auto t = ((emitter.x - wall.a.x) * wx + (emitter.y - wall.a.y) * wy) / len2;
    const auto footX = wall.a.x + t * wx, footY = wall.a.y + t * wy;
    const auto imageX = 2.0 * footX - emitter.x, imageY = 2.0 * footY - emitter.y;

    const auto sx = mic.x - imageX, sy = mic.y - imageY;
    const auto denom = sx * wy - sy * wx;

    if (std::abs (denom) < 1.0e-9)
        return false;

    const auto rx = wall.a.x - imageX, ry = wall.a.y - imageY;
    const auto u = (rx * wy - ry * wx) / denom;

    if (u <= 0.0 || u >= 1.0)
        return false;

    point = { imageX + u * sx, imageY + u * sy };

    const auto along = std::sqrt ((point.x - wall.a.x) * (point.x - wall.a.x)
                                  + (point.y - wall.a.y) * (point.y - wall.a.y));
    const auto length = std::sqrt (len2);
    return along > 0.01 && along < length - 0.01;
}

double distanceGain (double distance)
{
    return 1.0 / (1.0 + juce::jmax (0.0, distance));
}

double lowPassForDistance (double distance, double sampleRate)
{
    const auto cutoff = juce::jlimit (1200.0, 20000.0, 20000.0 / (1.0 + distance * 0.35));
    return 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate);
}
} // namespace

std::shared_ptr<const RenderPlan> buildRenderPlan (const Project& project, double sampleRate)
{
    auto plan = std::make_shared<RenderPlan>();
    const auto& scene = project.studio;

    plan->masterGain = (float) std::pow (10.0, juce::jlimit (-60.0, 6.0,
                                                             (double) project.masterVolumeDb) / 20.0);

    for (const auto& channel : scene.mixer.channels)
    {
        RenderChannel c;
        c.id = channel.id;
        c.trimDb = channel.trimDb;
        c.hpfHz = channel.hpfHz;
        c.lowDb = channel.lowDb;
        c.highDb = channel.highDb;
        c.faderDb = channel.faderDb;
        c.pan = channel.pan;
        c.mute = channel.mute;
        c.solo = channel.solo;
        plan->channels.push_back (c);
    }

    auto channelForInput = [&] (Id inputId)
    {
        if (inputId == 0)
            return -1;

        for (int i = 0; i < (int) scene.mixer.channels.size(); ++i)
            if (scene.mixer.channels[(size_t) i].inputId == inputId)
                return i;

        return -1;
    };

    enum class NodeKind { other, speaker, input, splitter, combiner };

    auto kindOf = [&] (Id id)
    {
        for (const auto& s : scene.speakers) if (s.id == id) return NodeKind::speaker;
        for (const auto& i : scene.mixerInputs) if (i.id == id) return NodeKind::input;
        for (const auto& s : scene.splitters) if (s.id == id) return NodeKind::splitter;
        for (const auto& c : scene.combiners) if (c.id == id) return NodeKind::combiner;
        return NodeKind::other;
    };

    auto resolvedTargets = [&] (Id fromNode, NodeKind wanted)
    {
        std::vector<Id> result;
        std::vector<Id> stack { fromNode };
        std::vector<Id> visited;

        while (! stack.empty())
        {
            const auto current = stack.back();
            stack.pop_back();

            if (std::find (visited.begin(), visited.end(), current) != visited.end())
                continue;

            visited.push_back (current);

            for (const auto& c : scene.connections)
                if (c.fromNode == current)
                {
                    const auto kind = kindOf (c.toNode);

                    if (kind == wanted)
                    {
                        if (std::find (result.begin(), result.end(), c.toNode) == result.end())
                            result.push_back (c.toNode);
                    }
                    else if (kind == NodeKind::splitter || kind == NodeKind::combiner)
                    {
                        stack.push_back (c.toNode);
                    }
                }
        }

        return result;
    };

    std::vector<Emitter> emitters;

    // Microphones first: a mic that has no input still exists, but it contributes
    // nothing to the mixer.
    for (const auto& mic : scene.microphones)
    {
        RenderMic m;
        m.position = mic.position;
        m.angleDeg = mic.angleDeg;
        m.pattern = mic.pattern;
        m.gainDb = mic.inputGainDb;

        const auto inputs = resolvedTargets (mic.id, NodeKind::input);
        bool added = false;

        for (const auto inputId : inputs)
        {
            const auto channel = channelForInput (inputId);

            if (channel >= 0)
            {
                m.channelIndex = channel;
                plan->mics.push_back (m);
                added = true;
            }
        }

        if (! added)
        {
            m.channelIndex = -1;
            plan->mics.push_back (m);
        }
    }

    for (const auto& speaker : scene.speakers)
    {
        RenderSpeaker s;
        s.id = speaker.id;
        s.position = speaker.position;
        s.angleDeg = speaker.angleDeg;
        s.units = speaker.units;
        s.spreadMeters = speaker.spreadMeters;
        s.inputGainDb = speaker.inputGainDb;
        s.volume = speaker.volume;
        s.lfDb = speaker.lfDb;
        s.hfDb = speaker.hfDb;
        s.wooferInches = speaker.wooferInches;
        s.crossoverHz = speaker.crossoverHz;
        s.maxSplDb = speaker.maxSplDb;
        s.directivityDeg = speaker.directivityDeg;

        const auto perpendicular = juce::degreesToRadians (speaker.angleDeg + 90.0);
        const auto offset = speaker.spreadMeters * 0.5;

        for (int unit = 0; unit < juce::jlimit (1, 2, speaker.units); ++unit)
        {
            Emitter e;
            e.position = speaker.position;
            e.angleDeg = speaker.angleDeg;
            e.directivityDeg = speaker.directivityDeg;
            e.speakerIndex = (int) plan->speakers.size();

            if (speaker.units == 2)
                e.position = { speaker.position.x + std::cos (perpendicular)
                                                    * (unit == 0 ? -offset : offset),
                               speaker.position.y + std::sin (perpendicular)
                                                    * (unit == 0 ? -offset : offset) };

            s.emitterIndex[unit] = (int) emitters.size();
            emitters.push_back (e);
        }

        plan->speakers.push_back (s);
    }

    for (const auto& track : project.tracks)
    {
        const auto* source = findSourceForTrack (scene, track.id);

        if (source == nullptr)
            continue;

        RenderSource route;
        route.trackId = track.id;
        route.mode = source->mode;
        route.volume = std::pow (10.0, juce::jlimit (-60.0f, 6.0f, track.volumeDb) / 20.0);
        route.pan = source->pan;

        if (source->mode == SourceMode::external)
        {
            Emitter e;
            e.position = source->position;
            e.angleDeg = source->angleDeg;
            e.directivityDeg = source->directivityDeg;
            route.emitterIndex = (int) emitters.size();
            emitters.push_back (e);
        }
        else
        {
            if (source->mode == SourceMode::mixer)
            {
                for (const auto input : resolvedTargets (source->id, NodeKind::input))
                {
                    const auto channel = channelForInput (input);

                    if (channel >= 0)
                    {
                        route.directChannels.push_back (channel);

                        if (route.directChannel < 0)
                            route.directChannel = channel;
                    }
                }
            }
            else
            {
                for (const auto target : resolvedTargets (source->id, NodeKind::speaker))
                    for (int i = 0; i < (int) scene.speakers.size(); ++i)
                        if (scene.speakers[(size_t) i].id == target)
                        {
                            route.speakerIndices.push_back (i);

                            if (route.speakerIndex < 0)
                                route.speakerIndex = i;
                            break;
                        }
            }
        }

        plan->sources.push_back (route);
    }

    std::vector<RenderPath> paths;
    paths.reserve ((size_t) emitters.size() * plan->mics.size() * 4);

    auto addPath = [&] (int emitterIndex, int micIndex, double pathLength,
                        double gain, const Vec2& emitterPosition, const Vec2& micPosition,
                        bool reflection)
    {
        if (gain <= 0.00001 || pathLength > kSpeedOfSound * kMaxDelaySeconds)
            return;

        const auto& mic = plan->mics[(size_t) micIndex];
        const auto pattern = micGain (mic.pattern, mic.angleDeg, micPosition, emitterPosition);
        const auto micLinear = std::pow (10.0, mic.gainDb / 20.0);
        const auto wallGain = reflection ? gain : gain;

        RenderPath p;
        p.emitterIndex = emitterIndex;
        p.micIndex = micIndex;
        p.delaySamples = juce::jmax (0, (int) std::lround (pathLength / kSpeedOfSound * sampleRate));
        p.gain = (float) (wallGain * pattern * micLinear * distanceGain (pathLength));
        p.lp = (float) lowPassForDistance (pathLength, sampleRate);

        if (p.gain > 0.0f)
            paths.push_back (p);
    };

    for (int e = 0; e < (int) emitters.size(); ++e)
    {
        const auto& emitter = emitters[(size_t) e];

        for (int m = 0; m < (int) plan->mics.size(); ++m)
        {
            const auto& mic = plan->mics[(size_t) m];

            if (mic.channelIndex < 0)
                continue;

            int pathCount = 0;
            const auto directDx = mic.position.x - emitter.position.x;
            const auto directDy = mic.position.y - emitter.position.y;
            const auto directDistance = std::sqrt (directDx * directDx + directDy * directDy);
            const auto directivity = coneGain (emitter.angleDeg, emitter.directivityDeg,
                                               emitter.position, mic.position);
            const auto occlusion = occlusionFor (scene, emitter.position, mic.position);

            addPath (e, m, directDistance, directivity * occlusion,
                     emitter.position, mic.position, false);
            ++pathCount;

            for (const auto& wall : scene.walls)
            {
                if (pathCount >= kMaxPathsPerPair || wall.reflection <= 0.0)
                    break;

                Vec2 reflectionPoint;

                if (! reflectPoint (emitter.position, wall, mic.position, reflectionPoint))
                    continue;

                const auto first = std::sqrt ((reflectionPoint.x - emitter.position.x)
                                                  * (reflectionPoint.x - emitter.position.x)
                                              + (reflectionPoint.y - emitter.position.y)
                                                  * (reflectionPoint.y - emitter.position.y));
                const auto second = std::sqrt ((mic.position.x - reflectionPoint.x)
                                                   * (mic.position.x - reflectionPoint.x)
                                               + (mic.position.y - reflectionPoint.y)
                                                   * (mic.position.y - reflectionPoint.y));
                const auto directivityAtWall = coneGain (emitter.angleDeg,
                                                         emitter.directivityDeg,
                                                         emitter.position, reflectionPoint);
                const auto transmission = occlusionFor (scene, reflectionPoint, mic.position);

                addPath (e, m, first + second,
                         wall.reflection * directivityAtWall * transmission,
                         reflectionPoint, mic.position, true);
                ++pathCount;
            }
        }
    }

    if ((int) paths.size() > kMaxPaths)
    {
        std::sort (paths.begin(), paths.end(),
                   [] (const RenderPath& a, const RenderPath& b)
                   { return a.gain > b.gain; });
        paths.resize (kMaxPaths);
    }

    plan->paths = std::move (paths);
    return plan;
}

} // namespace hmdaw::studio
