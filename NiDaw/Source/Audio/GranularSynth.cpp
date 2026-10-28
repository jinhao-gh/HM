#include "GranularSynth.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    juce::dsp::StateVariableTPTFilterType filterTypeFor (FilterMode mode) noexcept
    {
        switch (mode)
        {
            case FilterMode::bandPass: return juce::dsp::StateVariableTPTFilterType::bandpass;
            case FilterMode::highPass: return juce::dsp::StateVariableTPTFilterType::highpass;
            case FilterMode::notch:
            case FilterMode::comb:
            case FilterMode::lowPass:  break;
        }

        return juce::dsp::StateVariableTPTFilterType::lowpass;
    }

    /** Folds 0..1 into 0..1..0..1: scanning past an end turns round instead of
        jumping to the other one (1 → 2 → 1). Negative numbers fold the same way. */
    double fold01 (double value) noexcept
    {
        const auto wrapped = value - 2.0 * std::floor (value * 0.5);
        return wrapped > 1.0 ? 2.0 - wrapped : wrapped;
    }
}

//==============================================================================
GranularSynth::GranularSynth()
{
    prepare (48000.0);
}

void GranularSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& voice : voices)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            // One channel per cascade: each output channel is filtered on its own, so
            // the grains' own panning survives the filter.
            voice.filterStage1[(size_t) channel].prepare ({ sampleRate, 512, 1 });
            voice.filterStage2[(size_t) channel].prepare ({ sampleRate, 512, 1 });
        }

        voice.filterStage1[0].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        voice.filterStage1[1].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        voice.filterStage2[0].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        voice.filterStage2[1].setType (juce::dsp::StateVariableTPTFilterType::lowpass);

        applyEnvelopes (voice);
    }
}

void GranularSynth::reset()
{
    for (auto& voice : voices)
    {
        voice.active   = false;
        voice.released = false;
        voice.noteId   = 0;
        voice.amplitude = 1.0f;
        voice.elapsedSeconds = 0.0;
        voice.scanFraction = 0.0;
        voice.noteRate = 1.0;
        voice.nextGrainIn = 0.0;
        voice.envelope.reset();
        voice.positionEnvelope.reset();

        for (int channel = 0; channel < 2; ++channel)
        {
            voice.filterStage1[(size_t) channel].reset();
            voice.filterStage2[(size_t) channel].reset();
        }

        voice.appliedCutoff = 0.0f;
        voice.appliedResonance = 0.0f;

        for (auto& grain : voice.grains)
            grain = Grain{};
    }

    orderCounter = 0;
    startedGrains = 0;
    publishGrains();
}

void GranularSynth::setParams (const GranularParams& newParams)
{
    params = newParams;

    for (auto& voice : voices)
        applyEnvelopes (voice);
}

void GranularSynth::setSample (std::shared_ptr<const LoadedSample> newSample)
{
    sample = std::move (newSample);
}

void GranularSynth::applyEnvelopes (Voice& voice)
{
    juce::ADSR::Parameters amplitude;
    amplitude.attack  = juce::jmax (0.0f, params.ampAttack);
    amplitude.decay   = juce::jmax (0.0f, params.ampDecay);
    amplitude.sustain = juce::jlimit (0.0f, 1.0f, params.ampSustain);
    amplitude.release = juce::jmax (0.0f, params.ampRelease);
    voice.envelope.setParameters (amplitude);

    juce::ADSR::Parameters position;
    position.attack  = juce::jmax (0.0f, params.posAttack);
    position.decay   = juce::jmax (0.0f, params.posDecay);
    position.sustain = juce::jlimit (0.0f, 1.0f, params.posSustain);
    position.release = juce::jmax (0.0f, params.posRelease);
    voice.positionEnvelope.setParameters (position);
}

GranularSynth::Voice* GranularSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& voice : voices)
        if (voice.active && voice.noteId == noteId)
            return &voice;

    return nullptr;
}

GranularSynth::Voice& GranularSynth::findVoiceToUse()
{
    for (auto& voice : voices)
        if (! voice.active)
            return voice;

    Voice* best = nullptr;

    for (auto& voice : voices)
        if (voice.released && (best == nullptr || voice.startOrder < best->startOrder))
            best = &voice;

    if (best != nullptr)
        return *best;

    Voice* oldest = &voices[0];

    for (auto& voice : voices)
        if (voice.startOrder < oldest->startOrder)
            oldest = &voice;

    return *oldest;
}

