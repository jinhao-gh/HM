#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "MidiInputMap.h"
#include "MidiArp.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

/** All user facing text is Chinese and stored as UTF-8. juce::String(const char*)
    asserts on bytes above 127, so every literal goes through this macro. */
#define NI_TXT(s) juce::String::fromUTF8 (s)

namespace nidaw
{

constexpr int kTicksPerQuarter   = 960;   // PPQ
constexpr int kMaxTracks         = 64;    // hard limit for the audio engine
constexpr int kVoicesPerTrack    = 16;    // polyphony per instrument
constexpr int kMaxReferenceLines = 512;   // safety cap when generating octave families

constexpr double kMinVisibleHz = 8.0;
constexpr double kMaxVisibleHz = 20000.0;

/** Size of the fixed colour palette shared by tracks and reference dots. */
constexpr int kReferencePaletteSize = 8;
/** Default palette index for the reference group's two frame dots. */
constexpr int kFrameDotColourIndex = 7;

/** Default length of a new clip: four bars of 4:4. */
constexpr juce::int64 kDefaultClipTicks = (juce::int64) kTicksPerQuarter * 16;
/** A clip may never be shorter than this. */
constexpr juce::int64 kMinClipTicks = (juce::int64) kTicksPerQuarter;

/** The size of the FM engine: six operators and two banks of 32 algorithms.

    1..32 are HM's own routings (the ones every project written before the DX7
    work uses); 33..64 are the Yamaha DX7's 32 factory routings, added so an
    imported DX7 voice can use its real topology instead of an approximation.
*/
constexpr int kFmOperators      = 6;
constexpr int kFmAlgorithmCount = 64;

/** Which bank an algorithm index belongs to. */
constexpr int kFmDx7AlgorithmBase = 32;

/** How an operator's frequency follows the note. */
enum class FmOscMode
{
    ratio = 0,   // frequency = note x ratio
    fixed = 1    // a fixed frequency, ignoring the note
};

/** The four keyboard level scaling curves the DX7 offers. */
enum class FmScaleCurve
{
    negLin = 0,  // -LIN: quieter away from the breakpoint
    negExp = 1,  // -EXP: quieter, curving down
    posExp = 2,  // +EXP: louder, curving up
    posLin = 3   // +LIN: louder away from the breakpoint
};

/** One FM operator, in the DX7 idiom.

    All six operators of a voice run at once: each has a sine oscillator whose
    frequency is the note's frequency times `ratio` (or a fixed frequency), its
    own four-stage RATE/LEVEL envelope, and an output level. Where that output
    goes depends on the algorithm - to the track's audio output (a carrier) or
    into another operator's phase (a modulator).

    Rates and levels are 0..99 like the original: for a rate, larger is faster.
*/
struct FmOperator
{
    FmOscMode mode     = FmOscMode::ratio;
    float ratio       = 1.0f;    // 0.5 .. 32 times the note frequency
    float fixedHz     = 1000.0f; // used when mode == fixed
    float detune      = 0.0f;    // -1 .. +1, a few cents either way
    int   outputLevel = 99;      // 0..99; on a modulator this is the FM depth
    int   velSens     = 0;       // 0..7, how much velocity pulls this one down
    int   rateScale   = 0;       // 0..7, shortens the envelope for higher notes
    int   rate[4]     = { 99, 70, 60, 60 };   // R1..R4
    int   level[4]    = { 99, 99, 99, 0 };    // L1..L4; L3 is the sustain level

    /** Keyboard level scaling (DX7): the level falls (or rises) away from the
        breakpoint, with an independent curve and depth on either side. */
    int          breakPoint = 48;                       // MIDI note, A-1..C8
    FmScaleCurve leftCurve  = FmScaleCurve::negLin;
    FmScaleCurve rightCurve = FmScaleCurve::negLin;
    int          leftDepth  = 0;                        // 0..99
    int          rightDepth = 0;                        // 0..99

    /** 0..3: how much the LFO's amplitude modulation reaches this operator. */
    int   amSens      = 0;

    /** A muted operator keeps running its envelope but contributes nothing:
        it neither sounds nor modulates anyone. The wirings stay as they are, so
        the switch is a pure "is this operator in the stack right now" toggle. */
    bool  enabled     = true;

    bool operator== (const FmOperator&) const = default;
};

/** The DX7 pitch envelope: four rates and four levels, 50 = no pitch offset. */
struct FmPitchEg
{
    int rate[4]  = { 99, 99, 99, 99 };
    int level[4] = { 50, 50, 50, 50 };

    bool operator== (const FmPitchEg&) const = default;
};

/** The operators a patch starts from: one audible sine carrier and five silent
    modulators, so a track that has never been touched plays a clean note rather
    than a six-operator stack at full level. */
inline std::array<FmOperator, kFmOperators> makeDefaultOperators()
{
    std::array<FmOperator, kFmOperators> ops {};

    for (auto& op : ops)
        op.outputLevel = 0;

    ops[0] = FmOperator();   // ratio 1, level 99, a simple sustaining envelope
    return ops;
}

/** A complete FM sound: six operators wired up by an algorithm, plus the LFO. */
struct FmPatch
{
    juce::String name;
    /** ops[0] is operator 1 - the one nearest the output in the diagrams. */
    std::array<FmOperator, kFmOperators> ops = makeDefaultOperators();
    int algorithm = 1;     // 1..64 (1..32 HM, 33..64 DX7)
    int feedback  = 0;     // 0..7, the algorithm's feedback operator modulating itself
    int lfoRate       = 35;   // 0..99
    int lfoDelay      = 0;    // 0..99, fade-in time
    /** 0 triangle, 1 saw down, 2 saw up, 3 square, 4 sine, 5 sample & hold when
        classicEngine is on; the older order (0 triangle, 1 saw up, 2 saw down,
        3 square, 4 sample & hold) when it is off. */
    int lfoWaveform   = 0;
    int lfoPitchDepth = 0;    // 0..99, vibrato
    int lfoAmpDepth   = 0;    // 0..99, tremolo
    int lfoPitchModSens = 0;  // 0..7, DX7 pitch modulation sensitivity
    bool lfoSync      = false; // true: the LFO phase restarts on every note-on
    int transpose = 0;        // semitones, -24..+24

    FmPitchEg pitchEg;

    /** True: the DX7-style dB level/rate curves, keyboard scaling, fixed
        frequency, pitch EG and velocity curve. False: the behaviour every
        project written before this existed heard, kept so old songs do not
        change. New patches and the DX7 factory bank use true. */
    bool classicEngine = true;

    bool operator== (const FmPatch&) const = default;
};

struct SynthParams
{
    FmPatch patch;
    float gain = 1.0f;        // linear output trim (0..1)

    // Low pass filter. The cutoff default sits at the top of the audible range,
    // i.e. wide open, so a patch that does not touch it is unfiltered.
    float filterCutoffHz  = 20000.0f;
    float filterResonance = 0.707f;   // Q

    bool operator== (const SynthParams&) const = default;
};

/** A sampled instrument: one audio file played back at a rate that makes it
    sound the pitch the note asks for.

    The file is referenced by path and loaded when the project is opened or the
    file is changed; `rootHz` is the pitch the recording actually is, so a note at
    `f` plays the sample at `f / rootHz` speed.
*/
struct SamplerParams
{
    juce::String name;          // the preset name the sound carries
    juce::String filePath;      // absolute path, empty when nothing is loaded
    juce::String fileName;      // what the editor shows
    double rootHz = 440.0;      // the pitch the sample was recorded at

    bool   looped    = false;
    double sampleStart = 0.0;   // 0..1 of the file
    double sampleEnd   = 1.0;
    double loopStart = 0.0;     // 0..1 of the sample length
    double loopEnd   = 1.0;
    double crossfade = 0.0;     // 0..1 of the loop length, applied at loopEnd

    float attack  = 0.005f;     // seconds
    float decay   = 0.100f;
    float sustain = 0.900f;     // 0..1
    float release = 0.200f;

