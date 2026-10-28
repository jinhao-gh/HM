#include "HybridFxChain.h"

#include <cmath>

namespace nidaw
{

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    float onePoleCoefficient (float milliseconds, double sampleRate) noexcept
    {
        const auto seconds = juce::jmax (0.0001f, milliseconds * 0.001f);
        return 1.0f - std::exp (-1.0f / (seconds * (float) sampleRate));
    }

    double fold01 (double value) noexcept
    {
        return value - std::floor (value);
    }
}

int hybridFxTypeCount() noexcept
{
    return 9;
}

juce::String hybridFxTypeName (HybridFxType type)
{
    switch (type)
    {
        case HybridFxType::none:       return NI_TXT ("关闭");
        case HybridFxType::distortion: return NI_TXT ("失真");
        case HybridFxType::bitcrush:   return NI_TXT ("比特压碎");
        case HybridFxType::chorus:     return NI_TXT ("合唱");
        case HybridFxType::phaser:     return NI_TXT ("相位");
        case HybridFxType::delay:      return NI_TXT ("延迟");
        case HybridFxType::reverb:     return NI_TXT ("混响");
        case HybridFxType::eq:         return NI_TXT ("三段 EQ");
        case HybridFxType::compressor: return NI_TXT ("压缩");
    }

    return NI_TXT ("关闭");
}

juce::String hybridFxParam1Name (HybridFxType type)
{
    switch (type)
    {
        case HybridFxType::distortion: return NI_TXT ("驱动");
        case HybridFxType::bitcrush:   return NI_TXT ("位深");
        case HybridFxType::chorus:     return NI_TXT ("速率");
        case HybridFxType::phaser:     return NI_TXT ("速率");
        case HybridFxType::delay:      return NI_TXT ("时间");
        case HybridFxType::reverb:     return NI_TXT ("空间");
        case HybridFxType::eq:         return NI_TXT ("中频增益");
        case HybridFxType::compressor: return NI_TXT ("阈值");
        case HybridFxType::none:       break;
    }

    return NI_TXT ("参数 1");
}

juce::String hybridFxParam2Name (HybridFxType type)
{
    switch (type)
    {
        case HybridFxType::distortion: return NI_TXT ("音色");
        case HybridFxType::bitcrush:   return NI_TXT ("降采样");
        case HybridFxType::chorus:     return NI_TXT ("深度");
        case HybridFxType::phaser:     return NI_TXT ("深度");
        case HybridFxType::delay:      return NI_TXT ("反馈");
        case HybridFxType::reverb:     return NI_TXT ("阻尼");
        case HybridFxType::eq:         return NI_TXT ("倾斜");
        case HybridFxType::compressor: return NI_TXT ("压缩比");
        case HybridFxType::none:       break;
    }

    return NI_TXT ("参数 2");
}

void HybridFxChain::Biquad::reset() noexcept
{
    z1.fill (0.0f);
    z2.fill (0.0f);
}

float HybridFxChain::Biquad::process (int channel, float input) noexcept
{
    const auto ch = (size_t) juce::jlimit (0, 1, channel);
    const auto output = b0 * input + z1[ch];
    z1[ch] = b1 * input - a1 * output + z2[ch];
    z2[ch] = b2 * input - a2 * output;
    return output;
}

void HybridFxChain::Biquad::setLowShelf (double sr, float frequency, float q,
                                         float gainDb) noexcept
{
    const auto A = std::pow (10.0f, gainDb / 40.0f);
    const auto w = 2.0f * kPi * juce::jlimit (10.0f, 20000.0f, frequency) / (float) sr;
    const auto cosw = std::cos (w);
    const auto sinw = std::sin (w);
    const auto alpha = sinw / (2.0f * juce::jmax (0.1f, q));
    const auto sqrtA = std::sqrt (A);
    const auto a0 = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;

    b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw) / a0;
    b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw) / a0;
    a2 = ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
}

void HybridFxChain::Biquad::setHighShelf (double sr, float frequency, float q,
                                          float gainDb) noexcept
{
    const auto A = std::pow (10.0f, gainDb / 40.0f);
    const auto w = 2.0f * kPi * juce::jlimit (10.0f, 20000.0f, frequency) / (float) sr;
    const auto cosw = std::cos (w);
    const auto sinw = std::sin (w);
    const auto alpha = sinw / (2.0f * juce::jmax (0.1f, q));
    const auto sqrtA = std::sqrt (A);
    const auto a0 = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;

    b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw) / a0;
    b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw) / a0;
    a2 = ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
}