double GranularSynth::randomUnit (Voice& voice) noexcept
{
    // xorshift32: cheap, no allocation, and the same sequence every run.
    voice.rng ^= voice.rng << 13;
    voice.rng ^= voice.rng >> 17;
    voice.rng ^= voice.rng << 5;

    return (double) (voice.rng >> 8) / (double) (1u << 24);
}

void GranularSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (frequencyHz <= 0.0f || sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();

    voice.active   = true;
    voice.released = false;
    voice.noteId   = noteId;
    voice.amplitude = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.elapsedSeconds = 0.0;
    voice.scanFraction = fold01 ((double) params.position);
    voice.startOrder = ++orderCounter;
    voice.nextGrainIn = 0.0;   // the first grain starts with the note
    voice.rng = 0x9e3779b9u ^ (juce::uint32) (noteId * 2654435761u) ^ (voice.startOrder * 2246822519u);

    if (voice.rng == 0)
        voice.rng = 0x1234567u;

    // The heart of it, exactly like the sampler: the note's frequency decides how
    // fast the file is read, so HM's exact frequencies stay exact.
    const auto root = juce::jmax (1.0, params.rootHz);
    voice.noteRate = ((double) frequencyHz / root) * sample->sourceSampleRate / sampleRate;
    voice.keyTrackRatio = (double) frequencyHz / root;

    for (auto& grain : voice.grains)
        grain = Grain{};

    applyEnvelopes (voice);
    voice.envelope.reset();
    voice.envelope.noteOn();
    voice.positionEnvelope.reset();
    voice.positionEnvelope.noteOn();

    for (int channel = 0; channel < 2; ++channel)
    {
        voice.filterStage1[(size_t) channel].reset();
        voice.filterStage2[(size_t) channel].reset();
    }

    voice.appliedCutoff = 0.0f;
    voice.appliedResonance = 0.0f;
}

void GranularSynth::noteOff (juce::int64 noteId)
{
    for (auto& voice : voices)
        if (voice.active && ! voice.released && voice.noteId == noteId)
        {
            voice.released = true;
            voice.envelope.noteOff();
            voice.positionEnvelope.noteOff();
        }
}

void GranularSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f) || sample == nullptr || sampleRate <= 0.0)
        return;

    if (auto* voice = findVoiceForNote (noteId))
    {
        const auto root = juce::jmax (1.0, params.rootHz);
        voice->noteRate = ((double) frequencyHz / root)
                          * sample->sourceSampleRate / sampleRate;
        voice->keyTrackRatio = (double) frequencyHz / root;
    }
}

void GranularSynth::allNotesOff (bool immediately)
{
    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        if (immediately)
        {
            voice.active = false;
            voice.released = false;
            voice.envelope.reset();
            voice.positionEnvelope.reset();

            for (auto& grain : voice.grains)
                grain.active = false;
        }
        else if (! voice.released)
        {
            voice.released = true;
            voice.envelope.noteOff();
            voice.positionEnvelope.noteOff();
        }
    }
}

bool GranularSynth::isActive() const noexcept
{
    for (const auto& voice : voices)
        if (voice.active)
            return true;

    return false;
}

int GranularSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.active)
            ++count;

    return count;
}

int GranularSynth::activeGrainCount() const noexcept
{
    int count = 0;

    for (const auto& voice : voices)
        for (const auto& grain : voice.grains)
            if (grain.active)
                ++count;

    return count;
}

float GranularSynth::getScanFractionForTest (juce::int64 noteId) const noexcept
{
    for (const auto& voice : voices)
        if (voice.active && voice.noteId == noteId)
            return (float) voice.scanFraction;

    return -1.0f;
}

float GranularSynth::getLongestGrainSecondsForTest() const noexcept
{
    auto longest = 0.0;

    for (const auto& voice : voices)
        for (const auto& grain : voice.grains)
            if (grain.active)
                longest = juce::jmax (longest, grain.length / sampleRate);

    return (float) longest;
}