    // A little modulation of the sample: vibrato and tremolo.
    float lfoRateHz     = 0.0f;
    float lfoPitchCents = 0.0f;
    float lfoAmpDepth   = 0.0f;

    float gain = 1.0f;
    float filterCutoffHz  = 20000.0f;
    float filterResonance = 0.707f;

    bool operator== (const SamplerParams&) const = default;
};

/** One oscillator of a subtractive voice. */
enum class OscWave
{
    saw = 0,
    square,
    pulse,
    triangle,
    sine
};

struct SubtractiveOsc
{
    bool    enabled     = true;
    OscWave wave        = OscWave::saw;
    int     octave      = 0;        // -2 .. +2
    int     semitone    = 0;        // -12 .. +12
    float   detuneCents = 0.0f;     // -50 .. +50
    float   level       = 1.0f;     // 0 .. 1
    float   pulseWidth  = 0.5f;     // 0.05 .. 0.95, pulse wave only

    bool operator== (const SubtractiveOsc&) const = default;
};

enum class FilterMode
{
    lowPass = 0,
    bandPass,
    highPass,
    notch,
    comb
};

/** A complete subtractive (analogue style) sound.

    Two oscillators plus noise run into a multimode filter with its own
    envelope, and from there into the amplifier envelope. One LFO can reach the
    pitch, the filter and the amplitude.
*/
struct SubtractiveParams
{
    juce::String name;
    std::array<SubtractiveOsc, 2> oscs;
    float noiseLevel = 0.0f;        // 0 .. 1
    FilterMode filterMode     = FilterMode::lowPass;
    int   filterSlopeDb       = 24;      // 12 or 24
    float filterCutoffHz      = 2000.0f; // 20 .. 20000
    float filterResonance     = 0.707f;  // Q
    float filterEnvAmount     = 0.5f;    // -1 .. +1, times eight octaves
    float filterKeyTrack      = 0.0f;    // 0 .. 1
    float ampAttack  = 0.005f;      // seconds
    float ampDecay   = 0.200f;
    float ampSustain = 0.700f;      // 0 .. 1
    float ampRelease = 0.300f;
    float filterAttack  = 0.005f;
    float filterDecay   = 0.300f;
    float filterSustain = 0.400f;
    float filterRelease = 0.300f;
    float lfoRateHz     = 5.0f;     // 0 .. 20
    int   lfoWaveform   = 0;        // 0 triangle, 1 sine, 2 square, 3 sample & hold
    float lfoPitchCents = 0.0f;     // 0 .. 100
    float lfoCutoffDepth = 0.0f;    // 0 .. 1
    float lfoAmpDepth    = 0.0f;    // 0 .. 1
    float gain = 1.0f;              // linear output trim (0..1)

    bool operator== (const SubtractiveParams&) const = default;
};

//==============================================================================
/** The Minimoog Model D instrument: three oscillators, noise, a 24 dB ladder
    filter, two analogue-style contours, glide and the classic modulation mix.

    Unlike the real machine this one can play polyphonically (the user asked for
    it): `polyphony` is a performance setting, not part of a sound, so presets
    leave it alone. `velocitySensitivity` is the same kind of extension.
*/

/** How many voices "unlimited" really gets. One ladder filter per voice is the
    expensive part, and 64 of them for a single track is already generous. */
constexpr int kMaxMinimoogVoices = 64;

enum class MinimoogWave
{
    triangle = 0,
    triSaw,        // the "triangle-sawtooth" position: a steep rise, slow fall
    saw,
    square,
    widePulse,
    narrowPulse
};

enum class MinimoogRange
{
    lo = 0,        // sub-audio / LFO territory
    feet32,
    feet16,
    feet8,
    feet4,
    feet2
};

struct MinimoogOsc
{
    bool    enabled = true;
    MinimoogWave wave = MinimoogWave::saw;
    MinimoogRange range = MinimoogRange::feet8;
    float   tuneSemitones = 0.0f;   // -7 .. +7 (oscillators 2 and 3)
    float   level = 0.8f;           // 0 .. 1, mixer volume

    bool operator== (const MinimoogOsc&) const = default;
};

struct MinimoogParams
{
    MinimoogParams()
    {
        // A fresh patch is one saw oscillator; the second and third are off
        // until a sound or the player turns them on.
        oscs[1].enabled = false;
        oscs[1].level = 0.0f;
        oscs[2].enabled = false;
        oscs[2].level = 0.0f;
    }

    juce::String name;
    std::array<MinimoogOsc, 3> oscs;
    float noiseLevel = 0.0f;          // 0 .. 1
    bool  noisePink  = false;
    float masterTuneCents = 0.0f;     // -50 .. +50

    /** 0 = monophonic (the real Model D: last-note priority and glide), 1..64 =
        that many voices, -1 = as many as `kMaxMinimoogVoices` allows. */
    int   polyphony = 0;
    /** 0 .. 1; 0 makes the instrument ignore velocity entirely (the real one). */
    float velocitySensitivity = 0.7f;

    float glideSeconds = 0.0f;        // 0 .. 2; monophonic mode only
    bool  glideOn = false;
    float modMix = 0.0f;              // 0 = oscillator 3, 1 = noise
    float modDepth = 0.0f;            // 0 .. 1, the mod wheel
    bool  oscModOn = false;
    bool  filterModOn = false;
    /** false: oscillator 3 keeps the C4 reference instead of following the key. */
    bool  osc3Control = true;

    float filterCutoffHz = 800.0f;    // 10 .. 20000
    float filterEmphasis = 0.0f;      // 0 .. 1
    float filterContourOctaves = 4.0f;// 0 .. 8
    bool  filterKeyTrack1 = false;    // keyboard control 1: one third
    bool  filterKeyTrack2 = false;    // keyboard control 2: another third

    float filterAttack = 0.005f, filterDecay = 0.4f;
    bool  filterDecayShort = false;
    float filterSustainLevel = 0.8f;   // 0 .. 1
    float ampAttack = 0.005f, ampDecay = 0.3f;
    bool  ampDecayShort = false, releaseOn = true;
    float ampSustainLevel = 0.8f;      // 0 .. 1
    float volume = 0.7f;              // 0 .. 1

    bool operator== (const MinimoogParams&) const = default;
};

/** Frame length of a custom (user-made) wavetable, in samples: the same 2048 the
    built-in bank uses, so the mipmap builder and every drawing path are shared. */
constexpr int kCustomWavetableFrameSize = 2048;

/** How many frames a custom table may hold (1 = a plain waveform, 8 = a morph). */
constexpr int kMaxCustomWavetableFrames = 64;

/** A wavetable the user made: imported from a WAV, sampled from an audio clip, or
    drawn by hand.

    `samples` is `frameCount * kCustomWavetableFrameSize` 16-bit samples (frames back
    to back, -32768..32767). Storing PCM rather than the play-ready mipmaps is what
    keeps the project small: the band-limited levels are derived data and are built
    again whenever the table is loaded.
*/
struct CustomWavetable
{
    juce::int64 id = 0;
    juce::String name;
    int frameCount = 1;                    // 1 .. kMaxCustomWavetableFrames
    std::vector<juce::int16> samples;

    int samplesPerFrame() const noexcept { return kCustomWavetableFrameSize; }

    /** The frames this table really holds, after the data is taken into account. */
    int effectiveFrameCount() const noexcept
    {
        if (samplesPerFrame() <= 0)
            return 1;

        const auto fromData = (int) (samples.size() / (size_t) samplesPerFrame());
        return juce::jlimit (1, kMaxCustomWavetableFrames, juce::jmin (frameCount, juce::jmax (1, fromData)));
    }

