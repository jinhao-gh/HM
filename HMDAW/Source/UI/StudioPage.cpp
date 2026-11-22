#if defined(_MSC_VER)
#pragma warning(disable: 4456)
#endif

#include "StudioPage.h"
#include "Theme.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace hmdaw
{

namespace
{
constexpr double kPixelsPerMeter = 48.0;
constexpr double kNodeHitRadiusPx = 40.0;

enum class NodeKind { none, source, speaker, mic, input, splitter, combiner, wall };

struct NodeRef
{
    NodeKind kind = NodeKind::none;
    studio::Id id = 0;
};

struct PortRef
{
    studio::Id nodeId = 0;
    int port = 0;
    bool isOutput = false;

    bool valid() const noexcept { return nodeId != 0; }
};

juce::String sourceName (const Project& project, const studio::Source& source)
{
    if (const auto* track = project.findTrack (source.trackId))
        return track->name;

    return juce::String::fromUTF8 ("乐器");
}

NodeRef findNode (const studio::Scene& scene, studio::Id id)
{
    for (const auto& s : scene.sources) if (s.id == id) return { NodeKind::source, id };
    for (const auto& s : scene.speakers) if (s.id == id) return { NodeKind::speaker, id };
    for (const auto& m : scene.microphones) if (m.id == id) return { NodeKind::mic, id };
    for (const auto& i : scene.mixerInputs) if (i.id == id) return { NodeKind::input, id };
    for (const auto& s : scene.splitters) if (s.id == id) return { NodeKind::splitter, id };
    for (const auto& c : scene.combiners) if (c.id == id) return { NodeKind::combiner, id };
    for (const auto& w : scene.walls) if (w.id == id) return { NodeKind::wall, id };
    return {};
}

studio::Vec2 nodePosition (const studio::Scene& scene, const NodeRef& node)
{
    switch (node.kind)
    {
        case NodeKind::source: for (const auto& s : scene.sources) if (s.id == node.id) return s.position; break;
        case NodeKind::speaker: for (const auto& s : scene.speakers) if (s.id == node.id) return s.position; break;
        case NodeKind::mic: for (const auto& m : scene.microphones) if (m.id == node.id) return m.position; break;
        case NodeKind::input:
            for (const auto& i : scene.mixerInputs) if (i.id == node.id) return i.position;
            break;
        case NodeKind::splitter:
            for (const auto& s : scene.splitters) if (s.id == node.id) return s.position;
            break;
        case NodeKind::combiner:
            for (const auto& c : scene.combiners) if (c.id == node.id) return c.position;
            break;
        case NodeKind::wall: break;
        default: break;
    }

    return {};
}

double distanceToSegment (const studio::Vec2& p, const studio::Vec2& a, const studio::Vec2& b)
{
    const auto vx = b.x - a.x, vy = b.y - a.y;
    const auto wx = p.x - a.x, wy = p.y - a.y;
    const auto len2 = vx * vx + vy * vy;

    if (len2 <= 1.0e-9)
        return std::sqrt (wx * wx + wy * wy);

    const auto t = juce::jlimit (0.0, 1.0, (wx * vx + wy * vy) / len2);
    const auto dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
    return std::sqrt (dx * dx + dy * dy);
}

juce::Path speakerCone (juce::Point<float> p, double angleDeg, double widthDeg, double radiusPx)
{
    juce::Path path;
    const auto a = juce::degreesToRadians (angleDeg);
    const auto half = juce::degreesToRadians (juce::jlimit (10.0, 180.0, widthDeg * 0.5));
    path.addPieSegment (p.x - (float) radiusPx, p.y - (float) radiusPx,
                        (float) radiusPx * 2.0f, (float) radiusPx * 2.0f,
                        (float) (-(a + half)), (float) (-(a - half)), 0.0f);
    return path;
}

juce::Path micLobe (juce::Point<float> p, double angleDeg, studio::MicPattern pattern,
                    double radiusPx)
{
    juce::Path path;
    constexpr int steps = 48;

    for (int i = 0; i <= steps; ++i)
    {
        const auto theta = juce::MathConstants<double>::twoPi * (double) i / steps;
        const auto rel = theta - juce::degreesToRadians (angleDeg);
        double gain = 1.0;

        switch (pattern)
        {
            case studio::MicPattern::omni: gain = 1.0; break;
            case studio::MicPattern::cardioid: gain = 0.5 + 0.5 * std::cos (rel); break;
            case studio::MicPattern::supercardioid: gain = 0.37 + 0.63 * std::cos (rel); break;
            case studio::MicPattern::hypercardioid: gain = 0.25 + 0.75 * std::cos (rel); break;
            case studio::MicPattern::figure8: gain = std::abs (std::cos (rel)); break;
        }

        gain = juce::jmax (0.0, gain);
        const auto r = radiusPx * gain;
        const auto x = p.x + (float) (std::cos (theta) * r);
        const auto y = p.y - (float) (std::sin (theta) * r);

        if (i == 0) path.startNewSubPath (x, y);
        else path.lineTo (x, y);
    }

    path.closeSubPath();
    return path;
}

juce::Colour kindColour (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::source: return juce::Colour (0xffe1a04b);
        case NodeKind::speaker: return juce::Colour (0xff61b6e8);
        case NodeKind::mic: return juce::Colour (0xff7fdd9b);
        case NodeKind::input: return juce::Colour (0xffcf8bff);
        case NodeKind::splitter:
        case NodeKind::combiner: return juce::Colour (0xffe4d26c);
        default: return theme::text;
    }
}
} // namespace

