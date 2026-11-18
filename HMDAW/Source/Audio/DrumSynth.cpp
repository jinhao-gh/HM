#include "DrumSynth.h"

namespace hmdaw
{

namespace
{
    constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

    // Headroom for sixteen simultaneous hits; a kit is loud by nature.
    constexpr float kMasterTrim = 0.32f;

    /** How long each drum rings by default, in seconds (before the pad's own
        decay multiplier and the kit's master decay are folded in). */
    double baseDecay (int pad)
    {
        switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
        {
            case DrumPad::kick:      return 0.45;
            case DrumPad::snare:     return 0.30;
            case DrumPad::rim:       return 0.09;
            case DrumPad::clap:      return 0.28;
            case DrumPad::closedHat: return 0.06;
            case DrumPad::openHat:   return 0.42;
            case DrumPad::lowTom:    return 0.55;
            case DrumPad::midTom:    return 0.45;
            case DrumPad::highTom:   return 0.35;
            case DrumPad::cowbell:   return 0.35;
            case DrumPad::crash:     return 1.30;
            case DrumPad::ride:      return 0.85;
            case DrumPad::count:     break;
        }

        return 0.3;
    }

    /** Where each drum sits in the stereo field at full width. */
    double basePan (int pad)
    {
        switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
        {
            case DrumPad::closedHat: return 0.35;
            case DrumPad::openHat:   return 0.35;
            case DrumPad::ride:      return 0.45;
            case DrumPad::crash:     return -0.40;
            case DrumPad::lowTom:    return -0.35;
            case DrumPad::highTom:   return 0.40;
            case DrumPad::cowbell:   return 0.25;
            case DrumPad::clap:      return -0.20;
            case DrumPad::rim:       return -0.25;
            default: break;
        }

        return 0.0;
    }

    /** The metallic partial ratios the 808 hat and cymbals are built from. */
    double metalRatio (int index)
    {
        static const double ratios[] = { 1.0, 1.34, 1.71, 2.05, 2.47, 3.03 };
        return ratios[juce::jlimit (0, 5, index)];
    }

    juce::dsp::StateVariableTPTFilterType modeFor (int pad)
    {
        switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
        {
            case DrumPad::clap:                       return juce::dsp::StateVariableTPTFilterType::bandpass;
            case DrumPad::closedHat:
            case DrumPad::openHat:
            case DrumPad::crash:
            case DrumPad::ride:                       return juce::dsp::StateVariableTPTFilterType::highpass;
            default:                                  break;
        }

        return juce::dsp::StateVariableTPTFilterType::lowpass;
    }

    /** The filter a pad runs its noise through, and how hard it is driven. */
    void filterFor (int pad, double& cutoff, double& resonance)
    {
        switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
        {
            case DrumPad::clap:      cutoff = 1200.0; resonance = 1.6; break;
            case DrumPad::closedHat: cutoff = 7000.0; resonance = 0.9; break;
            case DrumPad::openHat:   cutoff = 7000.0; resonance = 0.9; break;
            case DrumPad::crash:     cutoff = 5000.0; resonance = 0.8; break;
            case DrumPad::ride:      cutoff = 6000.0; resonance = 0.9; break;
            default:                 cutoff = 20000.0; resonance = 0.707; break;
        }
    }
}

double DrumSynth::decaySecondsFor (int pad, const DrumKitParams& kit)
{
    const auto index = juce::jlimit (0, kDrumPads - 1, pad);
    return baseDecay (index) * (double) kit.pads[(size_t) index].decay
               * (double) kit.masterDecay;
}

DrumSynth::DrumSynth()
{
    prepare (48000.0);
}

void DrumSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& v : voices)
    {
        v.active = false;
        v.filter.prepare ({ sampleRate, 512, 2 });
        v.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }
}

void DrumSynth::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.choked = false;
        v.choke  = 1.0;
        v.filter.reset();
    }
}

void DrumSynth::setParams (const DrumKitParams& newParams)
{
    params = newParams;
}

DrumSynth::Voice* DrumSynth::findVoiceForNote (juce::int64 noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId)
            return &v;

    return nullptr;
}

DrumSynth::Voice& DrumSynth::findVoiceToUse()
{
    for (auto& v : voices)
        if (! v.active)
            return v;

    Voice* oldest = &voices[0];

    for (auto& v : voices)
        if (v.startOrder < oldest->startOrder)
            oldest = &v;

    return *oldest;
}

