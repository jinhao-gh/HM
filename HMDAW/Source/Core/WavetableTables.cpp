#include "WavetableTables.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace hmdaw::wavetabletables
{

namespace
{
    constexpr float kQuantise = 32767.0f;

    juce::int16 toPcm (float value) noexcept
    {
        return (juce::int16) juce::jlimit (-32768.0f, 32767.0f,
                                           std::round (juce::jlimit (-1.0f, 1.0f, value) * kQuantise));
    }

    float fromPcm (juce::int16 value) noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, (float) value / kQuantise);
    }

    int framesOf (const CustomWavetable& table) noexcept
    {
        return juce::jlimit (1, kMaxCustomWavetableFrames, table.frameCount);
    }

    /** The channel of a table, as floats - what the mipmap builder wants. */
    std::vector<float> toFloats (const CustomWavetable& table)
    {
        std::vector<float> frames;
        frames.reserve ((size_t) framesOf (table) * (size_t) kCustomWavetableFrameSize);

        for (const auto sample : table.samples)
            frames.push_back (fromPcm (sample));

        frames.resize ((size_t) framesOf (table) * (size_t) kCustomWavetableFrameSize, 0.0f);
        return frames;
    }

    /** Copies `source` (any length) into `destination` (exactly `outLength` values)
        by averaging the source samples that fall into each output slot. Averaging
        rather than picking is what stops a long slice from folding its top octaves
        back down into the table's shape. */
    void boxResample (const float* source, juce::int64 sourceLength,
                      float* destination, int outLength)
    {
        if (outLength <= 0)
            return;

        if (source == nullptr || sourceLength <= 0)
        {
            std::fill (destination, destination + outLength, 0.0f);
            return;
        }

        for (int i = 0; i < outLength; ++i)
        {
            const auto start = (juce::int64) ((double) i * (double) sourceLength / (double) outLength);
            auto end         = (juce::int64) ((double) (i + 1) * (double) sourceLength / (double) outLength);

            if (end <= start)
                end = juce::jmin (sourceLength, start + 1);

            if (start >= sourceLength)
            {
                destination[i] = 0.0f;
                continue;
            }

            auto sum = 0.0;

            for (auto s = start; s < juce::jmin (end, sourceLength); ++s)
                sum += (double) source[s];

            destination[i] = (float) (sum / (double) juce::jmax ((juce::int64) 1,
                                                                 juce::jmin (end, sourceLength) - start));
        }
    }
}

float sampleAt (const CustomWavetable& table, int frame, int index) noexcept
{
    const auto frames = framesOf (table);

    if (frame < 0 || frame >= frames || index < 0 || index >= kCustomWavetableFrameSize)
        return 0.0f;

    const auto offset = (size_t) frame * (size_t) kCustomWavetableFrameSize + (size_t) index;

    return offset < table.samples.size() ? fromPcm (table.samples[offset]) : 0.0f;
}

void setSampleAt (CustomWavetable& table, int frame, int index, float value)
{
    const auto frames = framesOf (table);

    if (frame < 0 || frame >= frames || index < 0 || index >= kCustomWavetableFrameSize)
        return;

    const auto offset = (size_t) frame * (size_t) kCustomWavetableFrameSize + (size_t) index;

    if (table.samples.size() < (size_t) frames * (size_t) kCustomWavetableFrameSize)
        table.samples.resize ((size_t) frames * (size_t) kCustomWavetableFrameSize, 0);

    table.samples[offset] = toPcm (value);
}

CustomWavetable makeEmpty (juce::int64 id, const juce::String& name, int frameCount)
{
    CustomWavetable table;
    table.id         = id;
    table.name       = name;
    table.frameCount = juce::jlimit (1, kMaxCustomWavetableFrames, frameCount);
    table.samples.assign ((size_t) table.frameCount * (size_t) kCustomWavetableFrameSize, 0);
    return table;
}

CustomWavetable makeFromAudio (const float* mono, juce::int64 numSamples,
                               int frameCount, juce::int64 id, const juce::String& name)
{
    auto table = makeEmpty (id, name, frameCount);

    if (mono == nullptr || numSamples <= 0)
        return table;

    std::vector<float> frame ((size_t) kCustomWavetableFrameSize, 0.0f);

    for (int f = 0; f < table.frameCount; ++f)
    {
        // Equal slices, with the boundaries taken from the ratio so no sample is
        // skipped and none is used twice.
        const auto start = (juce::int64) ((double) f * (double) numSamples / (double) table.frameCount);
        auto end         = (juce::int64) ((double) (f + 1) * (double) numSamples / (double) table.frameCount);
        end = juce::jlimit (start, numSamples, end);

        boxResample (mono + start, juce::jmax ((juce::int64) 0, end - start),
                     frame.data(), kCustomWavetableFrameSize);

        // Each frame gets its own DC removed: a frame that sits off centre would
        // otherwise click when the position morphs onto it.
        auto sum = 0.0;

        for (const auto value : frame)
            sum += (double) value;

        const auto mean = (float) (sum / (double) kCustomWavetableFrameSize);

        for (auto& value : frame)
            value -= mean;

        auto* destination = table.samples.data() + (size_t) f * (size_t) kCustomWavetableFrameSize;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            destination[i] = toPcm (frame[(size_t) i]);
    }

    normalise (table);
    return table;
}

