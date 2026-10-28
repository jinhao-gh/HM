#include "SynthEngine.h"
#include "../Core/ControllerMath.h"
#include "../Core/FmMath.h"
#include "../Core/Wavetables.h"
#include "../Core/WavetableTables.h"

#include <map>

namespace nidaw
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    /** Headroom trim so that sixteen simultaneous voices stay well clear of clipping. */
    constexpr float kMasterTrim = 0.22f;

    /** An operator at output level 99 shifts its target's phase by two cycles. */
    constexpr double kModulationScale = 2.0;

    /** How hard operator 6 feeds back into itself at feedback = 7. */
    constexpr double kFeedbackScale = 0.35;

    /** The LFO runs from about 0.07 Hz to about 21 Hz. */
    constexpr double kLfoMinHz = 0.07;
    constexpr double kLfoRange = 300.0;

    inline double wrap01 (double x) noexcept
    {
        return x - std::floor (x);
    }

    /** Output level 0..99 to amplitude 0..1, with finer steps near the top.
        Takes a double so an envelope can be traced smoothly between whole steps. */
    inline double levelToAmplitude (double level) noexcept
    {
        const auto n = juce::jlimit (0.0, 99.0, level) / 99.0;
        return n * n;
    }

    inline double levelToAmplitude (int level) noexcept
    {
        return levelToAmplitude ((double) juce::jlimit (0, 99, level));
    }

    inline double lfoHz (int rate) noexcept
    {
        const auto r = juce::jlimit (0, 99, rate) / 99.0;
        return kLfoMinHz * std::pow (kLfoRange, r);
    }

    constexpr juce::uint8 opBit (int index) noexcept { return (juce::uint8) (1u << index); }

    /** The 32 routings.

        These follow the DX7 idiom - operators are numbered from the output
        upwards, an algorithm is a little forest of stacks, and operator 6 is the
        one with feedback - but the set itself is written for this project.
    */
    const FmAlgorithm kAlgorithms[kFmAlgorithmCount] =
    {
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01,          "6-5-4-3-2-1" },
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01 | 0x20,   "6-5-4-3-2-1 + 6" },
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01 | 0x10,   "6-5-4-3-2-1 + 5" },
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01 | 0x08,   "6-5-4-3-2-1 + 4" },
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01 | 0x02,   "6-5-4-3-2-1 + 2" },
        { { 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 }, 0x01 | 0x02 | 0x20, "6-5-4-3-2-1 + 2 + 6" },
        { { 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, 0x01 | 0x20,   "5-4-3-2-1 + 6" },
        { { 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, 0x01 | 0x10 | 0x20, "5-4-3-2-1 + 5 + 6" },
        { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x01 | 0x08,   "6-5-4 | 3-2-1" },
        { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x01 | 0x04 | 0x08, "6-5-4 | 3-2-1 + 3" },
        { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x01 | 0x04 | 0x08 | 0x10, "6-5-4 | 3-2-1 + 3 + 5" },
        { { 0x02, 0x04, 0x00, 0x10, 0x00, 0x00 }, 0x01 | 0x08 | 0x10 | 0x20, "5-4-3-2-1 + 5 + 6" },
        { { 0x02, 0x04, 0x00, 0x10, 0x00, 0x00 }, 0x01 | 0x08 | 0x20, "5-4-3-2-1 + 4 + 6" },
        { { 0x02, 0x04, 0x08, 0x00, 0x00, 0x00 }, 0x01 | 0x10 | 0x20, "4-3-2-1 + 5 + 6" },
        { { 0x02, 0x04, 0x08, 0x00, 0x20, 0x00 }, 0x01 | 0x20,   "6-5 + 4-3-2-1" },
        { { 0x02, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x01 | 0x04 | 0x20, "5-4-3 | 2-1 | 6" },
        { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01 | 0x04 | 0x10, "6-5 | 4-3 | 2-1" },
        { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01 | 0x04 | 0x10 | 0x20, "6-5 | 4-3 | 2-1 + 6" },
        { { 0x02, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x01 | 0x04 | 0x20, "5-4 | 4-3 | 2-1" },
        { { 0x02, 0x00, 0x08, 0x00, 0x00, 0x00 }, 0x01 | 0x04 | 0x10 | 0x20, "4-3 + 2-1 + 5 + 6" },
        { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x04 | 0x08 | 0x10 | 0x20, "2-1 + 4 sines" },
        { { 0x02, 0x00, 0x00, 0x00, 0x20, 0x00 }, 0x01 | 0x04 | 0x08 | 0x10, "6-5 + 4 sines" },
        { { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x02 | 0x08 | 0x10 | 0x20, "3-1 + 4 sines" },
        { { 0x02, 0x04, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x08 | 0x10 | 0x20, "3-2-1 + 3 sines" },
        { { 0x00, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01 | 0x02 | 0x04 | 0x10, "6-5 | 4-3 + 1 + 2" },
        { { 0x00, 0x00, 0x00, 0x10, 0x20, 0x00 }, 0x01 | 0x02 | 0x04 | 0x08, "6-5-4 + 1 + 2 + 3" },
        { { 0x00, 0x00, 0x00, 0x00, 0x20, 0x00 }, 0x01 | 0x02 | 0x04 | 0x08 | 0x10, "6-5 + 4 sines" },
        { { 0x00, 0x00, 0x00, 0x00, 0x20, 0x00 }, 0x01 | 0x02 | 0x04 | 0x08 | 0x20, "6-5 + 1 + 2 + 3 + 6" },
        { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x02 | 0x04 | 0x08 | 0x10, "5 sines" },
        { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x02 | 0x04 | 0x08, "4 sines" },
        { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x01 | 0x02 | 0x04, "3 sines" },
        { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3f, "6 sines" },
    };
}

const FmAlgorithm& getFmAlgorithm (int index) noexcept
{
    return kAlgorithms[juce::jlimit (1, kFmAlgorithmCount, index) - 1];
}

//==============================================================================
RenderSchedule buildSchedule (const Project& project, const std::set<juce::int64>& onlyTracks,
                              const HostedInstrumentFactory& instrumentFactory,
                              const HostedEffectFactory& effectFactory)
{
    RenderSchedule schedule;
    // The master fader is part of the project, and the export obeys it: 0 dB is a
    // gain of 1, so a project saved before the mixer existed renders exactly as before.
    schedule.masterGain = (float) decibelsToGain (juce::jlimit (-60.0f, 6.0f,
                                                               project.masterVolumeDb));
    const music::TempoMap tempoMap (project.tempoMarkers);

    schedule.tracks.reserve (project.tracks.size());

    for (const auto& track : project.tracks)
    {
        // A stem export asks for one track at a time; everything else about the
        // render is identical to the full mix.
        if (! onlyTracks.empty() && onlyTracks.find (track.id) == onlyTracks.end())
            continue;

        // Track level mute / solo: a track that cannot be heard contributes
        // nothing at all - no events, no audio, and no render length.
        if (! isTrackAudible (project, track))
            continue;

        TrackRenderInfo info;
        info.trackId      = track.id;
        info.kind         = track.kind;
        info.synth        = track.synth;
        info.sampler      = track.sampler;
        info.subtractive  = track.subtractive;
        info.wavetable    = track.wavetable;
        info.drums        = track.drums;
        info.granular     = track.granular;
        info.hybrid       = track.hybrid;

        // A VST track's sound comes from the plugin. The live instances are put in
        // place by the audio engine (the message thread owns them); an export asks
        // the factory for private ones. No factory and no instance simply means
        // this track renders silence.
        if (track.kind == TrackKind::vst && instrumentFactory && track.vst.hasPlugin())
            info.instrument = instrumentFactory (track.id, track.vst);

        // A plugin whose pitch bend moves the whole patch cannot take per-note
        // tuning, so this track can be set to play semitones instead (no bend at all).
        info.equalTemperament = track.kind == TrackKind::vst && track.vst.equalTemperament;

        // The insert chain: an export asks the factory for private instances, the
        // live engine has them put in place by the plugin host (see AudioEngine).
        if (effectFactory)
        {
            info.effects.reserve (track.effects.size());

            for (int i = 0; i < (int) track.effects.size(); ++i)
                info.effects.push_back (effectFactory (track.id, i, track.effects[(size_t) i]));
        }

        // The table bank is a read-only library: build it here, on the message
        // thread, so the audio thread only ever reads it.
        wavetable::ensureBuilt();

        // A wavetable track's oscillators may point at the project's own tables
        // instead: resolve them (mipmaps included) into this snapshot, so the
        // audio thread still only ever reads immutable data.
        if (track.kind == TrackKind::wavetable)
            for (int i = 0; i < (int) info.wavetables.size(); ++i)
            {
                const auto tableId = track.wavetable.oscs[(size_t) i].customTableId;

                if (tableId != 0)
                    info.wavetables[(size_t) i] =
                        wavetabletables::playableTableFor (project, tableId);
            }

        if (track.kind == TrackKind::hybrid)
        {
            for (int layer = 0; layer < 3; ++layer)
            {
                const auto& l = track.hybrid.layers[(size_t) layer];

                if (l.mode == HybridLayerMode::wavetable)
                {
                    for (int osc = 0; osc < 2; ++osc)
                    {
                        const auto id = l.wavetable.oscs[(size_t) osc].customTableId;

                        if (id != 0)
                            info.hybridWavetables[(size_t) layer][(size_t) osc] =
                                wavetabletables::playableTableFor (project, id);
                    }
                }
                else if (l.mode == HybridLayerMode::sample || l.mode == HybridLayerMode::granular)
                {
                    const auto& path = l.mode == HybridLayerMode::sample ? l.sample.filePath
                                                                         : l.granular.filePath;

                    if (path.isNotEmpty())
                    {
                        juce::String sampleError;
                        info.hybridSamples[(size_t) layer] = getCachedSample (path, sampleError);
                    }
                }
            }
        }

        // Both of the sample-based instruments read their source through the same
        // decoded-and-cached LoadedSample: the sampler plays it, the granular
        // instrument cuts grains out of it.
        if (track.kind == TrackKind::sampler || track.kind == TrackKind::granular)
        {
            const auto& path = track.kind == TrackKind::granular ? track.granular.filePath
                                                                 : track.sampler.filePath;

            if (path.isNotEmpty())
            {
                juce::String sampleError;
                info.sample = getCachedSample (path, sampleError);
            }
        }

        if (track.kind == TrackKind::audio)
        {
            // The tempo map has to be built before the clips are placed; it is
            // already available above.
            // Audio clips are placed on the timeline: where the clip sits, how long
            // it is, and where its window starts inside the file.
            for (const auto& clip : track.clips)
            {
                if (clip.audioFilePath.isEmpty())
                    continue;

                juce::String clipError;
                auto sample = getCachedSample (clip.audioFilePath, clipError);

                if (sample == nullptr)
                    continue;

                AudioPlacement placement;
                placement.startSeconds = tempoMap.secondsAtTick (clip.startTick);
                placement.lengthSeconds = tempoMap.secondsAtTick (clip.endTick())
                                              - placement.startSeconds;
                placement.sourceOffsetSeconds = juce::jmax (0.0, clip.audioOffsetSeconds);
                placement.gain = juce::jlimit (0.0f, 4.0f, clip.audioGain);
                placement.sample = std::move (sample);
                info.audioClips.push_back (std::move (placement));

                schedule.lengthSeconds = juce::jmax (schedule.lengthSeconds,
                                                     info.audioClips.back().startSeconds
                                                         + info.audioClips.back().lengthSeconds);
            }
        }

        info.volumeLinear = (float) decibelsToGain (track.volumeDb);
        info.pan          = juce::jlimit (-1.0f, 1.0f, track.pan);
        info.events.reserve (track.clips.size() * 16);

        // Clips are expanded into absolute events. Every placed note gets its own
        // event id so that repeats of the same note cannot steal each other's voice.
        juce::int64 eventId = 0;

        for (const auto& clip : track.clips)
        {
            const auto placements = music::expandClip (clip);
            std::map<std::pair<int, juce::int64>, int> placementByNote;
            std::vector<int> next (placements.size(), -1);
            std::vector<int> previous (placements.size(), -1);

            for (int i = 0; i < (int) placements.size(); ++i)
            {
                const auto& placement = placements[(size_t) i];
                const auto eligible = ! placement.note->muted
                                      && ! (track.kind == TrackKind::drums
                                            && placement.note->pad < 0);

                if (eligible)
                    placementByNote[{ placement.repeat, placement.note->id }] = i;
            }

            for (int i = 0; i < (int) placements.size(); ++i)
            {
                const auto& placement = placements[(size_t) i];

                if (placement.note->muted
                    || placement.note->tieFromNoteId == 0
                    || (track.kind == TrackKind::drums && placement.note->pad < 0))
                    continue;

                const auto found = placementByNote.find (
                    { placement.repeat, placement.note->tieFromNoteId });

                if (found == placementByNote.end())
                    continue;

                const auto predecessor = found->second;

                if (placements[(size_t) predecessor].note->muted
                    || placements[(size_t) predecessor].endTick != placement.startTick
                    || next[(size_t) predecessor] >= 0)
                    continue;

                next[(size_t) predecessor] = i;
                previous[(size_t) i] = predecessor;
            }

            std::vector<bool> visited (placements.size(), false);

            for (int head = 0; head < (int) placements.size(); ++head)
            {
                const auto& headPlacement = placements[(size_t) head];
                const auto eligible = ! headPlacement.note->muted
                                      && ! (track.kind == TrackKind::drums
                                            && headPlacement.note->pad < 0);

                if (! eligible || visited[(size_t) head] || previous[(size_t) head] >= 0)
                    continue;

                std::vector<int> chain;
                auto cursor = head;

                while (cursor >= 0 && cursor < (int) placements.size()
                       && ! visited[(size_t) cursor])
                {
                    chain.push_back (cursor);
                    visited[(size_t) cursor] = true;
                    cursor = next[(size_t) cursor];
                }

                if (chain.empty())
                    continue;

                const auto& firstPlacement = placements[(size_t) chain.front()];
                const auto& lastPlacement = placements[(size_t) chain.back()];
                const auto startSeconds = tempoMap.secondsAtTick (firstPlacement.startTick);
                const auto endSeconds = tempoMap.secondsAtTick (lastPlacement.endTick);
                const auto id = ++eventId;

                SynthEvent on;
                on.timeSeconds = startSeconds;
                on.noteId      = id;
                on.isNoteOn    = true;
                on.frequency   = (float) firstPlacement.note->frequencyHz;
                on.velocity    = (float) firstPlacement.note->velocity;
                on.pad         = track.kind == TrackKind::drums
                                     ? firstPlacement.note->pad : -1;
                info.events.push_back (on);

                auto addPitch = [&] (double time, double hz)
                {
                    if (! (hz > 0.0) || ! std::isfinite (hz))
                        return;

                    SynthEvent pitch;
                    pitch.timeSeconds = time;
                    pitch.noteId = id;
                    pitch.isNoteOn = false;
                    pitch.isPitch = true;
                    pitch.frequency = (float) hz;
                    pitch.velocity = on.velocity;
                    pitch.pad = on.pad;
                    info.events.push_back (pitch);
                };

                for (int chainIndex = 0; chainIndex < (int) chain.size(); ++chainIndex)
                {
                    const auto& placement = placements[(size_t) chain[(size_t) chainIndex]];
                    const auto& note = *placement.note;
                    const auto noteStartSeconds = tempoMap.secondsAtTick (placement.startTick);
                    const auto noteEndSeconds = tempoMap.secondsAtTick (placement.endTick);

                    if (chainIndex > 0)
                        addPitch (noteStartSeconds, note.frequencyHz);

                    if (note.hasPitchTilt())
                    {
                        std::vector<double> times { noteStartSeconds, noteEndSeconds };
                        const auto tiltStartTick = placement.startTick
                            + (juce::int64) std::llround ((double) note.tiltStart
                                                          * (double) note.durationTicks);
                        const auto tiltEndTick = placement.startTick
                            + (juce::int64) std::llround ((double) note.tiltEnd
                                                          * (double) note.durationTicks);
                        times.push_back (tempoMap.secondsAtTick (
                            juce::jlimit (placement.startTick, placement.endTick, tiltStartTick)));
                        times.push_back (tempoMap.secondsAtTick (
                            juce::jlimit (placement.startTick, placement.endTick, tiltEndTick)));

                        constexpr double kPitchEventStepSeconds = 0.005;

                        for (double time = noteStartSeconds + kPitchEventStepSeconds;
                             time < noteEndSeconds; time += kPitchEventStepSeconds)
                            times.push_back (time);

                        std::sort (times.begin(), times.end());
                        times.erase (std::unique (times.begin(), times.end()), times.end());

                        for (const auto time : times)
                        {
                            const auto tick = tempoMap.tickAtSeconds (time);
                            const auto local = note.durationTicks > 0
                                ? (tick - (double) placement.startTick)
                                      / (double) note.durationTicks
                                : 1.0;
                            addPitch (time, music::pitchAt (note, local));
                        }
                    }
                }

                SynthEvent off;
                off.timeSeconds = endSeconds;
                off.noteId      = id;
                off.isNoteOn    = false;
                off.frequency   = (float) lastPlacement.note->effectiveTailHz();
                off.pad         = on.pad;
                info.events.push_back (off);

                schedule.lengthSeconds = juce::jmax (schedule.lengthSeconds, endSeconds);
            }
        }

        std::sort (info.events.begin(), info.events.end(),
                   [] (const SynthEvent& a, const SynthEvent& b)
                   {
                       if (a.timeSeconds != b.timeSeconds)
                           return a.timeSeconds < b.timeSeconds;

                       // A note-off at the same instant has to come first, otherwise a
                       // repeated note would cut its own new voice short.
                       if (a.isPitch != b.isPitch)
                           return a.isPitch;

                       return (! a.isNoteOn) && b.isNoteOn;
                   });

        // The controller curves travel with the clip exactly as the notes do: one
        // period tiled across the clip's window, then placed on the timeline through
        // the tempo map. They never make a render longer on their own.
        for (const auto& clip : track.clips)
            for (const auto& lane : clip.controllerCurves)
                for (const auto& value : controllers::expandControllers (clip, lane))
                    info.controllers.push_back ({ tempoMap.secondsAtTick (value.tick),
                                                  lane.controller, value.value });

        std::sort (info.controllers.begin(), info.controllers.end(),
                   [] (const ControllerEvent& a, const ControllerEvent& b)
                   {
                       if (a.timeSeconds != b.timeSeconds)
                           return a.timeSeconds < b.timeSeconds;

                       return a.controller < b.controller;
                   });

        schedule.tracks.push_back (std::move (info));
    }

    wavetabletables::pruneTableCache (project);
    return schedule;
}


//==============================================================================
TrackSynth::TrackSynth()
{
    prepare (48000.0);
}

void TrackSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& v : voices)
    {
        v.filter.prepare ({ sampleRate, 512, 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    setParams (params);
}

void TrackSynth::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.released = false;
        v.noteId = 0;
        v.frequency = 440.0;
        v.amplitude = 1.0f;
        v.midiNote = 60;
        v.feedbackMemory = 0.0;
        v.lfoPhase = 0.0;
        v.lfoDelayTime = 0.0;
        v.lfoHold = 0.0;
        v.lfoHoldValid = false;

        for (auto& op : v.ops)
        {
            op.phase = 0.0;
            op.envelope = 0.0;
            op.step = 0.0;
            op.stage = 0;
            op.remaining = 0.0;
            op.output = 0.0;
            op.finished = false;
        }

        v.filter.reset();
        v.appliedCutoff = 0.0f;
        v.appliedResonance = 0.0f;
    }

    orderCounter = 0;
}

void TrackSynth::setParams (const SynthParams& newParams)
{
    params = newParams;
}

TrackSynth::Voice* TrackSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId)
            return &v;

    return nullptr;
}

TrackSynth::Voice& TrackSynth::findVoiceToUse()
{
    // Prefer a completely free voice.
    for (auto& v : voices)
        if (! v.active)
            return v;

    // Then a voice that is already releasing.
    Voice* best = nullptr;

    for (auto& v : voices)
        if (v.released && (best == nullptr || v.startOrder < best->startOrder))
            best = &v;

    if (best != nullptr)
        return *best;

    // Otherwise steal the oldest one.
    Voice* oldest = &voices[0];

    for (auto& v : voices)
        if (v.startOrder < oldest->startOrder)
            oldest = &v;

    return *oldest;
}

void TrackSynth::beginSegment (Voice& voice, int opIndex, int stage)
{
    auto& op = voice.ops[opIndex];
    const auto& settings = params.patch.ops[(size_t) opIndex];

    op.stage = stage;
    op.holding = false;

    // Keyboard rate scaling: higher notes run their envelopes faster, which is
    // most of the reason an FM piano sounds right across the keyboard.
    const auto scale = settings.rateScale * (voice.midiNote - 60) / 12.0;
    const auto rate = juce::jlimit (0, 99, settings.rate[stage] + (int) std::lround (scale));

    op.remaining = juce::jmax (1.0, fm::rateToSeconds (rate) * sampleRate);
    op.step = (settings.level[stage] - op.envelope) / op.remaining;
}

void TrackSynth::startVoice (Voice& voice)
{
    for (int i = 0; i < kFmOperators; ++i)
    {
        auto& op = voice.ops[i];

        op.phase = 0.0;
        op.output = 0.0;
        op.finished = false;
        op.stage = 0;
        op.holding = false;

        // R1 runs from silence up to L1, which is what gives an FM patch its
        // attack transient; a stolen voice starts clean rather than from wherever
        // the previous note happened to be.
        op.envelope = 0.0;
        beginSegment (voice, i, 0);
    }

    voice.feedbackMemory = 0.0;
    voice.lfoPhase = 0.0;
    voice.lfoDelayTime = 0.0;
    voice.lfoHold = 0.0;
    voice.lfoHoldValid = false;
}

void TrackSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (frequencyHz <= 0.0f)
        return;

    // Retrigger an existing voice that is already playing this note.
    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();

    voice.active    = true;
    voice.released  = false;
    voice.noteId    = noteId;
    voice.frequency = (double) frequencyHz;
    voice.amplitude = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.midiNote  = juce::jlimit (0, 127,
                                    (int) std::lround (69.0 + 12.0 * std::log2 (voice.frequency / 440.0)));
    voice.startOrder = ++orderCounter;

    startVoice (voice);

    voice.filter.reset();
    voice.appliedCutoff = 0.0f;
    voice.appliedResonance = 0.0f;
}

void TrackSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f))
        return;

    if (auto* voice = findVoiceForNote (noteId))
    {
        voice->frequency = music::VerticalAxis::clampHz ((double) frequencyHz);
        voice->midiNote = juce::jlimit (
            0, 127, (int) std::lround (69.0 + 12.0 * std::log2 (voice->frequency / 440.0)));
    }
}