//==============================================================================
class StudioCanvas : public juce::Component
{
public:
    explicit StudioCanvas (StudioPage& ownerToUse) : owner (ownerToUse) {}

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void setWallMode (bool shouldDrawWalls)
    {
        wallMode = shouldDrawWalls;

        if (wallMode)
            connectMode = false;

        setMouseCursor (wallMode || connectMode ? juce::MouseCursor::CrosshairCursor
                                                : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void setConnectMode (bool shouldConnect)
    {
        connectMode = shouldConnect;
        connectFrom = 0;

        if (connectMode)
            wallMode = false;

        setMouseCursor (wallMode || connectMode ? juce::MouseCursor::CrosshairCursor
                                                : juce::MouseCursor::NormalCursor);
        repaint();
    }

    studio::Vec2 screenToWorld (juce::Point<float> p) const;
    juce::Point<float> worldToScreen (studio::Vec2 p) const;

private:
    NodeRef hitNode (studio::Vec2 p) const;
    NodeRef hitWall (studio::Vec2 p, double toleranceMeters) const;
    juce::Point<float> nodeScreen (const NodeRef& node) const;
    int outputPortCount (const NodeRef& node) const;
    int inputPortCount (const NodeRef& node) const;
    juce::Point<float> portScreen (const NodeRef& node, int port, bool isOutput) const;
    PortRef hitPort (juce::Point<float> screen) const;
    void drawJack (juce::Graphics& g, juce::Point<float> p, juce::Colour colour,
                   bool connected, bool hovered) const;
    void drawCable (juce::Graphics& g, const studio::Connection& connection) const;
    void drawCablePath (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                        juce::Colour colour, bool pending) const;

    StudioPage& owner;
    double pixelsPerMeter = kPixelsPerMeter;
    juce::Point<float> panPixels;
    bool wallMode = false;
    bool connectMode = false;
    studio::Id connectFrom = 0;
    bool draggingCable = false;
    PortRef cableFrom;
    PortRef cableTarget;
    juce::Point<float> cableMouse;

    bool draggingNode = false;
    studio::Id draggedId = 0;
    NodeKind draggedNodeKind = NodeKind::none;
    studio::Id lastAddedWall = 0;
    bool draggedSinceDown = false;
    studio::Id pendingInspectId = 0;
    studio::Vec2 dragStartWorld;
    studio::Vec2 dragStartNode;

    bool drawingWall = false;
    studio::Vec2 wallStart;
    studio::Vec2 wallEnd;
};

studio::Vec2 StudioCanvas::screenToWorld (juce::Point<float> p) const
{
    const auto centre = juce::Point<float> ((float) getWidth() * 0.5f,
                                             (float) getHeight() * 0.5f);
    const auto px = (p.x - centre.x - panPixels.x) / (float) pixelsPerMeter;
    const auto py = -(p.y - centre.y - panPixels.y) / (float) pixelsPerMeter;
    return { px, py };
}

juce::Point<float> StudioCanvas::worldToScreen (studio::Vec2 p) const
{
    const auto centre = juce::Point<float> ((float) getWidth() * 0.5f,
                                             (float) getHeight() * 0.5f);
    return { centre.x + panPixels.x + (float) (p.x * pixelsPerMeter),
             centre.y + panPixels.y - (float) (p.y * pixelsPerMeter) };
}

juce::Point<float> StudioCanvas::nodeScreen (const NodeRef& node) const
{
    const auto& scene = owner.document.getProject().studio;
    return worldToScreen (nodePosition (scene, node));
}

int StudioCanvas::outputPortCount (const NodeRef& node) const
{
    const auto& scene = owner.document.getProject().studio;

    if (node.kind == NodeKind::splitter)
        for (const auto& s : scene.splitters)
            if (s.id == node.id) return juce::jlimit (1, 8, s.outputs);

    if (node.kind == NodeKind::source || node.kind == NodeKind::mic
        || node.kind == NodeKind::combiner)
        return 1;

    return 0;
}

int StudioCanvas::inputPortCount (const NodeRef& node) const
{
    const auto& scene = owner.document.getProject().studio;

    if (node.kind == NodeKind::combiner)
        for (const auto& c : scene.combiners)
            if (c.id == node.id) return juce::jlimit (1, 8, c.inputs);

    if (node.kind == NodeKind::speaker || node.kind == NodeKind::input
        || node.kind == NodeKind::splitter)
        return 1;

    return 0;
}

juce::Point<float> StudioCanvas::portScreen (const NodeRef& node, int port, bool isOutput) const
{
    const auto p = nodeScreen (node);
    const auto count = isOutput ? outputPortCount (node) : inputPortCount (node);
    const auto index = juce::jlimit (0, juce::jmax (0, count - 1), port);

    if (node.kind == NodeKind::splitter || node.kind == NodeKind::combiner)
    {
        const auto y = p.y + ((float) index - (float) (count - 1) * 0.5f) * 16.0f;
        return { p.x + (isOutput ? 38.0f : -38.0f), y };
    }

    return { p.x + (isOutput ? 28.0f : -28.0f), p.y };
}

PortRef StudioCanvas::hitPort (juce::Point<float> screen) const
{
    const auto& scene = owner.document.getProject().studio;
    PortRef best;

    auto test = [&] (const NodeRef& node, int port, bool output)
    {
        const auto p = portScreen (node, port, output);
        if (p.getDistanceFrom (screen) <= 14.0f)
            best = { node.id, port, output };
    };

    for (const auto& s : scene.sources) test ({ NodeKind::source, s.id }, 0, true);
    for (const auto& m : scene.microphones) test ({ NodeKind::mic, m.id }, 0, true);
    for (const auto& s : scene.speakers) test ({ NodeKind::speaker, s.id }, 0, false);
    for (const auto& i : scene.mixerInputs) test ({ NodeKind::input, i.id }, 0, false);

    for (const auto& s : scene.splitters)
    {
        test ({ NodeKind::splitter, s.id }, 0, false);
        for (int p = 0; p < juce::jlimit (1, 8, s.outputs); ++p)
            test ({ NodeKind::splitter, s.id }, p, true);
    }

    for (const auto& c : scene.combiners)
    {
        test ({ NodeKind::combiner, c.id }, 0, true);
        for (int p = 0; p < juce::jlimit (1, 8, c.inputs); ++p)
            test ({ NodeKind::combiner, c.id }, p, false);
    }

    return best;
}

void StudioCanvas::drawCablePath (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                                  juce::Colour colour, bool pending) const
{
    const auto sag = juce::jlimit (20.0f, 90.0f, 18.0f + std::abs (b.x - a.x) * 0.28f);
    juce::Path path;
    path.startNewSubPath (a);
    path.cubicTo (a.x, a.y + sag, b.x, b.y + sag, b.x, b.y);

    g.setColour (juce::Colours::black.withAlpha (pending ? 0.45f : 0.7f));
    g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (pending ? 0.65f : 0.95f));
    g.strokePath (path, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void StudioCanvas::drawCable (juce::Graphics& g, const studio::Connection& connection) const
{
    const auto& scene = owner.document.getProject().studio;
    const auto fromNode = findNode (scene, connection.fromNode);
    const auto toNode = findNode (scene, connection.toNode);

    if (fromNode.kind == NodeKind::none || toNode.kind == NodeKind::none)
        return;

    const auto colour = juce::Colour::fromHSV ((float) (connection.id % 8) / 8.0f,
                                               0.55f, 0.9f, 1.0f);
    drawCablePath (g, portScreen (fromNode, connection.fromPort, true),
                   portScreen (toNode, connection.toPort, false), colour, false);
}

void StudioCanvas::drawJack (juce::Graphics& g, juce::Point<float> p, juce::Colour colour,
                             bool connected, bool hovered) const
{
    g.setColour (juce::Colour (0xff111116));
    g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
    g.setColour (hovered ? juce::Colours::white : colour.brighter (connected ? 0.35f : 0.05f));
    g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, hovered ? 2.5f : 2.0f);
    g.setColour (juce::Colour (0xff050508));
    g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
}

NodeRef StudioCanvas::hitNode (studio::Vec2 p) const
{
    const auto& scene = owner.document.getProject().studio;
    NodeRef best;
    auto bestDistance = juce::jmax (0.2, kNodeHitRadiusPx / pixelsPerMeter);

    auto test = [&] (NodeRef node, studio::Vec2 pos)
    {
        const auto dx = p.x - pos.x, dy = p.y - pos.y;
        const auto inside = std::abs (dx) <= 0.75 && std::abs (dy) <= 0.55;
        const auto d = inside ? 0.0 : std::sqrt (dx * dx + dy * dy);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = node;
        }
    };

    for (const auto& s : scene.sources) test ({ NodeKind::source, s.id }, s.position);
    for (const auto& s : scene.speakers) test ({ NodeKind::speaker, s.id }, s.position);
    for (const auto& m : scene.microphones) test ({ NodeKind::mic, m.id }, m.position);
    for (const auto& i : scene.mixerInputs) test ({ NodeKind::input, i.id }, i.position);
    for (const auto& s : scene.splitters) test ({ NodeKind::splitter, s.id }, s.position);
    for (const auto& c : scene.combiners) test ({ NodeKind::combiner, c.id }, c.position);

    return best;
}

NodeRef StudioCanvas::hitWall (studio::Vec2 p, double toleranceMeters) const
{
    const auto& scene = owner.document.getProject().studio;

    for (const auto& w : scene.walls)
        if (distanceToSegment (p, w.a, w.b) <= toleranceMeters)
            return { NodeKind::wall, w.id };

    return {};
}

void StudioCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff18181d));

    // grid
    g.setColour (juce::Colour (0xff26262f));
    for (int i = -20; i <= 20; ++i)
    {
        const auto x = worldToScreen ({ (double) i, 0.0 }).x;
        const auto y = worldToScreen ({ 0.0, (double) i }).y;
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
    }

    g.setColour (juce::Colour (0xff3c3c48));
    const auto origin = worldToScreen ({ 0.0, 0.0 });
    g.drawLine (origin.x - 18.0f, origin.y, origin.x + 18.0f, origin.y, 1.0f);
    g.drawLine (origin.x, origin.y - 18.0f, origin.x, origin.y + 18.0f, 1.0f);

    const auto& scene = owner.document.getProject().studio;

    for (const auto& w : scene.walls)
    {
        const auto a = worldToScreen (w.a);
        const auto b = worldToScreen (w.b);
        g.setColour (w.id == draggedId ? theme::accent
                                      : juce::Colour (0xffb8b09a));
        g.drawLine (a.x, a.y, b.x, b.y, 5.0f);
        g.setColour (juce::Colour (0xffd8cfb8));
        g.fillEllipse (a.x - 3.0f, a.y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse (b.x - 3.0f, b.y - 3.0f, 6.0f, 6.0f);
    }

    if (drawingWall)
    {
        const auto a = worldToScreen (wallStart);
        const auto b = worldToScreen (wallEnd);
        g.setColour (theme::accent);
        g.drawLine (a.x, a.y, b.x, b.y, 3.0f);
    }

    for (const auto& c : scene.connections)
        drawCable (g, c);

    for (const auto& source : scene.sources)
    {
        const auto p = worldToScreen (source.position);
        g.setColour (kindColour (NodeKind::source));
        g.fillEllipse (p.x - 20.0f, p.y - 20.0f, 40.0f, 40.0f);
        g.setColour (juce::Colours::black);
        g.setFont (theme::font (10.0f, true));
        g.drawText ("IN", juce::Rectangle<float> (p.x - 20.0f, p.y - 8.0f, 40.0f, 16.0f), juce::Justification::centred, false);
        g.setColour (theme::text);
        g.setFont (theme::font (11.0f));
        g.drawText (sourceName (owner.document.getProject(), source),
                    juce::Rectangle<float> (p.x - 50.0f, p.y + 24.0f, 100.0f, 16.0f),
                    juce::Justification::centred, false);
        g.setColour (juce::Colour (0xff8d8d9e));
        g.drawText (source.mode == studio::SourceMode::external ? NI_TXT ("外放")
                     : source.mode == studio::SourceMode::speaker ? NI_TXT ("音箱")
                                                                  : NI_TXT ("直连"),
                    juce::Rectangle<float> (p.x - 40.0f, p.y + 38.0f, 80.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    for (const auto& speaker : scene.speakers)
    {
        const auto p = worldToScreen (speaker.position);
        const auto angle = speaker.angleDeg;
        g.setColour (kindColour (NodeKind::speaker).withAlpha (0.14f));
        g.fillPath (speakerCone (p, angle, speaker.directivityDeg, 110.0f));
        g.setColour (kindColour (NodeKind::speaker));

        if (speaker.units == 2)
        {
            const auto side = juce::degreesToRadians (speaker.angleDeg + 90.0);
            const auto offset = (float) (speaker.spreadMeters * 0.5 * pixelsPerMeter);
            const auto p0 = juce::Point<float> (p.x + (float) std::cos (side) * offset,
                                                 p.y - (float) std::sin (side) * offset);
            const auto p1 = juce::Point<float> (p.x - (float) std::cos (side) * offset,
                                                 p.y + (float) std::sin (side) * offset);

            for (const auto& unit : { p0, p1 })
                g.fillRoundedRectangle (unit.x - 15.0f, unit.y - 13.0f, 30.0f, 26.0f, 4.0f);

            g.setColour (juce::Colours::black);
            g.setFont (theme::font (9.0f, true));
            g.drawText ("L", juce::Rectangle<float> (p0.x - 15.0f, p0.y - 7.0f, 30.0f, 14.0f),
                        juce::Justification::centred, false);
            g.drawText ("R", juce::Rectangle<float> (p1.x - 15.0f, p1.y - 7.0f, 30.0f, 14.0f),
                        juce::Justification::centred, false);
        }
        else
        {
            g.fillRoundedRectangle (p.x - 22.0f, p.y - 15.0f, 44.0f, 30.0f, 5.0f);
            g.setColour (juce::Colours::black);
            g.setFont (theme::font (10.0f, true));
            g.drawText ("SPK", juce::Rectangle<float> (p.x - 22.0f, p.y - 8.0f, 44.0f, 16.0f),
                        juce::Justification::centred, false);
        }

        g.setColour (theme::text);
        g.setFont (theme::font (11.0f));
        g.drawText (speaker.name,
                    juce::Rectangle<float> (p.x - 55.0f, p.y + 20.0f, 110.0f, 16.0f),
                    juce::Justification::centred, false);
    }

    for (const auto& mic : scene.microphones)
    {
        const auto p = worldToScreen (mic.position);
        g.setColour (kindColour (NodeKind::mic).withAlpha (0.10f));
        g.fillPath (micLobe (p, mic.angleDeg, mic.pattern, 100.0f));
        g.setColour (kindColour (NodeKind::mic));
        g.fillEllipse (p.x - 14.0f, p.y - 14.0f, 28.0f, 28.0f);
        g.setColour (juce::Colours::black);
        g.setFont (theme::font (9.0f, true));
        g.drawText ("MIC", juce::Rectangle<float> (p.x - 14.0f, p.y - 6.0f, 28.0f, 12.0f), juce::Justification::centred, false);
        g.setColour (theme::text);
        g.setFont (theme::font (11.0f));
        g.drawText (mic.name,
                    juce::Rectangle<float> (p.x - 55.0f, p.y + 18.0f, 110.0f, 16.0f),
                    juce::Justification::centred, false);
    }

    auto drawBox = [&g, this] (studio::Vec2 pos, const juce::String& text, NodeKind kind)
    {
        const auto p = worldToScreen (pos);
        g.setColour (kindColour (kind));
        g.fillRoundedRectangle (p.x - 30.0f, p.y - 13.0f, 60.0f, 26.0f, 4.0f);
        g.setColour (juce::Colours::black);
        g.setFont (theme::font (10.0f, true));
        g.drawText (text, juce::Rectangle<float> (p.x - 30.0f, p.y - 8.0f, 60.0f, 16.0f), juce::Justification::centred, false);
    };

    for (const auto& input : scene.mixerInputs)
        drawBox (input.position, input.name, NodeKind::input);

    for (const auto& splitter : scene.splitters)
        drawBox (splitter.position, splitter.name + " 1:" + juce::String (splitter.outputs),
                 NodeKind::splitter);

    for (const auto& combiner : scene.combiners)
        drawBox (combiner.position, combiner.name + " " + juce::String (combiner.inputs) + ":1",
                 NodeKind::combiner);

    if (draggingCable)
    {
        const auto fromNode = findNode (scene, cableFrom.nodeId);
        const auto from = portScreen (fromNode, cableFrom.port, true);
        const auto to = cableTarget.valid()
                            ? portScreen (findNode (scene, cableTarget.nodeId), cableTarget.port, false)
                            : cableMouse;
        drawCablePath (g, from, to, cableTarget.valid() ? theme::accent : juce::Colours::red, true);
    }

    auto isConnected = [&scene] (studio::Id nodeId, int port, bool output)
    {
        for (const auto& c : scene.connections)
            if (output ? (c.fromNode == nodeId && c.fromPort == port)
                       : (c.toNode == nodeId && c.toPort == port))
                return true;
        return false;
    };

    auto drawPorts = [&] (const NodeRef& node)
    {
        for (int p = 0; p < outputPortCount (node); ++p)
        {
            const auto point = portScreen (node, p, true);
            const auto hovered = cableTarget.valid() && cableTarget.nodeId == node.id
                                     && cableTarget.port == p && ! cableTarget.isOutput;
            drawJack (g, point, kindColour (node.kind), isConnected (node.id, p, true), hovered);
        }

        for (int p = 0; p < inputPortCount (node); ++p)
        {
            const auto point = portScreen (node, p, false);
            const auto hovered = cableTarget.valid() && cableTarget.nodeId == node.id
                                     && cableTarget.port == p && ! cableTarget.isOutput;
            drawJack (g, point, kindColour (node.kind), isConnected (node.id, p, false), hovered);
        }
    };

    for (const auto& s : scene.sources) drawPorts ({ NodeKind::source, s.id });
    for (const auto& m : scene.microphones) drawPorts ({ NodeKind::mic, m.id });
    for (const auto& s : scene.speakers) drawPorts ({ NodeKind::speaker, s.id });
    for (const auto& i : scene.mixerInputs) drawPorts ({ NodeKind::input, i.id });
    for (const auto& s : scene.splitters) drawPorts ({ NodeKind::splitter, s.id });
    for (const auto& c : scene.combiners) drawPorts ({ NodeKind::combiner, c.id });

    g.setColour (theme::dimText);
    g.setFont (theme::font (10.0f));
    g.drawText (NI_TXT ("拖动节点移动；从插孔拖出连线；右键插孔断开；滚轮缩放"),
                8, getHeight() - 20, getWidth() - 16, 16, juce::Justification::centredLeft);
}

void StudioCanvas::mouseDown (const juce::MouseEvent& e)
{
    const auto world = screenToWorld (e.position);
    dragStartWorld = world;

    if (wallMode)
    {
        drawingWall = true;
        wallStart = world;
        wallEnd = world;

        if (const auto wall = hitWall (world, 0.25); wall.kind == NodeKind::wall)
            if (const auto* w = [&]() -> const studio::Wall*
                {
                    for (const auto& x : owner.document.getProject().studio.walls)
                        if (x.id == wall.id) return &x;
                    return nullptr;
                }())
                wallStart = (std::abs (w->a.x - world.x) + std::abs (w->a.y - world.y)
                             < std::abs (w->b.x - world.x) + std::abs (w->b.y - world.y))
                                ? w->a : w->b;

        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        const auto port = hitPort (e.position);

        if (port.valid())
        {
            owner.deleteConnectionsAtPort (port.nodeId, port.port, port.isOutput);
            repaint();
        }

        return;
    }

    const auto outputPort = hitPort (e.position);

    if (outputPort.valid() && outputPort.isOutput)
    {
        draggingCable = true;
        cableFrom = outputPort;
        cableTarget = {};
        cableMouse = e.position;
        repaint();
        return;
    }

    const auto node = hitNode (world);

    if (node.kind != NodeKind::none)
    {
        draggedId = node.id;
        draggedNodeKind = node.kind;
        dragStartNode = nodePosition (owner.document.getProject().studio, node);
        draggingNode = true;
        draggedSinceDown = false;
        pendingInspectId = node.id;
        owner.document.beginEdit (NI_TXT ("移动工作室节点"));
        if (owner.onNodeSelected) owner.onNodeSelected (node.id);
        repaint();
        return;
    }

    if (const auto wall = hitWall (world, 0.2); wall.kind == NodeKind::wall)
    {
        owner.openInspector (wall.id);
        repaint();
    }
}

void StudioCanvas::mouseDrag (const juce::MouseEvent& e)
{
    const auto world = screenToWorld (e.position);

    if (draggingCable)
    {
        cableMouse = e.position;
        const auto hit = hitPort (e.position);
        cableTarget = (hit.valid() && ! hit.isOutput) ? hit : PortRef {};
        repaint();
        return;
    }

    if (wallMode && drawingWall)
    {
        wallEnd = world;
        repaint();
        return;
    }

    if (! draggingNode)
        return;

    if (e.getDistanceFromDragStart() > 2)
        draggedSinceDown = true;

    const auto dx = world.x - dragStartWorld.x;
    const auto dy = world.y - dragStartWorld.y;
    const auto node = findNode (owner.document.getProject().studio, draggedId);

    owner.document.applyEdit (NI_TXT ("移动工作室节点"), [dx, dy, this] (Project& project)
    {
        auto& scene = project.studio;
        const studio::Vec2 p { dragStartNode.x + dx, dragStartNode.y + dy };

        auto move = [this, p] (auto& nodes)
        {
            for (auto& n : nodes)
                if (n.id == draggedId)
                {
                    n.position = p;
                    return true;
                }

            return false;
        };

        if (move (scene.sources)) return;
        if (move (scene.speakers)) return;
        if (move (scene.microphones)) return;
        if (move (scene.mixerInputs)) return;
        if (move (scene.splitters)) return;
        move (scene.combiners);
    });

    repaint();
}

void StudioCanvas::mouseUp (const juce::MouseEvent&)
{
    if (draggingCable)
    {
        const auto from = cableFrom;
        const auto to = cableTarget;
        draggingCable = false;
        cableFrom = {};
        cableTarget = {};

        if (from.valid() && to.valid())
            owner.createConnection (from.nodeId, from.port, to.nodeId, to.port);

        repaint();
        return;
    }

    if (wallMode && drawingWall)
    {
        drawingWall = false;

        for (const auto& existing : owner.document.getProject().studio.walls)
        {
            for (const auto& endpoint : { existing.a, existing.b })
            {
                const auto dx = endpoint.x - wallEnd.x, dy = endpoint.y - wallEnd.y;

                if (std::sqrt (dx * dx + dy * dy) < 0.25)
                    wallEnd = endpoint;
            }
        }

        if (std::abs (wallEnd.x - wallStart.x) + std::abs (wallEnd.y - wallStart.y) > 0.05)
        {
            owner.document.applyEdit (NI_TXT ("添加墙壁"), [this] (Project& project)
            {
                studio::Wall w;
                w.id = studio::allocateId (project.studio);
                w.a = wallStart;
                w.b = wallEnd;
                project.studio.walls.push_back (w);
                lastAddedWall = w.id;
            });

            if (lastAddedWall != 0)
                owner.openInspector (lastAddedWall);
        }

        repaint();
        return;
    }

    if (draggingNode)
    {
        draggingNode = false;
        owner.document.notifyEdit();

        if (! draggedSinceDown && pendingInspectId != 0)
            owner.openInspector (pendingInspectId);

        pendingInspectId = 0;
        repaint();
    }
}

void StudioCanvas::mouseWheelMove (const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel)
{
    const auto before = screenToWorld (e.position);
    pixelsPerMeter = juce::jlimit (18.0, 140.0,
                                   pixelsPerMeter * (1.0 + (double) wheel.deltaY * 0.12));
    const auto after = screenToWorld (e.position);
    panPixels.x += (float) ((after.x - before.x) * pixelsPerMeter);
    panPixels.y -= (float) ((after.y - before.y) * pixelsPerMeter);
    repaint();
}

//==============================================================================
class StudioInspector : public juce::Component
{
public:
    StudioInspector (StudioPage& ownerToUse, studio::Id nodeIdToUse)
        : owner (ownerToUse), nodeId (nodeIdToUse)
    {
        setOpaque (true);
        setAlwaysOnTop (true);
        setInterceptsMouseClicks (true, true);
        setSize (300, 330);

        addAndMakeVisible (title);
        title.setInterceptsMouseClicks (false, false);
        title.setFont (theme::font (13.0f, true));
        title.setColour (juce::Label::textColourId, theme::text);

        addAndMakeVisible (closeButton);
        closeButton.onClick = [this]
        {
            juce::Component::SafePointer<StudioInspector> safe (this);
            juce::MessageManager::callAsync ([safe]
            {
                if (safe != nullptr)
                    safe->owner.closeInspector (safe->nodeId);
            });
        };

        build();

        const auto node = findNode (owner.document.getProject().studio, nodeId);

        if (node.kind != NodeKind::source)
        {
            addAndMakeVisible (deleteButton);
            deleteButton.onClick = [this]
            {
                juce::Component::SafePointer<StudioInspector> safe (this);
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr)
                        safe->owner.deleteNode (safe->nodeId);
                });
            };
        }

        setSize (300, 52 + juce::jmax (1, (int) controls.size()) * 28
                        + (deleteButton.isVisible() ? 30 : 0));
        refresh();
    }

    ~StudioInspector() override = default;

    studio::Id getNodeId() const noexcept { return nodeId; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff25252e));
        g.setColour (juce::Colour (0xff4a4a58));
        g.drawRect (getLocalBounds(), 1);
        g.setColour (juce::Colour (0xff353541));
        g.fillRect (0, 0, getWidth(), 28);
    }

    void resized() override
    {
        title.setBounds (10, 2, getWidth() - 44, 24);
        closeButton.setBounds (getWidth() - 30, 4, 24, 20);

        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (30);

        for (int i = 0; i < controls.size() && i < labels.size(); ++i)
        {
            auto row = area.removeFromTop (28);
            labels[i]->setBounds (row.removeFromLeft (96));
            controls[(size_t) i]->setBounds (row);
        }

        if (deleteButton.isVisible())
            deleteButton.setBounds (area.removeFromTop (26).reduced (70, 2));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        toFront (true);
        dragging = e.y <= 30;
        dragStartWindow = getPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            setTopLeftPosition (dragStartWindow.x + e.getDistanceFromDragStartX(),
                                dragStartWindow.y + e.getDistanceFromDragStartY());
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    void refresh()
    {
        const auto& scene = owner.document.getProject().studio;
        const auto node = findNode (scene, nodeId);
        juce::String titleText = juce::String::fromUTF8 ("检查器");

        switch (node.kind)
        {
            case NodeKind::source: titleText = juce::String::fromUTF8 ("乐器"); break;
            case NodeKind::speaker: titleText = juce::String::fromUTF8 ("音箱"); break;
            case NodeKind::mic: titleText = juce::String::fromUTF8 ("麦克风"); break;
            case NodeKind::input: titleText = juce::String::fromUTF8 ("混音台接口"); break;
            case NodeKind::splitter: titleText = juce::String::fromUTF8 ("一分多"); break;
            case NodeKind::combiner: titleText = juce::String::fromUTF8 ("多合一"); break;
            case NodeKind::wall: titleText = juce::String::fromUTF8 ("墙壁"); break;
            default: break;
        }

        juce::String nodeName;

        if (node.kind == NodeKind::source)
        {
            for (const auto& s : scene.sources)
                if (s.id == nodeId)
                {
                    if (const auto* track = owner.document.getProject().findTrack (s.trackId))
                        nodeName = track->name;
                    break;
                }
        }
        else if (node.kind == NodeKind::speaker)
            for (const auto& s : scene.speakers) if (s.id == nodeId) { nodeName = s.name; break; }
        else if (node.kind == NodeKind::mic)
            for (const auto& m : scene.microphones) if (m.id == nodeId) { nodeName = m.name; break; }
        else if (node.kind == NodeKind::input)
            for (const auto& i : scene.mixerInputs) if (i.id == nodeId) { nodeName = i.name; break; }
        else if (node.kind == NodeKind::splitter)
            for (const auto& s : scene.splitters) if (s.id == nodeId) { nodeName = s.name; break; }
        else if (node.kind == NodeKind::combiner)
            for (const auto& c : scene.combiners) if (c.id == nodeId) { nodeName = c.name; break; }

        const juce::ScopedValueSetter<bool> guard (updating, true);
        title.setText (nodeName.isNotEmpty() ? titleText + " · " + nodeName : titleText,
                       juce::dontSendNotification);

        for (auto* slider : sliders)
            if (slider != activeSlider && slider->getProperties().contains ("field"))
                syncSlider (*slider);

        for (auto* combo : combos)
            if (combo->getProperties().contains ("field"))
                syncCombo (*combo);

        if (nameEditor != nullptr)
        {
            juce::String name;
            if (node.kind == NodeKind::input)
                for (const auto& i : scene.mixerInputs) if (i.id == nodeId) name = i.name;
            else if (node.kind == NodeKind::splitter)
                for (const auto& s : scene.splitters) if (s.id == nodeId) name = s.name;
            else if (node.kind == NodeKind::combiner)
                for (const auto& c : scene.combiners) if (c.id == nodeId) name = c.name;

            nameEditor->setText (name, juce::dontSendNotification);
        }

        repaint();
    }

private:
    void build();
    void addSlider (const juce::String& field, const char* label,
                    double min, double max, double step);
    void addCombo (const juce::String& field, const char* label,
                   std::initializer_list<const char*> items);
    void syncSlider (juce::Slider& slider);
    void syncCombo (juce::ComboBox& combo);
    void applySlider (juce::Slider& slider);
    void applyCombo (juce::ComboBox& combo);
    void applyName();
    studio::Id targetForSource() const;
    studio::Id inputForMic() const;

    StudioPage& owner;
    studio::Id nodeId = 0;
    juce::Label title;
    juce::TextButton closeButton { "x" };
    juce::TextButton deleteButton { NI_TXT ("删除") };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> labels;
    std::unique_ptr<juce::TextEditor> nameEditor;
    std::vector<juce::Component*> controls;
    std::vector<juce::String> comboFields;
    bool updating = false;
    bool applying = false;
    bool dragging = false;
    juce::Point<int> dragStartWindow;
    juce::Slider* activeSlider = nullptr;
};