CustomWavetable makeFromFrames (const std::vector<std::vector<float>>& frames,
                                juce::int64 id, const juce::String& name)
{
    const auto wanted = juce::jlimit (1, kMaxCustomWavetableFrames, (int) frames.size());
    auto table = makeEmpty (id, name, wanted);

    std::vector<float> frame ((size_t) kCustomWavetableFrameSize, 0.0f);

    for (int f = 0; f < wanted && f < (int) frames.size(); ++f)
    {
        const auto& source = frames[(size_t) f];

        boxResample (source.empty() ? nullptr : source.data(), (juce::int64) source.size(),
                     frame.data(), kCustomWavetableFrameSize);

        auto* destination = table.samples.data() + (size_t) f * (size_t) kCustomWavetableFrameSize;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            destination[i] = toPcm (frame[(size_t) i]);
    }

    return table;
}

CustomWavetable copyBuiltIn (int builtInIndex, int frameCount,
                             juce::int64 id, const juce::String& name)
{
    const auto& source = wavetable::getTable (builtInIndex);
    const auto frames  = juce::jlimit (1, kMaxCustomWavetableFrames,
                                       juce::jmin (frameCount, juce::jmax (1, source.frameCount)));

    auto table = makeEmpty (id, name, frames);

    for (int f = 0; f < frames; ++f)
    {
        const auto* data = source.frameData (0, f);
        auto* destination = table.samples.data() + (size_t) f * (size_t) kCustomWavetableFrameSize;

        for (int i = 0; i < kCustomWavetableFrameSize; ++i)
            destination[i] = toPcm (data[i]);
    }

    return table;
}

void smoothFrame (CustomWavetable& table, int frame)
{
    const auto frames = framesOf (table);

    if (frame < 0 || frame >= frames)
        return;

    std::vector<float> values ((size_t) kCustomWavetableFrameSize, 0.0f);

    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
        values[(size_t) i] = sampleAt (table, frame, i);

    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
    {
        const auto previous = values[(size_t) ((i + kCustomWavetableFrameSize - 1)
                                               % kCustomWavetableFrameSize)];
        const auto next     = values[(size_t) ((i + 1) % kCustomWavetableFrameSize)];
        setSampleAt (table, frame, i, (previous + 2.0f * values[(size_t) i] + next) * 0.25f);
    }
}

void clearFrame (CustomWavetable& table, int frame)
{
    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
        setSampleAt (table, frame, i, 0.0f);
}

void normalise (CustomWavetable& table)
{
    auto peak = 0.0f;

    for (const auto sample : table.samples)
        peak = juce::jmax (peak, std::abs (fromPcm (sample)));

    if (peak <= 1.0e-6f)
        return;

    const auto gain = kNormalisePeak / peak;

    for (auto& sample : table.samples)
        sample = toPcm (fromPcm (sample) * gain);
}

std::vector<float> analyseHarmonics (const CustomWavetable& table, int frame, int count)
{
    return analyseHarmonicsWithPhase (table, frame, count).amplitudes;
}

HarmonicSpectrum analyseHarmonicsWithPhase (const CustomWavetable& table, int frame, int count)
{
    const auto wanted = juce::jlimit (0, kMaxHarmonicBars, count);
    HarmonicSpectrum result;
    result.amplitudes.assign ((size_t) wanted, 0.0f);
    result.phases.assign ((size_t) wanted, 0.0f);

    if (wanted == 0 || frame < 0 || frame >= framesOf (table))
        return result;

    juce::dsp::FFT fft (11);   // 2^11 == kCustomWavetableFrameSize
    std::vector<float> spectrum ((size_t) kCustomWavetableFrameSize * 2, 0.0f);

    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
        spectrum[(size_t) i] = sampleAt (table, frame, i);

    fft.performRealOnlyForwardTransform (spectrum.data());

    // A real signal of amplitude a at bin n turns into a magnitude of a * N / 2.
    const auto scale = 2.0f / (float) kCustomWavetableFrameSize;

    for (int n = 1; n <= wanted; ++n)
    {
        const auto real = spectrum[(size_t) (2 * n)];
        const auto imaginary = spectrum[(size_t) (2 * n + 1)];
        result.amplitudes[(size_t) (n - 1)] =
            std::sqrt (real * real + imaginary * imaginary) * scale;
        auto harmonicPhase = std::atan2 (imaginary, real)
                                 + juce::MathConstants<float>::halfPi;
        while (harmonicPhase > juce::MathConstants<float>::pi)
            harmonicPhase -= juce::MathConstants<float>::twoPi;
        while (harmonicPhase < -juce::MathConstants<float>::pi)
            harmonicPhase += juce::MathConstants<float>::twoPi;
        result.phases[(size_t) (n - 1)] = harmonicPhase;
    }

    return result;
}