void DrumSynth::noteOn (juce::int64 noteId, int pad, float velocity)
{
    const auto index = juce::jlimit (0, kDrumPads - 1, pad);

    if (! params.pads[(size_t) index].enabled)
        return;

    // The classic hi-hat rule: anything hitting a hat shuts a ringing open hat.
    if (index == (int) DrumPad::closedHat || index == (int) DrumPad::openHat)
        for (auto& v : voices)
            if (v.active && v.pad == (int) DrumPad::openHat && ! v.choked)
            {
                v.choked = true;
                v.choke  = 1.0;
            }

    auto* existing = findVoiceForNote (noteId);
    auto& voice = existing != nullptr ? *existing : findVoiceToUse();

    voice.active    = true;
    voice.noteId    = noteId;
    voice.pad       = index;
    voice.amplitude = juce::jlimit (0.0f, 1.0f, velocity / 127.0f);
    voice.startOrder = ++orderCounter;

    voice.age        = 0.0;
    voice.phase[0]   = 0.0;
    voice.phase[1]   = 0.0;
    voice.metalPhase = 0.0;
    voice.noise      = random.nextDouble() * 2.0 - 1.0;
    voice.choke      = 1.0;
    voice.choked     = false;

    voice.filter.reset();
    voice.filter.setType (modeFor (index));
    voice.appliedCutoff    = 0.0f;
    voice.appliedResonance = 0.0f;
    voice.appliedMode      = -1;
}

void DrumSynth::noteOff (juce::int64)
{
    // Drums are one-shots: they ring out on their own.
}

void DrumSynth::allNotesOff (bool immediately)
{
    for (auto& v : voices)
        if (v.active && immediately)
            v.active = false;
}

bool DrumSynth::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active)
            return true;

    return false;
}

int DrumSynth::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        if (v.active)
            ++count;

    return count;
}

int DrumSynth::getVoicePadForTest (juce::int64 noteId) const
{
    for (const auto& v : voices)
        if (v.active && v.noteId == noteId)
            return v.pad;

    return -1;
}

double DrumSynth::renderVoiceSample (Voice& voice)
{
    const auto index = juce::jlimit (0, kDrumPads - 1, voice.pad);
    const auto& pad = params.pads[(size_t) index];

    const auto decay = juce::jmax (0.01, decaySecondsFor (index, params));
    const auto t = voice.age;
    const auto pitchRatio = std::pow (2.0, ((double) pad.pitch + (double) params.masterPitch)
                                               / 12.0);
    const auto tone = (double) juce::jlimit (0.0f, 1.0f, pad.tone);
    const auto snap = (double) juce::jlimit (0.0f, 1.0f, pad.snap);
    const auto level = (double) juce::jlimit (0.0f, 1.0f, pad.level);

    const auto noiseSample = random.nextDouble() * 2.0 - 1.0;
    voice.noise = noiseSample;

    auto value = 0.0;

    switch ((DrumPad) index)
    {
        case DrumPad::kick:
        {
            // A sine that drops from a click into the body pitch.
            const auto sweep = 1.0 + 2.2 * (1.0 - tone) * std::exp (-t * 38.0);
            const auto hz = 50.0 * pitchRatio * sweep;
            voice.phase[0] += hz / sampleRate;
            value = std::sin (kTwoPi * voice.phase[0]) * std::exp (-t / decay);
            value += snap * 0.5 * std::exp (-t * 320.0) * noiseSample;
            break;
        }

        case DrumPad::snare:
        {
            const auto body = std::sin (kTwoPi * 180.0 * pitchRatio * t)
                                  + 0.7 * std::sin (kTwoPi * 330.0 * pitchRatio * t);
            const auto bodyEnv = std::exp (-t / juce::jmax (0.05, decay * 0.8));
            const auto noiseEnv = std::exp (-t / juce::jmax (0.03, decay * 0.55));
            value = body * bodyEnv * (1.0 - 0.55 * tone) + noiseSample * noiseEnv * (0.5 + tone);
            value += snap * 0.35 * std::exp (-t * 400.0) * noiseSample;
            break;
        }

        case DrumPad::rim:
        {
            voice.phase[0] += 1700.0 * pitchRatio / sampleRate;
            value = std::sin (kTwoPi * voice.phase[0]) * std::exp (-t / decay) * 0.6 + 0.5 * noiseSample * std::exp (-t / juce::jmax (0.01, decay * 0.5));
            value += snap * 0.4 * std::exp (-t * 500.0) * noiseSample;
            break;
        }

        case DrumPad::clap:
        {
            // Three quick bursts then a short body: the classic hand clap.
            auto bursts = 0.0;

            for (int i = 0; i < 3; ++i)
            {
                const auto offset = 0.009 * (double) i;

                if (t >= offset)
                    bursts = juce::jmax (bursts, std::exp (-(t - offset) * 120.0));
            }

            const auto body = std::exp (-t / juce::jmax (0.05, decay));
            value = noiseSample * (bursts * 0.9 + body * 0.25) * (0.6 + 0.6 * tone);
            break;
        }

        case DrumPad::closedHat:
        case DrumPad::openHat:
        case DrumPad::crash:
        case DrumPad::ride:
        {
            // Six inharmonic partials plus noise: the metal of a cymbal.
            auto metal = 0.0;

            for (int i = 0; i < 6; ++i)
                metal += std::sin (kTwoPi * voice.metalPhase * metalRatio (i));

            metal /= 6.0;
            voice.metalPhase += 320.0 * pitchRatio / sampleRate;

            const auto isRide = index == (int) DrumPad::ride;
            const auto isCrash = index == (int) DrumPad::crash;
            const auto metallic = isRide ? 0.85 : (isCrash ? 0.55 : 0.45);
            const auto body = std::exp (-t / decay);

            value = (metal * metallic + noiseSample * (0.5 + 0.5 * tone)) * body;

            // A ride keeps a clear ping on top of the wash.
            if (isRide)
            {
                voice.phase[0] += 1500.0 * pitchRatio / sampleRate;
                value += std::sin (kTwoPi * voice.phase[0]) * std::exp (-t / juce::jmax (0.05, decay * 0.35)) * 0.5;
            }

            value += snap * 0.25 * std::exp (-t * 600.0) * noiseSample;
            break;
        }

        case DrumPad::lowTom:
        case DrumPad::midTom:
        case DrumPad::highTom:
        {
            const auto base = (index == (int) DrumPad::lowTom) ? 100.0
                                  : (index == (int) DrumPad::midTom) ? 150.0 : 220.0;
            const auto sweep = 1.0 + 0.5 * (1.0 - tone) * std::exp (-t * 30.0);
            voice.phase[0] += base * pitchRatio * sweep / sampleRate;
            value = std::sin (kTwoPi * voice.phase[0]) * std::exp (-t / decay);
            value += noiseSample * 0.12 * std::exp (-t / juce::jmax (0.03, decay * 0.4));
            value += snap * 0.3 * std::exp (-t * 400.0) * noiseSample;
            break;
        }

        case DrumPad::cowbell:
        {
            voice.phase[0] += 540.0 * pitchRatio / sampleRate;
            voice.phase[1] += 800.0 * pitchRatio / sampleRate;
            const auto square = [] (double phase)
            {
                return std::fmod (phase, 1.0) < 0.5 ? 1.0 : -1.0;
            };
            value = (square (voice.phase[0]) + square (voice.phase[1])) * 0.5
                        * std::exp (-t / decay);
            value *= (0.7 + 0.5 * tone);
            break;
        }

        case DrumPad::count:
            break;
    }

    if (voice.choked)
        voice.choke *= std::exp (-1.0 / (0.004 * sampleRate));

    voice.age += 1.0 / sampleRate;

    return value * level * voice.choke;
}