studio::Id StudioInspector::targetForSource() const
{
    const auto& scene = owner.document.getProject().studio;
    for (const auto& c : scene.connections)
        if (c.fromNode == nodeId)
            return c.toNode;
    return 0;
}

studio::Id StudioInspector::inputForMic() const
{
    const auto& scene = owner.document.getProject().studio;
    for (const auto& c : scene.connections)
        if (c.fromNode == nodeId)
            return c.toNode;
    return 0;
}

void StudioInspector::build()
{
    const auto scene = owner.document.getProject().studio;
    const auto node = findNode (scene, nodeId);

    if (node.kind == NodeKind::source)
    {
        addCombo ("mode", "模式", { "外放", "连接音箱", "直连混音台" });
        addSlider ("volume", "音量 dB", -60.0, 6.0, 0.1);
        addSlider ("pan", "声像", -1.0, 1.0, 0.01);
        addCombo ("target", "目标", {});
        addSlider ("x", "X (m)", -15.0, 15.0, 0.05);
        addSlider ("y", "Y (m)", -15.0, 15.0, 0.05);
        addSlider ("angle", "角度", -180.0, 180.0, 1.0);
        addSlider ("directivity", "辐射范围", 10.0, 360.0, 1.0);
    }
    else if (node.kind == NodeKind::speaker)
    {
        addCombo ("units", "数量", { "1 只", "2 只（L/R）" });
        addSlider ("x", "X (m)", -15.0, 15.0, 0.05);
        addSlider ("y", "Y (m)", -15.0, 15.0, 0.05);
        addSlider ("angle", "角度", -180.0, 180.0, 1.0);
        addSlider ("spread", "间距 m", 0.0, 5.0, 0.05);
        addSlider ("gain", "输入增益 dB", -24.0, 24.0, 0.1);
        addSlider ("volume", "音量", 0.0, 2.0, 0.01);
        addSlider ("lf", "低频 dB", -18.0, 18.0, 0.1);
        addSlider ("hf", "高频 dB", -18.0, 18.0, 0.1);
        addSlider ("woofer", "低音单元英寸", 3.0, 18.0, 0.5);
        addSlider ("crossover", "分频 Hz", 300.0, 8000.0, 10.0);
        addSlider ("maxSpl", "最大声压 dB", 70.0, 140.0, 1.0);
        addSlider ("directivity", "方向范围", 30.0, 360.0, 1.0);
    }
    else if (node.kind == NodeKind::mic)
    {
        addCombo ("pattern", "指向", { "全向", "心形", "超心形", "超指向", "8 字" });
        addCombo ("input", "接口", {});
        addSlider ("x", "X (m)", -15.0, 15.0, 0.05);
        addSlider ("y", "Y (m)", -15.0, 15.0, 0.05);
        addSlider ("angle", "角度", -180.0, 180.0, 1.0);
        addSlider ("gain", "输入增益 dB", -24.0, 24.0, 0.1);
    }
    else if (node.kind == NodeKind::input
             || node.kind == NodeKind::splitter
             || node.kind == NodeKind::combiner)
    {
        auto* label = labels.add (new juce::Label());
        label->setText (NI_TXT ("名称"), juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, theme::dimText);
        label->setFont (theme::font (11.0f));
        addAndMakeVisible (label);

        nameEditor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible (*nameEditor);

        if (node.kind == NodeKind::splitter)
            addSlider ("outputs", "输出数", 1.0, 8.0, 1.0);
        else if (node.kind == NodeKind::combiner)
            addSlider ("inputs", "输入数", 1.0, 8.0, 1.0);
        nameEditor->setColour (juce::TextEditor::backgroundColourId, theme::panelRaised);
        nameEditor->setColour (juce::TextEditor::textColourId, theme::text);
        nameEditor->onReturnKey = [this] { applyName(); };
        nameEditor->onFocusLost = [this] { applyName(); };
        controls.push_back (nameEditor.get());
    }
    else if (node.kind == NodeKind::wall)
    {
        addSlider ("reflection", "反射系数", 0.0, 1.0, 0.01);
        addSlider ("occlusion", "遮挡衰减", 0.0, 1.0, 0.01);
    }
}