void HybridFxChain::Biquad::setPeak (double sr, float frequency, float q,
                                     float gainDb) noexcept
{
    const auto A = std::pow (10.0f, gainDb / 40.0f);
    const auto w = 2.0f * kPi * juce::jlimit (10.0f, 20000.0f, frequency) / (float) sr;
    const auto cosw = std::cos (w);
    const auto sinw = std::sin (w);
    const auto alpha = sinw / (2.0f * juce::jmax (0.1f, q));
    const auto a0 = 1.0f + alpha / A;

    b0 = (1.0f + alpha * A) / a0;
    b1 = (-2.0f * cosw) / a0;
    b2 = (1.0f - alpha * A) / a0;
    a1 = (-2.0f * cosw) / a0;
    a2 = (1.0f - alpha / A) / a0;
}

HybridFxChain::HybridFxChain()
{
    dryBuffer.setSize (2, 2048, false, true, true);
}

void HybridFxChain::prepare (double newSampleRate, int newMaximumBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maximumBlockSize = juce::jlimit (32, 16384, newMaximumBlockSize);
    dryBuffer.setSize (2, maximumBlockSize, false, true, true);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maximumBlockSize;
    spec.numChannels = 2;

    for (auto& state : states)
        state.reset (sampleRate, maximumBlockSize);
}

void HybridFxChain::SlotState::reset (double sr, int maxBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32) maxBlock;
    spec.numChannels = 2;

    for (auto& line : chorusDelay)
    {
        line.setMaximumDelayInSamples ((int) std::ceil (sr * 0.04) + 2);
        line.prepare (spec);
    }

    delay.setMaximumDelayInSamples ((int) std::ceil (sr * 1.25));
    delay.prepare (spec);
    delay.setDelay ((float) (sr * 0.25));

    reverb.prepare (spec);

    eqBands[0].setLowShelf (sr, 220.0f, 0.707f, 0.0f);
    eqBands[1].setPeak (sr, 1100.0f, 0.8f, 0.0f);
    eqBands[2].setHighShelf (sr, 4200.0f, 0.707f, 0.0f);

    resetStateOnly();
}

void HybridFxChain::SlotState::resetStateOnly()
{
    for (auto& line : chorusDelay)
        line.reset();

    chorusPhase = 0.0;
    phaserPhase = 0.0;
    phaserFeedback.fill (0.0f);

    for (auto& channel : phaserStages)
        for (auto& stage : channel)
            stage.reset();
    delay.reset();
    reverb.reset();
    toneState.fill (0.0f);
    crushHold.fill (0.0f);
    crushCounter.fill (0);
    compressorEnvelope.fill (0.0f);

    for (auto& band : eqBands)
        band.reset();
}

void HybridFxChain::reset()
{
    for (auto& state : states)
        state.resetStateOnly();

    tailSamplesRemaining = 0;
}

void HybridFxChain::setParams (const std::array<HybridFxSlot, 4>& newSlots)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& slot = newSlots[(size_t) i];
        auto& state = states[(size_t) i];
        const auto typeChanged = state.type != slot.type;
        const auto becameEnabled = slot.enabled && ! state.wasEnabled;

        if (typeChanged || becameEnabled)
            state.resetStateOnly();

        state.type = slot.type;
        state.wasEnabled = slot.enabled;
    }

    slots = newSlots;
}

bool HybridFxChain::hasAnyEnabledSlot() const noexcept
{
    for (const auto& slot : slots)
        if (slot.enabled && ! slot.bypass && slot.type != HybridFxType::none
            && slot.mix > 0.0001f)
            return true;

    return false;
}

void HybridFxChain::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() < 1)
        return;

    for (int i = 0; i < 4; ++i)
        processSlot (i, buffer, numSamples);

    if (hasAnyEnabledSlot()
        && buffer.getMagnitude (0, 0, numSamples) > 1.0e-5f)
        tailSamplesRemaining = (int) std::ceil (sampleRate * 1.0);
    else
        tailSamplesRemaining = juce::jmax (0, tailSamplesRemaining - numSamples);
}