void DrumSynth::renderAdding (juce::AudioBuffer<float>& buffer,
                              int startSample,
                              int numSamples,
                              float gainLeft,
                              float gainRight)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    const auto kitTrim = kMasterTrim * juce::jlimit (0.0f, 1.0f, params.gain);
    const auto width = (double) juce::jlimit (0.0f, 1.0f, params.width);

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto index = juce::jlimit (0, kDrumPads - 1, v.pad);
        const auto pan = basePan (index) * width;
        const auto voiceGain = kitTrim * v.amplitude;
        const auto lg = voiceGain * gainLeft * (float) (1.0 - juce::jmax (0.0, pan) * 0.6);
        const auto rg = voiceGain * gainRight * (float) (1.0 + juce::jmin (0.0, pan) * 0.6);

        double cutoff = 0.0, resonance = 0.0;
        filterFor (index, cutoff, resonance);

        if (std::abs ((float) cutoff - v.appliedCutoff) > 0.5f
            || std::abs ((float) resonance - v.appliedResonance) > 1.0e-4f)
        {
            v.filter.setCutoffFrequency ((float) juce::jlimit (20.0, sampleRate * 0.49, cutoff));
            v.filter.setResonance ((float) resonance);
            v.appliedCutoff = (float) cutoff;
            v.appliedResonance = (float) resonance;
        }

        const auto needsFilter = modeFor (index) != juce::dsp::StateVariableTPTFilterType::lowpass
                                     || index == (int) DrumPad::snare;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sample = renderVoiceSample (v);

            if (needsFilter)
                sample = (double) v.filter.processSample (0, (float) sample);

            left[i]  += (float) (sample * (double) lg);
            right[i] += (float) (sample * (double) rg);

            const auto decay = decaySecondsFor (index, params);
            const auto finished = v.choked ? v.choke < 0.001 : v.age > decay * 4.0 + 0.2;

            if (finished)
            {
                v.active = false;
                break;
            }
        }
    }
}

} // namespace hmdaw
