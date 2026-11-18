#include "SamplePlayer.h"

#include <map>
#include <mutex>

namespace hmdaw
{

namespace
{
    constexpr float kMasterTrim = 0.6f;

    /** Sample cache: the schedule is rebuilt on every edit, and re-reading a file
        each time would be both slow and glitchy. Entries stay alive for the life of
        the process - a project has a handful of samples at most. */
    std::map<juce::String, std::shared_ptr<const LoadedSample>>& cache()
    {
        static std::map<juce::String, std::shared_ptr<const LoadedSample>> samples;
        return samples;
    }

    std::mutex& cacheLock()
    {
        static std::mutex lock;
        return lock;
    }

    juce::AudioFormatManager& formatManager()
    {
        static juce::AudioFormatManager manager;
        static std::once_flag once;
        std::call_once (once, [] { manager.registerBasicFormats(); });
        return manager;
    }
}

std::shared_ptr<const LoadedSample> loadSampleFile (const juce::String& path, juce::String& error)
{
    if (path.isEmpty())
    {
        error = NI_TXT ("还没有载入音频文件。");
        return nullptr;
    }

    const juce::File file (path);

    if (! file.existsAsFile())
    {
        error = NI_TXT ("找不到音频文件：") + path;
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));

    if (reader == nullptr)
    {
        error = NI_TXT ("无法识别这个音频格式：") + file.getFileName();
        return nullptr;
    }

    const auto lengthSamples = (int) juce::jmin ((juce::int64) reader->lengthInSamples,
                                                 (juce::int64) (reader->sampleRate * 60.0 * 10.0));

    if (lengthSamples <= 0)
    {
        error = NI_TXT ("这个音频文件是空的。");
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedSample>();
    loaded->filePath = path;
    loaded->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;

    // Mono in memory: the sampler plays one sample into a stereo field anyway.
    loaded->audio.setSize (1, lengthSamples);
    reader->read (&loaded->audio, 0, lengthSamples, 0, true, false);

    // A coarse peak envelope for the waveform displays (512 buckets is plenty for a
    // lane thumbnail and cheap to draw).
    constexpr int kPeakBuckets = 512;
    const auto buckets = juce::jmin (kPeakBuckets, lengthSamples);
    loaded->peaks.assign ((size_t) buckets, 0.0f);
    const auto* data = loaded->audio.getReadPointer (0);

    for (int bucket = 0; bucket < buckets; ++bucket)
    {
        const auto from = (int) ((juce::int64) bucket * lengthSamples / buckets);
        const auto to = juce::jmax (from + 1, (int) ((juce::int64) (bucket + 1) * lengthSamples / buckets));
        auto peak = 0.0f;

        for (int i = from; i < to && i < lengthSamples; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));

        loaded->peaks[(size_t) bucket] = peak;
    }

    return loaded;
}

std::shared_ptr<const LoadedSample> getCachedSample (const juce::String& path, juce::String& error)
{
    if (path.isEmpty())
    {
        error.clear();
        return nullptr;
    }

    {
        const std::lock_guard<std::mutex> lock (cacheLock());

        if (auto it = cache().find (path); it != cache().end())
            return it->second;
    }

    auto loaded = loadSampleFile (path, error);

    if (loaded == nullptr)
        return nullptr;

    const std::lock_guard<std::mutex> lock (cacheLock());
    cache()[path] = loaded;
    return loaded;
}

void clearSampleCache (const juce::String& path)
{
    const std::lock_guard<std::mutex> lock (cacheLock());

    if (path.isEmpty())
        cache().clear();
    else
        cache().erase (path);
}

void renderAudioClips (const std::vector<AudioPlacement>& placements,
                       juce::AudioBuffer<float>& buffer,
                       int startSample,
                       int numSamples,
                       double blockStartSeconds,
                       double outputSampleRate,
                       float gainLeft,
                       float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2 || outputSampleRate <= 0.0)
        return;

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (const auto& placement : placements)
    {
        if (placement.sample == nullptr || placement.lengthSeconds <= 0.0)
            continue;

        const auto clipEnd = placement.startSeconds + placement.lengthSeconds;

        if (blockStartSeconds >= clipEnd)
            continue;

        const auto blockEnd = blockStartSeconds + (double) numSamples / outputSampleRate;

        if (blockEnd <= placement.startSeconds)
            continue;

        const auto* source = placement.sample->audio.getReadPointer (0);
        const auto sourceLength = (juce::int64) placement.sample->audio.getNumSamples();
        const auto sourceRate = placement.sample->sourceSampleRate > 0.0
                                    ? placement.sample->sourceSampleRate
                                    : outputSampleRate;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto time = blockStartSeconds + (double) i / outputSampleRate;

            if (time < placement.startSeconds || time >= clipEnd)
                continue;

            const auto sourcePosition = (placement.sourceOffsetSeconds
                                             + (time - placement.startSeconds)) * sourceRate;
            const auto index = (juce::int64) sourcePosition;

            if (index < 0 || index >= sourceLength)
                continue;

            const auto next = juce::jmin (sourceLength - 1, index + 1);
            const auto fraction = (float) (sourcePosition - (double) index);
            const auto value = (source[index] + (source[next] - source[index]) * fraction)
                                   * placement.gain;

            left[i]  += value * gainLeft;
            right[i] += value * gainRight;
        }
    }
}