void HybridFxChain::processSlot (int index, juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto& slot = slots[(size_t) index];

    if (! slot.enabled || slot.bypass || slot.type == HybridFxType::none
        || slot.mix <= 0.0001f)
        return;

    const auto channels = juce::jmin (2, buffer.getNumChannels());
    const auto samples = juce::jmin (numSamples, buffer.getNumSamples(),
                                     dryBuffer.getNumSamples());

    for (int ch = 0; ch < channels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, samples);

    switch (slot.type)
    {
        case HybridFxType::distortion: processDistortion (index, slot, buffer, samples); break;
        case HybridFxType::bitcrush:   processBitcrush (index, slot, buffer, samples); break;
        case HybridFxType::chorus:     processChorus (index, slot, buffer, samples); break;
        case HybridFxType::phaser:     processPhaser (index, slot, buffer, samples); break;
        case HybridFxType::delay:      processDelay (index, slot, buffer, samples); break;
        case HybridFxType::reverb:     processReverb (index, slot, buffer, samples); break;
        case HybridFxType::eq:         processEq (index, slot, buffer, samples); break;
        case HybridFxType::compressor: processCompressor (index, slot, buffer, samples); break;
        case HybridFxType::none:       break;
    }

    const auto wet = juce::jlimit (0.0f, 1.0f, slot.mix);
    const auto dry = 1.0f - wet;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* out = buffer.getWritePointer (ch);
        const auto* in = dryBuffer.getReadPointer (ch);

        for (int i = 0; i < samples; ++i)
            out[i] = in[i] * dry + out[i] * wet;
    }
}

void HybridFxChain::processDistortion (int index, const HybridFxSlot& slot,
                                       juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    const auto drive = 1.0f + juce::jlimit (0.0f, 1.0f, slot.param1) * 28.0f;
    const auto tone = juce::jlimit (0.02f, 1.0f, slot.param2);
    const auto toneCoeff = onePoleCoefficient (2.0f + (1.0f - tone) * 28.0f, sampleRate);
    const auto compensation = 1.0f / (1.0f + std::log (drive) * 0.65f);
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& toneState = state.toneState[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto shaped = std::tanh (data[i] * drive) * compensation;
            toneState += toneCoeff * (shaped - toneState);
            data[i] = toneState;
        }
    }
}

void HybridFxChain::processBitcrush (int index, const HybridFxSlot& slot,
                                     juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    const auto bits = juce::jlimit (1.0f, 16.0f, 1.0f + slot.param1 * 15.0f);
    const auto step = 2.0f / std::pow (2.0f, bits);
    const auto downsample = juce::jlimit (1, 64, 1 + (int) std::round (slot.param2 * 63.0f));
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& hold = state.crushHold[(size_t) ch];

        auto& counter = state.crushCounter[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            if (counter <= 0)
            {
                hold = std::round (data[i] / step) * step;
                counter = downsample;
            }

            data[i] = hold;
            --counter;
        }
    }
}

void HybridFxChain::processChorus (int index, const HybridFxSlot& slot,
                                   juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    const auto rate = 0.1f + juce::jlimit (0.0f, 1.0f, slot.param1) * 1.9f;
    const auto depthMs = juce::jlimit (0.0f, 1.0f, slot.param2) * 8.0f;
    const auto increment = (double) rate / sampleRate;
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        state.chorusPhase = fold01 (state.chorusPhase + increment);
        const auto leftLfo = std::sin (juce::MathConstants<double>::twoPi
                                       * state.chorusPhase);
        const auto rightLfo = std::sin (juce::MathConstants<double>::twoPi
                                            * state.chorusPhase
                                        + juce::MathConstants<double>::halfPi);
        const auto leftDelay = (12.0 + depthMs * leftLfo) * sampleRate / 1000.0;
        const auto rightDelay = (15.0 + depthMs * rightLfo) * sampleRate / 1000.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& line = state.chorusDelay[(size_t) ch];
            const auto delaySamples = (float) (ch == 0 ? leftDelay : rightDelay);
            const auto delayed = line.popSample (0, delaySamples);
            line.pushSample (0, data[i]);
            data[i] = delayed;
        }
    }
}