void StudioInspector::addSlider (const juce::String& field, const char* label,
                                 double min, double max, double step)
{
    auto* slider = sliders.add (new juce::Slider());
    slider->setSliderStyle (juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
    slider->setRange (min, max, step);
    slider->getProperties().set ("field", field);
    slider->getProperties().set ("label", label);
    slider->onValueChange = [this, slider] { applySlider (*slider); };
    slider->onDragStart = [this, slider] { activeSlider = slider; };
    slider->onDragEnd = [this] { activeSlider = nullptr; };
    slider->setInterceptsMouseClicks (true, true);
    addAndMakeVisible (slider);
    controls.push_back (slider);

    auto* rowLabel = labels.add (new juce::Label());
    rowLabel->setText (juce::String::fromUTF8 (label), juce::dontSendNotification);
    rowLabel->setColour (juce::Label::textColourId, theme::dimText);
    rowLabel->setFont (theme::font (11.0f));
    addAndMakeVisible (rowLabel);
}

void StudioInspector::addCombo (const juce::String& field, const char* label,
                                std::initializer_list<const char*> items)
{
    auto* combo = combos.add (new juce::ComboBox());
    combo->getProperties().set ("field", field);
    combo->getProperties().set ("label", label);
    int itemId = 1;
    for (const auto* item : items)
        combo->addItem (juce::String::fromUTF8 (item), itemId++);
    combo->onChange = [this, combo] { applyCombo (*combo); };
    combo->setInterceptsMouseClicks (true, true);
    addAndMakeVisible (combo);
    controls.push_back (combo);

    auto* rowLabel = labels.add (new juce::Label());
    rowLabel->setText (juce::String::fromUTF8 (label), juce::dontSendNotification);
    rowLabel->setColour (juce::Label::textColourId, theme::dimText);
    rowLabel->setFont (theme::font (11.0f));
    addAndMakeVisible (rowLabel);
}

void StudioInspector::syncSlider (juce::Slider& slider)
{
    const auto field = slider.getProperties()["field"].toString();
    const auto& scene = owner.document.getProject().studio;
    const auto node = findNode (scene, nodeId);
    double value = 0.0;

    if (node.kind == NodeKind::source)
        for (const auto& s : scene.sources) if (s.id == nodeId)
        {
            if (field == "volume")
                if (const auto* track = owner.document.getProject().findTrack (s.trackId))
                    value = (double) track->volumeDb;
            else if (field == "pan") value = s.pan;
            else if (field == "x") value = s.position.x;
            else if (field == "y") value = s.position.y;
            else if (field == "angle") value = s.angleDeg;
            else if (field == "directivity") value = s.directivityDeg;
        }
    else if (node.kind == NodeKind::speaker)
        for (const auto& s : scene.speakers) if (s.id == nodeId)
        {
            if (field == "x") value = s.position.x;
            else if (field == "y") value = s.position.y;
            else if (field == "angle") value = s.angleDeg;
            else if (field == "spread") value = s.spreadMeters;
            else if (field == "gain") value = s.inputGainDb;
            else if (field == "volume") value = s.volume;
            else if (field == "lf") value = s.lfDb;
            else if (field == "hf") value = s.hfDb;
            else if (field == "woofer") value = s.wooferInches;
            else if (field == "crossover") value = s.crossoverHz;
            else if (field == "maxSpl") value = s.maxSplDb;
            else if (field == "directivity") value = s.directivityDeg;
            else if (field == "outputs") for (const auto& sp : scene.splitters) if (sp.id == nodeId) value = sp.outputs;
            else if (field == "inputs") for (const auto& cm : scene.combiners) if (cm.id == nodeId) value = cm.inputs;
        }
    else if (node.kind == NodeKind::mic)
        for (const auto& m : scene.microphones) if (m.id == nodeId)
        {
            if (field == "x") value = m.position.x;
            else if (field == "y") value = m.position.y;
            else if (field == "angle") value = m.angleDeg;
            else if (field == "gain") value = m.inputGainDb;
        }
    else if (node.kind == NodeKind::wall)
        for (const auto& w : scene.walls) if (w.id == nodeId)
        {
            if (field == "reflection") value = w.reflection;
            else if (field == "occlusion") value = w.occlusion;
        }
    else if (node.kind == NodeKind::splitter)
        for (const auto& sp : scene.splitters) if (sp.id == nodeId)
            if (field == "outputs") value = sp.outputs;
    else if (node.kind == NodeKind::combiner)
        for (const auto& cm : scene.combiners) if (cm.id == nodeId)
            if (field == "inputs") value = cm.inputs;

    slider.setValue (value, juce::dontSendNotification);
}

void StudioInspector::syncCombo (juce::ComboBox& combo)
{
    const auto field = combo.getProperties()["field"].toString();
    const auto& scene = owner.document.getProject().studio;
    const auto node = findNode (scene, nodeId);

    if (field == "mode" && node.kind == NodeKind::source)
        for (const auto& s : scene.sources) if (s.id == nodeId)
            combo.setSelectedId ((int) s.mode + 1, juce::dontSendNotification);
    else if (field == "units" && node.kind == NodeKind::speaker)
        for (const auto& s : scene.speakers) if (s.id == nodeId)
            combo.setSelectedId (s.units, juce::dontSendNotification);
    else if (field == "pattern" && node.kind == NodeKind::mic)
        for (const auto& m : scene.microphones) if (m.id == nodeId)
            combo.setSelectedId ((int) m.pattern + 1, juce::dontSendNotification);
    else if (field == "target" && node.kind == NodeKind::source)
    {
        // Rebuild the target list from the current mode.
        const auto mode = [&]
        {
            for (const auto& s : scene.sources) if (s.id == nodeId) return s.mode;
            return studio::SourceMode::external;
        }();

        const auto current = targetForSource();
        combo.clear (juce::dontSendNotification);
        combo.addItem (juce::String::fromUTF8 ("无"), 1);
        int index = 2;
        int selected = 1;

        if (mode == studio::SourceMode::speaker)
            for (const auto& s : scene.speakers)
            {
                combo.addItem (s.name, index);

                if (s.id == current)
                    selected = index;

                ++index;
            }
        else if (mode == studio::SourceMode::mixer)
            for (const auto& i : scene.mixerInputs)
            {
                combo.addItem (i.name, index);

                if (i.id == current)
                    selected = index;

                ++index;
            }

        combo.setSelectedId (selected, juce::dontSendNotification);
    }
    else if (field == "input" && node.kind == NodeKind::mic)
    {
        const auto current = inputForMic();
        combo.clear (juce::dontSendNotification);
        combo.addItem (juce::String::fromUTF8 ("无"), 1);
        int index = 2;
        int selected = 1;

        for (const auto& i : scene.mixerInputs)
        {
            combo.addItem (i.name, index);

            if (i.id == current)
                selected = index;

            ++index;
        }

        combo.setSelectedId (selected, juce::dontSendNotification);
    }
}

void StudioInspector::applySlider (juce::Slider& slider)
{
    if (applying)
        return;

    const juce::ScopedValueSetter<bool> applyGuard (applying, true);

    const auto field = slider.getProperties()["field"].toString();
    const auto value = slider.getValue();

    owner.document.applyEdit (NI_TXT ("编辑工作室参数"), [this, field, value] (Project& project)
    {
        auto& scene = project.studio;
        const auto node = findNode (scene, nodeId);

        if (node.kind == NodeKind::source)
            for (auto& s : scene.sources) if (s.id == nodeId)
            {
                if (field == "volume")
                {
                    if (auto* track = project.findTrack (s.trackId))
                        track->volumeDb = (float) value;
                }
                else if (field == "pan") s.pan = value;
                else if (field == "x") s.position.x = value;
                else if (field == "y") s.position.y = value;
                else if (field == "angle") s.angleDeg = value;
                else if (field == "directivity") s.directivityDeg = value;
            }
        else if (node.kind == NodeKind::speaker)
            for (auto& s : scene.speakers) if (s.id == nodeId)
            {
                if (field == "x") s.position.x = value;
                else if (field == "y") s.position.y = value;
                else if (field == "angle") s.angleDeg = value;
                else if (field == "spread") s.spreadMeters = value;
                else if (field == "gain") s.inputGainDb = value;
                else if (field == "volume") s.volume = value;
                else if (field == "lf") s.lfDb = value;
                else if (field == "hf") s.hfDb = value;
                else if (field == "woofer") s.wooferInches = value;
                else if (field == "crossover") s.crossoverHz = value;
                else if (field == "maxSpl") s.maxSplDb = value;
                else if (field == "directivity") s.directivityDeg = value;
            }
        else if (node.kind == NodeKind::mic)
            for (auto& m : scene.microphones) if (m.id == nodeId)
            {
                if (field == "x") m.position.x = value;
                else if (field == "y") m.position.y = value;
                else if (field == "angle") m.angleDeg = value;
                else if (field == "gain") m.inputGainDb = value;
            }
        else if (node.kind == NodeKind::wall)
            for (auto& w : scene.walls) if (w.id == nodeId)
            {
                if (field == "reflection") w.reflection = value;
                else if (field == "occlusion") w.occlusion = value;
            }
        else if (node.kind == NodeKind::splitter)
            for (auto& sp : scene.splitters) if (sp.id == nodeId)
                if (field == "outputs")
                {
                    sp.outputs = juce::jlimit (1, 8, (int) value);
                    scene.connections.erase (
                        std::remove_if (scene.connections.begin(), scene.connections.end(),
                                        [&sp] (const studio::Connection& c)
                                        { return c.fromNode == sp.id && c.fromPort >= sp.outputs; }),
                        scene.connections.end());
                }
        else if (node.kind == NodeKind::combiner)
            for (auto& cm : scene.combiners) if (cm.id == nodeId)
                if (field == "inputs")
                {
                    cm.inputs = juce::jlimit (1, 8, (int) value);
                    scene.connections.erase (
                        std::remove_if (scene.connections.begin(), scene.connections.end(),
                                        [&cm] (const studio::Connection& c)
                                        { return c.toNode == cm.id && c.toPort >= cm.inputs; }),
                        scene.connections.end());
                }
    });
}

void StudioInspector::applyCombo (juce::ComboBox& combo)
{
    if (applying)
        return;

    const juce::ScopedValueSetter<bool> applyGuard (applying, true);

    const auto field = combo.getProperties()["field"].toString();
    const auto id = combo.getSelectedId();

    owner.document.applyEdit (NI_TXT ("编辑工作室参数"), [this, field, id] (Project& project)
    {
        auto& scene = project.studio;
        const auto node = findNode (scene, nodeId);

        auto replaceConnection = [&] (studio::Id from, studio::Id to)
        {
            scene.connections.erase (std::remove_if (scene.connections.begin(),
                                                     scene.connections.end(),
                                                     [from] (const studio::Connection& c)
                                                     { return c.fromNode == from; }),
                                     scene.connections.end());

            if (to != 0)
            {
                studio::Connection c;
                c.id = studio::allocateId (scene);
                c.fromNode = from;
                c.toNode = to;
                scene.connections.push_back (c);
            }
        };

        if (field == "mode")
            for (auto& s : scene.sources) if (s.id == nodeId)
            {
                s.mode = (studio::SourceMode) juce::jlimit (0, 2, id - 1);
                replaceConnection (s.id, 0);
            }
        else if (field == "units")
            for (auto& s : scene.speakers) if (s.id == nodeId)
                s.units = juce::jlimit (1, 2, id);
        else if (field == "pattern")
            for (auto& m : scene.microphones) if (m.id == nodeId)
                m.pattern = (studio::MicPattern) juce::jlimit (0, 4, id - 1);
        else if (field == "target")
        {
            const auto mode = [&]
            {
                for (const auto& s : scene.sources) if (s.id == nodeId) return s.mode;
                return studio::SourceMode::external;
            }();

            studio::Id target = 0;
            int index = id - 2;
            if (mode == studio::SourceMode::speaker)
                target = index >= 0 && index < (int) scene.speakers.size()
                             ? scene.speakers[(size_t) index].id : 0;
            else if (mode == studio::SourceMode::mixer)
                target = index >= 0 && index < (int) scene.mixerInputs.size()
                             ? scene.mixerInputs[(size_t) index].id : 0;

            for (auto& s : scene.sources) if (s.id == nodeId)
                replaceConnection (s.id, target);
        }
        else if (field == "input")
        {
            int index = id - 2;
            studio::Id target = index >= 0 && index < (int) scene.mixerInputs.size()
                                    ? scene.mixerInputs[(size_t) index].id : 0;
            for (const auto& m : scene.microphones) if (m.id == nodeId)
                replaceConnection (m.id, target);
        }
    });

    owner.refresh();
}

void StudioInspector::applyName()
{
    if (applying || nameEditor == nullptr)
        return;

    const juce::ScopedValueSetter<bool> applyGuard (applying, true);

    const auto text = nameEditor->getText().trim();

    if (text.isEmpty())
        return;

    owner.document.applyEdit (NI_TXT ("重命名工作室接口"), [this, text] (Project& project)
    {
        for (auto& i : project.studio.mixerInputs)
            if (i.id == nodeId) { i.name = text; return; }
        for (auto& s : project.studio.splitters)
            if (s.id == nodeId) { s.name = text; return; }
        for (auto& c : project.studio.combiners)
            if (c.id == nodeId) { c.name = text; return; }
    });
}

//==============================================================================
StudioPage::StudioPage (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                        AppState& appStateToUse)
    : document (documentToUse), audio (audioToUse), appState (appStateToUse)
{
    setOpaque (true);

    addAndMakeVisible (titleLabel);
    titleLabel.setText (NI_TXT ("工作室"), juce::dontSendNotification);
    titleLabel.setFont (theme::font (22.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (hintLabel);
    hintLabel.setText (NI_TXT ("在编排里新建乐器会自动出现在这里"), juce::dontSendNotification);
    hintLabel.setFont (theme::font (11.0f));
    hintLabel.setColour (juce::Label::textColourId, theme::dimText);

    auto configure = [this] (juce::TextButton& button)
    {
        addAndMakeVisible (button);
        button.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        button.setColour (juce::TextButton::textColourOffId, theme::text);
    };

    configure (addSpeakerButton);
    configure (addMicButton);
    configure (addSplitterButton);
    configure (addCombinerButton);
    configure (addInputButton);
    configure (wallButton);
    configure (connectButton);

    addSpeakerButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, NI_TXT ("1 只音箱"));
        menu.addItem (2, NI_TXT ("2 只音箱（L/R）"));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSpeakerButton),
                            [this] (int result)
        {
            if (result != 0)
                addSpeaker (result);
        });
    };
    addMicButton.onClick = [this] { addMicrophone(); };
    addSplitterButton.onClick = [this] { addSplitter(); };
    addCombinerButton.onClick = [this] { addCombiner(); };
    addInputButton.onClick = [this] { addMixerInput(); };
    wallButton.onClick = [this] { setWallMode (! wallMode); };
    connectButton.onClick = [this] { setConnectMode (! connectMode); };

    canvas = std::make_unique<StudioCanvas> (*this);
    addAndMakeVisible (*canvas);

    document.addChangeListener (this);
    refresh();
}