    bool operator== (const CustomWavetable& other) const
    {
        return id == other.id && name == other.name && frameCount == other.frameCount
                   && samples == other.samples;
    }
};

/** One wavetable oscillator.

    `tableIndex` points into the built-in table bank (the tables themselves are a
    read-only library, so a project only ever stores the index), and `position`
    morphs across that table's frames.
*/
struct WavetableOsc
{
    bool  enabled     = true;
    int   tableIndex  = 0;
    /** Non-zero: the project's own table with this id, which wins over `tableIndex`.
        Zero (the default, and what every project written before custom tables has)
        means "use the built-in table `tableIndex`". */
    juce::int64 customTableId = 0;
    float position    = 0.0f;    // 0 .. 1 across the frames
    int   unisonVoices = 1;       // 1 .. 16
    float unisonDetuneCents = 12.0f; // 0 .. 100
    bool  positionDiscrete = false;
    int   octave      = 0;       // -2 .. +2
    int   semitone    = 0;       // -12 .. +12
    float detuneCents = 0.0f;    // -50 .. +50
    float level       = 1.0f;    // 0 .. 1

    bool operator== (const WavetableOsc&) const = default;
};

inline constexpr int kMaxWavetableUnisonVoices = 16;

enum class HybridTimeDivision
{
    d1_32 = 0, d1_16, d1_8, d1_4, d1_2, d1, d2, d4
};

inline double hybridDivisionBeats (HybridTimeDivision division) noexcept
{
    switch (division)
    {
        case HybridTimeDivision::d1_32: return 1.0 / 32.0;
        case HybridTimeDivision::d1_16: return 1.0 / 16.0;
        case HybridTimeDivision::d1_8:  return 1.0 / 8.0;
        case HybridTimeDivision::d1_4:  return 1.0 / 4.0;
        case HybridTimeDivision::d1_2:  return 1.0 / 2.0;
        case HybridTimeDivision::d1:    return 1.0;
        case HybridTimeDivision::d2:    return 2.0;
        case HybridTimeDivision::d4:    return 4.0;
    }

    return 0.25;
}

inline juce::String hybridDivisionName (HybridTimeDivision division)
{
    switch (division)
    {
        case HybridTimeDivision::d1_32: return "1/32";
        case HybridTimeDivision::d1_16: return "1/16";
        case HybridTimeDivision::d1_8:  return "1/8";
        case HybridTimeDivision::d1_4:  return "1/4";
        case HybridTimeDivision::d1_2:  return "1/2";
        case HybridTimeDivision::d1:    return "1";
        case HybridTimeDivision::d2:    return "2";
        case HybridTimeDivision::d4:    return "4";
    }

    return "1/4";
}

enum class HybridTimeMode { milliseconds = 0, beats = 1 };

/** A complete wavetable sound: two table oscillators plus noise into the same
    multimode filter the subtractive instrument uses, with an extra envelope that
    sweeps the table position. */
struct WavetableParams
{
    juce::String name;
    std::array<WavetableOsc, 2> oscs;
    float noiseLevel = 0.0f;        // 0 .. 1
    FilterMode filterMode     = FilterMode::lowPass;
    int   filterSlopeDb       = 24;      // 12 or 24
    float filterCutoffHz      = 8000.0f; // 20 .. 20000
    float filterResonance     = 0.707f;  // Q
    float filterKeyTrack      = 0.0f;    // 0 .. 1
    float ampAttack  = 0.005f;      // seconds
    float ampDecay   = 0.200f;
    float ampSustain = 0.700f;      // 0 .. 1
    float ampRelease = 0.300f;

    /** The position envelope: what makes the table sweep while a note is held. */
    float posAttack    = 0.005f;
    float posDecay     = 0.600f;
    float posSustain   = 0.000f;
    float posRelease   = 0.400f;
    float posEnvAmount = 0.500f;    // -1 .. +1, added to the static position

    float lfoRateHz      = 5.0f;    // 0 .. 20
    int   lfoWaveform    = 0;       // 0 triangle, 1 sine, 2 square, 3 sample & hold
    float lfoPositionDepth = 0.0f;  // 0 .. 1
    float lfoPitchCents  = 0.0f;    // 0 .. 100
    float lfoCutoffDepth = 0.0f;    // 0 .. 1
    float lfoAmpDepth    = 0.0f;    // 0 .. 1
    float gain = 1.0f;              // linear output trim (0..1)

    bool operator== (const WavetableParams&) const = default;
};

/** The twelve drums a kit holds, in pad order.

    Pad 0 sits at the bottom of the drum grid and pad 11 at the top, which is the
    layout every drum machine uses: the kick underneath, the cymbals above.
*/
enum class DrumPad
{
    kick = 0,
    snare,
    rim,
    clap,
    closedHat,
    openHat,
    lowTom,
    midTom,
    highTom,
    cowbell,
    crash,
    ride,
    count
};

constexpr int kDrumPads = (int) DrumPad::count;

/** The Chinese name of a pad (index clamped). */
inline juce::String drumPadName (int pad)
{
    switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
    {
        case DrumPad::kick:      return NI_TXT ("底鼓");
        case DrumPad::snare:     return NI_TXT ("军鼓");
        case DrumPad::rim:       return NI_TXT ("边击");
        case DrumPad::clap:      return NI_TXT ("拍手");
        case DrumPad::closedHat: return NI_TXT ("闭镲");
        case DrumPad::openHat:   return NI_TXT ("开镲");
        case DrumPad::lowTom:    return NI_TXT ("低音通鼓");
        case DrumPad::midTom:    return NI_TXT ("中音通鼓");
        case DrumPad::highTom:   return NI_TXT ("高音通鼓");
        case DrumPad::cowbell:   return NI_TXT ("牛铃");
        case DrumPad::crash:     return NI_TXT ("吊镲");
        case DrumPad::ride:      return NI_TXT ("叮叮镲");
        case DrumPad::count:     break;
    }

    return NI_TXT ("鼓");
}

/** The pitch each drum is built around. The drum engine ignores the note's
    frequency; this is only the placeholder a drum note carries and what the
    editor shows, so the data stays readable. */
inline double drumPadBaseHz (int pad)
{
    switch ((DrumPad) juce::jlimit (0, kDrumPads - 1, pad))
    {
        case DrumPad::kick:      return 50.0;
        case DrumPad::snare:     return 180.0;
        case DrumPad::rim:       return 1700.0;
        case DrumPad::clap:      return 1000.0;
        case DrumPad::closedHat: return 8000.0;
        case DrumPad::openHat:   return 8000.0;
        case DrumPad::lowTom:    return 100.0;
        case DrumPad::midTom:    return 150.0;
        case DrumPad::highTom:   return 220.0;
        case DrumPad::cowbell:   return 540.0;
        case DrumPad::crash:     return 5000.0;
        case DrumPad::ride:      return 4000.0;
        case DrumPad::count:     break;
    }

    return 440.0;
}

/** One drum's controls. */
struct DrumPadParams
{
    bool  enabled = true;
    float level   = 1.0f;    // 0 .. 1
    float pitch   = 0.0f;    // semitones, -12 .. +12
    float decay   = 1.0f;    // multiplier, 0.1 .. 4
    float tone    = 0.5f;    // 0 .. 1, each drum interprets it (body vs noise)
    float snap    = 0.5f;    // 0 .. 1, attack click

    bool operator== (const DrumPadParams&) const = default;
};

/** A whole kit: twelve pads plus the controls that act on all of them. */
struct DrumKitParams
{
    juce::String name;
    std::array<DrumPadParams, kDrumPads> pads;
    float gain        = 0.9f;    // 0 .. 1  总音量
    float masterPitch = 0.0f;    // semitones, -12 .. +12  总音高
    float masterDecay = 1.0f;    // 0.1 .. 4  总衰减
    float width       = 0.5f;    // 0 .. 1  声场宽度（0 = 全居中）

