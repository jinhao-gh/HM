#include "TestSupport.h"

#include "../Audio/StudioRenderPlan.h"
#include "../Core/StudioScene.h"

class StudioTests : public juce::UnitTest
{
public:
    StudioTests() : juce::UnitTest ("Studio", "hmdaw") {}

    static Project makeStudioProject()
    {
        auto project = makeDefaultProject();
        addTrack (project, TrackKind::synth);
        project.tempoMarkers = { TempoMarker { 0, 4, 4, 120.0 } };

        Note note;
        note.id = 1;
        note.startTick = kTicksPerQuarter;
        note.durationTicks = kTicksPerQuarter;
        note.frequencyHz = 440.0;
        project.tracks.front().clips.front().notes = { note };
        return project;
    }

    void runTest() override
    {
        beginTest ("default studio: crossed mics, L/R inputs and channels");
        {
            const auto scene = studio::makeDefaultScene();
            expectEquals ((int) scene.microphones.size(), 2);
            expectEquals ((int) scene.mixerInputs.size(), 2);
            expectEquals ((int) scene.mixer.channels.size(), 2);
            expectEquals (scene.microphones[0].position.x, 0.0);
            expectEquals (scene.microphones[0].position.y, 0.0);
            expectEquals ((int) scene.microphones[0].pattern, (int) studio::MicPattern::cardioid);
            expectEquals (scene.mixer.channels[0].inputId, scene.mixerInputs[0].id);
            expectEquals (scene.mixer.channels[1].inputId, scene.mixerInputs[1].id);
        }

        beginTest ("adding a track creates an external source in front of the mics");
        {
            auto project = makeDefaultProject();
            const auto trackId = addTrack (project, TrackKind::synth);
            const auto* source = studio::findSourceForTrack (project.studio, trackId);
            expect (source != nullptr);
            expectEquals ((int) source->mode, (int) studio::SourceMode::external);
            expectGreaterThan (source->position.y, 0.0);
        }

        beginTest ("studio scene survives a project round trip");
        {
            auto project = makeStudioProject();
            project.studio.walls.push_back ({ 0, { -1.0, 0.0 }, { 1.0, 0.0 }, 0.6, 0.4 });

            Project loaded;
            juce::String error;
            expect (projectFromJson (projectToJson (project), loaded, error), error);
            expectEquals ((int) loaded.studio.microphones.size(), 2);
            expectEquals ((int) loaded.studio.sources.size(), 1);
            expectEquals ((int) loaded.studio.walls.size(), 1);
            expectWithinAbsoluteError (loaded.studio.walls.front().reflection, 0.6, 1.0e-6);
            expectEquals (loaded.studio.connections.front().fromPort, 0);
            expectEquals (loaded.studio.connections.front().toPort, 0);
        }

        beginTest ("old projects keep a direct mixer path");
        {
            auto project = makeStudioProject();
            const auto parsed = juce::JSON::parse (projectToJson (project));
            auto* root = parsed.getDynamicObject();
            root->removeProperty ("studio");
            root->setProperty ("version", 1);

            Project loaded;
            juce::String error;
            expect (projectFromJson (juce::JSON::toString (parsed), loaded, error), error);
            expectEquals ((int) loaded.studio.sources.size(), 1);
            expectEquals ((int) loaded.studio.sources.front().mode,
                          (int) studio::SourceMode::mixer);
            expectGreaterThan ((int) loaded.studio.mixer.channels.size(), 2);
        }

        beginTest ("the render plan maps source, microphones and mixer channels");
        {
            auto project = makeStudioProject();
            auto plan = studio::buildRenderPlan (project, 48000.0);
            expect (plan != nullptr);
            expectEquals ((int) plan->mics.size(), 2);
            expectEquals ((int) plan->channels.size(), 2);
            expectEquals ((int) plan->sources.size(), 1);
            expectEquals ((int) plan->sources.front().mode, (int) studio::SourceMode::external);
            expectGreaterThan ((int) plan->paths.size(), 0);
        }

        beginTest ("an external source reaches the master through the default mics");
        {
            auto project = makeStudioProject();
            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (buildSchedule (project)));
            engine.setPositionSeconds (0.0);
            engine.setPlaying (true);

            float peak = 0.0f;

            for (int i = 0; i < 80; ++i)
            {
                engine.renderNextBlock (512);
                peak = juce::jmax (peak, engine.getOutputBuffer().getMagnitude (0, 0, 512));
            }

            expectGreaterThan (peak, 0.0001f);
        }

