#include "TestSupport.h"

#include "../Core/ControllerMath.h"

namespace
{
    /** A clip with one curve in it, for the tests that only care about the shape. */
    Clip makeCurveClip (juce::int64 lengthTicks, bool looped = false,
                        juce::int64 loopLengthTicks = 0)
    {
        auto project = makeDefaultProject();
        Clip clip = makeDefaultClip (project);
        clip.startTick = 0;
        clip.lengthTicks = lengthTicks;
        clip.looped = looped;
        clip.loopLengthTicks = loopLengthTicks > 0 ? loopLengthTicks : lengthTicks;
        return clip;
    }
}

/** The controller curve's arithmetic: the shape of a drawn line, the editing rules
    that keep it well formed, and the event stream the engine plays. */
class ControllerTests : public juce::UnitTest
{
public:
    ControllerTests() : juce::UnitTest ("Controller", "hmdaw") {}

    void runTest() override
    {
        beginTest (NI_TXT ("曲线的取值：点之间线性插值，两端各自保持"));

        {
            ControllerCurve curve;
            curve.controller = 11;

            expectLessThan (controllers::laneValueAt (curve, 100.0), 0.0,
                            "an empty curve has no value");

            controllers::upsertControllerPoint (curve, 480, 20);
            controllers::upsertControllerPoint (curve, 1440, 100);

            expectWithinAbsoluteError (controllers::laneValueAt (curve, 0.0), 20.0, 1.0e-9);
            expectWithinAbsoluteError (controllers::laneValueAt (curve, 480.0), 20.0, 1.0e-9);
            expectWithinAbsoluteError (controllers::laneValueAt (curve, 960.0), 60.0, 1.0e-9);
            expectWithinAbsoluteError (controllers::laneValueAt (curve, 1440.0), 100.0, 1.0e-9);
            expectWithinAbsoluteError (controllers::laneValueAt (curve, 9000.0), 100.0, 1.0e-9);
        }

        beginTest (NI_TXT ("编辑规则：按 tick 排序、同 tick 覆盖、值被夹在 0–127"));

        {
            ControllerCurve curve;
            curve.controller = 11;

            expect (controllers::upsertControllerPoint (curve, 960, 64));
            expect (controllers::upsertControllerPoint (curve, 0, 10));
            expect (controllers::upsertControllerPoint (curve, 480, 200));   // clamped

            expectEquals ((int) curve.points.size(), 3);
            expectEquals ((int) curve.points[0].tick, 0);
            expectEquals ((int) curve.points[1].tick, 480);
            expectEquals ((int) curve.points[2].tick, 960);
            expectEquals (curve.points[1].value, 127);

            // The same tick again replaces rather than duplicating, and says whether
            // anything really changed.
            expect (! controllers::upsertControllerPoint (curve, 480, 127));
            expect (controllers::upsertControllerPoint (curve, 480, 5));
            expectEquals ((int) curve.points.size(), 3);
            expectEquals (curve.points[1].value, 5);

            // A negative tick is pulled to the start.
            expect (controllers::upsertControllerPoint (curve, -700, 1));
            expectEquals ((int) curve.points[0].tick, 0);

            expect (controllers::removeControllerPoint (curve, 960, 0));
            expectEquals ((int) curve.points.size(), 3);
            expect (! controllers::removeControllerPoint (curve, 2000, 10));
        }

        beginTest (NI_TXT ("一条曲线落在片段的窗口里，循环时按周期平铺"));

        {
            auto clip = makeCurveClip (3840);
            auto& curve = controllers::laneFor (clip, 11);
            controllers::upsertControllerPoint (curve, 0, 0);
            controllers::upsertControllerPoint (curve, 1920, 127);

            const auto values = controllers::expandControllers (clip, curve);

            expectGreaterThan ((int) values.size(), 10);
            expectEquals (values.front().tick, (juce::int64) 0);
            expectEquals (values.front().value, 0);
            expectEquals (values.back().tick, (juce::int64) 1920);
            expectEquals (values.back().value, 127);

            // Half way along the ramp the value is half way up it.
            auto halfway = -1;

            for (const auto& value : values)
                if (value.tick == 960)
                    halfway = value.value;

            expectWithinAbsoluteError ((double) halfway, 64.0, 2.0);

            // Every step is 1/16 of a beat and the times only go forwards.
            auto increasing = true;
            auto onGrid = true;

            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0 && values[i].tick <= values[i - 1].tick)
                    increasing = false;

                if (values[i].tick % controllers::kDefaultStepTicks != 0)
                    onGrid = false;
            }

            expect (increasing, "the events are in order");
            expect (onGrid, "and land on the 1/16 grid");

            // A clip that loops tiles the curve once per period, and only the points
            // inside one period take part.
            auto looped = makeCurveClip (7680, true, 3840);
            auto& loopCurve = controllers::laneFor (looped, 11);
            controllers::upsertControllerPoint (loopCurve, 0, 0);
            controllers::upsertControllerPoint (loopCurve, 1920, 127);
            controllers::upsertControllerPoint (loopCurve, 5000, 64);   // outside one period

