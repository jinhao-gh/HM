#include "Wavetables.h"

namespace hmdaw::wavetable
{

namespace
{
    constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

    double saw (double phase)    { return 2.0 * phase - 1.0; }
    double square (double phase) { return phase < 0.5 ? 1.0 : -1.0; }
    double triangle (double phase) { return 4.0 * std::abs (phase - 0.5) - 1.0; }

    /** A deterministic pseudo-random value in -1..1 for harmonic n. */
    double hashValue (int n, int salt)
    {
        auto x = (juce::uint32) (n * 2654435761u + (juce::uint32) salt * 40503u);
        x ^= x >> 13;
        x *= 1274126177u;
        x ^= x >> 16;
        return (double) (x & 0xffffu) / 32767.5 - 1.0;
    }

    // The sixteen frames are plain shapes of the phase; the builder turns each one
    // into a spectrum with a forward FFT and then truncates it per level, so even
    // the naive square / pulse shapes come out band limited.

    double sineToSaw (double p, double t)
    {
        return (1.0 - t) * std::sin (kTwoPi * p) + t * saw (p);
    }

    double sawToSquare (double p, double t)
    {
        return (1.0 - t) * saw (p) + t * square (p);
    }

    double oddHarmonics (double p, double t)
    {
        const auto limit = 1 + 2 * (int) std::floor (t * 8.0);
        auto value = 0.0;

        for (int n = 1; n <= limit; n += 2)
            value += std::sin (kTwoPi * n * p) / n;

        return value;
    }

    double formant (double p, double t)
    {
        const auto centre = 1.0 + 24.0 * t;
        constexpr auto width = 2.5;
        auto value = 0.0;

        for (int n = 1; n <= 48; ++n)
        {
            const auto bump = std::exp (-std::pow (n - centre, 2.0) / (2.0 * width * width));
            value += bump * std::sin (kTwoPi * n * p);
        }

        return value;
    }

    double bell (double p, double t)
    {
        static const int partials[] = { 1, 2, 3, 5, 7, 9, 11, 13, 17 };
        auto value = 0.0;

        for (int n : partials)
        {
            const auto amp = std::pow (1.0 / n, 1.3) * (n <= 3 ? 1.0 : 0.4 + 0.6 * t);
            value += amp * std::sin (kTwoPi * n * p);
        }

        return value;
    }

    double pulseWidth (double p, double t)
    {
        const auto duty = 0.5 - 0.45 * t;
        return p < duty ? 1.0 : -1.0;
    }

    double triangleToSaw (double p, double t)
    {
        return (1.0 - t) * triangle (p) + t * saw (p);
    }

    double harmonicStack (double p, double t)
    {
        const auto limit = 1 + (int) std::floor (t * 24.0);
        auto value = 0.0;

        for (int n = 1; n <= limit; ++n)
            value += std::sin (kTwoPi * n * p) / std::pow ((double) n, 1.1);

        return value;
    }

