#include "SubtractiveSynth.h"

namespace hmdaw
{

namespace
{
    // Two oscillators plus noise are summed, so the trim keeps a fistful of
    // voices from clipping.
    constexpr float kMasterTrim = 0.22f;

    /** How often the pitch and cutoff modulation is refreshed, in samples. */
    constexpr int kControlRate = 16;

    /** How far the filter envelope can push the cutoff, in octaves. */
    constexpr double kFilterEnvOctaves = 8.0;

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
}

SubtractiveSynth::SubtractiveSynth()
{
    prepare (48000.0);
}

void SubtractiveSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& v : voices)
    {
        v.active   = false;
        v.released = false;

        v.filter.prepare ({ sampleRate, 512, 2 });
        v.filter2.prepare ({ sampleRate, 512, 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        v.filter2.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

        v.amp.setSampleRate (sampleRate);
        v.filterEnv.setSampleRate (sampleRate);

        applyEnvelopes (v);
    }
}

void SubtractiveSynth::reset()
{
    for (auto& v : voices)
    {
        v.active   = false;
        v.released = false;
        v.amp.reset();
        v.filterEnv.reset();
        v.filter.reset();
        v.filter2.reset();
    }
}

void SubtractiveSynth::setParams (const SubtractiveParams& newParams)
{
    params = newParams;
}

void SubtractiveSynth::applyEnvelopes (Voice& voice)
{
    juce::ADSR::Parameters amp;
    amp.attack  = juce::jmax (0.0f, params.ampAttack);
    amp.decay   = juce::jmax (0.0f, params.ampDecay);
    amp.sustain = juce::jlimit (0.0f, 1.0f, params.ampSustain);
    amp.release = juce::jmax (0.0f, params.ampRelease);
    voice.amp.setParameters (amp);

    juce::ADSR::Parameters filter;
    filter.attack  = juce::jmax (0.0f, params.filterAttack);
    filter.decay   = juce::jmax (0.0f, params.filterDecay);
    filter.sustain = juce::jlimit (0.0f, 1.0f, params.filterSustain);
    filter.release = juce::jmax (0.0f, params.filterRelease);
    voice.filterEnv.setParameters (filter);
}

SubtractiveSynth::Voice* SubtractiveSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId)
            return &v;

    return nullptr;
}

SubtractiveSynth::Voice& SubtractiveSynth::findVoiceToUse()
{
    for (auto& v : voices)
        if (! v.active)
            return v;

    // Then a voice that is already releasing, then the oldest one.
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

void SubtractiveSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (frequencyHz <= 0.0f)
        return;

    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();

    voice.active    = true;
    voice.released  = false;
    voice.noteId    = noteId;
    voice.frequency = (double) frequencyHz;
    voice.amplitude = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.startOrder = ++orderCounter;

    applyEnvelopes (voice);

    voice.amp.noteOn();
    voice.filterEnv.noteOn();

    voice.oscPhase[0] = 0.0;
    voice.oscPhase[1] = 0.0;
    voice.lfoPhase    = 0.0;
    voice.lfoHoldValid = false;
    voice.controlCountdown = 0;

    voice.filter.reset();
    voice.filter2.reset();
    voice.appliedCutoff    = 0.0f;
    voice.appliedResonance = 0.0f;
    voice.appliedMode      = -1;
    voice.appliedSlope     = 0;
}

void SubtractiveSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f))
        return;

    if (auto* voice = findVoiceForNote (noteId))
        voice->frequency = juce::jlimit (kMinVisibleHz, kMaxVisibleHz,
                                         (double) frequencyHz);
}

void SubtractiveSynth::noteOff (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.noteId == noteId)
        {
            v.released = true;
            v.amp.noteOff();
            v.filterEnv.noteOff();
        }
}

void SubtractiveSynth::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        if (immediately)
            v.active = false;
        else if (! v.released)
        {
            v.released = true;
            v.amp.noteOff();
            v.filterEnv.noteOff();
        }
    }
}

bool SubtractiveSynth::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active)
            return true;

    return false;
}

int SubtractiveSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.active)
            ++count;

    return count;
}

double SubtractiveSynth::getVoiceFrequencyForTest (juce::int64 noteId) const
{
    for (const auto& v : voices)
        if (v.active && v.noteId == noteId)
            return v.frequency;

    return 0.0;
}

