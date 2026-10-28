#include "HybridSynth.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr int kControlRate = 16;
    constexpr double kFilterEnvOctaves = 6.0;

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

    double fold01 (double value) noexcept
    {
        return value - std::floor (value);
    }

    double envelopeSeconds (float milliseconds, HybridTimeMode mode,
                            HybridTimeDivision division, double bpm) noexcept
    {
        if (mode == HybridTimeMode::beats)
            return hybridDivisionBeats (division) * 60.0 / juce::jmax (1.0, bpm);

        return juce::jmax (0.0, (double) milliseconds);
    }

    double lfoRateHzFor (const HybridLfoParams& lfo, double bpm) noexcept
    {
        if (lfo.timeMode == HybridTimeMode::beats)
            return juce::jmax (0.001, (juce::jmax (1.0, bpm) / 60.0)
                                         / hybridDivisionBeats (lfo.syncDivision));

        return juce::jmax (0.001, 1000.0 / juce::jmax (0.1f, lfo.rateMs));
    }
}

HybridSynth::HybridSynth()
    : fxChain (std::make_unique<HybridFxChain>()),
      voices (std::make_unique<std::array<HybridVoice, kVoicesPerTrack>>())
{
}

void HybridSynth::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const auto block = juce::jlimit (32, 16384, maxBlockSize);
    scratch.setSize (2, block, false, true, true);
    voiceBuffer.setSize (2, block, false, true, true);
    parallelBuffer.setSize (2, block, false, true, true);

    juce::dsp::ProcessSpec filterSpec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.maximumBlockSize = (juce::uint32) block;
    filterSpec.numChannels = 1;

    for (auto& voice : *voices)
    {
        for (auto& pair : voice.filters)
            for (auto& filter : pair)
                filter.prepare (filterSpec);

        for (auto& pair : voice.filtersStage2)
            for (auto& filter : pair)
                filter.prepare (filterSpec);

        for (auto& pair : voice.combFilters)
            for (auto& line : pair)
            {
                line.setMaximumDelayInSamples ((int) std::ceil (sampleRate * 0.05) + 2);
                line.prepare (filterSpec);
                line.reset();
            }

        applyEnvelopeParams (voice);
    }

    for (auto& l : layers)
    {
        l.wave.prepare (sampleRate);
        l.subtractive.prepare (sampleRate);
        l.sample.prepare (sampleRate);
        l.granular.prepare (sampleRate);
    }

    subSynth.prepare (sampleRate);
    noiseSynth.prepare (sampleRate);
    fxChain->prepare (sampleRate, block);
}

void HybridSynth::setTempo (double newBpm)
{
    newBpm = juce::jlimit (1.0, 999.0, newBpm);

    if (std::abs (newBpm - currentBpm) < 1.0e-6)
        return;

    currentBpm = newBpm;

    for (auto& voice : *voices)
        applyEnvelopeParams (voice);
}

void HybridSynth::reset()
{
    for (auto& l : layers)
    {
        l.wave.reset();
        l.subtractive.reset();
        l.sample.reset();
        l.granular.reset();
    }

    subSynth.reset();
    noiseSynth.reset();
    fxChain->reset();
    latestFxMixMod.fill (0.0f);
    latestFxParam1Mod.fill (0.0f);
    latestFxParam2Mod.fill (0.0f);
    for (int i = 0; i < 4; ++i)
    {
        published.envelopePosition[i].store (0.0f, std::memory_order_relaxed);
        published.envelopeValue[i].store (0.0f, std::memory_order_relaxed);
    }
    for (int i = 0; i < 6; ++i)
    {
        published.lfoPhase[i].store (0.0f, std::memory_order_relaxed);
        published.lfoValue[i].store (0.0f, std::memory_order_relaxed);
    }
    orderCounter = 0;

    for (auto& voice : *voices)
    {
        voice.active = false;
        voice.released = false;
        voice.noteId = 0;
        voice.controlCountdown = 0;
        voice.gainMod = 0.0f;
        voice.panMod = 0.0f;
        voice.layerLevelMod.fill (0.0f);
        voice.layerPanMod.fill (0.0f);
        voice.subLevelMod = voice.subPanMod = 0.0f;
        voice.noiseLevelMod = voice.noisePanMod = 0.0f;
        voice.fxMixMod.fill (0.0f);
        voice.fxParam1Mod.fill (0.0f);
        voice.fxParam2Mod.fill (0.0f);
        voice.layerPositionMod.fill (0.0f);
        voice.layerUnisonMod.fill (0.0f);
        voice.layerDetuneMod.fill (0.0f);
        voice.envelopeElapsed.fill (0.0);
        voice.ageSeconds = 0.0;
        voice.appliedCutoff.fill (0.0f);
        voice.appliedResonance.fill (0.0f);
        voice.appliedMode.fill (-1);
        voice.appliedSlope.fill (0);

        for (auto& env : voice.envelopes)
            env.reset();

        for (auto& pair : voice.filters)
            for (auto& filter : pair)
                filter.reset();

        for (auto& pair : voice.filtersStage2)
            for (auto& filter : pair)
                filter.reset();

        voice.notchCoeff.fill (0.0f);

        for (auto& pair : voice.notchX1)
            pair.fill (0.0f);

        for (auto& pair : voice.notchX2)
            pair.fill (0.0f);

        for (auto& pair : voice.combFilters)
            for (auto& line : pair)
                line.reset();
    }
}

