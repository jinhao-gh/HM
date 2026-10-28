#pragma once

#include "Types.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace nidaw::wavetable
{

/** Frame length in samples. 2048 gives 1024 harmonics before the mipmaps start. */
constexpr int kFrameSize    = 2048;
constexpr int kFrameCount   = 8;
constexpr int kMaxHarmonics = kFrameSize / 2;
/** 1024, 512, 256 ... 1 harmonic: one level per octave of brightness. */
constexpr int kLevelCount   = 11;

/** One table: `kFrameCount` frames, each stored at `kLevelCount` bandwidths.

    levels[level] holds every frame back to back, so frame `f` of level `L` starts
    at `levels[L][f * kFrameSize]`. Level 0 is the full-bandwidth version; level k
    keeps at most `kMaxHarmonics >> k` harmonics, which is what keeps the top
    octaves from aliasing.
*/
struct Wavetable
{
    juce::String name;
    int frameCount = 0;
    int levelCount = 0;
    std::vector<std::vector<float>> levels;

    const float* frameData (int level, int frame) const
    {
        jassert (level >= 0 && level < (int) levels.size());
        jassert (frame >= 0 && frame < frameCount);
        return levels[(size_t) level].data() + (size_t) frame * (size_t) kFrameSize;
    }
};

/** How many tables the built-in bank holds. */
int builtInTableCount();

/** The name of a built-in table (index clamped). */
juce::String tableName (int index);

/** A built-in table (index clamped). The reference stays valid for the life of
    the process. */
const Wavetable& getTable (int index);

/** Builds the bank if it has not been built yet. Call it from the message thread
    (the schedule builder does) so the audio thread never has to allocate. */
void ensureBuilt();

/** Builds a play-ready table from `frameCount` frames of `kFrameSize` samples each
    (`frames` holds `frameCount * kFrameSize` values, frames back to back).

    Every level is band limited by truncating that frame's spectrum, which is what
    keeps the top octaves from aliasing. `normalise` scales the whole table by one
    gain taken from the full-bandwidth frames: the built-in bank wants that, a
    table the user drew or imported keeps the levels it was given (the editor has
    its own 归一化 button for when they do want it).
*/
Wavetable buildTableFromFrames (const std::vector<float>& frames, int frameCount,
                                const juce::String& name, bool normalise);

/** The brightest level whose harmonics still fit under Nyquist for this pitch. */
inline int levelForFrequency (double frequencyHz, double sampleRate) noexcept
{
    const auto allowed = sampleRate * 0.5 / juce::jmax (1.0, frequencyHz);

    for (int level = 0; level < kLevelCount; ++level)
        if ((double) juce::jmax (1, kMaxHarmonics >> level) <= allowed)
            return level;

    return kLevelCount - 1;
}

/** The ratio a wavetable oscillator runs at: octave, semitone and detune folded
    in (the same rule the subtractive oscillator uses). */
inline double oscillatorRatio (const WavetableOsc& osc) noexcept
{
    const auto semitones = (double) osc.octave * 12.0 + (double) osc.semitone
                               + (double) osc.detuneCents / 100.0;
    return std::pow (2.0, semitones / 12.0);
}

} // namespace nidaw::wavetable