            const auto tiled = controllers::expandControllers (looped, loopCurve);
            auto knees = 0;

            for (const auto& value : tiled)
                if (value.tick == 0 || value.tick == 1920 || value.tick == 3840 || value.tick == 5760)
                    ++knees;

            expectEquals (knees, 4, "the knees appear once per period");

            // The knee-only view is the same shape without the steps between them.
            const auto points = controllers::expandControllerPoints (looped, loopCurve);

            expectEquals ((int) points.size(), 4);
            expectEquals (points.back().tick, (juce::int64) 5760);
        }

        beginTest (NI_TXT ("空曲线被剪掉，保留的控制器号有据可查"));

        {
            auto clip = makeCurveClip (1920);
            controllers::laneFor (clip, 1);
            controllers::upsertControllerPoint (controllers::laneFor (clip, 64), 0, 127);

            expectEquals ((int) clip.controllerCurves.size(), 2);

            controllers::pruneEmptyCurves (clip);

            expectEquals ((int) clip.controllerCurves.size(), 1);
            expect (controllers::findLane (clip, 64) != nullptr, "the used one stays");
            expect (controllers::findLane (clip, 1) == nullptr, "the empty one is gone");

            expect (controllers::isReservedController (6));
            expect (controllers::isReservedController (101));
            expect (! controllers::isReservedController (11));
            expect (! controllers::isReservedController (64));
        }

        beginTest (NI_TXT ("曲线随工程保存，旧工程没有这一段就是空"));

        {
            auto project = makeProjectWithSynthTrack();
            auto& track = project.tracks.front();

            Note note;
            note.id = project.nextNoteId++;
            note.durationTicks = 480;
            track.clips.front().notes.push_back (note);

            auto& curve = controllers::laneFor (track.clips.front(), 11);
            controllers::upsertControllerPoint (curve, 0, 12);
            controllers::upsertControllerPoint (curve, 960, 120);
            controllers::upsertControllerPoint (controllers::laneFor (track.clips.front(), 64), 480, 127);

            const auto json = projectToJson (project);
            Project reloaded;
            juce::String error;

            expect (projectFromJson (json, reloaded, error), error);
            expectEquals ((int) reloaded.tracks.front().clips.front().controllerCurves.size(), 2);

            const auto* expression = controllers::findLane (reloaded.tracks.front().clips.front(), 11);

            expect (expression != nullptr);

            if (expression != nullptr)
            {
                expectEquals ((int) expression->points.size(), 2);
                expectEquals ((int) expression->points[0].tick, 0);
                expectEquals (expression->points[0].value, 12);
                expectEquals ((int) expression->points[1].tick, 960);
                expectEquals (expression->points[1].value, 120);
            }

            // A file written before the lane existed has no "controllers" array: the
            // clip simply has no curves.
            auto stripped = juce::JSON::parse (json);

            if (auto* root = stripped.getDynamicObject())
                if (auto* tracks = root->getProperty ("tracks").getArray())
                    for (auto& trackVar : *tracks)
                        if (auto* trackObject = trackVar.getDynamicObject())
                            if (auto* clips = trackObject->getProperty ("clips").getArray())
                                for (auto& clipVar : *clips)
                                    if (auto* clipObject = clipVar.getDynamicObject())
                                        clipObject->removeProperty ("controllers");

            Project old;
            expect (projectFromJson (juce::JSON::toString (stripped, false), old, error), error);
            expect (old.tracks.front().clips.front().controllerCurves.empty(),
                    "an old project opens with no curves at all");
        }

        beginTest (NI_TXT ("调度里带着曲线：按拍号换成秒，跟着片段一起平铺"));

        {
            auto project = makeProjectWithSynthTrack();
            project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };   // 0.5 s per beat

            auto& clip = project.tracks.front().clips.front();
            clip.startTick = 0;
            clip.lengthTicks = 3840;    // two beats
            clip.loopLengthTicks = 3840;

            auto& curve = controllers::laneFor (clip, 7);
            controllers::upsertControllerPoint (curve, 0, 0);
            controllers::upsertControllerPoint (curve, 960, 100);

            const auto schedule = buildSchedule (project);

            expectEquals ((int) schedule.tracks.size(), 1);

            const auto& events = schedule.tracks.front().controllers;

            expectGreaterThan ((int) events.size(), 4);
            expectEquals (events.front().controller, 7);
            expectEquals (events.front().value, 0);
            expectWithinAbsoluteError (events.front().timeSeconds, 0.0, 1.0e-9);

            // 960 ticks at 120 bpm is half a second.
            auto lastTickTime = -1.0;

            for (const auto& event : events)
                lastTickTime = juce::jmax (lastTickTime, event.timeSeconds);

            expectWithinAbsoluteError (lastTickTime, 0.5, 1.0e-6);

            // A track with no curves carries no controller events at all.
            auto plain = makeProjectWithSynthTrack();
            expect (buildSchedule (plain).tracks.front().controllers.empty(),
                    "nothing drawn means nothing sent");
        }
    }
};

namespace
{
    ControllerTests controllerTests;
}
