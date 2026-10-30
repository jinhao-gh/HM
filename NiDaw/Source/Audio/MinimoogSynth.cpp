#include "MinimoogSynth.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr float  kOutputTrim = 0.5f;

    inline double wrap01 (double x) noexcept
    {
        return x - std::floor (x);
    }

    inline int midiFromHz (double hz) noexcept
    {
        return juce::jlimit (0, 127, (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
    }

    inline double noteHz (double hz) noexcept
    {
        return juce::jlimit (1.0, 20000.0, hz);
    }
}

//==============================================================================
void MinimoogSynth::Envelope::prepare (double sr)
{
    sampleRate = sr;
    configure();
}

float MinimoogSynth::Envelope::coefficient (double seconds, double sr) noexcept
{
    const auto time = juce::jmax (0.0005, seconds);
    return (float) (1.0 - std::exp (-4.0 / (time * sr)));
}

void MinimoogSynth::Envelope::configure() noexcept
{
    attackCoef = coefficient (attack, sampleRate);
    decayCoef = coefficient (decayShort ? decay * 0.1f : decay, sampleRate);
    releaseCoef = coefficient (releaseOn ? 0.08 : 0.002, sampleRate);
}

void MinimoogSynth::Envelope::release() noexcept
{
    if (stage != Stage::idle)
        stage = Stage::release;
}

float MinimoogSynth::Envelope::next() noexcept
{
    switch (stage)
    {
        case Stage::attack:
            level += (1.0f - level) * attackCoef;

            if (level > 0.995f)
            {
                level = 1.0f;
                stage = Stage::decay;
            }
            break;

        case Stage::decay:
        {
            const auto target = sustainOn ? 0.8f : 0.0f;
            level += (target - level) * decayCoef;

            if (std::abs (level - target) < 0.002f)
            {
                level = target;
                stage = Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            level = sustainOn ? 0.8f : 0.0f;
            break;

        case Stage::release:
            level += (0.0f - level) * releaseCoef;

            if (level < 0.0005f)
            {
                level = 0.0f;
                stage = Stage::idle;
            }
            break;

        case Stage::idle:
        default:
            level = 0.0f;
            break;
    }

    return level;
}

//==============================================================================
MinimoogSynth::MinimoogSynth() = default;

void MinimoogSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& voice : voices)
    {
        voice.ampEnv.prepare (sampleRate);
        voice.filterEnv.prepare (sampleRate);

        voice.filter.prepare (sampleRate);
        voice.filter.reset();

        voice.active = false;
        voice.released = false;
    }

    heldNotes.clear();
    monoNoteId = 0;
    monoCurrentHz = 440.0;
    monoLastHz = 0.0;
}

void MinimoogSynth::reset()
{
    allNotesOff (true);

    for (auto& voice : voices)
        voice.filter.reset();
}

void MinimoogSynth::setParams (const MinimoogParams& newParams)
{
    const auto polyphonyChanged = params.polyphony != newParams.polyphony;
    params = newParams;

    for (auto& voice : voices)
        configureEnvelopes (voice);

    // Changing the voice count while notes are held would leave the old voices
    // orphaned: let them go instead.
    if (polyphonyChanged)
        allNotesOff (true);
}

void MinimoogSynth::configureEnvelopes (Voice& voice)
{
    voice.ampEnv.attack = params.ampAttack;
    voice.ampEnv.decay = params.ampDecay;
    voice.ampEnv.decayShort = params.ampDecayShort;
    voice.ampEnv.sustainOn = params.ampSustainOn;
    voice.ampEnv.releaseOn = params.releaseOn;
    voice.ampEnv.configure();

    voice.filterEnv.attack = params.filterAttack;
    voice.filterEnv.decay = params.filterDecay;
    voice.filterEnv.decayShort = params.filterDecayShort;
    voice.filterEnv.sustainOn = params.filterSustainOn;
    voice.filterEnv.releaseOn = true;
    voice.filterEnv.configure();
}

int MinimoogSynth::voiceLimit() const noexcept
{
    if (params.polyphony < 0)
        return kMaxMinimoogVoices;

    return juce::jlimit (1, kMaxMinimoogVoices, params.polyphony);
}

void MinimoogSynth::startVoice (Voice& voice, juce::int64 noteId, double hz, float velocity)
{
    const auto wasActive = voice.active;

    voice.active = true;
    voice.released = false;
    voice.noteId = noteId;
    voice.startOrder = ++orderCounter;

    voice.targetHz = noteHz (hz);
    voice.currentHz = voice.targetHz;
    voice.midiNote = midiFromHz (voice.targetHz);

    const auto normalized = juce::jlimit (0.0, 1.0, (double) velocity / 127.0);
    voice.velocityGain = 1.0f - params.velocitySensitivity
                             + params.velocitySensitivity
                                   * (float) std::pow (normalized, 1.2);

    configureEnvelopes (voice);
    voice.ampEnv.trigger();
    voice.filterEnv.trigger();

    voice.pinkStateA = 0.0;
    voice.pinkStateB = 0.0;

    // Stealing a voice while it is still sounding resets the ladder so the new
    // note does not inherit the old one's ringing.
    if (wasActive)
        voice.filter.reset();
}

void MinimoogSynth::setVoiceTarget (Voice& voice, double hz)
{
    voice.targetHz = noteHz (hz);
    voice.midiNote = midiFromHz (voice.targetHz);

    if (! params.glideOn)
        voice.currentHz = voice.targetHz;
}

MinimoogSynth::Voice& MinimoogSynth::findFreeVoice()
{
    for (auto& voice : voices)
        if (! voice.active)
            return voice;

    Voice* released = nullptr;
    Voice* oldest = &voices[0];

    for (auto& voice : voices)
    {
        if (voice.startOrder < oldest->startOrder)
            oldest = &voice;

        if (voice.released && (released == nullptr || voice.startOrder < released->startOrder))
            released = &voice;
    }

    return released != nullptr ? *released : *oldest;
}

MinimoogSynth::Voice* MinimoogSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& voice : voices)
        if (voice.active && voice.noteId == noteId)
            return &voice;

    return nullptr;
}

