#pragma once

#include "WavetableTables.h"

#include <cmath>

namespace hmdaw::waveformpresets
{

struct WaveformPreset
{
    const char* name;
    bool multiFrame;
};

inline std::vector<WaveformPreset>& bank()
{
    static std::vector<WaveformPreset> presets = {
        { "正弦", false },
        { "三角", false },
        { "锯齿", false },
        { "方波", false },
        { "25% 脉冲", false },
        { "管风琴", false },
        { "玻璃铃", false },
        { "元音阿", false },
        { "金属谐波", false },
        { "软饱和锯齿", false },
        { "正弦→锯齿", true },
        { "三角→方波", true },
        { "共振峰扫频", true },
        { "谐波堆叠", true },
        { "玻璃→噪声", true },
        { "五度堆叠", true }
    };
    return presets;
}

inline int count() { return (int) bank().size(); }
inline juce::String name (int index)
{
    const auto& list = bank();
    return list.empty() ? juce::String()
                        : juce::String::fromUTF8 (list[(size_t) juce::jlimit (
                              0, (int) list.size() - 1, index)].name);
}
inline bool isMultiFrame (int index)
{
    const auto& list = bank();
    return ! list.empty() && list[(size_t) juce::jlimit (0, (int) list.size() - 1, index)].multiFrame;
}

namespace detail
{
    inline float wrap (float p) { return p - std::floor (p); }
    inline float sine (float p) { return std::sin (juce::MathConstants<float>::twoPi * p); }
    inline float saw (float p) { return 2.0f * wrap (p) - 1.0f; }
    inline float square (float p) { return wrap (p) < 0.5f ? 1.0f : -1.0f; }
    inline float triangle (float p) { return 4.0f * std::abs (wrap (p) - 0.5f) - 1.0f; }

    inline float singleShape (int index, float p)
    {
        switch (index)
        {
            case 0: return sine (p);
            case 1: return triangle (p);
            case 2: return saw (p);
            case 3: return square (p);
            case 4: return wrap (p) < 0.25f ? 1.0f : -1.0f;
            case 5: return 0.55f * sine (p) + 0.3f * sine (p * 2.0f) + 0.15f * sine (p * 4.0f);
            case 6: return 0.55f * sine (p) + 0.3f * sine (p * 3.0f) + 0.15f * sine (p * 7.0f);
            case 7: return 0.5f * sine (p) + 0.25f * sine (p * 2.0f) + 0.2f * sine (p * 3.0f);
            case 8: return 0.45f * sine (p) + 0.35f * sine (p * 5.0f) + 0.2f * sine (p * 11.0f);
            default: return std::tanh (2.5f * saw (p)) * 0.8f;
        }
    }

    inline float mixedShape (int index, float p, float t)
    {
        switch (index)
        {
            case 10: return (1.0f - t) * sine (p) + t * saw (p);
            case 11: return (1.0f - t) * triangle (p) + t * square (p);
            case 12:
            {
                const auto formant = 0.5f + 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * (p * (1.0f + t * 6.0f)));
                return (0.55f * sine (p) + 0.35f * sine (p * 3.0f)) * formant;
            }
            case 13: return 0.4f * sine (p) + 0.25f * sine (p * 3.0f)
                            + 0.15f * sine (p * 5.0f) + 0.1f * sine (p * 7.0f)
                            + 0.1f * sine (p * (9.0f + t * 8.0f));
            case 14:
            {
                const auto hiss = wrap (p * (7.0f + t * 31.0f)) * 0.18f * t;
                return (1.0f - t) * singleShape (6, p) + hiss;
            }
            default:
                return 0.45f * sine (p) + 0.2f * sine (p * 3.0f)
                       + (0.15f + 0.1f * t) * sine (p * 5.0f)
                       + 0.15f * t * sine (p * (7.0f + 2.0f * t));
        }
    }
}

inline CustomWavetable build (int index, juce::int64 id)
{
    const auto i = juce::jlimit (0, juce::jmax (0, count() - 1), index);
    const auto multi = isMultiFrame (i);
    const auto frames = multi ? 8 : 1;
    std::vector<std::vector<float>> data ((size_t) frames,
                                          std::vector<float> (kCustomWavetableFrameSize));

    for (int f = 0; f < frames; ++f)
    {
        const auto t = frames > 1 ? (float) f / (float) (frames - 1) : 0.0f;

        for (int s = 0; s < kCustomWavetableFrameSize; ++s)
        {
            const auto phase = (float) s / (float) kCustomWavetableFrameSize;
            data[(size_t) f][(size_t) s] = multi ? detail::mixedShape (i, phase, t)
                                                 : detail::singleShape (i, phase);
        }
    }

    auto table = wavetabletables::makeFromFrames (data, id, name (i));
    wavetabletables::normalise (table);
    return table;
}

} // namespace hmdaw::waveformpresets