    bool operator== (const DrumKitParams&) const = default;
};

/** 粒子合成器: an audio file is read by a cloud of short grains.

    The source is a file the user imported (only its path is stored, exactly like the
    sampler). Each sounding note spawns grains at the density below; every grain has
    its own read position, its own length and its own rate, so the cloud moves.
    `position` / `scanSpeed` / the position envelope / the spray decide *where* in the
    file a grain reads; the note's own frequency decides *how fast*, so HM's exact
    frequencies stay exact (`rate = 音符频率 ÷ rootHz`), exactly like the sampler.

    Everything else is the shape the other built-in instruments use: a multimode
    filter, an amplitude envelope and a stereo spread.
*/
struct GranularParams
{
    juce::String name;          // the preset name the sound carries
    juce::String filePath;      // absolute path, empty when nothing is loaded
    juce::String fileName;      // what the editor shows
    double rootHz = 440.0;      // the pitch the file was recorded at

    /** Where in the file (0..1) the cloud reads, and how fast that walks. */
    float position  = 0.25f;    // 0 .. 1
    float scanSpeed = 0.0f;     // -2 .. +2, whole file lengths per second

    /** The grains themselves. */
    float grainMs        = 120.0f;  // 5 .. 500
    float densityHz      = 20.0f;   // 1 .. 100, grains per second
    float spray          = 0.05f;   // 0 .. 1, position scatter per grain
    float spacingJitter  = 0.2f;    // 0 .. 0.9, of the gap between grains
    float pitchJitterCents = 0.0f;  // 0 .. 1200, per grain
    float reverseProb    = 0.0f;    // 0 .. 1, chance a grain reads backwards

    /** The position envelope: what sweeps the cloud while a note is held. */
    float posAttack    = 0.005f;
    float posDecay     = 0.600f;
    float posSustain   = 0.0f;
    float posRelease   = 0.400f;
    float posEnvAmount = 0.5f;      // -1 .. +1

    FilterMode filterMode     = FilterMode::lowPass;
    int   filterSlopeDb       = 24;      // 12 or 24
    float filterCutoffHz      = 8000.0f; // 20 .. 20000
    float filterResonance     = 0.707f;  // Q
    float filterKeyTrack      = 0.0f;    // 0 .. 1

    float ampAttack  = 0.005f;      // seconds
    float ampDecay   = 0.200f;
    float ampSustain = 0.700f;      // 0 .. 1
    float ampRelease = 0.300f;

    float stereoSpread = 0.3f;      // 0 .. 1  声像扩展
    float gain         = 1.0f;      // 0 .. 1

    bool operator== (const GranularParams&) const = default;
};

/** One layer of the hybrid instrument.

    The layer keeps the parameter blocks of every generator type, so switching
    modes never destroys the sound the user had on the other page. */
enum class HybridLayerMode
{
    off = 0,
    wavetable,
    subtractive,
    sample,
    granular,
    sub,
    noise
};

struct HybridLayerParams
{
    bool enabled = true;
    HybridLayerMode mode = HybridLayerMode::wavetable;
    float level = 1.0f;   // 0 .. 1
    float pan   = 0.0f;   // -1 .. +1
    int   subOctave = -1; // -2 .. -1
    float noiseLevel = 0.4f;
    float noiseCutoffHz = 8000.0f;

    WavetableParams   wavetable;
    SubtractiveParams subtractive;
    SamplerParams     sample;
    GranularParams    granular;

    bool operator== (const HybridLayerParams&) const = default;
};

struct HybridSubParams
{
    bool enabled = false;
    int waveform = 0;       // 0 sine, 1 triangle, 2 square
    int octave = -1;        // -2 or -1
    float level = 0.5f;
    float pan = 0.0f;

    bool operator== (const HybridSubParams&) const = default;
};

struct HybridNoiseParams
{
    bool enabled = false;
    int type = 0;           // 0 white, 1 pink, 2 brown
    float level = 0.25f;
    float pan = 0.0f;
    float toneHz = 8000.0f;

    bool operator== (const HybridNoiseParams&) const = default;
};

enum class HybridFilterRouting
{
    series = 0,
    parallel,
    filter1Only
};

struct HybridFilterParams
{
    FilterMode mode = FilterMode::lowPass;
    int slopeDb = 24;
    float cutoffHz = 8000.0f;
    float resonance = 0.707f;
    float drive = 0.0f;
    float keyTrack = 0.0f;
    float envAmount = 0.0f;

    bool operator== (const HybridFilterParams&) const = default;
};

struct HybridEnvelopeParams
{
    float attack = 0.005f;
    float decay = 0.200f;
    float sustain = 0.700f;
    float release = 0.300f;
    HybridTimeMode timeMode = HybridTimeMode::milliseconds;
    HybridTimeDivision attackDivision = HybridTimeDivision::d1_32;
    HybridTimeDivision decayDivision = HybridTimeDivision::d1_8;
    HybridTimeDivision releaseDivision = HybridTimeDivision::d1_8;

    bool operator== (const HybridEnvelopeParams&) const = default;
};

struct HybridLfoParams
{
    int waveform = 0;   // triangle/sine/square/saw/hold
    float rateHz = 2.0f;    // legacy; migrated to rateMs
    float rateMs = 500.0f;
    HybridTimeMode timeMode = HybridTimeMode::milliseconds;
    HybridTimeDivision syncDivision = HybridTimeDivision::d1_4;
    bool sync = false;
    bool retrigger = true;
    float phase = 0.0f;
    float depth = 1.0f;      // 0 .. 1
    float fadeMs = 0.0f;     // 0 .. 5000