void HybridSynth::applyEnvelopeParams (HybridVoice& voice)
{
    for (int i = 0; i < 4; ++i)
    {
        const auto& e = params.envelopes[(size_t) i];
        juce::ADSR::Parameters ap;
        ap.attack = (float) envelopeSeconds (e.attack, e.timeMode,
                                             e.attackDivision, currentBpm);
        ap.decay = (float) envelopeSeconds (e.decay, e.timeMode,
                                            e.decayDivision, currentBpm);
        ap.sustain = juce::jlimit (0.0f, 1.0f, e.sustain);
        ap.release = (float) envelopeSeconds (e.release, e.timeMode,
                                              e.releaseDivision, currentBpm);
        voice.envelopes[(size_t) i].setParameters (ap);
    }
}

void HybridSynth::setParams (const HybridParams& newParams)
{
    params = newParams;

    for (int i = 0; i < 3; ++i)
    {
        auto& l = layers[(size_t) i];
        const auto& p = params.layers[(size_t) i];
        auto singleTable = p.wavetable;
        singleTable.oscs[1].enabled = false;
        l.wave.setParams (singleTable);
        l.subtractive.setParams (p.subtractive);
        l.sample.setParams (p.sample);
        l.granular.setParams (p.granular);
    }

    for (auto& voice : *voices)
        applyEnvelopeParams (voice);

    SubtractiveParams sp;
    sp.oscs[0].wave = params.sub.waveform == 0 ? OscWave::sine
                     : params.sub.waveform == 1 ? OscWave::triangle : OscWave::square;
    sp.oscs[0].octave = juce::jlimit (-2, -1, params.sub.octave);
    sp.oscs[1].enabled = false;
    sp.noiseLevel = 0.0f;
    sp.filterCutoffHz = 20000.0f;
    subSynth.setParams (sp);

    auto np = sp;
    np.oscs[0].enabled = false;
    np.noiseLevel = 1.0f;
    np.filterCutoffHz = params.noise.type == 0 ? params.noise.toneHz
                      : params.noise.type == 1 ? params.noise.toneHz * 0.45f
                                               : params.noise.toneHz * 0.18f;
    noiseSynth.setParams (np);
    fxChain->setParams (params.fxSlots);
}

void HybridSynth::setSources (
    const std::array<std::shared_ptr<const LoadedSample>, 3>& samples,
    const std::array<std::array<std::shared_ptr<const wavetable::Wavetable>, 2>, 3>& tables)
{
    for (int i = 0; i < 3; ++i)
    {
        auto& l = layers[(size_t) i];
        l.wave.setTables (tables[(size_t) i][0], tables[(size_t) i][1]);
        l.sample.setSample (samples[(size_t) i]);
        l.granular.setSample (samples[(size_t) i]);
    }
}

HybridSynth::HybridVoice* HybridSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& voice : *voices)
        if (voice.active && voice.noteId == noteId)
            return &voice;

    return nullptr;
}

HybridSynth::HybridVoice& HybridSynth::findVoiceToUse()
{
    for (auto& voice : *voices)
        if (! voice.active)
            return voice;

    HybridVoice* best = nullptr;

    for (auto& voice : *voices)
        if (voice.released && (best == nullptr || voice.startOrder < best->startOrder))
            best = &voice;

    if (best != nullptr)
        return *best;

    HybridVoice* oldest = &(*voices)[0];

    for (auto& voice : *voices)
        if (voice.startOrder < oldest->startOrder)
            oldest = &voice;

    return *oldest;
}

void HybridSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (frequencyHz <= 0.0f)
        return;

    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();
    const auto stolenNote = (! (existing != nullptr) && voice.active) ? voice.noteId : 0;

    if (stolenNote != 0)
    {
        for (auto& l : layers)
        {
            l.wave.noteOff (stolenNote);
            l.subtractive.noteOff (stolenNote);
            l.sample.noteOff (stolenNote);
            l.granular.noteOff (stolenNote);
        }

        subSynth.noteOff (stolenNote);
        noiseSynth.noteOff (stolenNote);
    }

    voice.active = true;
    voice.released = false;
    voice.noteId = noteId;
    voice.frequency = (double) frequencyHz;
    voice.midiNote = 69.0 + 12.0 * std::log2 (juce::jmax (1.0f, frequencyHz) / 440.0);
    voice.velocity = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.startOrder = ++orderCounter;
    voice.controlCountdown = 0;
    voice.gainMod = 0.0f;
    voice.panMod = 0.0f;
    voice.layerLevelMod.fill (0.0f);
    voice.layerPanMod.fill (0.0f);
    voice.subLevelMod = voice.subPanMod = 0.0f;
    voice.noiseLevelMod = voice.noisePanMod = 0.0f;
    voice.fxMixMod.fill (0.0f);
    voice.fxParam1Mod.fill (0.0f);
    voice.fxParam2Mod.fill (0.0f);
    voice.layerPositionMod.fill (0.0f);
    voice.layerUnisonMod.fill (0.0f);
    voice.layerDetuneMod.fill (0.0f);
    voice.envelopeElapsed.fill (0.0);
    voice.ageSeconds = 0.0;
    voice.appliedCutoff.fill (0.0f);
    voice.appliedResonance.fill (0.0f);
    voice.appliedMode.fill (-1);
    voice.appliedSlope.fill (0);

    for (auto& pair : voice.filters)
        for (auto& filter : pair)
            filter.reset();

    for (auto& pair : voice.filtersStage2)
        for (auto& filter : pair)
            filter.reset();

    voice.notchCoeff.fill (0.0f);

    for (auto& pair : voice.notchX1)
        pair.fill (0.0f);

    for (auto& pair : voice.notchX2)
        pair.fill (0.0f);

    for (auto& pair : voice.combFilters)
        for (auto& line : pair)
            line.reset();

    for (int i = 0; i < 4; ++i)
    {
        voice.envelopes[(size_t) i].reset();
        voice.envelopes[(size_t) i].noteOn();
        voice.envelopeElapsed[(size_t) i] = 0.0;
    }

    for (int i = 0; i < 6; ++i)
    {
        if (params.lfos[(size_t) i].retrigger)
            voice.lfoPhases[(size_t) i] = juce::jlimit (
                0.0, 1.0, (double) params.lfos[(size_t) i].phase);

        voice.lfoHoldValid[(size_t) i] = false;
    }

    for (int i = 0; i < 3; ++i)
    {
        auto& l = layers[(size_t) i];
        const auto& p = params.layers[(size_t) i];

        if (! p.enabled)
            continue;

        switch (p.mode)
        {
            case HybridLayerMode::wavetable:
                l.wave.noteOn (noteId, frequencyHz, velocity);
                break;
            case HybridLayerMode::sample:
                l.sample.noteOn (noteId, frequencyHz, velocity);
                break;
            case HybridLayerMode::granular:
                l.granular.noteOn (noteId, frequencyHz, velocity);
                break;
            case HybridLayerMode::subtractive:
            case HybridLayerMode::sub:
            case HybridLayerMode::noise:
            case HybridLayerMode::off:
                break;
        }
    }

    if (params.sub.enabled)
        subSynth.noteOn (noteId, frequencyHz, velocity);
    if (params.noise.enabled)
        noiseSynth.noteOn (noteId, frequencyHz, velocity);
}

void HybridSynth::noteOff (juce::int64 noteId)
{
    auto* voice = findVoiceForNote (noteId);

    if (voice == nullptr)
        return;

    voice->released = true;

    for (auto& env : voice->envelopes)
        env.noteOff();

    for (auto& l : layers)
    {
        l.wave.noteOff (noteId);
        l.subtractive.noteOff (noteId);
        l.sample.noteOff (noteId);
        l.granular.noteOff (noteId);
    }

    subSynth.noteOff (noteId);
    noiseSynth.noteOff (noteId);
}

void HybridSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f))
        return;

    auto* voice = findVoiceForNote (noteId);

    if (voice == nullptr)
        return;

    voice->frequency = (double) frequencyHz;
    voice->midiNote = 69.0 + 12.0 * std::log2 (juce::jmax (1.0f, frequencyHz) / 440.0f);

    for (int i = 0; i < 3; ++i)
    {
        auto& l = layers[(size_t) i];

        switch (params.layers[(size_t) i].mode)
        {
            case HybridLayerMode::wavetable: l.wave.setNoteFrequency (noteId, frequencyHz); break;
            case HybridLayerMode::sample:    l.sample.setNoteFrequency (noteId, frequencyHz); break;
            case HybridLayerMode::granular:  l.granular.setNoteFrequency (noteId, frequencyHz); break;
            default: break;
        }
    }

    subSynth.setNoteFrequency (noteId, frequencyHz);
    noiseSynth.setNoteFrequency (noteId, frequencyHz);
}

void HybridSynth::allNotesOff (bool immediately)
{
    for (auto& voice : *voices)
    {
        if (! voice.active)
            continue;

        if (immediately)
        {
            voice.active = false;
            voice.released = false;

            for (auto& env : voice.envelopes)
                env.reset();

            for (auto& pair : voice.filters)
                for (auto& filter : pair)
                    filter.reset();

            for (auto& pair : voice.filtersStage2)
                for (auto& filter : pair)
                    filter.reset();
        }
        else
        {
            voice.released = true;

            for (auto& env : voice.envelopes)
                env.noteOff();
        }
    }

    for (auto& l : layers)
    {
        l.wave.allNotesOff (immediately);
        l.subtractive.allNotesOff (immediately);
        l.sample.allNotesOff (immediately);
        l.granular.allNotesOff (immediately);
    }

    subSynth.allNotesOff (immediately);
    noiseSynth.allNotesOff (immediately);

    if (immediately)
        fxChain->reset();
}