StudioPage::~StudioPage()
{
    document.removeChangeListener (this);
    inspectors.clear();
    canvas.reset();
}

void StudioPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void StudioPage::refresh()
{
    if (canvas != nullptr)
        canvas->repaint();

    refreshInspectors();

    wallButton.setColour (juce::TextButton::buttonColourId,
                          wallMode ? theme::accentDim : theme::panelRaised);
    wallButton.setButtonText (wallMode ? NI_TXT ("墙壁：开") : NI_TXT ("墙壁"));
    connectButton.setColour (juce::TextButton::buttonColourId,
                             connectMode ? theme::accentDim : theme::panelRaised);
    connectButton.setButtonText (connectMode ? NI_TXT ("连接：开") : NI_TXT ("连接"));
}

void StudioPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (38);
    g.setColour (theme::outline);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
}

void StudioPage::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto header = r.removeFromTop (38);
    titleLabel.setBounds (header.removeFromLeft (150));
    header.removeFromLeft (20);
    wallButton.setBounds (header.removeFromLeft (84));
    header.removeFromLeft (4);
    connectButton.setBounds (header.removeFromLeft (84));
    header.removeFromLeft (6);
    addSpeakerButton.setBounds (header.removeFromLeft (92));
    header.removeFromLeft (6);
    addMicButton.setBounds (header.removeFromLeft (98));
    header.removeFromLeft (6);
    addSplitterButton.setBounds (header.removeFromLeft (74));
    header.removeFromLeft (6);
    addCombinerButton.setBounds (header.removeFromLeft (74));
    header.removeFromLeft (6);
    addInputButton.setBounds (header.removeFromLeft (104));
    hintLabel.setBounds (header);

    r.removeFromTop (8);

    if (canvas != nullptr)
        canvas->setBounds (r);
}