    bool operator== (const HybridLfoParams&) const = default;
};

enum class HybridModSource
{
    none = 0,
    env1, env2, env3, env4,
    lfo1, lfo2, lfo3, lfo4,
    velocity, keyTrack, modWheel,
    macro1, macro2, macro3, macro4,
    lfo5, lfo6
};

enum class HybridModDestination
{
    none = 0,
    filter1Cutoff, filter2Cutoff,
    filter1Resonance, filter2Resonance,
    gain, pan,
    layer1Level, layer2Level, layer3Level,
    layer1Pan, layer2Pan, layer3Pan,
    subLevel, subPan,
    noiseLevel, noisePan,
    fx1Mix, fx1Param1, fx1Param2,
    fx2Mix, fx2Param1, fx2Param2,
    fx3Mix, fx3Param1, fx3Param2,
    fx4Mix, fx4Param1, fx4Param2
    , layer1Position, layer1Unison, layer1Detune,
    layer2Position, layer2Unison, layer2Detune,
    layer3Position, layer3Unison, layer3Detune
};

inline int hybridModSourceCount() noexcept { return 18; }
inline int hybridModDestinationCount() noexcept { return 38; }

inline juce::String hybridModSourceName (HybridModSource source)
{
    switch (source)
    {
        case HybridModSource::none: return "-";
        case HybridModSource::env1: return "ENV 1";
        case HybridModSource::env2: return "ENV 2";
        case HybridModSource::env3: return "ENV 3";
        case HybridModSource::env4: return "ENV 4";
        case HybridModSource::lfo1: return "LFO 1";
        case HybridModSource::lfo2: return "LFO 2";
        case HybridModSource::lfo3: return "LFO 3";
        case HybridModSource::lfo4: return "LFO 4";
        case HybridModSource::lfo5: return "LFO 5";
        case HybridModSource::lfo6: return "LFO 6";
        case HybridModSource::velocity: return NI_TXT ("力度");
        case HybridModSource::keyTrack: return NI_TXT ("键位");
        case HybridModSource::modWheel: return NI_TXT ("调制轮");
        case HybridModSource::macro1: return NI_TXT ("宏 1");
        case HybridModSource::macro2: return NI_TXT ("宏 2");
        case HybridModSource::macro3: return NI_TXT ("宏 3");
        case HybridModSource::macro4: return NI_TXT ("宏 4");
    }

    return "-";
}

inline juce::String hybridModDestinationName (HybridModDestination destination)
{
    switch (destination)
    {
        case HybridModDestination::none: return "-";
        case HybridModDestination::filter1Cutoff: return NI_TXT ("Filter 1 截止");
        case HybridModDestination::filter2Cutoff: return NI_TXT ("Filter 2 截止");
        case HybridModDestination::filter1Resonance: return NI_TXT ("Filter 1 共振");
        case HybridModDestination::filter2Resonance: return NI_TXT ("Filter 2 共振");
        case HybridModDestination::gain: return NI_TXT ("总音量");
        case HybridModDestination::pan: return NI_TXT ("声相");
        case HybridModDestination::layer1Level: return NI_TXT ("主振 1 电平");
        case HybridModDestination::layer2Level: return NI_TXT ("主振 2 电平");
        case HybridModDestination::layer3Level: return NI_TXT ("主振 3 电平");
        case HybridModDestination::layer1Pan: return NI_TXT ("主振 1 声相");
        case HybridModDestination::layer2Pan: return NI_TXT ("主振 2 声相");
        case HybridModDestination::layer3Pan: return NI_TXT ("主振 3 声相");
        case HybridModDestination::subLevel: return NI_TXT ("Sub 电平");
        case HybridModDestination::subPan: return NI_TXT ("Sub 声相");
        case HybridModDestination::noiseLevel: return NI_TXT ("Noise 电平");
        case HybridModDestination::noisePan: return NI_TXT ("Noise 声相");
        case HybridModDestination::fx1Mix: return NI_TXT ("FX 1 干湿");
        case HybridModDestination::fx1Param1: return NI_TXT ("FX 1 参数 1");
        case HybridModDestination::fx1Param2: return NI_TXT ("FX 1 参数 2");
        case HybridModDestination::fx2Mix: return NI_TXT ("FX 2 干湿");
        case HybridModDestination::fx2Param1: return NI_TXT ("FX 2 参数 1");
        case HybridModDestination::fx2Param2: return NI_TXT ("FX 2 参数 2");
        case HybridModDestination::fx3Mix: return NI_TXT ("FX 3 干湿");
        case HybridModDestination::fx3Param1: return NI_TXT ("FX 3 参数 1");
        case HybridModDestination::fx3Param2: return NI_TXT ("FX 3 参数 2");
        case HybridModDestination::fx4Mix: return NI_TXT ("FX 4 干湿");
        case HybridModDestination::fx4Param1: return NI_TXT ("FX 4 参数 1");
        case HybridModDestination::fx4Param2: return NI_TXT ("FX 4 参数 2");
        case HybridModDestination::layer1Position: return NI_TXT ("主振 1 位置");
        case HybridModDestination::layer1Unison: return NI_TXT ("主振 1 齐奏");
        case HybridModDestination::layer1Detune: return NI_TXT ("主振 1 失谐");
        case HybridModDestination::layer2Position: return NI_TXT ("主振 2 位置");
        case HybridModDestination::layer2Unison: return NI_TXT ("主振 2 齐奏");
        case HybridModDestination::layer2Detune: return NI_TXT ("主振 2 失谐");
        case HybridModDestination::layer3Position: return NI_TXT ("主振 3 位置");
        case HybridModDestination::layer3Unison: return NI_TXT ("主振 3 齐奏");
        case HybridModDestination::layer3Detune: return NI_TXT ("主振 3 失谐");
    }

    return "-";
}

struct HybridModSlot
{
    bool enabled = false;
    HybridModSource source = HybridModSource::none;
    HybridModDestination destination = HybridModDestination::none;
    float amount = 0.0f;

    bool operator== (const HybridModSlot&) const = default;
};

enum class HybridFxType
{
    none = 0,
    distortion,
    bitcrush,
    chorus,
    phaser,
    delay,
    reverb,
    eq,
    compressor
};

struct HybridFxSlot
{
    bool enabled = false;
    bool bypass = false;
    HybridFxType type = HybridFxType::none;
    float mix = 1.0f;
    float param1 = 0.5f;
    float param2 = 0.5f;

    bool operator== (const HybridFxSlot&) const = default;
};

/** A three-layer hybrid: each layer can be a wavetable, analogue, sample or
    granular generator. A fourth Sub layer is represented by a sine wavetable at
    -1/-2 octaves in the first layer's second oscillator for now. */
struct HybridParams
{
    juce::String name;
    std::array<HybridLayerParams, 3> layers;
    HybridSubParams sub;
    HybridNoiseParams noise;
    std::array<HybridFilterParams, 2> filters;
    HybridFilterRouting filterRouting = HybridFilterRouting::series;
    std::array<HybridEnvelopeParams, 4> envelopes;
    std::array<HybridLfoParams, 6> lfos;
    std::array<HybridModSlot, 16> modSlots;
    std::array<float, 4> macros {};
    std::array<juce::String, 4> macroNames;
    std::array<HybridFxSlot, 4> fxSlots;
    float gain = 1.0f;

    bool operator== (const HybridParams&) const = default;
};

/** An external VST3 instrument plugged into a track.

    The project stores where the plugin lives, which plugin it is (so a dialog can
    find it again when the path moved) and the plugin's own state as an opaque
    blob. The blob is base64 because a state chunk is arbitrary binary and the
    project file is JSON - a few MB for a big Kontakt library, which is the price
    of a self contained project.

    Everything above the plugin is the same as for the built-in instruments: the
    track has clips and notes, and the note's absolute frequency is what reaches
    the plugin (as a MIDI note plus the cents that are left over).
*/
struct VstParams
{
    juce::String name;          // the plugin's own name, shown on the lane header
    juce::String pluginPath;    // the .vst3 bundle the user picked
    juce::String pluginUid;     // the plugin's unique id, for relocating by name
    juce::String stateBase64;   // the plugin's state chunk, base64 encoded

    /** Runtime knowledge: the instance is up. Never serialised, so an old project
        never claims to be loaded before the plugin has actually been created. */
    bool loadedOk = false;

    /** 按半音演奏（不用逐音弯音）: HM's usual trick for an exact frequency is to give
        the note its own MIDI channel and put the leftover cents on that channel's
        pitch bend. That only works on plugins that treat a bend as *per channel*.
        Plenty of instruments - Serum 2 and Kontakt, measured, among others - bend
        the whole patch, so every sounding note is dragged onto whichever bend came
        last. With this on, notes are played at the nearest semitone and **no bend is
        ever sent**: equal temperament instead of the exact ratio, but in tune and
        completely free of that interference. Off by default, so nothing changes for
        a project written before this field existed. */
    bool equalTemperament = false;

    /** A track is only playable once a plugin is named. */
    bool hasPlugin() const noexcept { return pluginPath.isNotEmpty(); }

    bool operator== (const VstParams&) const = default;
};

/** One slot in a track's insert chain.

    The chain sits between the instrument and the track's fader, one VST3 effect
    per slot. `kind` is 0 for a VST3 plugin today; it is there so a future built-in
    effect can be added without the project file needing a migration. The plugin's
    own state travels with the project exactly like an instrument's.
*/
struct EffectSlot
{
    int          kind = 0;          // 0 = VST3
    juce::String name;              // the plugin's name, for the row
    juce::String pluginPath;
    juce::String pluginUid;
    juce::String stateBase64;
    bool         bypass = false;

    bool hasPlugin() const noexcept { return pluginPath.isNotEmpty(); }

    bool operator== (const EffectSlot&) const = default;
};

struct Note
{
    juce::int64 id            = 0;
    juce::int64 startTick     = 0;
    juce::int64 durationTicks = kTicksPerQuarter;
    double      frequencyHz   = 440.0;
    /** Tail frequency. 0 means "the same as the head" for old projects. */
    double      tailFrequencyHz = 0.0;
    /** -1 .. +1. It bends the timing of the log-frequency interpolation. */
    float       pitchCurve    = 0.0f;
    /** Normalised interval inside the note where the pitch moves. */
    float       tiltStart     = 0.0f;
    float       tiltEnd       = 1.0f;
    /** The earlier note this one is joined to; only head-to-tail joins are valid. */
    juce::int64 tieFromNoteId = 0;
    int         velocity      = 100;   // 0..127
    bool        muted         = false; // greyed out and produces no synth events
    juce::String comment;