void GranularSynth::spawnGrain (Voice& voice)
{
    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    // A free slot, or this voice's oldest grain (the documented 32-grain ceiling).
    Grain* slot = nullptr;

    for (auto& grain : voice.grains)
        if (! grain.active)
        {
            slot = &grain;
            break;
        }

    if (slot == nullptr)
    {
        slot = &voice.grains[0];

        for (auto& grain : voice.grains)
            if (grain.startOrder < slot->startOrder)
                slot = &grain;
    }

    const auto sourceLength = (double) sample->audio.getNumSamples();
    const auto lengthSeconds = juce::jlimit (0.005, 0.5, (double) params.grainMs * 0.001);

    slot->length  = juce::jmax (2.0, lengthSeconds * sampleRate);
    slot->elapsed = 0.0;
    slot->active  = true;
    slot->startOrder = ++orderCounter;

    // This grain's own pitch: the note's rate, plus its own random detune.
    const auto cents = (randomUnit (voice) * 2.0 - 1.0) * (double) params.pitchJitterCents;
    const auto rate = voice.noteRate * std::pow (2.0, cents / 1200.0);
    const auto reverse = randomUnit (voice) < (double) params.reverseProb;
    slot->rate = reverse ? -rate : rate;

    // Where it reads from: the scan position, scattered by the spray, folded into
    // the file (a reverse grain starts at the right hand side of its spot).
    const auto spray = (randomUnit (voice) * 2.0 - 1.0) * (double) params.spray;
    const auto fraction = juce::jlimit (0.0, 1.0, voice.scanFraction + spray);
    const auto start = fraction * sourceLength;
    slot->position = reverse ? juce::jlimit (0.0, juce::jmax (0.0, sourceLength - 2.0), start)
                             : start;

    const auto pan = (randomUnit (voice) * 2.0 - 1.0) * (double) params.stereoSpread;
    slot->panLeft  = (float) juce::jlimit (0.0, 1.0, 1.0 - juce::jmax (0.0, pan));
    slot->panRight = (float) juce::jlimit (0.0, 1.0, 1.0 + juce::jmin (0.0, pan));

    ++startedGrains;
}

void GranularSynth::publishGrains() noexcept
{
    int written = 0;
    int soundingVoices = 0;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        ++soundingVoices;

        for (const auto& grain : voice.grains)
        {
            if (! grain.active || written >= kPublishedGrains)
                continue;

            const auto length = juce::jmax (1.0, grain.length);
            const auto position = sample != nullptr && sample->audio.getNumSamples() > 0
                                      ? (float) juce::jlimit (0.0, 1.0, grain.position
                                                                               / (double) sample->audio.getNumSamples())
                                      : 0.0f;

            published.positions[written].store (position, std::memory_order_relaxed);
            published.ages[written].store ((float) juce::jlimit (0.0, 1.0, grain.elapsed / length),
                                           std::memory_order_relaxed);
            ++written;
        }
    }

    published.grains.store (written, std::memory_order_relaxed);
    published.voices.store (soundingVoices, std::memory_order_relaxed);
}