void MinimoogSynth::noteOn (juce::int64 noteId, float frequencyHz, float velocity)
{
    if (! (frequencyHz > 0.0f))
        return;

    const auto hz = (double) frequencyHz;

    if (params.polyphony == 0)
    {
        auto& voice = voices[0];
        const auto firstNote = heldNotes.empty();

        if (! voice.active || firstNote)
        {
            startVoice (voice, noteId, hz, velocity);

            if (params.glideOn && monoLastHz > 0.0)
                voice.currentHz = monoLastHz;
        }
        else
        {
            // Legato: the envelopes keep running, only the pitch glides.
            setVoiceTarget (voice, hz);
        }

        heldNotes.erase (std::remove_if (heldNotes.begin(), heldNotes.end(),
                                         [noteId] (const HeldNote& n) { return n.id == noteId; }),
                         heldNotes.end());
        heldNotes.push_back ({ noteId, hz });

        monoNoteId = noteId;
        monoLastHz = hz;
        monoCurrentHz = voice.currentHz;
    }
    else
    {
        auto& voice = findFreeVoice();
        startVoice (voice, noteId, hz, velocity);
    }
}

void MinimoogSynth::noteOff (juce::int64 noteId)
{
    if (params.polyphony == 0)
    {
        heldNotes.erase (std::remove_if (heldNotes.begin(), heldNotes.end(),
                                         [noteId] (const HeldNote& n) { return n.id == noteId; }),
                         heldNotes.end());

        auto& voice = voices[0];

        if (heldNotes.empty())
        {
            if (voice.active)
            {
                voice.released = true;
                voice.ampEnv.release();
                voice.filterEnv.release();
            }

            monoNoteId = 0;
        }
        else if (voice.active)
        {
            const auto& last = heldNotes.back();
            monoNoteId = last.id;
            setVoiceTarget (voice, last.hz);
        }

        return;
    }

    if (auto* voice = findVoiceForNote (noteId))
    {
        voice->released = true;
        voice->ampEnv.release();
        voice->filterEnv.release();
    }
}