void HybridFxChain::processPhaser (int index, const HybridFxSlot& slot,
                                   juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    const auto rate = 0.1f + juce::jlimit (0.0f, 1.0f, slot.param1) * 1.9f;
    const auto depth = juce::jlimit (0.0f, 1.0f, slot.param2);
    const auto increment = (double) rate / sampleRate;
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        state.phaserPhase = fold01 (state.phaserPhase + increment);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto phase = state.phaserPhase
                                   + (ch == 0 ? 0.0 : 0.25);
            const auto sweep = 0.5 + 0.5 * std::sin (
                juce::MathConstants<double>::twoPi * phase);
            const auto frequency = 200.0 + sweep * depth * 1800.0;
            const auto tangent = std::tan (juce::MathConstants<double>::pi
                                           * frequency / sampleRate);
            const auto a = (float) ((1.0 - tangent) / (1.0 + tangent));
            auto* data = buffer.getWritePointer (ch);
            auto sample = data[i] + state.phaserFeedback[(size_t) ch] * 0.25f;

            for (auto& stage : state.phaserStages[(size_t) ch])
            {
                stage.a = a;
                sample = stage.process (sample);
            }

            state.phaserFeedback[(size_t) ch] = sample;
            data[i] = sample;
        }
    }
}

void HybridFxChain::processDelay (int index, const HybridFxSlot& slot,
                                  juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& delay = states[(size_t) index].delay;
    const auto delaySeconds = 0.025f + juce::jlimit (0.0f, 1.0f, slot.param1) * 0.875f;
    const auto delaySamples = (float) (delaySeconds * sampleRate);
    const auto feedback = juce::jlimit (0.0f, 0.88f, slot.param2 * 0.88f);
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto delayed = delay.popSample (ch, delaySamples);
            delay.pushSample (ch, data[i] + delayed * feedback);
            data[i] = delayed;
        }
    }
}

void HybridFxChain::processReverb (int index, const HybridFxSlot& slot,
                                   juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& reverb = states[(size_t) index].reverb;
    juce::Reverb::Parameters parameters;
    parameters.roomSize = juce::jlimit (0.0f, 1.0f, slot.param1);
    parameters.damping = juce::jlimit (0.0f, 1.0f, slot.param2);
    parameters.wetLevel = 1.0f;
    parameters.dryLevel = 0.0f;
    parameters.width = 1.0f;
    parameters.freezeMode = 0.0f;
    reverb.setParameters (parameters);

    juce::dsp::AudioBlock<float> block (buffer);
    block = block.getSubBlock (0, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);
}

void HybridFxChain::updateEqCoefficients (SlotState& state, const HybridFxSlot& slot)
{
    const auto midGain = (slot.param1 * 2.0f - 1.0f) * 12.0f;
    const auto tilt = (slot.param2 * 2.0f - 1.0f) * 9.0f;
    state.eqBands[0].setLowShelf (sampleRate, 220.0f, 0.707f, -tilt);
    state.eqBands[1].setPeak (sampleRate, 1100.0f, 0.8f, midGain);
    state.eqBands[2].setHighShelf (sampleRate, 4200.0f, 0.707f, tilt);
}

void HybridFxChain::processEq (int index, const HybridFxSlot& slot,
                               juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    updateEqCoefficients (state, slot);
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            auto sample = data[i];

            for (auto& band : state.eqBands)
                sample = band.process (ch, sample);

            data[i] = sample;
        }
    }
}

void HybridFxChain::processCompressor (int index, const HybridFxSlot& slot,
                                       juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto& state = states[(size_t) index];
    const auto thresholdDb = -40.0f + juce::jlimit (0.0f, 1.0f, slot.param1) * 40.0f;
    const auto ratio = 1.0f + juce::jlimit (0.0f, 1.0f, slot.param2) * 19.0f;
    const auto attack = onePoleCoefficient (4.0f, sampleRate);
    const auto release = onePoleCoefficient (120.0f, sampleRate);
    const auto channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& envelope = state.compressorEnvelope[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto level = std::abs (data[i]);
            const auto coefficient = level > envelope ? attack : release;
            envelope += coefficient * (level - envelope);

            const auto envelopeDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, envelope));
            const auto over = envelopeDb - thresholdDb;
            auto gainDb = 0.0f;

            if (over > 0.0f)
                gainDb = -over * (1.0f - 1.0f / ratio);

            data[i] *= dbToGain (gainDb);
        }
    }
}

} // namespace nidaw