        beginTest ("cardioid microphones reject a source behind them");
        {
            auto project = makeStudioProject();
            auto* source = studio::findSourceForTrack (project.studio, project.tracks.front().id);
            expect (source != nullptr);
            source->position = { 0.0, 2.0 };
            auto front = studio::buildRenderPlan (project, 48000.0);
            source->position = { 0.0, -2.0 };
            auto rear = studio::buildRenderPlan (project, 48000.0);

            float frontGain = 0.0f, rearGain = 0.0f;
            for (const auto& p : front->paths) frontGain += p.gain;
            for (const auto& p : rear->paths) rearGain += p.gain;
            expectGreaterThan (frontGain, rearGain);
        }

        beginTest ("a source connected to a speaker uses the speaker route");
        {
            auto project = makeStudioProject();
            auto& scene = project.studio;
            const auto sourceId = studio::findSourceForTrack (scene, project.tracks.front().id)->id;
            studio::Speaker speaker;
            speaker.id = studio::allocateId (scene);
            speaker.name = "测试音箱";
            speaker.position = { 0.0, -2.0 };
            speaker.angleDeg = 90.0;
            scene.speakers.push_back (speaker);
            studio::findSourceForTrack (scene, project.tracks.front().id)->mode = studio::SourceMode::speaker;
            studio::Connection c;
            c.id = studio::allocateId (scene);
            c.fromNode = sourceId;
            c.toNode = speaker.id;
            scene.connections.push_back (c);

            auto plan = studio::buildRenderPlan (project, 48000.0);
            expectEquals ((int) plan->sources.front().mode, (int) studio::SourceMode::speaker);
            expect (plan->sources.front().speakerIndex >= 0);
            expectGreaterThan ((int) plan->paths.size(), 0);
        }

        beginTest ("an audio track source reaches the default microphones");
        {
            auto project = makeDefaultProject();
            addTrack (project, TrackKind::audio);
            auto plan = studio::buildRenderPlan (project, 48000.0);
            float gain = 0.0f;
            for (const auto& p : plan->paths) gain += p.gain;
            expectGreaterThan (gain, 0.0f);
        }

        beginTest ("a splitter feeds every speaker");
        {
            auto project = makeStudioProject();
            auto& scene = project.studio;
            auto* source = studio::findSourceForTrack (scene, project.tracks.front().id);
            source->mode = studio::SourceMode::speaker;

            studio::Splitter splitter;
            splitter.id = studio::allocateId (scene);
            splitter.name = "split";
            scene.splitters.push_back (splitter);

            std::vector<studio::Id> speakerIds;

            for (int i = 0; i < 2; ++i)
            {
                studio::Speaker speaker;
                speaker.id = studio::allocateId (scene);
                speaker.name = "spk";
                speaker.position = { (double) i * 2.0 - 1.0, 0.0 };
                speaker.angleDeg = 90.0;
                scene.speakers.push_back (speaker);
                speakerIds.push_back (speaker.id);
            }

            auto connect = [&] (studio::Id from, studio::Id to)
            {
                studio::Connection c;
                c.id = studio::allocateId (scene);
                c.fromNode = from;
                c.toNode = to;
                scene.connections.push_back (c);
            };

            connect (source->id, splitter.id);
            connect (splitter.id, speakerIds[0]);
            connect (splitter.id, speakerIds[1]);

            auto plan = studio::buildRenderPlan (project, 48000.0);
            expectEquals ((int) plan->sources.front().speakerIndices.size(), 2);
        }