bool GranularSynth::renderVoiceAdding (juce::int64 noteId,
                                       juce::AudioBuffer<float>& buffer,
                                       int startSample,
                                       int numSamples,
                                       float gainLeft,
                                       float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return false;

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
    {
        // Nothing to read out of: no voice may keep claiming to sound.
        allNotesOff (true);
        publishGrains();
        return false;
    }

    const auto* source = sample->audio.getReadPointer (0);
    const auto sourceLength = (juce::int64) sample->audio.getNumSamples();
    const auto dt = 1.0 / sampleRate;

    // Density × grain length is how many grains overlap; compensating for it keeps
    // turning the density up from turning into an explosion.
    const auto grainSeconds = juce::jlimit (0.005, 0.5, (double) params.grainMs * 0.001);
    const auto overlap = juce::jlimit (1.0, 32.0, (double) params.densityHz * grainSeconds);
    const auto voiceTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain)
                               * (float) (1.0 / std::sqrt (overlap));

    const auto type = filterTypeFor (params.filterMode);
    const auto slope = params.filterSlopeDb >= 24 ? 24 : 12;
    const auto resonance = juce::jlimit (0.1f, 20.0f, params.filterResonance);
    const auto stageResonance = slope == 24 ? juce::jmax (0.5f, resonance * 0.7071f) : resonance;

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (auto& voice : voices)
    {
        if (! voice.active || voice.noteId != noteId)
            continue;

        const auto keyed = (float) std::pow (2.0, (double) params.filterKeyTrack
                                                      * std::log2 (juce::jmax (1.0e-6, voice.keyTrackRatio)));
        const auto cutoff = juce::jlimit (20.0f, (float) (sampleRate * 0.49),
                                          params.filterCutoffHz * keyed);

        if (std::abs (cutoff - voice.appliedCutoff) > 0.5f
            || std::abs (stageResonance - voice.appliedResonance) > 1.0e-4f)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                voice.filterStage1[(size_t) channel].setType (type);
                voice.filterStage2[(size_t) channel].setType (type);
                voice.filterStage1[(size_t) channel].setCutoffFrequency (cutoff);
                voice.filterStage2[(size_t) channel].setCutoffFrequency (cutoff);
                voice.filterStage1[(size_t) channel].setResonance (stageResonance);
                voice.filterStage2[(size_t) channel].setResonance (stageResonance);
            }

            voice.appliedCutoff = cutoff;
            voice.appliedResonance = stageResonance;
        }

        const auto voiceGain = voiceTrim * voice.amplitude;
        const auto lg = voiceGain * gainLeft;
        const auto rg = voiceGain * gainRight;
        const auto gap = 1.0 / juce::jmax (1.0f, params.densityHz);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto envelopeValue = voice.envelope.getNextSample();

            if (! voice.envelope.isActive())
            {
                voice.active = false;
                voice.released = false;

                for (auto& grain : voice.grains)
                    grain.active = false;

                break;
            }

            const auto positionValue = voice.positionEnvelope.getNextSample();

            // Where the cloud is reading at this instant: the position, the scan and
            // the position envelope, folded back inside the file.
            const auto base = (double) params.position
                                  + (double) params.scanSpeed * voice.elapsedSeconds
                                  + (double) positionValue * (double) params.posEnvAmount;
            voice.scanFraction = fold01 (base);

            voice.nextGrainIn -= dt;

            if (voice.nextGrainIn <= 0.0)
            {
                spawnGrain (voice);

                const auto jitter = 1.0 + (randomUnit (voice) * 2.0 - 1.0)
                                              * (double) params.spacingJitter;
                voice.nextGrainIn += gap * juce::jmax (0.05, jitter);
            }

            auto sampleLeft = 0.0f;
            auto sampleRight = 0.0f;

            for (auto& grain : voice.grains)
            {
                if (! grain.active)
                    continue;

                const auto t = grain.elapsed / grain.length;
                const auto window = 0.5 - 0.5 * std::cos (2.0 * kPi * t);

                // Linear interpolation, exactly like the sampler's playback.
                const auto clamped = juce::jlimit (0.0, (double) sourceLength - 1.0, grain.position);
                const auto index = (juce::int64) clamped;
                const auto nextIndex = juce::jmin (sourceLength - 1, index + 1);
                const auto fraction = (float) (clamped - (double) index);
                const auto value = source[index]
                                       + (source[nextIndex] - source[index]) * fraction;
                const auto shaped = (float) (value * window);

                sampleLeft  += shaped * grain.panLeft;
                sampleRight += shaped * grain.panRight;

                grain.position += grain.rate;
                grain.elapsed  += 1.0;

                if (grain.elapsed >= grain.length || grain.position < 0.0
                    || grain.position > (double) sourceLength)
                    grain.active = false;
            }

            const auto outLeft  = voice.filterStage2[0].processSample (
                0, voice.filterStage1[0].processSample (0, sampleLeft * envelopeValue));
            const auto outRight = voice.filterStage2[1].processSample (
                0, voice.filterStage1[1].processSample (0, sampleRight * envelopeValue));

            left[i]  += outLeft * lg;
            right[i] += outRight * rg;

            voice.elapsedSeconds += dt;
        }
    }

    publishGrains();
    return findVoiceForNote (noteId) != nullptr;
}

void GranularSynth::renderAdding (juce::AudioBuffer<float>& buffer,
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

} // namespace nidaw