void MinimoogSynth::setNoteFrequency (juce::int64 noteId, float frequencyHz)
{
    if (! (frequencyHz > 0.0f))
        return;

    const auto hz = (double) frequencyHz;

    if (params.polyphony == 0)
    {
        for (auto& note : heldNotes)
            if (note.id == noteId)
                note.hz = hz;

        if (noteId == monoNoteId && voices[0].active)
        {
            setVoiceTarget (voices[0], hz);
            monoLastHz = hz;
        }

        return;
    }

    if (auto* voice = findVoiceForNote (noteId))
    {
        voice->targetHz = noteHz (hz);
        voice->midiNote = midiFromHz (voice->targetHz);
        voice->currentHz = voice->targetHz;
    }
}

void MinimoogSynth::allNotesOff (bool immediately)
{
    heldNotes.clear();
    monoNoteId = 0;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        if (immediately)
        {
            voice.active = false;
            voice.released = false;
            voice.ampEnv.kill();
            voice.filterEnv.kill();
            voice.filter.reset();
        }
        else
        {
            voice.released = true;
            voice.ampEnv.release();
            voice.filterEnv.release();
        }
    }
}

bool MinimoogSynth::isActive() const noexcept
{
    for (const auto& voice : voices)
        if (voice.active)
            return true;

    return false;
}

int MinimoogSynth::activeVoiceCount() const noexcept
{
    auto count = 0;

    for (const auto& voice : voices)
        if (voice.active)
            ++count;

    return count;
}

double MinimoogSynth::getVoiceFrequencyForTest (juce::int64 noteId) const
{
    for (const auto& voice : voices)
        if (voice.active && voice.noteId == noteId)
            return voice.targetHz;

    return 0.0;
}

bool MinimoogSynth::isGlidingForTest() const noexcept
{
    return params.polyphony == 0 && params.glideOn
               && std::abs (voices[0].currentHz - voices[0].targetHz) > 0.5;
}