    /** Which drum this note hits, -1 for an ordinary tonal note. Only drum
        tracks look at it; the frequency above is unused there. */
    int         pad           = -1;

    juce::int64 endTick() const noexcept { return startTick + durationTicks; }
    double effectiveTailHz() const noexcept
    {
        return tailFrequencyHz > 0.0 ? tailFrequencyHz : frequencyHz;
    }
    bool hasPitchTilt() const noexcept
    {
        return effectiveTailHz() != frequencyHz
               || std::abs (pitchCurve) > 1.0e-6f
               || tiltStart > 1.0e-6f || tiltEnd < 1.0f - 1.0e-6f;
    }
};

struct TempoMarker
{
    juce::int64 tick      = 0;
    int         numerator = 4;
    int         denominator = 4;
    double      bpm       = 120.0;
};

/** The loop (cycle) range: playback wraps from endTick back to startTick.

    Stored in ticks so it follows tempo changes. A region with
    endTick <= startTick counts as "not set" and the loop button does nothing.
*/
struct LoopRegion
{
    juce::int64 startTick = 0;
    juce::int64 endTick   = 0;
    bool        enabled   = false;

    bool isValid() const noexcept { return endTick > startTick; }
};

/** One point of a controller curve: a value at a tick, both relative to the clip.

    A point is a *knee* of the drawn polyline; between two points the curve is
    interpolated. `value` is a MIDI-style 0..127 (the same scale a note's velocity
    uses), so an expression or pedal curve reads the way a MIDI player reads it.
*/
struct ControllerPoint
{
    juce::int64 tick  = 0;
    int         value = 0;
};

/** One drawn controller curve inside a clip, for one MIDI controller number.

    Invariants (kept by Core/ControllerMath.h, which is the only thing that edits
    these): points are sorted by tick, there is at most one point per tick, and every
    value is 0..127. A curve with no points means "nothing drawn" - the controller is
    simply never sent, and an instrument keeps whatever it had.
*/
struct ControllerCurve
{
    int controller = 11;                    // CC number, 0..127
    std::vector<ControllerPoint> points;    // ticks relative to the clip

    bool empty() const noexcept { return points.empty(); }
};

/** A clip: a window of time holding notes, positioned on a track.

    Note ticks are relative to the clip's content origin, so moving a clip moves
    its notes for free and looping tiles the content. Notes are NOT limited to the
    window: only the ones starting inside it sound, the rest are kept but silent.
*/
struct Clip
{
    juce::int64 id            = 0;
    juce::String name;
    juce::int64 startTick     = 0;
    juce::int64 lengthTicks   = kDefaultClipTicks;   // the window on the timeline
    juce::int64 loopLengthTicks = kDefaultClipTicks; // one repeat period
    bool        looped        = false;
    std::vector<Note> notes;                         // ticks relative to the clip
    /** The controller curves drawn under the notes, in the same clip-relative
        ticks, so they tile with the notes when the clip loops. */
    std::vector<ControllerCurve> controllerCurves;

    // ---- audio clips (on an audio track) ----
    /** The file this clip plays; empty on a note clip. */
    juce::String audioFilePath;
    juce::String audioFileName;
    /** Where the clip's window starts inside the file, in seconds. Trimming the
        clip's left edge advances this so the audio stays anchored to the timeline. */
    double audioOffsetSeconds = 0.0;
    float  audioGain = 1.0f;

    /** True when this clip plays a file rather than notes. */
    bool isAudio() const noexcept { return audioFilePath.isNotEmpty(); }

    juce::int64 endTick() const noexcept { return startTick + lengthTicks; }
};

/** What kind of instrument a track holds.

    Only `synth` makes sound so far; the other two are reserved for the sampler and
    audio-track batches, which is why the new-track menu greys them out.
*/
enum class TrackKind
{
    synth,
    sampler,
    audio,
    /** 减法合成器: oscillators -> filter -> amplifier. Appended last so the
        serialised values of the older kinds (0 / 1 / 2) never move. */
    subtractive,
    /** 波表合成器: two wavetable oscillators -> filter -> amplifier. */
    wavetable,
    /** 鼓机: twelve synthesised drums, picked by the note's pad index. */
    drums,
    /** 外部 VST3 乐器: the plugin itself makes the sound; the track holds the
        plugin location and its state. */
    vst,
    /** 粒子合成器: a cloud of grains read out of an imported audio file. Appended
        last so the serialised values of the kinds above never move. */
    granular,
    /** Serum-style three-layer instrument. */
    hybrid,
    /** Minimoog Model D style monophonic/polyphonic analogue instrument. Appended
        last so the serialised values of the kinds above never move. */
    minimoog
};

/** The Chinese name of a track kind, used for the default track name and the
    type line on the lane header. */
inline juce::String trackKindLabel (TrackKind kind)
{
    switch (kind)
    {
        case TrackKind::synth:       return NI_TXT ("FM合成器");
        case TrackKind::sampler:     return NI_TXT ("采样器");
        case TrackKind::audio:       return NI_TXT ("音频");
        case TrackKind::subtractive: return NI_TXT ("减法合成器");
        case TrackKind::wavetable:   return NI_TXT ("波表合成器");
        case TrackKind::drums:       return NI_TXT ("鼓机");
        case TrackKind::vst:         return NI_TXT ("VST 乐器");
        case TrackKind::granular:    return NI_TXT ("粒子合成器");
        case TrackKind::hybrid:      return NI_TXT ("混合合成器");
        case TrackKind::minimoog:    return NI_TXT ("迷你穆格");
    }

    return NI_TXT ("FM合成器");
}

struct InstrumentTrack
{
    juce::int64 id          = 0;
    TrackKind   kind        = TrackKind::synth;
    juce::String name;
    float        volumeDb   = -6.0f;   // -60 .. +6
    float        pan        = 0.0f;    // -1 (L) .. +1 (R)
    int          colourIndex = 0;
    /** Track level mute / solo. A muted track never sounds, not even when it is
        also soloed (mute wins, like Cubase). */
    bool         muted      = false;
    bool         soloed     = false;

    SynthParams synth;
    SamplerParams sampler;
    SubtractiveParams subtractive;
    WavetableParams   wavetable;
    DrumKitParams     drums;
    VstParams         vst;
    GranularParams    granular;
    HybridParams      hybrid;
    MinimoogParams    minimoog;

    /** The insert chain: instrument -> these, in order -> track fader. */
    std::vector<EffectSlot> effects;

    std::vector<Clip> clips;

    Clip* findClip (juce::int64 clipId)
    {
        for (auto& c : clips)
            if (c.id == clipId)
                return &c;

        return nullptr;
    }

    const Clip* findClip (juce::int64 clipId) const
    {
        return const_cast<InstrumentTrack*> (this)->findClip (clipId);
    }
};

/** Which canvas the editor draws.

    `reference` is the frequency axis with reference lines the rest of the app is
    built around; `piano` is a piano roll whose rows are the 12 tone equal
    temperament semitones, so notes written there are quantized on the way in.
    The notes themselves stay absolute frequencies either way.
*/
enum class EditorMode
{
    reference = 0,
    piano
};

/** Which pane's horizontal timeline a component draws itself with.

    The arrangement (the track lanes, with their own ruler and marker track) and the
    editor (the note canvas, also with its own ruler and marker track) each keep
    their own pixels-per-quarter and offset, so zooming into a clip in the editor
    does not move the arrangement under the user's hand - and the other way round.
*/
enum class TimelineView
{
    arrangement,
    editor
};

struct SnapSettings
{
    bool enabled = true;
    int denominator = 1;

    int clampedDenominator() const noexcept
    {
        return juce::jlimit (1, 512, denominator);
    }
};

/** Editor view + editing state that lives inside the project file. */
struct EditorViewState
{
    double pixelsPerQuarter = 140.0;
    double horizontalOffsetTicks = 0.0;