//==============================================================================
SamplePlayer::SamplePlayer()
{
    prepare (48000.0);
}

void SamplePlayer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& v : voices)
    {
        v.filter.prepare ({ sampleRate, 512, 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        applyEnvelope (v);
    }
}

void SamplePlayer::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.released = false;
        v.noteId = 0;
        v.position = 0.0;
        v.amplitude = 1.0f;
        v.lfoPhase = 0.0;
        v.envelope.reset();
        v.filter.reset();
        v.appliedCutoff = 0.0f;
        v.appliedResonance = 0.0f;
    }

    orderCounter = 0;
}

void SamplePlayer::setParams (const SamplerParams& newParams)
{
    params = newParams;

    for (auto& v : voices)
        applyEnvelope (v);
}

void SamplePlayer::setSample (std::shared_ptr<const LoadedSample> newSample)
{
    sample = std::move (newSample);
}

void SamplePlayer::applyEnvelope (Voice& voice)
{
    juce::ADSR::Parameters adsr;
    adsr.attack  = juce::jmax (0.0f, params.attack);
    adsr.decay   = juce::jmax (0.0f, params.decay);
    adsr.sustain = juce::jlimit (0.0f, 1.0f, params.sustain);
    adsr.release = juce::jmax (0.0f, params.release);
    voice.envelope.setParameters (adsr);
}

SamplePlayer::Voice* SamplePlayer::findVoiceForNote (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId)
            return &v;

    return nullptr;
}

SamplePlayer::Voice& SamplePlayer::findVoiceToUse()
{
    for (auto& v : voices)
        if (! v.active)
            return v;

    Voice* best = nullptr;

    for (auto& v : voices)
        if (v.released && (best == nullptr || v.startOrder < best->startOrder))
            best = &v;

    if (best != nullptr)
        return *best;

    Voice* oldest = &voices[0];

    for (auto& v : voices)
        if (v.startOrder < oldest->startOrder)
            oldest = &v;

    return *oldest;
}

void SamplePlayer::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (frequencyHz <= 0.0f || sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();

    voice.active    = true;
    voice.released  = false;
    voice.noteId    = noteId;
    voice.position  = juce::jlimit (0.0, (double) sample->audio.getNumSamples() - 1.0,
                                    params.sampleStart * (double) sample->audio.getNumSamples());
    voice.amplitude = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.startOrder = ++orderCounter;
    voice.lfoPhase = 0.0;

    // The heart of it: playing the sample faster makes it sound higher.
    const auto root = juce::jmax (1.0, params.rootHz);
    const auto pitchRatio = (double) frequencyHz / root;
    voice.rate = pitchRatio * sample->sourceSampleRate / sampleRate;

    applyEnvelope (voice);
    voice.envelope.reset();
    voice.envelope.noteOn();
    voice.filter.reset();
    voice.appliedCutoff = 0.0f;
    voice.appliedResonance = 0.0f;
}

void SamplePlayer::noteOff (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.noteId == noteId)
        {
            v.released = true;
            v.envelope.noteOff();
        }
}

void SamplePlayer::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f) || sample == nullptr || sampleRate <= 0.0)
        return;

    if (auto* voice = findVoiceForNote (noteId))
    {
        const auto root = juce::jmax (1.0, params.rootHz);
        voice->rate = ((double) frequencyHz / root) * sample->sourceSampleRate / sampleRate;
    }
}

void SamplePlayer::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        if (immediately)
        {
            v.active = false;
            v.envelope.reset();
        }
        else if (! v.released)
        {
            v.released = true;
            v.envelope.noteOff();
        }
    }
}

bool SamplePlayer::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active)
            return true;

    return false;
}

int SamplePlayer::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.active)
            ++count;

    return count;
}

