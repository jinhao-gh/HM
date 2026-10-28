#include "VstPitchProbe.h"

#include "../Audio/PluginHost.h"
#include "../Core/VstSupport.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace nidaw::selftest
{

namespace
{
    constexpr double kProbeSampleRate = 48000.0;
    constexpr int    kProbeBlockSize  = 512;
    constexpr int    kFftOrder        = 15;                       // 32768 samples
    constexpr int    kFftSize         = 1 << kFftOrder;

    /** The centre of a 14 bit pitch wheel, and the value a +100 cent request is at
        a ±2 semitone range (what the host programs into the plugin). */
    constexpr int kBendCentre = 8192;
    constexpr int kBendPlus100Cents = 4096;

    double hzFor (double base, double cents)
    {
        return base * std::pow (2.0, cents / 1200.0);
    }

    /** The strongest FFT bin within ±1 of this frequency. A peak almost never lands
        exactly on a bin, and a windowed peak is smeared over two or three. */
    double magnitudeAt (const std::vector<float>& magnitudes, double hz, double binWidth)
    {
        const auto bin = (int) std::lround (hz / binWidth);
        auto best = 0.0;

        for (int i = juce::jmax (1, bin - 1); i <= juce::jmin ((int) magnitudes.size() - 1, bin + 1); ++i)
            best = juce::jmax (best, (double) magnitudes[(size_t) i]);

        return best;
    }

    juce::String dbText (double linear)
    {
        return linear > 1.0e-9
                   ? juce::String (juce::Decibels::gainToDecibels (linear), 1)
                   : juce::String ("-inf");
    }
}

juce::String runVstPitchProbe (const juce::File& pluginFile, const juce::File& outputFolder,
                               const juce::StringArray& setExactly,
                               const juce::StringArray& setLike)
{
    juce::StringArray lines;

    auto add = [&lines] (const juce::String& text) { lines.add (text); };

    add ("== VST pitch-bend probe  " + juce::Time::getCurrentTime().toString (true, true) + " ==");
    add ("plugin: " + pluginFile.getFullPathName());

    juce::String error;
    PluginHostManager host;
    auto description = host.describeFile (pluginFile);

    if (description == nullptr)
    {
        add ("读不到这个插件（bundle 里没有 VST3 描述）。");
        return lines.joinIntoString ("\n");
    }

    add ("name  : " + description->name);

    VstParams params;
    params.name = description->name;
    params.pluginPath = pluginFile.getFullPathName();

    auto instrument = host.makeOfflineInstrument (*description, params, kProbeSampleRate,
                                                  kProbeBlockSize, error);
    auto hosted = std::dynamic_pointer_cast<HostedPlugin> (instrument);

    if (hosted == nullptr)
    {
        add ("实例化失败：" + error);
        return lines.joinIntoString ("\n");
    }

    auto& instance = hosted->getInstance();
    const auto channels = juce::jmax (1, instance.getTotalNumOutputChannels());

    // The experiment hooks: change what the plugin does with a bend before the
    // measurement runs (see the header).
    auto applySet = [&instance, &add] (const juce::StringArray& specs, bool exact)
    {
        for (const auto& spec : specs)
        {
            const auto name = spec.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = spec.fromFirstOccurrenceOf ("=", false, false).trim().getDoubleValue();
            auto hits = 0;

            for (auto* parameter : instance.getParameters())
            {
                if (parameter == nullptr)
                    continue;

                const auto parameterName = parameter->getName (64);
                const auto matches = exact ? parameterName == name
                                           : parameterName.containsIgnoreCase (name);

                if (matches)
                {
                    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) value));
                    ++hits;
                }
            }

            add (juce::String (exact ? "exact " : "like ") + "\"" + name + "\" = "
                 + juce::String (value, 3) + "  →  " + juce::String (hits) + " 个参数");
        }
    };

    if (! setExactly.isEmpty() || ! setLike.isEmpty())
    {
        add ("");
        add ("先改这些参数再测：");
        applySet (setExactly, true);
        applySet (setLike, false);
    }

    // What the plugin offers that could change how it treats a pitch bend: an MPE
    // switch turns "one bend per channel" into "one bend per note", and a bend-range
    // control is what a plugin that ignores RPN 0 actually uses.
    {
        juce::StringArray interesting;

        for (auto* parameter : instance.getParameters())
        {
            if (parameter == nullptr)
                continue;

            const auto name = parameter->getName (64);

            if (name.containsIgnoreCase ("mpe") || name.containsIgnoreCase ("bend")
                || name.containsIgnoreCase ("pitch") || name.containsIgnoreCase ("channel")
                || name.containsIgnoreCase ("omni") || name.containsIgnoreCase ("micro")
                || name.containsIgnoreCase ("tuning") || name.containsIgnoreCase ("mono"))
                interesting.add ("  [" + juce::String (parameter->getParameterIndex()) + "] "
                                 + name + " = " + juce::String (parameter->getValue(), 3));
        }

        add ("");
        add ("和弯音 / 通道有关的参数（" + juce::String (interesting.size()) + " 个）：");

        for (const auto& line : interesting)
            add (line);
    }

    // Two notes, two channels, two different bends - the shape the host plays a chord
    // with. The bend of the second note arrives a second after the notes did, so a
    // plugin that treats the bend as global ("the patch bends") drags the *first*
    // note along with it, and that is what the measurement below looks for.
    const auto totalSamples = (int) (kProbeSampleRate * 3.0);
    const auto bendAt = (int) (kProbeSampleRate * 1.0);
    const auto windowStart = (int) (kProbeSampleRate * 1.5);

    juce::AudioBuffer<float> rendered (channels, totalSamples);
    rendered.clear();

    juce::AudioBuffer<float> block (channels, kProbeBlockSize);
    auto written = 0;

    while (written < totalSamples)
    {
        const auto todo = juce::jmin (kProbeBlockSize, totalSamples - written);
        block.setSize (channels, todo, false, false, true);
        block.clear();

        juce::MidiBuffer midi;

        if (written == 0)
        {
            for (int channel = 1; channel <= 2; ++channel)
            {
                // The bend range the host programs in: RPN 0 = ±2 semitones. A plugin
                // that ignores it keeps its own range, and every detuned note lands
                // somewhere else.
                for (const auto& message : vst::pitchBendRangeMessages())
                    midi.addEvent (juce::MidiMessage::controllerEvent (channel, message.controller,
                                                                       message.value),
                                   0);

                midi.addEvent (juce::MidiMessage::pitchWheel (channel, kBendCentre), 0);
            }

            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);   // A4
            midi.addEvent (juce::MidiMessage::noteOn (2, 72, (juce::uint8) 100), 0);   // C5
        }

        if (written <= bendAt && bendAt < written + todo)
            midi.addEvent (juce::MidiMessage::pitchWheel (2, kBendCentre + kBendPlus100Cents),
                           bendAt - written);

        instance.processBlock (block, midi);

        for (int channel = 0; channel < channels; ++channel)
            rendered.copyFrom (channel, written, block, channel, 0, todo);

        written += todo;
    }

    const auto rms = rendered.getRMSLevel (0, windowStart,
                                           juce::jmin (kFftSize, totalSamples - windowStart));
    add ("rendered RMS at the analysis window: " + juce::String (dbText (rms)) + " dB"
         + (rms < 1.0e-5 ? "   <-- 这个插件没有发声（默认音色是空的？）" : ""));

    std::vector<float> spectrum ((size_t) kFftSize * 2, 0.0f);
    const auto* source = rendered.getReadPointer (0);

    for (int i = 0; i < kFftSize; ++i)
    {
        const auto hann = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                  * (float) i / (float) (kFftSize - 1));
        const auto index = windowStart + i;
        spectrum[(size_t) i] = (index < totalSamples ? source[index] : 0.0f) * hann;
    }

    juce::dsp::FFT fft (kFftOrder);
    fft.performRealOnlyForwardTransform (spectrum.data());

    std::vector<float> magnitudes ((size_t) (kFftSize / 2), 0.0f);

    for (int n = 0; n < kFftSize / 2; ++n)
    {
        const auto real = spectrum[(size_t) (2 * n)];
        const auto imaginary = spectrum[(size_t) (2 * n + 1)];
        magnitudes[(size_t) n] = std::sqrt (real * real + imaginary * imaginary);
    }

    const auto binWidth = kProbeSampleRate / (double) kFftSize;

    // The six frequencies that say what happened: what each note should be, and where
    // it would land if the other note's bend had taken it over (at either bend range).
    struct Probe { const char* what; double hz; };
    const Probe probes[] =
    {
        { "A4 = 440.00（通道 1 应该在这里）",            440.0 },
        { "440 + 100c = 466.16（弯音漏过去、范围 ±2）",    hzFor (440.0, 100.0) },
        { "440 + 600c = 622.25（弯音漏过去、范围 ±12）",   hzFor (440.0, 600.0) },
        { "C5 = 523.25（通道 2 的弯音没到）",            523.2511306 },
        { "523.25 + 100c = 554.37（正确：按通道 + 范围 ±2）", hzFor (523.2511306, 100.0) },
        { "523.25 + 600c = 739.99（按通道，但插件范围是 ±12）", hzFor (523.2511306, 600.0) }
    };

    add ("");
    add ("在候选频率上的能量（dB）：");

    for (const auto& probe : probes)
        add ("  " + juce::String (probe.what).paddedRight (' ', 46) + " : "
             + dbText (magnitudeAt (magnitudes, probe.hz, binWidth)));

    // The loudest peaks, so a patch with strong harmonics can still be read by hand.
    add ("");
    add ("最强的几个峰（80–4000 Hz）：");

    std::vector<std::pair<double, double>> peaks;

    for (int n = 2; n < (int) magnitudes.size() - 1; ++n)
    {
        const auto hz = (double) n * binWidth;

        if (hz < 80.0 || hz > 4000.0)
            continue;

        const auto value = magnitudes[(size_t) n];

        if (value > magnitudes[(size_t) (n - 1)] && value >= magnitudes[(size_t) (n + 1)]
            && value > 1.0e-4f)
            peaks.push_back ({ hz, (double) value });
    }

    std::sort (peaks.begin(), peaks.end(), [] (const auto& a, const auto& b)
    {
        return a.second > b.second;
    });

    for (size_t i = 0; i < juce::jmin ((size_t) 8, peaks.size()); ++i)
        add ("  " + juce::String (peaks[i].first, 1) + " Hz : " + dbText (peaks[i].second));

    // ---- the verdict ---------------------------------------------------------
    const auto aPlain    = magnitudeAt (magnitudes, 440.0, binWidth);
    const auto aLeaked2  = magnitudeAt (magnitudes, hzFor (440.0, 100.0), binWidth);
    const auto aLeaked12 = magnitudeAt (magnitudes, hzFor (440.0, 600.0), binWidth);
    const auto cPlain    = magnitudeAt (magnitudes, 523.2511306, binWidth);
    const auto cRight    = magnitudeAt (magnitudes, hzFor (523.2511306, 100.0), binWidth);
    const auto cRange12  = magnitudeAt (magnitudes, hzFor (523.2511306, 600.0), binWidth);

    add ("");
    add ("结论：");

    if (rms < 1.0e-5)
    {
        add ("  没测到声音，无法判断（这个插件的默认状态是空的，比如 Kontakt 没有载入音色）。");
    }
    else
    {
        const auto leaked = juce::jmax (aLeaked2, aLeaked12);

        if (leaked > aPlain * 1.5)
            add ("  ✗ 第二个音的弯音把第一个音也带走了：这个插件把弯音当成**整个音色**的，"
                 "不是每个通道各自的（MPE 风格在这儿不成立）。");
        else
            add ("  ✓ 第一个音没有被第二个音的弯音带走：弯音是按通道走的。");

        if (cRight > cRange12 * 1.5)
            add ("  ✓ 第二个音的弯音正好是 100 音分：插件接受了宿主的弯音范围（±2 半音）。");
        else if (cRange12 > cRight * 1.5)
            add ("  ✗ 第二个音被弯了 600 音分而不是 100：插件**不理会 RPN 0**，用的是它自己的"
                 "范围（这里是 ±12 半音）。");
        else if (cPlain > juce::jmax (cRight, cRange12) * 1.5)
            add ("  ? 第二个音根本没有跟着弯音走。");
        else
            add ("  ? 弯音的结果读不出来（这个音色的谐波太乱）。");
    }

    const auto text = lines.joinIntoString ("\n");

    if (outputFolder != juce::File())
    {
        outputFolder.createDirectory();
        outputFolder.getChildFile ("vst-pitch-probe.txt")
            .appendText (text + "\n\n", false, false, "\n");
    }

    return text;
}

} // namespace nidaw::selftest