bool SubtractiveSynth::renderVoiceAdding (juce::int64 noteId,
                                          juce::AudioBuffer<float>& buffer,
                                          int startSample,
                                          int numSamples,
                                          float gainLeft,
                                          float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return false;

    const auto voiceTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain);
    const auto noiseLevel = juce::jlimit (0.0f, 1.0f, params.noiseLevel);
    const auto lfoIncrement = (double) juce::jmax (0.0f, params.lfoRateHz) / sampleRate;
    const auto pitchDepthCents = (double) juce::jlimit (0.0f, 100.0f, params.lfoPitchCents);
    const auto cutoffDepth = (double) juce::jlimit (0.0f, 1.0f, params.lfoCutoffDepth);
    const auto ampDepth = (double) juce::jlimit (0.0f, 1.0f, params.lfoAmpDepth);
    const auto filterEnvAmount = (double) juce::jlimit (-1.0f, 1.0f, params.filterEnvAmount);
    const auto keyTrack = (double) juce::jlimit (0.0f, 1.0f, params.filterKeyTrack);
    const auto baseCutoff = (double) juce::jlimit (20.0f, (float) (sampleRate * 0.49),
                                                   params.filterCutoffHz);
    const auto resonance = juce::jlimit (0.1f, 20.0f, params.filterResonance);
    const auto slope = params.filterSlopeDb >= 24 ? 24 : 12;
    const auto type = filterTypeFor (params.filterMode);

    // A two stage cascade needs a gentler Q per stage to stay stable.
    const auto stageResonance = slope == 24 ? juce::jmax (0.5f, resonance * 0.7071f)
                                            : resonance;

    const std::array<double, 2> oscRatios { subtractive::oscillatorRatio (params.oscs[0]),
                                            subtractive::oscillatorRatio (params.oscs[1]) };
    const std::array<float, 2> oscLevels { juce::jlimit (0.0f, 1.0f, params.oscs[0].level),
                                           juce::jlimit (0.0f, 1.0f, params.oscs[1].level) };

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (auto& v : voices)
    {
        if (! v.active || v.noteId != noteId)
            continue;

        applyEnvelopes (v);

        const auto voiceGain = voiceTrim * v.amplitude;
        const auto lg = voiceGain * gainLeft;
        const auto rg = voiceGain * gainRight;
        const auto midiNote = 69.0 + 12.0 * std::log2 (juce::jmax (1.0, v.frequency) / 440.0);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto ampValue = (double) v.amp.getNextSample();

            if (! v.amp.isActive())
            {
                v.active = false;
                v.released = false;
                break;
            }

            const auto filterValue = (double) v.filterEnv.getNextSample();

            // The LFO runs at audio rate here (it is one multiply) and the pitch
            // and cutoff are refreshed at the control rate just below.
            v.lfoPhase = subtractive::foldPhase (v.lfoPhase + lfoIncrement);
            const auto lfo = subtractive::lfoSample (params.lfoWaveform, v.lfoPhase,
                                                     v.lfoHold, v.lfoHoldValid, random);

            if (v.controlCountdown <= 0)
            {
                v.controlCountdown = kControlRate;

                const auto pitchCents = pitchDepthCents * lfo;
                v.pitchFactor = std::pow (2.0, pitchCents / 1200.0);

                const auto envOctaves = filterEnvAmount * filterValue * kFilterEnvOctaves;
                const auto keyOctaves = keyTrack * (midiNote - 60.0) / 12.0;
                const auto lfoOctaves = cutoffDepth * lfo * 2.0;
                v.cutoffHz = juce::jlimit (20.0, (double) sampleRate * 0.49,
                                           baseCutoff * std::pow (2.0, envOctaves + keyOctaves)
                                               * std::pow (2.0, lfoOctaves));

                const auto cutoff = (float) v.cutoffHz;

                if (std::abs (cutoff - v.appliedCutoff) > 0.5f
                    || std::abs (stageResonance - v.appliedResonance) > 1.0e-4f
                    || v.appliedMode != (int) params.filterMode
                    || v.appliedSlope != slope)
                {
                    if (v.appliedMode != (int) params.filterMode)
                    {
                        v.filter.setType (type);
                        v.filter2.setType (type);
                    }

                    v.filter.setCutoffFrequency (cutoff);
                    v.filter.setResonance (stageResonance);
                    v.filter2.setCutoffFrequency (cutoff);
                    v.filter2.setResonance (stageResonance);

                    v.appliedCutoff    = cutoff;
                    v.appliedResonance = stageResonance;
                    v.appliedMode      = (int) params.filterMode;
                    v.appliedSlope     = slope;
                }
            }

            --v.controlCountdown;

            auto mixed = 0.0;

            for (int o = 0; o < 2; ++o)
            {
                const auto& osc = params.oscs[(size_t) o];

                if (! osc.enabled || oscLevels[(size_t) o] <= 0.0f)
                    continue;

                const auto delta = v.frequency * oscRatios[(size_t) o] * v.pitchFactor / sampleRate;
                v.oscPhase[o] += delta;

                mixed += subtractive::oscillatorSample (osc.wave, v.oscPhase[o], delta,
                                                        osc.pulseWidth)
                             * (double) oscLevels[(size_t) o];
            }

            if (noiseLevel > 0.0f)
                mixed += (random.nextDouble() * 2.0 - 1.0) * (double) noiseLevel;

            auto filtered = v.filter.processSample (0, (float) mixed);

            if (slope == 24)
                filtered = v.filter2.processSample (0, filtered);

            const auto tremolo = 1.0 - ampDepth * (0.5 - 0.5 * lfo);
            const auto output = (double) filtered * ampValue * tremolo;

            left[i]  += (float) (output * lg);
            right[i] += (float) (output * rg);
        }
    }

    return findVoiceForNote (noteId) != nullptr;
}

void SubtractiveSynth::renderAdding (juce::AudioBuffer<float>& buffer,
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