void TrackSynth::noteOff (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.noteId == noteId)
            v.released = true;
}

void TrackSynth::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        if (immediately)
            v.active = false;
        else
            v.released = true;
    }
}

bool TrackSynth::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active)
            return true;

    return false;
}

int TrackSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.active)
            ++count;

    return count;
}

void TrackSynth::renderAdding (juce::AudioBuffer<float>& buffer,
                               int startSample,
                               int numSamples,
                               float gainLeft,
                               float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    const auto& patch = params.patch;
    const auto& alg = getFmAlgorithm (patch.algorithm);
    const auto dtRatio = 1.0 / sampleRate;
    const auto voiceTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain);

    // The transpose shifts the whole patch, exactly like holding the transpose
    // button on the original.
    const auto transposeRatio = std::pow (2.0, patch.transpose / 12.0);
    const auto feedbackAmount = kFeedbackScale * (patch.feedback / 7.0);
    const auto lfoIncrement = lfoHz (patch.lfoRate) / sampleRate;
    const auto lfoDelaySeconds = (patch.lfoDelay / 99.0) * 3.0;
    const auto pitchDepthCents = (patch.lfoPitchDepth / 99.0) * 100.0;
    const auto ampDepth = patch.lfoAmpDepth / 99.0;

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto voiceGain = voiceTrim * v.amplitude;
        const auto lg = voiceGain * gainLeft;
        const auto rg = voiceGain * gainRight;

        // Filter coefficients are refreshed once per block rather than per sample:
        // the block is ~10 ms, which is smooth enough for a cutoff knob.
        const auto cutoff = juce::jlimit (20.0f, (float) (sampleRate * 0.49), params.filterCutoffHz);
        const auto resonance = juce::jlimit (0.1f, 20.0f, params.filterResonance);

        if (std::abs (cutoff - v.appliedCutoff) > 0.5f
            || std::abs (resonance - v.appliedResonance) > 1.0e-4f)
        {
            v.filter.setCutoffFrequency (cutoff);
            v.filter.setResonance (resonance);
            v.appliedCutoff = cutoff;
            v.appliedResonance = resonance;
        }

        const auto baseFrequency = v.frequency * transposeRatio;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- LFO -------------------------------------------------------
            v.lfoPhase = wrap01 (v.lfoPhase + lfoIncrement);
            const auto lfoWrapped = v.lfoPhase < lfoIncrement;

            if (lfoDelaySeconds > 0.0)
                v.lfoDelayTime = juce::jmin (lfoDelaySeconds, v.lfoDelayTime + dtRatio);

            const auto delayGain = lfoDelaySeconds > 0.0 ? v.lfoDelayTime / lfoDelaySeconds : 1.0;

            double lfo = 0.0;

            switch (juce::jlimit (0, 4, patch.lfoWaveform))
            {
                case 0:  lfo = v.lfoPhase < 0.5 ? (4.0 * v.lfoPhase - 1.0)
                                                : (3.0 - 4.0 * v.lfoPhase); break;   // triangle
                case 1:  lfo = 2.0 * v.lfoPhase - 1.0; break;                        // saw up
                case 2:  lfo = 1.0 - 2.0 * v.lfoPhase; break;                        // saw down
                case 3:  lfo = v.lfoPhase < 0.5 ? 1.0 : -1.0; break;                 // square
                default:
                {
                    // Sample and hold: a new value once per LFO cycle.
                    if (! v.lfoHoldValid || lfoWrapped)
                    {
                        v.lfoHold = lfoRandom.nextDouble() * 2.0 - 1.0;
                        v.lfoHoldValid = true;
                    }

                    lfo = v.lfoHold;
                    break;
                }
            }

            lfo *= delayGain;

            const auto pitchFactor = pitchDepthCents > 0.0
                                         ? std::pow (2.0, (pitchDepthCents * lfo) / 1200.0)
                                         : 1.0;
            const auto tremolo = 1.0 - ampDepth * (0.5 - 0.5 * lfo);

            // ---- operators, from the top of the stack downwards --------------
            double output = 0.0;

            for (int o = kFmOperators - 1; o >= 0; --o)
            {
                auto& op = v.ops[o];
                const auto& settings = patch.ops[(size_t) o];

                // Releasing hands every operator that is still rising or holding
                // over to R4, which runs down to L4.
                if (v.released && ! op.finished && op.stage < 3)
                    beginSegment (v, o, 3);

                // Run the current segment: R1 -> L1, R2 -> L2, R3 -> L3, hold,
                // then R4 -> L4 once the note has ended.
                if (! op.finished && ! op.holding)
                {
                    op.envelope += op.step;
                    op.remaining -= 1.0;

                    if (op.remaining <= 0.0)
                    {
                        switch (op.stage)
                        {
                            case 0: op.envelope = settings.level[0]; beginSegment (v, o, 1); break;
                            case 1: op.envelope = settings.level[1]; beginSegment (v, o, 2); break;
                            case 2: op.envelope = settings.level[2]; op.holding = true; break;
                            default: op.envelope = settings.level[3]; op.finished = true; break;
                        }
                    }
                }

                const auto operatorFrequency = baseFrequency * settings.ratio
                                                   * std::pow (2.0, settings.detune * 0.02) * pitchFactor;
                op.phase = wrap01 (op.phase + operatorFrequency * dtRatio);

                // Phase modulation from every operator that feeds this one.
                double phase = op.phase;

                for (int m = kFmOperators - 1; m > o; --m)
                    if ((alg.modulators[o] & opBit (m)) != 0)
                        phase += v.ops[m].output * kModulationScale;

                if (o == kFmOperators - 1 && feedbackAmount > 0.0)
                    phase += v.feedbackMemory * feedbackAmount * kModulationScale;

                auto level = levelToAmplitude (settings.outputLevel)
                                 * (1.0 - (settings.velSens / 7.0) * (1.0 - v.amplitude))
                                 * levelToAmplitude (op.envelope);

                // A switched-off operator keeps tracing its envelope (so it picks
                // up where it would have been when it comes back) but contributes
                // nothing - it neither sounds nor modulates anyone.
                if (! settings.enabled)
                    level = 0.0;

                op.output = std::sin (kTwoPi * wrap01 (phase)) * level;

                if (o == kFmOperators - 1)
                    v.feedbackMemory = op.output;
            }

            for (int o = 0; o < kFmOperators; ++o)
                if ((alg.carriers & opBit (o)) != 0)
                    output += v.ops[o].output;

            output *= tremolo;

            // ---- voice ------------------------------------------------------
            bool anyRunning = false;

            for (const auto& op : v.ops)
                if (! op.finished)
                    anyRunning = true;

            if (! anyRunning)
            {
                v.active = false;
                v.released = false;
                break;
            }

            const auto filtered = v.filter.processSample (0, (float) output);

            left[i]  += filtered * lg;
            right[i] += filtered * rg;
        }
    }
}

} // namespace nidaw