void StudioPage::addNodeAndSelect (const juce::String& editName,
                                   const std::function<studio::Id (studio::Scene&)>& create)
{
    studio::Id id = 0;

    document.applyEdit (editName, [&id, &create] (Project& project)
    {
        id = create (project.studio);
    });

    if (id != 0)
        openInspector (id);
}

void StudioPage::addSpeaker (int units)
{
    addNodeAndSelect (NI_TXT ("添加音箱"), [units] (studio::Scene& scene)
    {
        studio::Speaker s;
        s.id = studio::allocateId (scene);
        s.name = juce::String::fromUTF8 ("音箱 ") + juce::String ((int) scene.speakers.size() + 1);
        s.units = juce::jlimit (1, 2, units);
        s.position = { 2.0, -2.0 };
        s.angleDeg = 180.0;
        scene.speakers.push_back (s);
        return s.id;
    });
}

void StudioPage::addMicrophone()
{
    addNodeAndSelect (NI_TXT ("添加麦克风"), [] (studio::Scene& scene)
    {
        studio::Microphone m;
        m.id = studio::allocateId (scene);
        m.name = juce::String::fromUTF8 ("麦克风 ") + juce::String ((int) scene.microphones.size() + 1);
        m.position = { -2.0, 0.0 };
        m.angleDeg = 0.0;
        m.pattern = studio::MicPattern::cardioid;
        scene.microphones.push_back (m);
        return m.id;
    });
}