    /** One collapsible panel's saved state.

        `size` is what the user dragged it to (0 = the panel's own default), and
        `collapsed` means it is folded down to the thin bar. Both travel with the
        project, like the zoom and the reference-line mode do.
    */
    struct PanelSize
    {
        int  size      = 0;
        bool collapsed = false;
    };

    PanelSize referenceGroupPanel;    // 参考组栏 (editor's left column)
    PanelSize presetPanel;            // 预设 / 历史栏 (beside it)
    PanelSize referenceLinesPanel;    // 参考线状态栏 (editor's right column)
    /** The controller lane at the bottom of the editor: how tall it was dragged and
        whether it is folded down to a thin bar. `size == 0` means "the default". */
    PanelSize controllerLane;

    /** The arrangement's own horizontal view. A file written before the two panes
        were separated has no such fields: loading it seeds them from the editor's,
        so it opens looking exactly as it was left. */
    double arrangementPixelsPerQuarter = 140.0;
    double arrangementHorizontalOffsetTicks = 0.0;

    /** Arrangement, piano editor and velocity/CC lane each own their snap. */
    SnapSettings trackSnap;
    SnapSettings editorSnap;
    SnapSettings controllerSnap;

    double centreHz       = 440.0;
    double octavesVisible = 4.0;
    bool   autoFitView    = false;   // false == view stays put, only visible lines are recomputed

    /** Whether hovering a ratio target line draws the little ratio arrow (「指示线」). */
    bool   ratioArrowVisible = true;

    /** Whether the transport clicks on every beat while playing. It is a
        monitoring aid: it never reaches an export. */
    bool   metronomeEnabled = false;

    /** 参考线模式 / 钢琴窗模式 (missing in older files -> reference). */
    EditorMode editorMode = EditorMode::reference;

    /** Whether the piano window draws the reference lines as a background. They
        are read-only there either way; this is only about seeing them. */
    bool   showReferenceLinesInPianoMode = true;

    /** 侧视: whether the canvas draws the other tracks' notes as a dim background
        board. They are read-only reference material either way - this is only
        about seeing them. Off by default, and it travels with the project like the
        other view switches do. */
    bool   sideView = false;

    /** 侧视: the other tracks the user has UN-ticked in the picker, by id.

        Held as "hidden" rather than "shown" so an empty set means exactly what the
        feature did before the picker existed - every other track - and a track that
        is added later shows up on its own.
    */
    std::set<juce::int64> sideViewHiddenTracks;
};

/** The zoom of one timeline, clamped the way the UI clamps it while zooming. */
/** A collapsed panel is this wide: a thin bar with nothing but its arrow. */
constexpr int kCollapsedPanelWidth = 18;
/** A panel never goes below this, however far it is dragged. */
constexpr int kMinPanelWidth = 120;
/** The canvas keeps at least this much room, however wide the panels want to be. */
constexpr int kMinCanvasWidth = 520;

/** The width a collapsible panel is laid out at. */
inline int panelWidthFor (const EditorViewState::PanelSize& panel, int defaultSize) noexcept
{
    if (panel.collapsed)
        return kCollapsedPanelWidth;

    return juce::jmax (kMinPanelWidth, panel.size > 0 ? panel.size : defaultSize);
}

/** Shrinks the given panels until `available` can hold them, their reserved room
    and the canvas: each step takes one pixel off whichever panel is widest now, so
    panels of the same width give way together instead of one of them being crushed
    to the minimum while the others keep their width. Collapsed panels stay thin,
    and nothing ever goes below kMinPanelWidth. */
inline void fitPanelsToCanvas (int available, int reserved, std::vector<int*> widths)
{
    auto total = 0;

    for (const auto* width : widths)
        total += *width;

    while (total + reserved > available)
    {
        auto* widest = (int*) nullptr;

        for (auto* width : widths)
            if (*width > kMinPanelWidth && (widest == nullptr || *width > *widest))
                widest = width;

        if (widest == nullptr)
            break;

        --*widest;
        --total;
    }
}

inline double pixelsPerQuarterFor (const EditorViewState& view, TimelineView which) noexcept
{
    return juce::jlimit (8.0, 2000.0,
                         which == TimelineView::arrangement ? view.arrangementPixelsPerQuarter
                                                           : view.pixelsPerQuarter);
}

/** Where one timeline starts, in ticks. */
inline double horizontalOffsetFor (const EditorViewState& view, TimelineView which) noexcept
{
    return juce::jmax (0.0,
                       which == TimelineView::arrangement ? view.arrangementHorizontalOffsetTicks
                                                         : view.horizontalOffsetTicks);
}

inline void setPixelsPerQuarterFor (EditorViewState& view, TimelineView which, double value) noexcept
{
    const auto clamped = juce::jlimit (8.0, 2000.0, value);

    if (which == TimelineView::arrangement)
        view.arrangementPixelsPerQuarter = clamped;
    else
        view.pixelsPerQuarter = clamped;
}

inline void setHorizontalOffsetFor (EditorViewState& view, TimelineView which, double value) noexcept
{
    const auto clamped = juce::jmax (0.0, value);

    if (which == TimelineView::arrangement)
        view.arrangementHorizontalOffsetTicks = clamped;
    else
        view.horizontalOffsetTicks = clamped;
}

/** A tick as an x inside one pane, and back. */
inline double xForTimelineTick (const EditorViewState& view, TimelineView which, double tick) noexcept
{
    return (tick - horizontalOffsetFor (view, which)) * pixelsPerQuarterFor (view, which)
               / (double) kTicksPerQuarter;
}

inline double timelineTickForX (const EditorViewState& view, TimelineView which, double x) noexcept
{
    return horizontalOffsetFor (view, which)
           + x * (double) kTicksPerQuarter / juce::jmax (1.0, pixelsPerQuarterFor (view, which));
}

//==============================================================================
/** One pitch class, collapsed into a single octave: position 0 is the lower edge
    of the reference-group segment, 1 the upper edge (the same class again).
    Colours are palette indices rather than juce::Colour so that the core layer
    stays free of any graphics dependency.
*/
struct ReferenceDot
{
    double position    = 0.0;   // [0, 1)
    int    colourIndex = 0;
};

/** The ratios currently laid out on the reference-group segment.

    Holds ratio information only - never absolute pitch. The two frame dots at
    positions 0 and 1 are always drawn and always selectable, but are not stored.
*/
struct ReferenceGroup
{
    juce::String name;
    std::vector<ReferenceDot> dots;   // sorted by position

    /** Colour of the two frame dots. They are the same pitch class at positions
        0 and 1, so they share one colour. */
    int frameColourIndex = kFrameDotColourIndex;
};

/** A reference group pinned to a concrete note.

    Produced by "应用": anchorHz is the frequency of the note that was clicked and
    anchorPosition is the dot that was clicked. The resulting line frequencies are
    fixed at that moment and do not follow the note afterwards.
*/
struct ReferenceLineGroup
{
    juce::String name;
    double anchorHz       = 440.0;
    double anchorPosition = 0.0;
    std::vector<ReferenceDot> dots;
    bool visible = true;
};

/** The four kinds of reference line the control panel can hold. */
enum class ReferenceLineKind
{
    fixedHz,     // 1st: a hand-typed frequency and its octaves (「赫兹」)
    noteRatio,   // 2nd: the selected note's own octaves plus its ratio targets
    group,       // 3rd: an applied reference group
    multiNote    // 4th: the octave families of every selected note
};

/** One line in the reference control panel ("参考线状态").

    The 2nd and 4th kinds follow the note selection while they are unlocked; locking
    them freezes the frequencies into frozenHz / frozenRatio / frozenNotes so they
    stop reacting to what is selected.
*/
struct ReferenceLineEntry
{
    juce::int64       id   = 0;
    ReferenceLineKind kind = ReferenceLineKind::fixedHz;
    juce::String      name;
    int               colourIndex = -1;   // -1 -> the kind's default role colour
    bool              locked  = false;
    bool              visible = true;
    double baseHz = 0.0;                  // fixedHz: the frequency that was typed in
    double anchorHz       = 0.0;          // group: where the reference group was pinned
    double anchorPosition = 0.0;
    std::vector<ReferenceDot> dots;
    double frozenHz    = 0.0;             // noteRatio: the note's frequency
    double frozenRatio = 2.0;             // noteRatio: the note's ratio
    std::vector<double> frozenNotes;      // multiNote: one frequency per selected note
};

//==============================================================================
/** 参考线组状态栏里一条「参考线组」条目的来源：工程自己的条目，或者预设库里的
    某一条（序号只是"当时是哪一条"，真正的显示用下面那份快照）。 */
struct MidiGroupSource
{
    enum class Kind { none, entry, preset };