    double fifths (double p, double t)
    {
        static const int partials[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
        const auto limit = 3 + (int) std::floor (t * 5.0);
        auto value = 0.0;

        for (int i = 0; i < limit && i < (int) (sizeof (partials) / sizeof (partials[0])); ++i)
        {
            const auto n = partials[i];
            value += std::sin (kTwoPi * n * p) / std::pow ((double) n, 1.2);
        }

        return value;
    }

    double phaseDistortion (double p, double t)
    {
        const auto knee = 0.5 + 0.4 * t;
        const auto warped = p < knee ? 0.5 * p / knee
                                     : 0.5 + 0.5 * (p - knee) / juce::jmax (1.0e-6, 1.0 - knee);
        return std::sin (kTwoPi * warped);
    }

    double glass (double p, double t)
    {
        return std::sin (kTwoPi * p)
                   + 0.6 * std::sin (kTwoPi * 3.0 * p)
                   + (0.2 + 0.4 * t) * std::sin (kTwoPi * 7.0 * p)
                   + (0.15 + 0.35 * t) * std::sin (kTwoPi * 11.0 * p);
    }

    double vowel (double p, double t)
    {
        const auto first  = 1.0 + 10.0 * t;
        const auto second = 5.0 + 26.0 * t;
        auto value = 0.0;

        for (int n = 1; n <= 48; ++n)
        {
            const auto a = std::exp (-std::pow (n - first, 2.0) / 18.0);
            const auto b = 0.7 * std::exp (-std::pow (n - second, 2.0) / 32.0);
            value += (a + b) * std::sin (kTwoPi * n * p);
        }

        return value;
    }

    double sawStack (double p, double t)
    {
        const auto layers = 1 + (int) std::floor (t * 4.0);
        auto value = 0.0;

        for (int k = 1; k <= layers; ++k)
            value += saw (std::fmod (p * k, 1.0)) / k;

        return value;
    }

    double squareToSine (double p, double t)
    {
        return (1.0 - t) * square (p) + t * std::sin (kTwoPi * p);
    }

    double harmonicNoise (double p, double t)
    {
        const auto limit = 1 + (int) std::floor (t * 48.0);
        auto value = 0.0;

        for (int n = 1; n <= limit; ++n)
            value += hashValue (n, 7) * std::sin (kTwoPi * n * p + hashValue (n, 23))
                         / std::pow ((double) n, 0.9);

        return value;
    }

    double organ (double p, double t)
    {
        return std::sin (kTwoPi * p)
                   + 0.7 * std::sin (kTwoPi * 2.0 * p)
                   + 0.5 * std::sin (kTwoPi * 4.0 * p)
                   + (0.2 + 0.3 * t) * std::sin (kTwoPi * 8.0 * p)
                   + 0.15 * std::sin (kTwoPi * 16.0 * p);
    }

    struct TableDef
    {
        const char* name;                          // UTF-8, read with fromUTF8
        double (*shape) (double phase, double t);
    };

    const TableDef kTables[] =
    {
        { "正弦→锯齿", &sineToSaw },
        { "锯齿→方波", &sawToSquare },
        { "奇次谐波", &oddHarmonics },
        { "共振峰", &formant },
        { "铃声", &bell },
        { "脉冲宽度", &pulseWidth },
        { "三角→锯齿", &triangleToSaw },
        { "谐波堆叠", &harmonicStack },
        { "五度堆叠", &fifths },
        { "相位失真", &phaseDistortion },
        { "玻璃", &glass },
        { "人声", &vowel },
        { "锯齿堆叠", &sawStack },
        { "方波→正弦", &squareToSine },
        { "谐波噪声", &harmonicNoise },
        { "管风琴", &organ },
    };

    constexpr int kTableCount = (int) (sizeof (kTables) / sizeof (kTables[0]));

    std::vector<Wavetable>& bankStorage()
    {
        static std::vector<Wavetable> bank;
        return bank;
    }

    void buildBank()
    {
        auto& bank = bankStorage();
        bank.clear();
        bank.reserve ((size_t) kTableCount);

        for (const auto& def : kTables)
        {
            std::vector<float> frames ((size_t) kFrameCount * (size_t) kFrameSize, 0.0f);

            for (int frame = 0; frame < kFrameCount; ++frame)
            {
                const auto t = kFrameCount > 1 ? (double) frame / (double) (kFrameCount - 1) : 0.0;
                auto* destination = frames.data() + (size_t) frame * (size_t) kFrameSize;

                for (int i = 0; i < kFrameSize; ++i)
                    destination[i] = (float) def.shape ((double) i / (double) kFrameSize, t);
            }

            bank.push_back (buildTableFromFrames (frames, kFrameCount,
                                                  juce::String::fromUTF8 (def.name), true));
        }
    }
}

Wavetable buildTableFromFrames (const std::vector<float>& frames, int frameCount,
                                const juce::String& name, bool normalise)
{
    const auto framesToUse = juce::jlimit (1, kFrameCount, frameCount);

    juce::dsp::FFT fft (11);   // 2^11 == kFrameSize
    std::vector<float> spectrum ((size_t) kFrameSize * 2, 0.0f);
    std::vector<float> scratch ((size_t) kFrameSize * 2, 0.0f);

    Wavetable table;
    table.name       = name;
    table.frameCount = framesToUse;
    table.levelCount = kLevelCount;
    table.levels.assign ((size_t) kLevelCount,
                         std::vector<float> ((size_t) framesToUse * kFrameSize, 0.0f));

    auto peak = 0.0f;

    for (int frame = 0; frame < framesToUse; ++frame)
    {
        // One frame in the time domain, then forward transformed: the truncation
        // below is what band limits every level.
        std::fill (spectrum.begin(), spectrum.end(), 0.0f);

        const auto offset = (size_t) frame * (size_t) kFrameSize;

        for (int i = 0; i < kFrameSize; ++i)
            if (offset + (size_t) i < frames.size())
                spectrum[(size_t) i] = frames[offset + (size_t) i];

        fft.performRealOnlyForwardTransform (spectrum.data());

        for (int level = 0; level < kLevelCount; ++level)
        {
            const auto harmonics = juce::jmax (1, kMaxHarmonics >> level);
            scratch = spectrum;

            for (int n = harmonics + 1; n <= kFrameSize / 2; ++n)
            {
                scratch[(size_t) (2 * n)] = 0.0f;
                scratch[(size_t) (2 * n + 1)] = 0.0f;

                const auto mirror = kFrameSize - n;
                scratch[(size_t) (2 * mirror)] = 0.0f;
                scratch[(size_t) (2 * mirror + 1)] = 0.0f;
            }

            fft.performRealOnlyInverseTransform (scratch.data());

            auto* destination = table.levels[(size_t) level].data()
                                    + (size_t) frame * (size_t) kFrameSize;
            std::copy (scratch.begin(), scratch.begin() + kFrameSize, destination);

            if (level == 0 && normalise)
                for (int i = 0; i < kFrameSize; ++i)
                    peak = juce::jmax (peak, std::abs (destination[i]));
        }
    }

    // One gain for the whole table, taken from the full-bandwidth frames, so a
    // quiet level is quiet rather than re-normalised.
    if (normalise && peak > 1.0e-6f)
    {
        const auto gain = 1.0f / peak;

        for (auto& level : table.levels)
            for (auto& sample : level)
                sample *= gain;
    }

    return table;
}

void ensureBuilt()
{
    // Magic statics make this run exactly once, from whichever thread gets here
    // first; every real path calls it from the message thread.
    static const bool built = [] { buildBank(); return true; }();
    juce::ignoreUnused (built);
}

int builtInTableCount()
{
    return kTableCount;
}

juce::String tableName (int index)
{
    const auto clamped = juce::jlimit (0, kTableCount - 1, index);
    return juce::String::fromUTF8 (kTables[clamped].name);
}

const Wavetable& getTable (int index)
{
    ensureBuilt();

    const auto& bank = bankStorage();
    const auto clamped = juce::jlimit (0, (int) bank.size() - 1, index);
    return bank[(size_t) clamped];
}

} // namespace hmdaw::wavetable