bool SamplePlayer::renderVoiceAdding (juce::int64 noteId,
                                      juce::AudioBuffer<float>& buffer,
                                      int startSample,
                                      int numSamples,
                                      float gainLeft,
                                      float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return false;

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return false;

    const auto* source = sample->audio.getReadPointer (0);
    const auto sourceLength = (juce::int64) sample->audio.getNumSamples();
    const auto voiceTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain);

    const auto regionStart = (juce::int64) juce::jlimit (
        (double) 0.0, (double) sourceLength - 1.0,
        params.sampleStart * (double) sourceLength);
    const auto regionEnd = (juce::int64) juce::jlimit (
        (double) regionStart + 1.0, (double) sourceLength,
        params.sampleEnd * (double) sourceLength);
    const auto loopStart = (juce::int64) juce::jlimit (
        (double) regionStart, (double) regionEnd - 1.0,
        params.loopStart * (double) sourceLength);
    const auto loopEnd = (juce::int64) juce::jlimit (
        (double) loopStart + 1.0, (double) regionEnd,
        params.loopEnd * (double) sourceLength);
    const auto crossfadeSamples = juce::jlimit (
        (juce::int64) 1, juce::jmax ((juce::int64) 1, loopEnd - loopStart),
        (juce::int64) (params.crossfade * (double) (loopEnd - loopStart)));

    const auto dtRatio = 1.0 / sampleRate;
    const auto lfoIncrement = juce::jmax (0.0f, params.lfoRateHz) * dtRatio;
    const auto pitchDepthCents = params.lfoPitchCents;
    const auto ampDepth = juce::jlimit (0.0f, 1.0f, params.lfoAmpDepth);

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (auto& v : voices)
    {
        if (! v.active || v.noteId != noteId)
            continue;

        const auto voiceGain = voiceTrim * v.amplitude;
        const auto lg = voiceGain * gainLeft;
        const auto rg = voiceGain * gainRight;

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

        for (int i = 0; i < numSamples; ++i)
        {
            const auto envelopeValue = v.envelope.getNextSample();

            if (! v.envelope.isActive())
            {
                v.active = false;
                v.released = false;
                break;
            }

            // A single LFO can wobble the pitch and the level of the sample.
            v.lfoPhase += lfoIncrement;

            if (v.lfoPhase >= 1.0)
                v.lfoPhase -= std::floor (v.lfoPhase);

            const auto lfo = lfoIncrement > 0.0
                                 ? std::sin (6.283185307179586 * v.lfoPhase)
                                 : 0.0;

            const auto rate = v.rate * std::pow (2.0, (pitchDepthCents * lfo) / 1200.0);
            const auto tremolo = 1.0 - ampDepth * (0.5 - 0.5 * lfo);

            if (v.position < (double) regionStart)
                v.position = (double) regionStart;

            if (v.position >= (double) regionEnd)
            {
                if (params.looped && loopEnd > loopStart)
                    v.position = (double) loopStart;
                else
                {
                    v.active = false;
                    v.released = false;
                    break;
                }
            }
            else if (params.looped && loopEnd > loopStart && v.position >= (double) loopEnd)
            {
                v.position = (double) loopStart + (v.position - (double) loopEnd);
            }

            auto readAt = [&] (double position)
            {
                const auto readIndex = juce::jlimit ((juce::int64) 0, sourceLength - 1,
                                                     (juce::int64) position);
                const auto nextIndex = juce::jmin (sourceLength - 1, readIndex + 1);
                const auto fraction = (float) (position - (double) readIndex);
                return source[readIndex]
                           + (source[nextIndex] - source[readIndex]) * fraction;
            };

            auto value = readAt (v.position);

            if (params.looped && crossfadeSamples > 1
                && v.position >= (double) (loopEnd - crossfadeSamples))
            {
                const auto fade = (float) juce::jlimit (
                    0.0, 1.0,
                    (v.position - (double) (loopEnd - crossfadeSamples))
                        / (double) crossfadeSamples);
                const auto wrapped = (double) loopStart
                                         + (v.position - (double) (loopEnd - crossfadeSamples));
                value = value * (1.0f - fade) + readAt (wrapped) * fade;
            }

            v.position += rate;

            // The product is a double (the tremolo depth is one); the filter works
            // in the sample type, so say so instead of letting the compiler narrow
            // it behind our back.
            const auto filtered = v.filter.processSample (0,
                                                         (float) (value * envelopeValue * tremolo));

            left[i]  += filtered * lg;
            right[i] += filtered * rg;
        }
    }

    return findVoiceForNote (noteId) != nullptr;
}

void SamplePlayer::renderAdding (juce::AudioBuffer<float>& buffer,
                                 int startSample,
                                 int numSamples,
                                 float gainLeft,
                                 float gainRight)
{
    for (auto& voice : voices)
        if (voice.active)
            renderVoiceAdding (voice.noteId, buffer, startSample, numSamples,
                               gainLeft, gainRight);
}

} // namespace hmdaw