void StudioPage::addSplitter()
{
    addNodeAndSelect (NI_TXT ("添加一分多"), [] (studio::Scene& scene)
    {
        studio::Splitter s;
        s.id = studio::allocateId (scene);
        s.name = juce::String::fromUTF8 ("一分多");
        s.outputs = 2;
        scene.splitters.push_back (s);
        return s.id;
    });
}

void StudioPage::addCombiner()
{
    addNodeAndSelect (NI_TXT ("添加多合一"), [] (studio::Scene& scene)
    {
        studio::Combiner c;
        c.id = studio::allocateId (scene);
        c.name = juce::String::fromUTF8 ("多合一");
        c.inputs = 2;
        scene.combiners.push_back (c);
        return c.id;
    });
}

void StudioPage::addMixerInput()
{
    addNodeAndSelect (NI_TXT ("添加混音台接口"), [] (studio::Scene& scene)
    {
        studio::MixerInput i;
        i.id = studio::allocateId (scene);
        i.name = juce::String::fromUTF8 ("接口 ") + juce::String ((int) scene.mixerInputs.size() + 1);
        scene.mixerInputs.push_back (i);
        return i.id;
    });
}

void StudioPage::setConnectMode (bool on)
{
    connectMode = on;

    if (canvas != nullptr)
        canvas->setConnectMode (on);

    refresh();
}

