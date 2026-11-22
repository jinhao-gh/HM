#include "GmSynth.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "../../external/tsf/tsf.h"

#include <cmath>

namespace hmdaw
{

namespace
{
constexpr int kChannels = 64;
constexpr int kMaxVoices = 128;

double frequencyToMidi (double hz) noexcept
{
    return 69.0 + 12.0 * std::log2 (juce::jmax (1.0e-6, hz) / 440.0);
}
} // namespace

GmSynth::GmSynth() = default;

GmSynth::~GmSynth()
{
    if (instance != nullptr)
        tsf_close (instance);
}

void GmSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    // AudioEngine's maximum block is 16384; reserve the stereo scratch once so
    // the audio callback never allocates on the first GM note.
    if (scratch.size() < 2 * 16384)
        scratch.resize (2 * 16384);

    if (instance != nullptr)
        tsf_set_output (instance, TSF_STEREO_INTERLEAVED, (int) sampleRate);
}

void GmSynth::setSoundFont (std::shared_ptr<const GmSoundFont> newFont)
{
    if (newFont == soundFont)
        return;

    allNotesOff();

    if (instance != nullptr)
    {
        tsf_close (instance);
        instance = nullptr;
    }

    soundFont = std::move (newFont);
    lastError.clear();

    if (soundFont == nullptr || ! soundFont->isUsable())
        lastError = soundFont == nullptr ? "没有加载 GM 音色库" : "GM 音色库无效";
}

void GmSynth::ensureInstance()
{
    if (instance != nullptr || soundFont == nullptr || ! soundFont->isUsable())
        return;

    instance = tsf_copy (soundFont->raw());

    if (instance == nullptr)
    {
        lastError = "无法创建 GM 合成器实例";
        return;
    }

    tsf_set_output (instance, TSF_STEREO_INTERLEAVED, (int) sampleRate);
    tsf_set_max_voices (instance, kMaxVoices);
    configureChannels();
}

void GmSynth::setParams (const GmParams& newParams)
{
    ensureInstance();

    const auto programChanged = newParams.program != params.program
                             || newParams.drums != params.drums;
    const auto rangeChanged = newParams.pitchBendRange != params.pitchBendRange;
    params = newParams;

    if (instance == nullptr || soundFont == nullptr)
        return;

    if (programChanged)
    {
        allNotesOff();
        configureChannels();
    }
    else if (rangeChanged)
    {
        configureChannels();
        setPitchBend (pitchBendSemitones);
    }
}

void GmSynth::configureChannels()
{
    if (instance == nullptr || soundFont == nullptr)
        return;

    const auto preset = params.drums ? soundFont->drumPreset()
                                     : soundFont->presetForProgram (params.program);

    if (preset < 0)
    {
        lastError = "音色库中没有这个 GM 音色";
        return;
    }

    lastError.clear();

    for (int channel = 0; channel < kChannels; ++channel)
    {
        tsf_channel_set_presetindex (instance, channel, preset);
        tsf_channel_set_pitchrange (instance, channel, juce::jlimit (0.0f, 12.0f,
                                                                     (float) params.pitchBendRange));
        tsf_channel_set_volume (instance, channel, 1.0f);
        tsf_channel_set_pan (instance, channel, 0.5f);
        tsf_channel_set_tuning (instance, channel, 0.0f);
    }
}

void GmSynth::releaseSlot (int slotIndex, bool fast)
{
    if (slotIndex < 0 || slotIndex >= (int) slots.size())
        return;

    auto& slot = slots[(size_t) slotIndex];

    if (! slot.active)
        return;

    if (instance != nullptr && slot.channel >= 0)
    {
        if (fast)
            tsf_channel_note_off_all (instance, slot.channel);
        else
            tsf_channel_note_off (instance, slot.channel, slot.key);
    }

    slot.active = false;
}