void MinimoogSynth::renderVoice (Voice& voice, juce::AudioBuffer<float>& buffer,
                                 int startSample, int numSamples,
                                 float gainLeft, float gainRight)
{
    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    const auto masterTune = std::pow (2.0, params.masterTuneCents / 1200.0);
    const auto glideCoefficient = 1.0 - std::exp (-4.0 / (juce::jmax (0.001f, params.glideSeconds)
                                                         * sampleRate));
    const auto isMono = params.polyphony == 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- glide (monophonic, like the real CV input) --------------------
        if (isMono && params.glideOn && params.glideSeconds > 0.0f)
        {
            const auto current = std::log (juce::jmax (1.0, voice.currentHz));
            const auto target = std::log (juce::jmax (1.0, voice.targetHz));
            voice.currentHz = std::exp (current + (target - current) * glideCoefficient);
        }
        else
        {
            voice.currentHz = voice.targetHz;
        }

        if (isMono)
            monoCurrentHz = voice.currentHz;

        const auto filterEnvLevel = voice.filterEnv.next();
        const auto ampEnvLevel = voice.ampEnv.next();

        // ---- oscillator 3 first: it can be the modulation source -----------
        auto& o3 = params.oscs[2];
        const auto drift3 = 2.0 * std::sin (kTwoPi * voice.driftPhase[2]);
        const auto base3 = params.osc3Control ? voice.currentHz : 261.6255653;
        const auto freq3 = base3 * minimoog::rangeMultiplier (o3.range)
                               * std::pow (2.0, o3.tuneSemitones / 12.0)
                               * masterTune * std::pow (2.0, drift3 / 1200.0);
        const auto delta3 = freq3 / sampleRate;
        voice.phase[2] = wrap01 (voice.phase[2] + delta3);
        const auto sample3 = minimoog::oscillatorSample (o3.wave, voice.phase[2], delta3);

        // ---- noise ---------------------------------------------------------
        const auto white = random.nextDouble() * 2.0 - 1.0;
        voice.pinkStateA += 0.02 * (white - voice.pinkStateA);
        voice.pinkStateB += 0.08 * (white - voice.pinkStateB);
        const auto pink = (white + voice.pinkStateA * 2.0 + voice.pinkStateB * 3.0) * 0.18;
        const auto noise = params.noisePink ? pink : white;

        // ---- modulation ----------------------------------------------------
        const auto modSource = sample3 + (noise - sample3) * params.modMix;
        const auto pitchMod = params.oscModOn
                                  ? std::pow (2.0, modSource * params.modDepth * 2.0 / 12.0)
                                  : 1.0;

        double mix = 0.0;

        for (int o = 0; o < 2; ++o)
        {
            const auto& osc = params.oscs[(size_t) o];
            const auto drift = 2.0 * std::sin (kTwoPi * voice.driftPhase[o]);
            const auto freq = voice.currentHz * minimoog::rangeMultiplier (osc.range)
                                  * std::pow (2.0, osc.tuneSemitones / 12.0)
                                  * masterTune * pitchMod * std::pow (2.0, drift / 1200.0);
            const auto delta = freq / sampleRate;
            voice.phase[o] = wrap01 (voice.phase[o] + delta);

            if (osc.enabled)
                mix += osc.level * minimoog::oscillatorSample (osc.wave, voice.phase[o], delta);
        }

        if (o3.enabled)
            mix += o3.level * sample3;

        mix += params.noiseLevel * noise;

        for (int o = 0; o < 3; ++o)
            voice.driftPhase[o] = wrap01 (voice.driftPhase[o] + (0.03 + 0.015 * o) / sampleRate);

        // ---- mixer overload, ladder, loudness ------------------------------
        const auto driven = std::tanh (1.5 * mix) / 0.9051482536;

        const auto keyTrack = (params.filterKeyTrack1 ? 1.0 / 3.0 : 0.0)
                                  + (params.filterKeyTrack2 ? 1.0 / 3.0 : 0.0);
        const auto velocityFactor = 0.25 + 0.75 * (double) voice.velocityGain;
        const auto filterMod = params.filterModOn ? modSource * params.modDepth * 4.0 : 0.0;

        auto cutoff = params.filterCutoffHz
                          * std::pow (2.0, (voice.midiNote - 60) / 12.0 * keyTrack
                                             + filterEnvLevel * params.filterContourOctaves
                                                   * velocityFactor
                                             + filterMod);
        cutoff = juce::jlimit (10.0, 20000.0, cutoff);

        const auto filtered = (float) voice.filter.processSample (driven, cutoff,
                                                                  (double) params.filterEmphasis);
        const auto out = std::tanh (filtered * ampEnvLevel * voice.velocityGain
                                    * params.volume * kOutputTrim);

        left[i]  += out * gainLeft;
        right[i] += out * gainRight;
    }

    if (voice.ampEnv.finished())
    {
        voice.active = false;
        voice.released = false;
    }
}

void MinimoogSynth::renderAdding (juce::AudioBuffer<float>& buffer, int startSample,
                                  int numSamples, float gainLeft, float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    for (auto& voice : voices)
        if (voice.active)
            renderVoice (voice, buffer, startSample, numSamples, gainLeft, gainRight);
}

bool MinimoogSynth::renderVoiceAdding (juce::int64 noteId, juce::AudioBuffer<float>& buffer,
                                       int startSample, int numSamples,
                                       float gainLeft, float gainRight)
{
    auto* voice = findVoiceForNote (noteId);

    if (voice == nullptr)
        return false;

    renderVoice (*voice, buffer, startSample, numSamples, gainLeft, gainRight);
    return true;
}

} // namespace nidaw
