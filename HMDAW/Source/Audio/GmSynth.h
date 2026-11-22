#pragma once

#include "../Core/Types.h"
#include "GmSoundFont.h"

#include <array>
#include <memory>
#include <vector>

struct tsf;

namespace hmdaw
{

/** A single General MIDI part: one program (or the GM drum kit) per track.

    Every sounding note owns one TSF channel, so a per-note pitch bend or a
    frequency update for a pitch curve cannot drag the other notes with it.
*/
class GmSynth
{
public:
    GmSynth();
    ~GmSynth();

    void prepare (double newSampleRate);
    void setSoundFont (std::shared_ptr<const GmSoundFont> newFont);
    void setParams (const GmParams& newParams);
    const GmParams& getParams() const noexcept { return params; }
    bool isActive() const noexcept { return getActiveVoiceCount() > 0; }

    void noteOn (juce::int64 noteId, double frequencyHz, float velocity);
    void noteOff (juce::int64 noteId);
    void setNoteFrequency (juce::int64 noteId, double frequencyHz);
    void setPitchBend (float semitones);
    void allNotesOff();

    void renderAdding (juce::AudioBuffer<float>& output, int startSample, int numSamples);

    int getActiveVoiceCount() const noexcept;
    bool hasSoundFont() const noexcept { return instance != nullptr; }
    const juce::String& getLastError() const noexcept { return lastError; }

    /** Test/debug access: the MIDI note and tuning currently assigned to a note. */
    int getNoteKeyForTest (juce::int64 noteId) const noexcept;
    float getNoteTuningForTest (juce::int64 noteId) const noexcept;

private:
    struct NoteSlot
    {
        juce::int64 id = 0;
        bool active = false;
        int channel = -1;
        int key = 0;
        double midi = 0.0;
    };

    void ensureInstance();
    void configureChannels();
    void releaseSlot (int slot, bool fast);

    tsf* instance = nullptr;
    std::shared_ptr<const GmSoundFont> soundFont;
    GmParams params;
    double sampleRate = 48000.0;
    float pitchBendSemitones = 0.0f;
    juce::String lastError;

    std::array<NoteSlot, 64> slots {};
    std::vector<float> scratch;
    juce::uint32 ageCounter = 0;
    std::array<juce::uint32, 64> ages {};
};

} // namespace hmdaw