void GmSynth::noteOn (juce::int64 noteId, double frequencyHz, float velocity)
{
    if (instance == nullptr)
        return;

    auto midi = frequencyToMidi (frequencyHz);

    if (params.drums)
    {
        const auto note = (int) std::lround (midi);
        if (note < 35 || note > 81)
            return;
    }

    const auto key = juce::jlimit (0, 127, (int) std::lround (midi));

    // One slot owns one TSF channel. Find a free slot, or steal the oldest one.
    int freeSlot = -1;
    int oldestSlot = -1;
    int activeCount = 0;

    for (int i = 0; i < (int) slots.size(); ++i)
    {
        if (slots[(size_t) i].active)
        {
            ++activeCount;

            if (oldestSlot < 0 || ages[(size_t) i] < ages[(size_t) oldestSlot])
                oldestSlot = i;
        }
        else if (freeSlot < 0)
        {
            freeSlot = i;
        }
    }

    const auto limit = juce::jlimit (1, (int) slots.size(), params.polyphony);
    int slotIndex = freeSlot;

    if (slotIndex < 0 || activeCount >= limit)
        slotIndex = oldestSlot >= 0 ? oldestSlot : 0;

    // Stop whatever was still ringing on that channel before reusing it.
    releaseSlot (slotIndex, true);

    auto& slot = slots[(size_t) slotIndex];
    const auto channel = slotIndex;

    slot.id = noteId;
    slot.active = true;
    slot.channel = channel;
    slot.key = key;
    slot.midi = midi;
    ages[(size_t) slotIndex] = ++ageCounter;

    tsf_channel_set_tuning (instance, channel,
                            (float) (midi - key) + pitchBendSemitones);
    tsf_channel_note_on (instance, channel, key,
                         juce::jlimit (0.0f, 1.0f, velocity / 127.0f));
}

void GmSynth::noteOff (juce::int64 noteId)
{
    for (int i = 0; i < (int) slots.size(); ++i)
        if (slots[(size_t) i].active && slots[(size_t) i].id == noteId)
        {
            releaseSlot (i, false);
            return;
        }
}

void GmSynth::setNoteFrequency (juce::int64 noteId, double frequencyHz)
{
    for (auto& slot : slots)
        if (slot.active && slot.id == noteId)
        {
            const auto midi = frequencyToMidi (frequencyHz);
            slot.midi = midi;
            tsf_channel_set_tuning (instance, slot.channel,
                                    (float) (midi - slot.key) + pitchBendSemitones);
            return;
        }
}

void GmSynth::setPitchBend (float semitones)
{
    pitchBendSemitones = semitones;

    if (instance == nullptr)
        return;

    for (auto& slot : slots)
        if (slot.active)
            tsf_channel_set_tuning (instance, slot.channel,
                                    (float) (slot.midi - slot.key) + pitchBendSemitones);
}

void GmSynth::allNotesOff()
{
    if (instance != nullptr)
        for (int channel = 0; channel < kChannels; ++channel)
            tsf_channel_note_off_all (instance, channel);

    for (auto& slot : slots)
        slot.active = false;
}

int GmSynth::getActiveVoiceCount() const noexcept
{
    return instance != nullptr ? tsf_active_voice_count (instance) : 0;
}

void GmSynth::renderAdding (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (instance == nullptr || numSamples <= 0)
        return;

    if (tsf_active_voice_count (instance) == 0)
        return;

    const auto needed = (size_t) numSamples * 2;
    if (scratch.size() < needed)
        scratch.resize (needed);

    std::fill (scratch.begin(), scratch.begin() + (std::ptrdiff_t) needed, 0.0f);
    tsf_render_float (instance, scratch.data(), numSamples, 0);

    const auto gain = juce::jlimit (0.0f, 2.0f, params.volume);
    const auto pan = juce::jlimit (-1.0f, 1.0f, params.pan);
    const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

    auto* left = output.getWritePointer (0, startSample);
    auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample)
                                               : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] += scratch[(size_t) i * 2] * leftGain;

        if (right != nullptr)
            right[i] += scratch[(size_t) i * 2 + 1] * rightGain;
    }
}

int GmSynth::getNoteKeyForTest (juce::int64 noteId) const noexcept
{
    for (const auto& slot : slots)
        if (slot.active && slot.id == noteId)
            return slot.key;
    return -1;
}

float GmSynth::getNoteTuningForTest (juce::int64 noteId) const noexcept
{
    for (const auto& slot : slots)
        if (slot.active && slot.id == noteId)
            return (float) (slot.midi - slot.key) + pitchBendSemitones;
    return 0.0f;
}

} // namespace hmdaw
