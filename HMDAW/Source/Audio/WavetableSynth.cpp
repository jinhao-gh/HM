#include "WavetableSynth.h"

namespace hmdaw
{
namespace
{
    // Two oscillators plus noise are summed, so the trim keeps a fistful of
    // voices from clipping. Same figure as the subtractive instrument.
    constexpr float kMasterTrim = 0.22f;
    constexpr int kControlRate = 16;

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

double WavetableSynth::sampleTable (const wavetable::Wavetable& table, int level,
                                    double phase, double framePosition,
                                    bool discrete) noexcept
{
    // Callers may hand in more than one cycle (the editors used to draw two).
    // Fold it here so a second cycle reads the same frame instead of running
    // past the 2048-point frame into the next frame.
    phase -= std::floor (phase);
    const auto frameCount = juce::jmax (1, table.frameCount);
    const auto clamped = juce::jlimit (0.0, 1.0, framePosition);
    const auto scaled  = discrete && frameCount > 1
                             ? (double) juce::jlimit (0, frameCount - 1,
                                                      (int) std::round (clamped * (frameCount - 1)))
                             : clamped * (double) (frameCount - 1);
    const auto lower   = juce::jlimit (0, frameCount - 1, (int) std::floor (scaled));
    const auto upper   = juce::jmin (frameCount - 1, lower + 1);
    const auto blend   = (float) (scaled - (double) lower);
    const auto* first  = table.frameData (level, lower);
    const auto* second = upper == lower ? first : table.frameData (level, upper);
    const auto position = wavetable::kFrameSize * phase;
    const auto index    = (int) position;
    const auto next     = (index + 1) % wavetable::kFrameSize;
    const auto fraction = (float) (position - (double) index);

    const auto readFrame = [&] (const float* data)
    {
        return data[index] + (data[next] - data[index]) * fraction;
    };

    return (double) (readFrame (first) * (1.0f - blend) + readFrame (second) * blend);
}

WavetableSynth::WavetableSynth()
{
    prepare (48000.0);
}

void WavetableSynth::prepare (double newSampleRate)
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
        v.positionEnv.setSampleRate (sampleRate);
        applyEnvelopes (v);
    }
}

void WavetableSynth::reset()
{
    published.voices.store (0, std::memory_order_relaxed);

    for (auto& v : voices)
    {
        v.active   = false;
        v.released = false;
        v.amp.reset();
        v.positionEnv.reset();
        v.filter.reset();
        v.filter2.reset();
    }
}

void WavetableSynth::setParams (const WavetableParams& newParams)
{
    params = newParams;
}

void WavetableSynth::setTables (std::shared_ptr<const wavetable::Wavetable> table0,
                                std::shared_ptr<const wavetable::Wavetable> table1)
{
    customTables[0] = std::move (table0);
    customTables[1] = std::move (table1);
}

void WavetableSynth::applyEnvelopes (Voice& voice)
{
    juce::ADSR::Parameters amp;
    amp.attack  = juce::jmax (0.0f, params.ampAttack);
    amp.decay   = juce::jmax (0.0f, params.ampDecay);
    amp.sustain = juce::jlimit (0.0f, 1.0f, params.ampSustain);
    amp.release = juce::jmax (0.0f, params.ampRelease);
    voice.amp.setParameters (amp);

    juce::ADSR::Parameters position;
    position.attack  = juce::jmax (0.0f, params.posAttack);
    position.decay   = juce::jmax (0.0f, params.posDecay);
    position.sustain = juce::jlimit (0.0f, 1.0f, params.posSustain);
    position.release = juce::jmax (0.0f, params.posRelease);
    voice.positionEnv.setParameters (position);
}

WavetableSynth::Voice* WavetableSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId)
            return &v;

    return nullptr;
}

WavetableSynth::Voice& WavetableSynth::findVoiceToUse()
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

void WavetableSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
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
    voice.positionEnv.noteOn();

    for (int o = 0; o < 2; ++o)
        for (int u = 0; u < kMaxWavetableUnisonVoices; ++u)
            voice.phase[(size_t) o][(size_t) u] =
                (double) u / (double) kMaxWavetableUnisonVoices;

    voice.positionOffset = 0.0f;
    voice.unisonOffset = 0.0f;
    voice.detuneOffset = 0.0f;
    voice.lfoPhase = 0.0;
    voice.lfoHoldValid = false;
    voice.controlCountdown = 0;

    voice.filter.reset();
    voice.filter2.reset();
    voice.appliedCutoff    = 0.0f;
    voice.appliedResonance = 0.0f;
    voice.appliedMode      = -1;
    voice.appliedSlope     = 0;

    // The editor's cursor follows the sound, and the sound may be rendered before
    // the engine ever asks "is this track active" - so the count is kept here as
    // well as in the render loop.
    published.voices.store (activeVoiceCount(), std::memory_order_relaxed);
}

void WavetableSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f))
        return;

    if (auto* voice = findVoiceForNote (noteId))
        voice->frequency = juce::jlimit (kMinVisibleHz, kMaxVisibleHz,
                                         (double) frequencyHz);
}

void WavetableSynth::noteOff (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.noteId == noteId)
        {
            v.released = true;
            v.amp.noteOff();
            v.positionEnv.noteOff();
        }

    published.voices.store (activeVoiceCount(), std::memory_order_relaxed);
}

void WavetableSynth::allNotesOff (bool immediately)
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
            v.positionEnv.noteOff();
        }
    }

    published.voices.store (activeVoiceCount(), std::memory_order_relaxed);
}

void WavetableSynth::setVoiceOffsets (juce::int64 noteId,
                                      float positionOffset,
                                      float unisonOffset,
                                      float detuneOffset)
{
    if (auto* voice = findVoiceForNote (noteId))
    {
        voice->positionOffset = juce::jlimit (-1.0f, 1.0f, positionOffset);
        voice->unisonOffset = juce::jlimit (-1.0f, 1.0f, unisonOffset);
        voice->detuneOffset = juce::jlimit (-1.0f, 1.0f, detuneOffset);
    }
}

bool WavetableSynth::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active)
            return true;

    return false;
}

int WavetableSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.active)
            ++count;

    return count;
}

double WavetableSynth::getVoiceFrequencyForTest (juce::int64 noteId) const
{
    for (const auto& v : voices)
        if (v.active && v.noteId == noteId)
            return v.frequency;

    return 0.0;
}