void StudioPage::createConnection (studio::Id fromNode, int fromPort,
                                   studio::Id toNode, int toPort)
{
    if (fromNode == 0 || toNode == 0 || fromNode == toNode)
        return;

    const auto scene = document.getProject().studio;
    const auto nodeFrom = findNode (scene, fromNode);
    const auto nodeTo = findNode (scene, toNode);

    auto portCount = [&] (const NodeRef& node, bool output)
    {
        if (node.kind == NodeKind::source || node.kind == NodeKind::mic)
            return output ? 1 : 0;
        if (node.kind == NodeKind::speaker || node.kind == NodeKind::input)
            return output ? 0 : 1;
        if (node.kind == NodeKind::splitter)
        {
            if (! output) return 1;
            for (const auto& s : scene.splitters) if (s.id == node.id)
                return juce::jlimit (1, 8, s.outputs);
        }
        if (node.kind == NodeKind::combiner)
        {
            if (output) return 1;
            for (const auto& c : scene.combiners) if (c.id == node.id)
                return juce::jlimit (1, 8, c.inputs);
        }
        return 0;
    };

    if (fromPort < 0 || fromPort >= portCount (nodeFrom, true)
        || toPort < 0 || toPort >= portCount (nodeTo, false))
        return;

    for (const auto& c : scene.connections)
        if (c.fromNode == fromNode && c.fromPort == fromPort
            && c.toNode == toNode && c.toPort == toPort)
            return;

    auto reaches = [&] (studio::Id start, studio::Id target)
    {
        std::vector<studio::Id> stack { start };
        std::vector<studio::Id> visited;

        while (! stack.empty())
        {
            const auto current = stack.back();
            stack.pop_back();

            if (current == target)
                return true;

            if (std::find (visited.begin(), visited.end(), current) != visited.end())
                continue;

            visited.push_back (current);

            for (const auto& c : scene.connections)
                if (c.fromNode == current)
                    stack.push_back (c.toNode);
        }

        return false;
    };

    if (reaches (toNode, fromNode))
        return;

    document.applyEdit (NI_TXT ("连接工作室元素"),
                        [fromNode, fromPort, toNode, toPort, nodeFrom, nodeTo] (Project& project)
    {
        if (nodeFrom.kind == NodeKind::source)
            for (auto& source : project.studio.sources)
                if (source.id == fromNode)
                {
                    if (nodeTo.kind == NodeKind::speaker)
                        source.mode = studio::SourceMode::speaker;
                    else if (nodeTo.kind == NodeKind::input)
                        source.mode = studio::SourceMode::mixer;
                    break;
                }

        studio::Connection c;
        c.id = studio::allocateId (project.studio);
        c.fromNode = fromNode;
        c.fromPort = fromPort;
        c.toNode = toNode;
        c.toPort = toPort;
        project.studio.connections.push_back (c);
    });
}

void StudioPage::deleteConnectionsAtPort (studio::Id nodeId, int port, bool output)
{
    document.applyEdit (NI_TXT ("断开工作室连线"), [nodeId, port, output] (Project& project)
    {
        auto& connections = project.studio.connections;
        connections.erase (std::remove_if (connections.begin(), connections.end(),
                                           [=] (const studio::Connection& c)
                                           {
                                               return output ? (c.fromNode == nodeId
                                                                && c.fromPort == port)
                                                             : (c.toNode == nodeId
                                                                && c.toPort == port);
                                           }),
                           connections.end());
    });
}

void StudioPage::setWallMode (bool on)
{
    wallMode = on;
    connectMode = false;

    if (canvas != nullptr)
        canvas->setWallMode (on);

    refresh();
}

void StudioPage::deleteNode (studio::Id nodeId)
{
    closeInspector (nodeId);

    document.applyEdit (NI_TXT ("删除工作室元素"), [nodeId] (Project& project)
    {
        auto& scene = project.studio;
        auto remove = [nodeId] (auto& nodes)
        {
            nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
                                         [nodeId] (const auto& n) { return n.id == nodeId; }),
                         nodes.end());
        };

        remove (scene.speakers);
        remove (scene.microphones);
        remove (scene.mixerInputs);
        remove (scene.splitters);
        remove (scene.combiners);
        remove (scene.walls);
        scene.connections.erase (std::remove_if (scene.connections.begin(), scene.connections.end(),
                                                 [nodeId] (const studio::Connection& c)
                                                 { return c.fromNode == nodeId || c.toNode == nodeId; }),
                                 scene.connections.end());
    });
}

void StudioPage::openInspector (studio::Id nodeId)
{
    for (auto* inspector : inspectors)
        if (inspector->getNodeId() == nodeId)
        {
            inspector->toFront (true);
            return;
        }

    auto* inspector = inspectors.add (new StudioInspector (*this, nodeId));
    addAndMakeVisible (inspector);
    inspector->setTopLeftPosition (24 + inspectors.size() * 18,
                                   80 + inspectors.size() * 18);
    inspector->toFront (true);
    if (onNodeSelected)
        onNodeSelected (nodeId);
}

void StudioPage::closeInspector (studio::Id nodeId)
{
    for (int i = 0; i < inspectors.size(); ++i)
        if (inspectors[i]->getNodeId() == nodeId)
        {
            inspectors.remove (i);
            return;
        }
}

void StudioPage::refreshInspectors()
{
    for (auto* inspector : inspectors)
    {
        inspector->toFront (true);
        inspector->refresh();
    }
}

int StudioPage::getSourceNodeCountForTest() const
{
    return (int) document.getProject().studio.sources.size();
}

int StudioPage::getSpeakerCountForTest() const
{
    return (int) document.getProject().studio.speakers.size();
}

int StudioPage::getMicrophoneCountForTest() const
{
    return (int) document.getProject().studio.microphones.size();
}

int StudioPage::getWallCountForTest() const
{
    return (int) document.getProject().studio.walls.size();
}

int StudioPage::getInspectorCountForTest() const
{
    return inspectors.size();
}

juce::Point<double> StudioPage::getNodePositionForTest (studio::Id nodeId) const
{
    const auto& scene = document.getProject().studio;
    for (const auto& s : scene.sources) if (s.id == nodeId) return { s.position.x, s.position.y };
    for (const auto& s : scene.speakers) if (s.id == nodeId) return { s.position.x, s.position.y };
    for (const auto& m : scene.microphones) if (m.id == nodeId) return { m.position.x, m.position.y };
    for (const auto& i : scene.mixerInputs) if (i.id == nodeId) return { i.position.x, i.position.y };
    for (const auto& s : scene.splitters) if (s.id == nodeId) return { s.position.x, s.position.y };
    for (const auto& c : scene.combiners) if (c.id == nodeId) return { c.position.x, c.position.y };
    return {};
}

void StudioPage::dragNodeForTest (studio::Id nodeId, double x, double y)
{
    document.applyEdit (NI_TXT ("移动工作室节点"), [nodeId, x, y] (Project& project)
    {
        auto& scene = project.studio;
        for (auto& s : scene.sources) if (s.id == nodeId) { s.position = { x, y }; return; }
        for (auto& s : scene.speakers) if (s.id == nodeId) { s.position = { x, y }; return; }
        for (auto& m : scene.microphones) if (m.id == nodeId) { m.position = { x, y }; return; }
    });
}

studio::Id StudioPage::getSourceNodeIdForTrackForTest (juce::int64 trackId) const
{
    if (const auto* source = studio::findSourceForTrack (document.getProject().studio, trackId))
        return source->id;
    return 0;
}

} // namespace hmdaw