void synthesiseFromHarmonics (CustomWavetable& table, int frame,
                              const std::vector<float>& amounts)
{
    synthesiseFromHarmonics (table, frame, amounts, {});
}

void synthesiseFromHarmonics (CustomWavetable& table, int frame,
                              const std::vector<float>& amounts,
                              const std::vector<float>& phases)
{
    const auto frames = framesOf (table);

    if (frame < 0 || frame >= frames || amounts.empty())
        return;

    const auto count = juce::jmin ((int) amounts.size(), kMaxHarmonicBars);

    for (int i = 0; i < kCustomWavetableFrameSize; ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * (double) i
                               / (double) kCustomWavetableFrameSize;
        auto sum = 0.0;

        for (int n = 1; n <= count; ++n)
        {
            const auto harmonicPhase = phases.size() >= (size_t) n
                                           ? (double) phases[(size_t) (n - 1)] : 0.0;
            sum += (double) amounts[(size_t) (n - 1)]
                       * std::sin (phase * (double) n + harmonicPhase);
        }

        setSampleAt (table, frame, i, (float) juce::jlimit (-1.0, 1.0, sum));
    }
}

std::size_t dataHash (const CustomWavetable& table) noexcept
{
    // FNV-1a over the PCM: cheap, and good enough to notice a redrawn point.
    std::size_t hash = 1469598103934665603ull;

    for (const auto sample : table.samples)
    {
        const auto unsignedSample = (unsigned short) sample;
        hash ^= (std::size_t) (unsignedSample & 0xff);
        hash *= 1099511628211ull;
        hash ^= (std::size_t) (unsignedSample >> 8);
        hash *= 1099511628211ull;
    }

    return hash;
}

bool sameData (const CustomWavetable& a, const CustomWavetable& b) noexcept
{
    return a.frameCount == b.frameCount && a.samples == b.samples;
}

void clampToShape (CustomWavetable& table)
{
    table.frameCount = juce::jlimit (1, kMaxCustomWavetableFrames, table.frameCount);

    const auto wanted = (size_t) table.frameCount * (size_t) kCustomWavetableFrameSize;
    table.samples.resize (wanted, 0);
}

int storedBytes (const CustomWavetable& table)
{
    return (int) table.samples.size() * 2;
}

wavetable::Wavetable buildCustomTable (const CustomWavetable& table)
{
    // normalise == false: what the user drew (or the gain the import worked out) is
    // the sound; the editor's 归一化 button is how they change it.
    return wavetable::buildTableFromFrames (toFloats (table), framesOf (table),
                                            table.name, false);
}

namespace
{
    struct CacheEntry
    {
        std::size_t hash = 0;
        std::shared_ptr<const wavetable::Wavetable> table;
    };

    std::map<juce::int64, CacheEntry>& tableCache()
    {
        static std::map<juce::int64, CacheEntry> cache;
        return cache;
    }
}

std::shared_ptr<const wavetable::Wavetable> playableTable (const CustomWavetable& table)
{
    if (table.samples.empty())
        return {};

    const auto hash = dataHash (table);
    auto& cache = tableCache();
    const auto found = cache.find (table.id);

    if (found != cache.end() && found->second.hash == hash && found->second.table != nullptr)
        return found->second.table;

    auto built = std::make_shared<const wavetable::Wavetable> (buildCustomTable (table));
    cache[table.id] = { hash, built };
    return built;
}

std::shared_ptr<const wavetable::Wavetable> playableTableFor (const Project& project,
                                                             juce::int64 customTableId)
{
    if (customTableId == 0)
        return {};

    const auto* table = project.findCustomWavetable (customTableId);

    return table != nullptr ? playableTable (*table) : std::shared_ptr<const wavetable::Wavetable>();
}

void pruneTableCache (const Project& project)
{
    auto& cache = tableCache();

    for (auto it = cache.begin(); it != cache.end(); )
        it = project.findCustomWavetable (it->first) != nullptr ? std::next (it) : cache.erase (it);
}

} // namespace hmdaw::wavetabletables
