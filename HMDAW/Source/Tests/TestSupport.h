#include "../Audio/SynthEngine.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/DrumSynth.h"
#include "../Audio/Exporter.h"
#include "../Core/MusicMath.h"
#include "../Core/EffectChain.h"
#include "../Core/ControllerMath.h"
#include "../Core/PluginGrouping.h"
#include "../Core/PluginScanProtocol.h"
#include "../Core/PresetStore.h"
#include "../Core/ProjectDocument.h"
#include "../Core/SubtractivePresets.h"
#include "../Core/VstSupport.h"
#include "../Core/WavetablePresets.h"
#include "../Core/WindowPlacement.h"

#include <iostream>
#include <algorithm>

 #if JUCE_WINDOWS
  #include <windows.h>
 #endif

using namespace hmdaw;


#pragma once

using namespace hmdaw;

namespace testsupport
{


/** A fresh project with one synth track, for the tests that need a lane to write
    into. makeDefaultProject() itself is deliberately empty. */
inline Project makeProjectWithSynthTrack()
{
    auto project = makeDefaultProject();
    addTrack (project, TrackKind::synth);

    // Unit tests predate the studio page: route this test track directly to a
    // centre-panned mixer channel so level/pan assertions stay about the engine,
    // not about the default microphone distance. Studio tests opt back into the
    // acoustic path explicitly.
    auto& scene = project.studio;
    auto* source = studio::findSourceForTrack (scene, project.tracks.front().id);

    if (source != nullptr)
    {
        source->mode = studio::SourceMode::mixer;
        source->pan = project.tracks.front().pan;

        studio::MixerInput input;
        input.id = studio::allocateId (scene);
        input.name = "测试输入";
        input.position = { 4.0, 1.5 };
        scene.mixerInputs.push_back (input);

        studio::MixerChannel channel;
        channel.id = studio::allocateId (scene);
        channel.name = "测试通道";
        channel.inputId = input.id;
        channel.pan = 0.0;
        scene.mixer.channels.push_back (channel);

        studio::Connection connection;
        connection.id = studio::allocateId (scene);
        connection.fromNode = source->id;
        connection.toNode = input.id;
        scene.connections.push_back (connection);
    }

    return project;
}

/** Prints the faulting address and module base so that a crash can be resolved
    against the linker map without a debugger attached. */
inline LONG WINAPI testCrashHandler (EXCEPTION_POINTERS* info)
{
    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        // 0x40010006 / 0x4001000A are the CRT's own debug-print exceptions.
        const auto code = (unsigned long) info->ExceptionRecord->ExceptionCode;

        if (code == 0x40010006ul || code == 0x4001000Aul)
            return EXCEPTION_CONTINUE_SEARCH;

        const auto base = (unsigned long long) (juce::pointer_sized_uint) ::GetModuleHandleW (nullptr);
        const auto address = (unsigned long long) (juce::pointer_sized_uint)
                                 info->ExceptionRecord->ExceptionAddress;

        std::fprintf (stderr, "CRASH code=0x%08lX address=0x%llX base=0x%llX offset=0x%llX\n",
                      (unsigned long) info->ExceptionRecord->ExceptionCode,
                      address, base, base != 0 ? address - base : 0);
        std::fflush (stderr);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}


} // namespace testsupport

namespace testsupport
{

/** A one second ramp written to a temp WAV, for the tests that need real audio on
    disk. It used to be a static member of one test class, which meant the class
    next door could not reach it once the tests lived in separate files. */
inline juce::File writeTempWav (const juce::String& name, double seconds)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (name);
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return file;

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleFormat (
                                 juce::AudioFormatWriterOptions::SampleFormat::integral)
                             .withSampleRate (48000.0)
                             .withNumChannels (1)
                             .withBitsPerSample (16);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return file;

    juce::AudioBuffer<float> tone (1, (int) (48000.0 * seconds));

    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample (0, i, (float) i / (float) juce::jmax (1, tone.getNumSamples()));

    writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    return file;
}

} // namespace testsupport

using namespace testsupport;

namespace testsupport
{

class CountingInstrument : public HostedInstrument
{
public:
    void prepare (double, int) override { prepared = true; }
    void reset() override
    {
        noteOnCalls = 0;
        noteOffCalls = 0;
        noteOnIds.clear();
        noteOnFrequencies.clear();
    }

    void noteOn (juce::int64 noteId, float frequencyHz, float) override
    {
        ++noteOnCalls;
        noteOnIds.push_back (noteId);
        noteOnFrequencies.push_back (frequencyHz);
    }

    void noteOff (juce::int64 noteId) override
    {
        ++noteOffCalls;
        noteOffIds.push_back (noteId);
    }

    void allNotesOff() override { allNotesOffCalls++; }

    /** The soft one: the engine uses this for stop / seek / loop wrap, and the
        tests check that it is what arrives there instead of `allNotesOff`. */
    void releaseHeldNotes() override { releaseHeldNotesCalls++; }

    void renderAdding (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       float gainLeft, float gainRight) override
    {
        renderCalls++;
        buffer.addSample (0, startSample, 0.25f * gainLeft);

        if (buffer.getNumChannels() > 1)
            buffer.addSample (1, startSample, 0.25f * gainRight);

        juce::ignoreUnused (numSamples);
    }

    bool isActive() const noexcept override { return active; }
    juce::MemoryBlock captureState() const override { return {}; }

    bool prepared = false;
    bool active = true;
    int  noteOnCalls = 0;
    int  noteOffCalls = 0;
    int  allNotesOffCalls = 0;
    int  releaseHeldNotesCalls = 0;
    int  renderCalls = 0;
    /** Every id this instrument was told to start / stop, in the order it heard
        them: the tests read these to prove two notes never arrive as one. */
    std::vector<juce::int64> noteOnIds, noteOffIds;
    /** The frequency each of those notes was played at - the engine decides that
        (exact, or snapped to a semitone: see `TrackRenderInfo::equalTemperament`). */
    std::vector<float> noteOnFrequencies;
};

class FakeEffect : public HostedEffect
{
public:
    FakeEffect (juce::int64 trackIdToUse, float gainToApply, int latencyToReport = 0,
                float offsetToAdd = 0.0f)
        : trackId (trackIdToUse),
          gain (gainToApply),
          latencySamples (latencyToReport),
          offset (offsetToAdd)
    {
    }

    void prepare (double, int) override { prepared = true; }
    void reset() override { resetCount++; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        if (bypass)
            return;

        processCount++;
        samplesSeen += numSamples;

        if (buffer.getNumChannels() > 0)
            peakSeen = juce::jmax (peakSeen, buffer.getMagnitude (0, startSample, numSamples));

        for (int channel = 0; channel < juce::jmin (2, buffer.getNumChannels()); ++channel)
        {
            auto* data = buffer.getWritePointer (channel, startSample);

            for (int i = 0; i < numSamples; ++i)
                data[i] = data[i] * gain + offset;
        }
    }

    bool isBypassed() const noexcept override { return bypass; }
    int getLatencySamples() const noexcept override { return latencySamples; }
    juce::MemoryBlock captureState() const override { return {}; }

    juce::int64 trackId = 0;
    float gain = 1.0f;
    int   latencySamples = 0;
    float offset = 0.0f;
    bool  bypass = false;
    bool  prepared = false;
    int   processCount = 0;
    int   samplesSeen = 0;
    int   resetCount = 0;
    /** The loudest sample the chain was handed, which is how the tests check what
        the fader did (or did not do) before it. */
    float peakSeen = 0.0f;
};

} // namespace testsupport