bool HybridSynth::renderVoice (HybridVoice& voice, int numSamples,
                               float gainLeft, float gainRight)
{
    voiceBuffer.clear (0, numSamples);

    for (int i = 0; i < 3; ++i)
    {
        const auto& p = params.layers[(size_t) i];

        if (! p.enabled || p.mode == HybridLayerMode::off || p.level <= 0.0f)
            continue;

        const auto pan = juce::jlimit (-1.0f, 1.0f,
                                       p.pan + voice.layerPanMod[(size_t) i]);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto level = juce::jlimit (
            0.0f, 2.0f, p.level * (1.0f + voice.layerLevelMod[(size_t) i]));
        const auto left = gainLeft * level * std::cos (angle);
        const auto right = gainRight * level * std::sin (angle);
        auto& l = layers[(size_t) i];

        l.wave.setVoiceOffsets (voice.noteId,
                                voice.layerPositionMod[(size_t) i],
                                voice.layerUnisonMod[(size_t) i],
                                voice.layerDetuneMod[(size_t) i]);

        switch (p.mode)
        {
            case HybridLayerMode::wavetable:
                l.wave.renderVoiceAdding (voice.noteId, voiceBuffer, 0, numSamples,
                                          left, right);
                break;
            case HybridLayerMode::sample:
                l.sample.renderVoiceAdding (voice.noteId, voiceBuffer, 0, numSamples,
                                            left, right);
                break;
            case HybridLayerMode::granular:
                l.granular.renderVoiceAdding (voice.noteId, voiceBuffer, 0, numSamples,
                                              left, right);
                break;
            case HybridLayerMode::subtractive:
            case HybridLayerMode::sub:
            case HybridLayerMode::noise:
            case HybridLayerMode::off:
                break;
        }
    }

    if (params.sub.enabled)
    {
        const auto pan = juce::jlimit (-1.0f, 1.0f,
                                       params.sub.pan + voice.subPanMod);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto level = juce::jlimit (
            0.0f, 2.0f, params.sub.level * (1.0f + voice.subLevelMod));
        subSynth.renderVoiceAdding (voice.noteId, voiceBuffer, 0, numSamples,
                                    gainLeft * level * std::cos (angle),
                                    gainRight * level * std::sin (angle));
    }

    if (params.noise.enabled)
    {
        const auto pan = juce::jlimit (-1.0f, 1.0f,
                                       params.noise.pan + voice.noisePanMod);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto level = juce::jlimit (
            0.0f, 2.0f, params.noise.level * (1.0f + voice.noiseLevelMod));
        noiseSynth.renderVoiceAdding (voice.noteId, voiceBuffer, 0, numSamples,
                                      gainLeft * level * std::cos (angle),
                                      gainRight * level * std::sin (angle));
    }

    auto* inLeft = voiceBuffer.getReadPointer (0);
    auto* inRight = voiceBuffer.getReadPointer (1);
    auto* outLeft = scratch.getWritePointer (0);
    auto* outRight = scratch.getWritePointer (1);
    const auto keyTrackNorm = juce::jlimit (
        -1.0f, 1.0f, (float) ((voice.midiNote - 60.0) / 48.0));

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<float, 4> envValues {};

        for (int e = 0; e < 4; ++e)
        {
            envValues[(size_t) e] = voice.envelopes[(size_t) e].getNextSample();
            voice.envelopeElapsed[(size_t) e] += 1.0 / sampleRate;

            const auto& envelope = params.envelopes[(size_t) e];
            const auto total = juce::jmax (
                0.001,
                envelopeSeconds (envelope.attack, envelope.timeMode,
                                 envelope.attackDivision, currentBpm)
                    + envelopeSeconds (envelope.decay, envelope.timeMode,
                                       envelope.decayDivision, currentBpm)
                    + envelopeSeconds (envelope.release, envelope.timeMode,
                                       envelope.releaseDivision, currentBpm)
                    + 0.25);
            published.envelopePosition[e].store (
                (float) juce::jlimit (0.0, 1.0,
                                      voice.envelopeElapsed[(size_t) e] / total),
                std::memory_order_relaxed);
            published.envelopeValue[e].store (envValues[(size_t) e],
                                              std::memory_order_relaxed);
        }

        if (! voice.envelopes[0].isActive())
        {
            voice.active = false;
            voice.released = false;
            break;
        }

        std::array<float, 6> lfoValues {};

        for (int l = 0; l < 6; ++l)
        {
            const auto& lfo = params.lfos[(size_t) l];
            voice.lfoPhases[(size_t) l] = fold01 (
                voice.lfoPhases[(size_t) l]
                + lfoRateHzFor (lfo, currentBpm) / sampleRate);
            lfoValues[(size_t) l] = (float) subtractive::lfoSample (
                lfo.waveform, voice.lfoPhases[(size_t) l],
                voice.lfoHold[(size_t) l], voice.lfoHoldValid[(size_t) l],
                random);
            const auto fade = lfo.fadeMs <= 0.0f
                                  ? 1.0f
                                  : (float) juce::jlimit (
                                        0.0, 1.0,
                                        voice.ageSeconds / (lfo.fadeMs * 0.001));
            lfoValues[(size_t) l] *= juce::jlimit (0.0f, 1.0f, lfo.depth) * fade;
            published.lfoPhase[l].store ((float) voice.lfoPhases[(size_t) l],
                                         std::memory_order_relaxed);
            published.lfoValue[l].store (lfoValues[(size_t) l],
                                         std::memory_order_relaxed);
        }

        voice.ageSeconds += 1.0 / sampleRate;

        if (voice.controlCountdown <= 0)
        {
            voice.controlCountdown = kControlRate;

            std::array<float, 18> sources {};

            for (int e = 0; e < 4; ++e)
                sources[(size_t) e + 1] = envValues[(size_t) e];
            for (int l = 0; l < 4; ++l)
                sources[(size_t) l + 5] = lfoValues[(size_t) l];

            sources[16] = lfoValues[4];
            sources[17] = lfoValues[5];

            sources[9] = voice.velocity;
            sources[10] = keyTrackNorm;
            sources[11] = 0.0f;

            for (int m = 0; m < 4; ++m)
                sources[(size_t) m + 12] = juce::jlimit (
                    0.0f, 1.0f, params.macros[(size_t) m]);

            std::array<float, 2> filterCutoffMod {};
            std::array<float, 2> filterResonanceMod {};
            std::array<float, 3> layerLevelMod {};
            std::array<float, 3> layerPanMod {};
            std::array<float, 3> layerPositionMod {};
            std::array<float, 3> layerUnisonMod {};
            std::array<float, 3> layerDetuneMod {};
            float subLevelMod = 0.0f, subPanMod = 0.0f;
            float noiseLevelMod = 0.0f, noisePanMod = 0.0f;
            float gainMod = 0.0f;
            float panMod = 0.0f;
            std::array<float, 4> fxMixMod {};
            std::array<float, 4> fxParam1Mod {};
            std::array<float, 4> fxParam2Mod {};

            for (const auto& slot : params.modSlots)
            {
                if (! slot.enabled || slot.source == HybridModSource::none
                    || slot.destination == HybridModDestination::none)
                    continue;

                const auto sourceIndex = (int) slot.source;
                const auto value = sourceIndex >= 0 && sourceIndex < (int) sources.size()
                                       ? sources[(size_t) sourceIndex] : 0.0f;
                const auto amount = value * slot.amount;

                switch (slot.destination)
                {
                    case HybridModDestination::filter1Cutoff: filterCutoffMod[0] += amount; break;
                    case HybridModDestination::filter2Cutoff: filterCutoffMod[1] += amount; break;
                    case HybridModDestination::filter1Resonance: filterResonanceMod[0] += amount; break;
                    case HybridModDestination::filter2Resonance: filterResonanceMod[1] += amount; break;
                    case HybridModDestination::gain: gainMod += amount; break;
                    case HybridModDestination::pan: panMod += amount; break;
                    case HybridModDestination::layer1Level: layerLevelMod[0] += amount; break;
                    case HybridModDestination::layer2Level: layerLevelMod[1] += amount; break;
                    case HybridModDestination::layer3Level: layerLevelMod[2] += amount; break;
                    case HybridModDestination::layer1Pan: layerPanMod[0] += amount; break;
                    case HybridModDestination::layer2Pan: layerPanMod[1] += amount; break;
                    case HybridModDestination::layer3Pan: layerPanMod[2] += amount; break;
                    case HybridModDestination::layer1Position: layerPositionMod[0] += amount; break;
                    case HybridModDestination::layer1Unison: layerUnisonMod[0] += amount; break;
                    case HybridModDestination::layer1Detune: layerDetuneMod[0] += amount; break;
                    case HybridModDestination::layer2Position: layerPositionMod[1] += amount; break;
                    case HybridModDestination::layer2Unison: layerUnisonMod[1] += amount; break;
                    case HybridModDestination::layer2Detune: layerDetuneMod[1] += amount; break;
                    case HybridModDestination::layer3Position: layerPositionMod[2] += amount; break;
                    case HybridModDestination::layer3Unison: layerUnisonMod[2] += amount; break;
                    case HybridModDestination::layer3Detune: layerDetuneMod[2] += amount; break;
                    case HybridModDestination::subLevel: subLevelMod += amount; break;
                    case HybridModDestination::subPan: subPanMod += amount; break;
                    case HybridModDestination::noiseLevel: noiseLevelMod += amount; break;
                    case HybridModDestination::noisePan: noisePanMod += amount; break;
                    case HybridModDestination::fx1Mix: fxMixMod[0] += amount; break;
                    case HybridModDestination::fx1Param1: fxParam1Mod[0] += amount; break;
                    case HybridModDestination::fx1Param2: fxParam2Mod[0] += amount; break;
                    case HybridModDestination::fx2Mix: fxMixMod[1] += amount; break;
                    case HybridModDestination::fx2Param1: fxParam1Mod[1] += amount; break;
                    case HybridModDestination::fx2Param2: fxParam2Mod[1] += amount; break;
                    case HybridModDestination::fx3Mix: fxMixMod[2] += amount; break;
                    case HybridModDestination::fx3Param1: fxParam1Mod[2] += amount; break;
                    case HybridModDestination::fx3Param2: fxParam2Mod[2] += amount; break;
                    case HybridModDestination::fx4Mix: fxMixMod[3] += amount; break;
                    case HybridModDestination::fx4Param1: fxParam1Mod[3] += amount; break;
                    case HybridModDestination::fx4Param2: fxParam2Mod[3] += amount; break;
                    case HybridModDestination::none: break;
                }
            }

            voice.gainMod = gainMod;
            voice.panMod = panMod;
            voice.layerLevelMod = layerLevelMod;
            voice.layerPanMod = layerPanMod;
            voice.layerPositionMod = layerPositionMod;
            voice.layerUnisonMod = layerUnisonMod;
            voice.layerDetuneMod = layerDetuneMod;
            voice.subLevelMod = subLevelMod;
            voice.subPanMod = subPanMod;
            voice.noiseLevelMod = noiseLevelMod;
            voice.noisePanMod = noisePanMod;
            voice.fxMixMod = fxMixMod;
            voice.fxParam1Mod = fxParam1Mod;
            voice.fxParam2Mod = fxParam2Mod;

            for (int which = 0; which < 2; ++which)
            {
                const auto& f = params.filters[(size_t) which];
                const auto filterEnv = which == 0 ? envValues[1] : envValues[2];
                const auto envOctaves = (double) f.envAmount * filterEnv * kFilterEnvOctaves;
                const auto keyOctaves = (double) f.keyTrack
                                            * (voice.midiNote - 60.0) / 12.0;
                const auto modOctaves = (double) filterCutoffMod[(size_t) which] * 4.0;
                const auto cutoff = juce::jlimit (
                    20.0, sampleRate * 0.49,
                    (double) f.cutoffHz
                        * std::pow (2.0, envOctaves + keyOctaves + modOctaves));
                const auto resonance = juce::jlimit (
                    0.1f, 20.0f,
                    f.resonance + filterResonanceMod[(size_t) which] * 8.0f);
                const auto type = filterTypeFor (f.mode);
                const auto slope = f.slopeDb >= 24 ? 24 : 12;

                if (f.mode == FilterMode::notch)
                {
                    const auto w = 2.0 * juce::MathConstants<double>::pi
                                       * juce::jlimit (20.0, sampleRate * 0.49, cutoff)
                                       / sampleRate;
                    voice.notchCoeff[(size_t) which] = (float) (-2.0 * std::cos (w));
                }
                else if (f.mode != FilterMode::comb)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto& filter = voice.filters[(size_t) which][(size_t) ch];
                        auto& stage2 = voice.filtersStage2[(size_t) which][(size_t) ch];

                        if (voice.appliedMode[(size_t) which] != (int) f.mode)
                        {
                            filter.setType (type);
                            stage2.setType (type);
                        }

                        filter.setCutoffFrequency ((float) cutoff);
                        stage2.setCutoffFrequency ((float) cutoff);
                        filter.setResonance (resonance);
                        stage2.setResonance (resonance);
                    }
                }

                voice.appliedCutoff[(size_t) which] = (float) cutoff;
                voice.appliedResonance[(size_t) which] = resonance;
                voice.appliedMode[(size_t) which] = (int) f.mode;
                voice.appliedSlope[(size_t) which] = slope;
            }
        }

        --voice.controlCountdown;

        const auto amplitude = envValues[0];
        const auto voiceGain = juce::jmax (0.0f, amplitude * (1.0f + voice.gainMod));
        const auto pan = juce::jlimit (-1.0f, 1.0f, voice.panMod);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto panLeft = std::cos (angle);
        const auto panRight = std::sin (angle);

        auto processFilterStage = [&] (int which, int channel, float input)
        {
            const auto& filterParams = params.filters[(size_t) which];

            if (filterParams.mode == FilterMode::notch)
            {
                auto& x1 = voice.notchX1[(size_t) which][(size_t) channel];
                auto& x2 = voice.notchX2[(size_t) which][(size_t) channel];
                const auto output = input + voice.notchCoeff[(size_t) which] * x1 + x2;
                x2 = x1;
                x1 = input;
                return output;
            }

            if (filterParams.mode == FilterMode::comb)
            {
                const auto delaySamples = (float) juce::jlimit (
                    1.0, sampleRate * 0.05,
                    sampleRate / juce::jmax (20.0, (double) voice.appliedCutoff[(size_t) which]));
                const auto feedback = juce::jlimit (
                    0.0f, 0.95f,
                    (voice.appliedResonance[(size_t) which] - 0.1f) / 19.9f);
                auto& line = voice.combFilters[(size_t) which][(size_t) channel];
                const auto delayed = line.popSample (0, delaySamples);
                line.pushSample (0, input + delayed * feedback);
                return 0.5f * (input + delayed);
            }

            auto output = voice.filters[(size_t) which][(size_t) channel]
                              .processSample (0, input);

            if (voice.appliedSlope[(size_t) which] >= 24)
                output = voice.filtersStage2[(size_t) which][(size_t) channel]
                             .processSample (0, output);

            return output;
        };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto input = ch == 0 ? inLeft[i] : inRight[i];
            const auto first = processFilterStage (0, ch, input);
            float filtered = first;

            switch (params.filterRouting)
            {
                case HybridFilterRouting::series:
                    filtered = processFilterStage (1, ch, filtered);
                    break;
                case HybridFilterRouting::parallel:
                    filtered = 0.5f * (first + processFilterStage (1, ch, input));
                    break;
                case HybridFilterRouting::filter1Only:
                    break;
            }

            const auto channelGain = voiceGain * (ch == 0 ? panLeft : panRight);
            auto& output = ch == 0 ? outLeft[i] : outRight[i];
            output += filtered * channelGain;
        }
    }

    latestFxMixMod = voice.fxMixMod;
    latestFxParam1Mod = voice.fxParam1Mod;
    latestFxParam2Mod = voice.fxParam2Mod;
    return voice.active;
}