    Kind        kind = Kind::none;
    juce::int64 entryId = 0;        // kind == entry: the project's own reference line
    int         presetIndex = -1;   // kind == preset: an index into the library
};

/** MIDI 输入的整份状态：这张映射表，加上窗口右侧正在显示哪一组参考线（以及它
    当时的模样，好让原来的条目或预设被删掉之后窗口仍显示得出东西）。

    这是**配置**而不是编辑：它随工程保存，但不进撤销栈。
*/
struct MidiInputState
{
    midimap::Map      map;
    MidiGroupSource   groupSource;
    /** A copy of the group as it was when it was picked. */
    ReferenceLineGroup group;

    /** The colour the source carried, if it had one (-1 = every dot keeps its own
        colour). 参考线组 status entries let the user colour the whole entry; the
        canvas then paints every line of that group in that colour, and the MIDI
        window has to do the same or the dots there would not match. */
    int groupColourIndex = -1;

    /** The library preset this came from / was last saved into (empty = it only
        lives in this project). The window's 保存 button uses it. */
    juce::String presetName;

    /** 琶音器: a property of the MIDI input, not of a track (there is one mapping
        and one arpeggiator per project, and both go into a mapping preset). It is
        configuration, so changing it never pushes an undo step. */
    midiarp::Settings arp;
};

//==============================================================================
struct Project
{
    int    version    = 1;
    double sampleRate = 48000.0;
    int    bufferSize = 512;

    /** The master output's own gain, in dB. It is a project setting (like a track's
        volume) and the export obeys it: what you hear is what is written. */
    float  masterVolumeDb = 0.0f;

    std::vector<TempoMarker>     tempoMarkers;
    LoopRegion                   loop;
    std::vector<InstrumentTrack> tracks;
    EditorViewState              view;
    ReferenceGroup               referenceGroup;
    std::vector<ReferenceLineEntry> referenceLines;

    /** MIDI 输入：映射表 + 窗口右侧显示的那一组参考线（配置，不进撤销栈）。 */
    MidiInputState               midiInput;

    /** 自定义波表：本工程自己的表，振荡器按 id 引用（两条轨用同一张表只存一份）。
        存的是 16 位 PCM，播放用的 mipmap 是载入后重新生成的派生数据。 */
    std::vector<CustomWavetable> customWavetables;

    juce::int64 nextTrackId       = 1;
    juce::int64 nextNoteId        = 1;
    juce::int64 nextClipId        = 1;
    juce::int64 nextReferenceLineId = 1;
    juce::int64 nextCustomWavetableId = 1;

    CustomWavetable* findCustomWavetable (juce::int64 tableId)
    {
        for (auto& table : customWavetables)
            if (table.id == tableId)
                return &table;

        return nullptr;
    }

    const CustomWavetable* findCustomWavetable (juce::int64 tableId) const
    {
        return const_cast<Project*> (this)->findCustomWavetable (tableId);
    }

    ReferenceLineEntry* findReferenceLine (juce::int64 lineId)
    {
        for (auto& e : referenceLines)
            if (e.id == lineId)
                return &e;

        return nullptr;
    }

    const ReferenceLineEntry* findReferenceLine (juce::int64 lineId) const
    {
        return const_cast<Project*> (this)->findReferenceLine (lineId);
    }

    Clip* findClip (juce::int64 clipId)
    {
        for (auto& t : tracks)
            if (auto* c = t.findClip (clipId))
                return c;

        return nullptr;
    }

    const Clip* findClip (juce::int64 clipId) const
    {
        return const_cast<Project*> (this)->findClip (clipId);
    }

    /** The track that owns the given clip, or nullptr. */
    InstrumentTrack* findTrackOfClip (juce::int64 clipId)
    {
        for (auto& t : tracks)
            if (t.findClip (clipId) != nullptr)
                return &t;

        return nullptr;
    }

    const InstrumentTrack* findTrackOfClip (juce::int64 clipId) const
    {
        return const_cast<Project*> (this)->findTrackOfClip (clipId);
    }

    InstrumentTrack* findTrack (juce::int64 trackId)
    {
        for (auto& t : tracks)
            if (t.id == trackId)
                return &t;

        return nullptr;
    }

    const InstrumentTrack* findTrack (juce::int64 trackId) const
    {
        return const_cast<Project*> (this)->findTrack (trackId);
    }
};

/** A fresh empty clip with an auto name; also advances the project's id counter. */
inline Clip makeDefaultClip (Project& project)
{
    Clip clip;
    clip.id = project.nextClipId++;
    clip.name = NI_TXT ("片段 ") + juce::String (clip.id);
    return clip;
}

/** The notes of a clip that are in the given selection, in id order.

    Used by the reference control panel: the 2nd and 4th kinds of reference line
    follow "the note(s) I have selected right now".
*/
inline std::vector<Note> selectedNotesOf (const Project& project,
                                          juce::int64 clipId,
                                          const std::set<juce::int64>& selectedNoteIds)
{
    std::vector<Note> notes;

    if (const auto* clip = project.findClip (clipId))
        for (const auto& note : clip->notes)
            if (selectedNoteIds.find (note.id) != selectedNoteIds.end())
                notes.push_back (note);

    return notes;
}

/** A brand new project: empty, with just a tempo marker.

    It deliberately has no track - the instrument column is gone, so the first thing
    the user does is press "+ 新建轨道".
*/
inline Project makeDefaultProject()
{
    Project p;
    p.tempoMarkers.push_back ({ 0, 4, 4, 120.0 });
    return p;
}

/** Appends one track of the given kind, with a single empty clip, and selects it.

    Returns the new track's id (0 when the track limit is already reached).
*/
inline juce::int64 addTrack (Project& project, TrackKind kind)
{
    if ((int) project.tracks.size() >= kMaxTracks)
        return 0;

    InstrumentTrack track;
    track.id          = project.nextTrackId++;
    track.kind        = kind;
    track.name        = trackKindLabel (kind) + " " + juce::String ((int) project.tracks.size() + 1);
    track.colourIndex = (int) project.tracks.size();

    // An audio track starts empty: its first clip comes from importing a file.
    if (kind != TrackKind::audio)
        track.clips.push_back (makeDefaultClip (project));

    const auto id = track.id;
    project.tracks.push_back (std::move (track));
    return id;
}

inline double decibelsToGain (double db) noexcept
{
    return std::pow (10.0, db / 20.0);
}

/** Whether a track reaches the output.

    Mute wins over solo (the Cubase rule). Solo is additive: as soon as any track
    in the project is soloed, only the soloed ones sound; with no solo anywhere
    every unmuted track sounds.
*/
inline bool isTrackAudible (const Project& project, const InstrumentTrack& track)
{
    if (track.muted)
        return false;

    for (const auto& other : project.tracks)
        if (other.soloed)
            return track.soloed;

    return true;
}

inline juce::String formatHz (double hz)
{
    if (hz >= 1000.0)
        return juce::String (hz, 1) + " Hz";

    return juce::String (hz, 2) + " Hz";
}

} // namespace nidaw