        beginTest ("a splitter feeds every mixer input");
        {
            auto project = makeStudioProject();
            auto& scene = project.studio;
            auto* source = studio::findSourceForTrack (scene, project.tracks.front().id);
            source->mode = studio::SourceMode::mixer;

            studio::Splitter splitter;
            splitter.id = studio::allocateId (scene);
            scene.splitters.push_back (splitter);

            std::vector<studio::Id> inputIds;

            for (const auto& input : scene.mixerInputs)
                inputIds.push_back (input.id);

            auto connect = [&] (studio::Id from, studio::Id to)
            {
                studio::Connection c;
                c.id = studio::allocateId (scene);
                c.fromNode = from;
                c.toNode = to;
                scene.connections.push_back (c);
            };

            connect (source->id, splitter.id);
            for (const auto input : inputIds)
                connect (splitter.id, input);

            auto plan = studio::buildRenderPlan (project, 48000.0);
            expectEquals ((int) plan->sources.front().directChannels.size(), 2);
        }

        beginTest ("an audio track plays through studio acoustics");
        {
            auto project = makeDefaultProject();
            const auto id = addTrack (project, TrackKind::audio);
            auto* track = project.findTrack (id);
            track->clips.clear();
            const auto file = writeTempWav ("studio-audio-regression.wav", 1.0);
            expect (file.existsAsFile(), file.getFullPathName());
            expectGreaterThan ((juce::int64) file.getSize(), (juce::int64) 0);
            expectEquals ((int) track->kind, (int) TrackKind::audio);

            Clip clip = makeDefaultClip (project);
            clip.startTick = 0;
            clip.lengthTicks = kTicksPerQuarter * 4;
            clip.loopLengthTicks = clip.lengthTicks;
            clip.audioFilePath = file.getFullPathName();
            clip.audioFileName = file.getFileName();
            track->clips.push_back (clip);

            const auto schedule = buildSchedule (project);
            expect (schedule.studioPlan != nullptr);
            expectEquals ((int) schedule.studioPlan->sources.size(), 1);
            expect (schedule.studioPlan->sources.front().emitterIndex >= 0);
            expectGreaterThan ((int) schedule.studioPlan->paths.size(), 0);
            expectEquals ((int) schedule.tracks.front().audioClips.size(), 1);

            juce::AudioBuffer<float> sourceBuffer (2, 512);
            sourceBuffer.clear();
            renderAudioClips (schedule.tracks.front().audioClips, sourceBuffer, 0, 512,
                              0.0, 48000.0, 1.0f, 1.0f);
            expectGreaterThan (sourceBuffer.getMagnitude (0, 0, 512), 0.0f);

            PlaybackEngine engine;
            engine.prepare (48000.0, 512);
            engine.setSchedule (std::make_shared<const RenderSchedule> (schedule));
            engine.setPlaying (true);

            float peak = 0.0f;
            for (int i = 0; i < 40; ++i)
            {
                engine.renderNextBlock (512);
                peak = juce::jmax (peak, engine.getOutputBuffer().getMagnitude (0, 0, 512));
            }

            expectGreaterThan (peak, 0.0f);
        }

        beginTest ("a wall between source and microphones lowers the direct path");
        {
            auto project = makeStudioProject();
            auto open = studio::buildRenderPlan (project, 48000.0);
            project.studio.walls.push_back ({ 0, { -3.0, 1.0 }, { 3.0, 1.0 }, 0.5, 0.9 });
            auto blocked = studio::buildRenderPlan (project, 48000.0);

            float openGain = 0.0f, blockedGain = 0.0f;

            for (const auto& p : open->paths) openGain += p.gain;
            for (const auto& p : blocked->paths) blockedGain += p.gain;

            expectGreaterThan (openGain, blockedGain);
        }
    }
};

static StudioTests studioTests;