void HybridSynth::renderAdding (juce::AudioBuffer<float>& buffer,
                                int startSample,
                                int numSamples,
                                float gainLeft,
                                float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    scratch.clear (0, numSamples);

    for (auto& voice : *voices)
        if (voice.active)
            renderVoice (voice, numSamples, gainLeft, gainRight);

    auto effectiveFx = params.fxSlots;

    for (int i = 0; i < 4; ++i)
    {
        effectiveFx[(size_t) i].mix = juce::jlimit (
            0.0f, 1.0f, effectiveFx[(size_t) i].mix + latestFxMixMod[(size_t) i]);
        effectiveFx[(size_t) i].param1 = juce::jlimit (
            0.0f, 1.0f, effectiveFx[(size_t) i].param1 + latestFxParam1Mod[(size_t) i]);
        effectiveFx[(size_t) i].param2 = juce::jlimit (
            0.0f, 1.0f, effectiveFx[(size_t) i].param2 + latestFxParam2Mod[(size_t) i]);
    }

    fxChain->setParams (effectiveFx);
    fxChain->process (scratch, numSamples);

    const auto master = juce::jlimit (0.0f, 1.0f, params.gain);

    for (int ch = 0; ch < 2; ++ch)
        buffer.addFrom (ch, startSample, scratch, ch, 0, numSamples, master);
}