bool WavetableSynth::renderVoiceAdding (juce::int64 noteId,
                                        juce::AudioBuffer<float>& buffer,
                                        int startSample,
                                        int numSamples,
                                        float gainLeft,
                                        float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return false;

    // So the editor's cursor knows whether anything is sounding, even for a block in
    // which a voice ends (the next block may never arrive once the track is idle).
    published.voices.store (activeVoiceCount(), std::memory_order_relaxed);

    const auto voiceTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain);
    const auto noiseLevel = juce::jlimit (0.0f, 1.0f, params.noiseLevel);
    const auto lfoIncrement = (double) juce::jmax (0.0f, params.lfoRateHz) / sampleRate;
    const auto lfoPositionDepth = (double) juce::jlimit (0.0f, 1.0f, params.lfoPositionDepth);
    const auto lfoPitchCents = (double) juce::jlimit (0.0f, 100.0f, params.lfoPitchCents);
    const auto lfoCutoffDepth = (double) juce::jlimit (0.0f, 1.0f, params.lfoCutoffDepth);
    const auto lfoAmpDepth = (double) juce::jlimit (0.0f, 1.0f, params.lfoAmpDepth);
    const auto posEnvAmount = (double) juce::jlimit (-1.0f, 1.0f, params.posEnvAmount);
    const auto keyTrack = (double) juce::jlimit (0.0f, 1.0f, params.filterKeyTrack);
    const auto baseCutoff = (double) juce::jlimit (20.0f, (float) (sampleRate * 0.49),
                                                   params.filterCutoffHz);
    const auto resonance = juce::jlimit (0.1f, 20.0f, params.filterResonance);
    const auto slope = params.filterSlopeDb >= 24 ? 24 : 12;
    const auto type = filterTypeFor (params.filterMode);
    const auto stageResonance = slope == 24 ? juce::jmax (0.5f, resonance * 0.7071f)
                                            : resonance;

    // A custom table wins over the built-in index: that is the whole point of the
    // project owning tables of its own.
    const auto& tableA = customTables[0] != nullptr ? *customTables[0]
                                                    : wavetable::getTable (params.oscs[0].tableIndex);
    const auto& tableB = customTables[1] != nullptr ? *customTables[1]
                                                    : wavetable::getTable (params.oscs[1].tableIndex);
    const std::array<const wavetable::Wavetable*, 2> tables { &tableA, &tableB };
    const std::array<double, 2> oscRatios { wavetable::oscillatorRatio (params.oscs[0]),
                                            wavetable::oscillatorRatio (params.oscs[1]) };
    const std::array<float, 2> oscLevels { juce::jlimit (0.0f, 1.0f, params.oscs[0].level),
                                           juce::jlimit (0.0f, 1.0f, params.oscs[1].level) };
    const std::array<float, 2> oscPositions { juce::jlimit (0.0f, 1.0f, params.oscs[0].position),
                                              juce::jlimit (0.0f, 1.0f, params.oscs[1].position) };

    // One bandwidth per oscillator for the whole block: the pitch barely moves
    // inside a buffer, and picking per control tick would only add work.
    std::array<int, 2> levels { 0, 0 };

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

        for (int o = 0; o < 2; ++o)
            levels[(size_t) o] = wavetable::levelForFrequency (v.frequency
                                                                   * oscRatios[(size_t) o],
                                                               sampleRate);

        std::array<std::array<double, kMaxWavetableUnisonVoices>, 2> unisonRatios {};
        std::array<int, 2> unisonCounts { 1, 1 };
        std::array<double, 2> unisonGains { 1.0, 1.0 };

        for (int o = 0; o < 2; ++o)
        {
            const auto& osc = params.oscs[(size_t) o];
            const auto count = juce::jlimit (
                1, kMaxWavetableUnisonVoices,
                osc.unisonVoices + (int) std::round (v.unisonOffset
                                                     * (float) kMaxWavetableUnisonVoices));
            const auto detune = juce::jlimit (
                0.0f, 100.0f, osc.unisonDetuneCents + v.detuneOffset * 100.0f);
            unisonCounts[(size_t) o] = count;
            unisonGains[(size_t) o] = 1.0 / std::sqrt ((double) count);

            for (int u = 0; u < count; ++u)
            {
                const auto offset = count > 1
                                        ? ((double) u / (double) (count - 1) * 2.0 - 1.0)
                                              * (double) detune
                                        : 0.0;
                unisonRatios[(size_t) o][(size_t) u] = std::pow (2.0, offset / 1200.0);
            }
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto ampValue = (double) v.amp.getNextSample();

            if (! v.amp.isActive())
            {
                v.active = false;
                v.released = false;
                break;
            }

            const auto positionValue = (double) v.positionEnv.getNextSample();

            v.lfoPhase = subtractive::foldPhase (v.lfoPhase + lfoIncrement);
            const auto lfo = subtractive::lfoSample (params.lfoWaveform, v.lfoPhase,
                                                     v.lfoHold, v.lfoHoldValid, random);

            if (v.controlCountdown <= 0)
            {
                v.controlCountdown = kControlRate;

                v.pitchFactor = std::pow (2.0, lfoPitchCents * lfo / 1200.0);

                const auto envOctaves = 0.0;
                const auto keyOctaves = keyTrack * (midiNote - 60.0) / 12.0;
                const auto lfoOctaves = lfoCutoffDepth * lfo * 2.0;
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

                for (int o = 0; o < 2; ++o)
                {
                    v.position[(size_t) o] = juce::jlimit (
                        0.0, 1.0,
                        (double) oscPositions[(size_t) o]
                            + posEnvAmount * positionValue
                            + lfoPositionDepth * lfo
                            + (double) v.positionOffset);

                    // What the waterfall view draws its cursor from: the very number
                    // this voice is about to read the table with.
                    published.position[o].store ((float) v.position[(size_t) o],
                                                 std::memory_order_relaxed);
                }
            }

            --v.controlCountdown;

            auto mixed = 0.0;

            for (int o = 0; o < 2; ++o)
            {
                const auto& osc = params.oscs[(size_t) o];

                if (! osc.enabled || oscLevels[(size_t) o] <= 0.0f)
                    continue;

                const auto count = unisonCounts[(size_t) o];
                auto unisonSample = 0.0;

                for (int u = 0; u < count; ++u)
                {
                    const auto delta = v.frequency * oscRatios[(size_t) o]
                                           * unisonRatios[(size_t) o][(size_t) u]
                                           * v.pitchFactor / sampleRate;
                    auto& phase = v.phase[(size_t) o][(size_t) u];
                    phase = subtractive::foldPhase (phase + delta);
                    unisonSample += sampleTable (
                        *tables[(size_t) o], levels[(size_t) o], phase,
                        v.position[(size_t) o], osc.positionDiscrete);
                }

                mixed += unisonSample * unisonGains[(size_t) o]
                             * (double) oscLevels[(size_t) o];
            }

            if (noiseLevel > 0.0f)
                mixed += (random.nextDouble() * 2.0 - 1.0) * (double) noiseLevel;

            auto filtered = v.filter.processSample (0, (float) mixed);

            if (slope == 24)
                filtered = v.filter2.processSample (0, filtered);

            const auto tremolo = 1.0 - lfoAmpDepth * (0.5 - 0.5 * lfo);
            const auto output = (double) filtered * ampValue * tremolo;

            left[i]  += (float) (output * lg);
            right[i] += (float) (output * rg);
        }
    }

    // ...and once more after the block: a voice that reached the end of its release
    // inside this block has just gone, and the callers that render "while active"
    // would otherwise never come back to notice it.
    published.voices.store (activeVoiceCount(), std::memory_order_relaxed);

    return findVoiceForNote (noteId) != nullptr;
}

void WavetableSynth::renderAdding (juce::AudioBuffer<float>& buffer,
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
