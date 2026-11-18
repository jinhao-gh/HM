#pragma once

#include "../Core/Types.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace hmdaw
{

/** One audio file, decoded and held in memory, ready to be played back. */
struct LoadedSample
{
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 48000.0;
    juce::String filePath;

    /** A coarse peak envelope (absolute peak per bucket), used for the waveform
        thumbnails on the arrangement lane and in the editor. Read-only after loading. */
    std::vector<float> peaks;

    double lengthSeconds() const noexcept
    {
        return sourceSampleRate > 0.0 ? (double) audio.getNumSamples() / sourceSampleRate : 0.0;
    }
};

/** Which peak bucket of a file a position inside a clip reads from, or -1 when
    that position falls outside the file (before its start or past its end).

    The arrangement's clip thumbnail and the editor's waveform both walk their
    width, turn it into a time inside the clip, add the clip's file offset and then
    want "the peak around here": this is that one step, in one place.
*/
inline int waveformBucketFor (double fileTimeSeconds, double fileLengthSeconds,
                              int bucketCount) noexcept
{
    if (fileLengthSeconds <= 0.0 || bucketCount <= 0)
        return -1;

    const auto bucket = (int) (fileTimeSeconds / fileLengthSeconds * (double) bucketCount);

    return (bucket < 0 || bucket >= bucketCount) ? -1 : bucket;
}

/** Reads an audio file into memory. Returns nullptr and fills in `error` on
    failure. The audio thread never calls this. */
std::shared_ptr<const LoadedSample> loadSampleFile (const juce::String& path, juce::String& error);

/** Cached version of the above, keyed by path, so rebuilding the schedule does not
    re-read the file every time the project changes. */
std::shared_ptr<const LoadedSample> getCachedSample (const juce::String& path, juce::String& error);

/** Drops one entry (or everything) from the cache; used when a file is replaced. */
void clearSampleCache (const juce::String& path = {});

/** A short read-only view of the timeline: one placed audio clip. */
struct AudioPlacement
{
    double startSeconds       = 0.0;   // where the clip begins on the timeline
    double lengthSeconds      = 0.0;
    double sourceOffsetSeconds = 0.0;  // where its window starts in the file
    float  gain               = 1.0f;
    std::shared_ptr<const LoadedSample> sample;
};

/** Mixes placed audio clips into the output. Stateless: everything the render
    needs is in the placement list plus the current transport time, so seeking and
    offline export go through exactly the same code. */
void renderAudioClips (const std::vector<AudioPlacement>& placements,
                       juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       double blockStartSeconds,
                       double outputSampleRate,
                       float gainLeft,
                       float gainRight);

//==============================================================================
/** Plays a LoadedSample polyphonically.

    The playback rate is `note frequency / root pitch`, so the sample sounds the
    pitch the note asks for. Linear interpolation, an ADSR, an optional loop and
    one low-pass filter per voice.
*/
class SamplePlayer
{
public:
    SamplePlayer();

    void prepare (double newSampleRate);
    void reset();

    void setParams (const SamplerParams& newParams);
    /** The sample to play; nullptr means "silent". */
    void setSample (std::shared_ptr<const LoadedSample> newSample);
    const SamplerParams& getParams() const noexcept { return params; }
    const std::shared_ptr<const LoadedSample>& getSample() const noexcept { return sample; }

    void noteOn (juce::int64 noteId, float frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, float frequencyHz);
    void allNotesOff (bool immediately);

    /** Adds this player's output into the first two channels of the buffer. */
    void renderAdding (juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       float gainLeft,
                       float gainRight);

    bool renderVoiceAdding (juce::int64 noteId,
                            juce::AudioBuffer<float>& buffer,
                            int startSample,
                            int numSamples,
                            float gainLeft,
                            float gainRight);

    bool isActive() const noexcept;
    int  activeVoiceCount() const noexcept;

private:
    struct Voice
    {
        bool        active   = false;
        bool        released = false;
        juce::int64 noteId   = 0;
        double      position = 0.0;   // read position in the sample, in samples
        double      rate     = 1.0;   // source samples per output sample
        float       amplitude = 1.0f;
        juce::uint32 startOrder = 0;
        double      lfoPhase = 0.0;
        juce::ADSR  envelope;
        juce::dsp::StateVariableTPTFilter<float> filter;
        float appliedCutoff = 0.0f;
        float appliedResonance = 0.0f;
    };

    Voice& findVoiceToUse();
    Voice* findVoiceForNote (juce::int64 noteId);
    void applyEnvelope (Voice& voice);

    std::array<Voice, kVoicesPerTrack> voices;
    SamplerParams params;
    std::shared_ptr<const LoadedSample> sample;
    double sampleRate = 48000.0;
    juce::uint32 orderCounter = 0;
};

} // namespace hmdaw
