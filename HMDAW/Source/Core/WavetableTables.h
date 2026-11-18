#pragma once

#include "Types.h"
#include "Wavetables.h"

#include <vector>
#include <memory>

namespace hmdaw::wavetabletables
{

/** How many harmonics the editor's harmonic strip shows (and can draw). */
constexpr int kMaxHarmonicBars = 64;

/** Peak a freshly imported / sampled table is normalised to. */
constexpr float kNormalisePeak = 0.98f;

/** One sample of one frame, as -1 .. +1 (the stored data is 16-bit). */
float sampleAt (const CustomWavetable& table, int frame, int index) noexcept;

/** Writes one sample of one frame (clamped into range and quantised to 16-bit). */
void setSampleAt (CustomWavetable& table, int frame, int index, float value);

/** An all-zero table: `frameCount` frames, each `kCustomWavetableFrameSize` long. */
CustomWavetable makeEmpty (juce::int64 id, const juce::String& name, int frameCount);

/** Turns a mono audio buffer into a table: the buffer is cut into `frameCount`
    equal slices, each slice is averaged down to `kCustomWavetableFrameSize` samples
    (a box average, i.e. a crude low pass before the mipmap levels band limit what
    is left), every frame has its own DC removed, and the whole table is then
    scaled by one gain so the loudest frame peaks at `kNormalisePeak` - which keeps
    the loudness *between* frames, i.e. a fade in the source stays a fade. */
CustomWavetable makeFromAudio (const float* mono, juce::int64 numSamples,
                               int frameCount, juce::int64 id, const juce::String& name);

/** Builds a table from frames of any length (each is box-averaged/resampled to the
    standard frame size). No normalising: the caller decides. */
CustomWavetable makeFromFrames (const std::vector<std::vector<float>>& frames,
                                juce::int64 id, const juce::String& name);

/** A starting point taken from the built-in bank's full-bandwidth frames. */
CustomWavetable copyBuiltIn (int builtInIndex, int frameCount,
                             juce::int64 id, const juce::String& name);

/** Three-point smoothing pass over one frame. */
void smoothFrame (CustomWavetable& table, int frame);

/** Silences one frame. */
void clearFrame (CustomWavetable& table, int frame);

/** Scales the whole table by one gain so its loudest sample peaks at 0.98. */
void normalise (CustomWavetable& table);

/** The amplitude of harmonics 1..count in one frame (FFT analysis), 0..~1. */
std::vector<float> analyseHarmonics (const CustomWavetable& table, int frame, int count);

/** Harmonic analysis with phase, in radians (-pi..pi). */
struct HarmonicSpectrum
{
    std::vector<float> amplitudes;
    std::vector<float> phases;
};

HarmonicSpectrum analyseHarmonicsWithPhase (const CustomWavetable& table, int frame, int count);

/** Rewrites one frame as the sum of its harmonics: `sum a_n * sin(2*pi*n*i/N)`.
    Anything past +/-1 is clamped; the result is not normalised, because the level
    the user drew is part of the sound. */
void synthesiseFromHarmonics (CustomWavetable& table, int frame,
                              const std::vector<float>& amounts);

/** The same, with one phase per harmonic (radians). */
void synthesiseFromHarmonics (CustomWavetable& table, int frame,
                              const std::vector<float>& amounts,
                              const std::vector<float>& phases);

/** A hash of the PCM only: two tables with the same frames hash the same. Used to
    tell "the mipmaps I built are still current" from "rebuild them". */
std::size_t dataHash (const CustomWavetable& table) noexcept;

/** True when two tables hold exactly the same frames (names and ids ignored): that
    is how a preset being loaded re-uses a table the project already has. */
bool sameData (const CustomWavetable& a, const CustomWavetable& b) noexcept;

/** Rounds a table off: frame count into 1..64, data length to frames * frame size
    (a hand-edited file must not be able to crash the engine). */
void clampToShape (CustomWavetable& table);

/** The play-ready (mipmapped) version of a custom table. */
wavetable::Wavetable buildCustomTable (const CustomWavetable& table);

/** The play-ready table for one of the project's tables, rebuilt only when its PCM
    actually changes (drawing a table fires a rebuild on every gesture, and the
    mipmaps are far too expensive to build each time). Message thread only. */
std::shared_ptr<const wavetable::Wavetable> playableTable (const CustomWavetable& table);

/** The same, looked up by the id an oscillator carries: null for 0 (a built-in
    table) and for an id the project no longer has. */
std::shared_ptr<const wavetable::Wavetable> playableTableFor (const Project& project,
                                                             juce::int64 customTableId);

/** Drops cached tables the project no longer has, so a long session cannot pile
    them up in memory. */
void pruneTableCache (const Project& project);

/** How many bytes the stored PCM takes (the editor shows the size). */
int storedBytes (const CustomWavetable& table);

} // namespace hmdaw::wavetabletables