bool HybridSynth::isActive() const noexcept
{
    for (const auto& voice : *voices)
        if (voice.active)
            return true;

    return fxChain->isTailActive();
}

int HybridSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voice : *voices)
        if (voice.active)
            ++count;

    return count;
}

float HybridSynth::getWavetablePosition (int layerIndex) const noexcept
{
    return layerIndex >= 0 && layerIndex < 3
               ? layers[(size_t) layerIndex].wave.getPublishedPosition (0)
               : 0.0f;
}

int HybridSynth::getGranularGrainCount (int layer) const noexcept
{
    return layer >= 0 && layer < 3
               ? layers[(size_t) layer].granular.getPublishedGrainCount()
               : 0;
}

float HybridSynth::getGranularGrainPosition (int layer, int index) const noexcept
{
    return layer >= 0 && layer < 3
               ? layers[(size_t) layer].granular.getPublishedGrainPosition (index)
               : 0.0f;
}

float HybridSynth::getGranularGrainAge (int layer, int index) const noexcept
{
    return layer >= 0 && layer < 3
               ? layers[(size_t) layer].granular.getPublishedGrainAge (index)
               : 0.0f;
}

float HybridSynth::getEnvelopePosition (int index) const noexcept
{
    return published.envelopePosition[juce::jlimit (0, 3, index)]
        .load (std::memory_order_relaxed);
}

float HybridSynth::getEnvelopeValue (int index) const noexcept
{
    return published.envelopeValue[juce::jlimit (0, 3, index)]
        .load (std::memory_order_relaxed);
}

float HybridSynth::getLfoPhase (int index) const noexcept
{
    return published.lfoPhase[juce::jlimit (0, 5, index)]
        .load (std::memory_order_relaxed);
}

float HybridSynth::getLfoValue (int index) const noexcept
{
    return published.lfoValue[juce::jlimit (0, 5, index)]
        .load (std::memory_order_relaxed);
}

} // namespace nidaw
