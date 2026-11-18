#include "Exporter.h"
#include "AudioEngine.h"
#include "../Core/FmMath.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace hmdaw
{

namespace
{
    constexpr int kRenderBlockSize = Exporter::kRenderBlockSize;

    /** How much extra time a file gets so a VST track's release is not cut off.
        A plugin's own tail length is not knowable before it is loaded, so this is
        a fixed allowance - generous enough for a reverb, short enough to be cheap. */
    constexpr double kHostedInstrumentTailSeconds = 3.0;

    /** A writer for the chosen format, or nullptr. */
    std::unique_ptr<juce::AudioFormatWriter> makeWriter (ExportSettings::Format format,
                                                         std::unique_ptr<juce::OutputStream>& stream,
                                                         double sampleRate,
                                                         int bitsPerSample)
    {
        const auto useFloat = bitsPerSample > 24 && ExportSettings::supportsFloat (format);

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleFormat (useFloat
                                                        ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral)
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (bitsPerSample);

        switch (format)
        {
            case ExportSettings::Format::wav:
            {
                juce::WavAudioFormat wav;
                return wav.createWriterFor (stream, options);
            }

            case ExportSettings::Format::aiff:
            {
                juce::AiffAudioFormat aiff;
                return aiff.createWriterFor (stream, options);
            }

            case ExportSettings::Format::flac:
            {
                juce::FlacAudioFormat flac;
                return flac.createWriterFor (stream, options);
            }
        }

        return nullptr;
    }
}

juce::String ExportSettings::extensionFor (Format format)
{
    switch (format)
    {
        case Format::wav:  return "wav";
        case Format::aiff: return "aiff";
        case Format::flac: return "flac";
    }

    return "wav";
}

bool ExportSettings::supportsFloat (Format format)
{
    // Only the WAV writer takes 32-bit float here: JUCE's AIFF writer tops out at
    // 24-bit (its possible bit depths are 8/16/24) and FLAC has no float at all.
    return format == Format::wav;
}

juce::Array<int> ExportSettings::bitDepthsFor (Format format)
{
    switch (format)
    {
        case Format::wav:  return { 16, 24, 32 };
        case Format::aiff: return { 16, 24 };
        case Format::flac: return { 16, 24 };
    }

    return { 16 };
}

void ExportSettings::clampToFormat()
{
    const auto allowed = bitDepthsFor (format);

    if (! allowed.contains (bitsPerSample))
        bitsPerSample = allowed.getLast();
}

bool Exporter::renderToFile (const Project& project,
                             const juce::File& destination,
                             const ExportSettings& settings,
                             const std::set<juce::int64>& onlyTracks,
                             const std::function<void (double)>& onProgress,
                             const std::function<bool()>& shouldAbort,
                             juce::String& errorMessage,
                             const HostedInstrumentFactory& instrumentFactory,
                             const HostedEffectFactory& effectFactory)
{
    const auto sampleRate = settings.sampleRate > 0.0
                                ? settings.sampleRate
                                : (project.sampleRate > 0.0 ? project.sampleRate : 48000.0);
    auto schedule = std::make_shared<const RenderSchedule> (
        buildSchedule (project, onlyTracks, instrumentFactory, effectFactory));

    if (schedule->lengthSeconds <= 0.0)
    {
        auto audioCount = 0;
        auto audibleTracks = 0;

        for (const auto& track : project.tracks)
        {
            if (! onlyTracks.empty() && onlyTracks.find (track.id) == onlyTracks.end())
                continue;

            if (isTrackAudible (project, track))
                ++audibleTracks;

            for (const auto& clip : track.clips)
                audioCount += (int) (clip.notes.size() + (clip.isAudio() ? 1 : 0));
        }

        // Track level mute / solo is a different story from every note being
        // muted, so the two get their own message.
        errorMessage = audibleTracks == 0
                           ? NI_TXT ("所有音轨都被静音或不在独奏状态，没有可导出的声音。")
                           : (audioCount > 0 ? NI_TXT ("所有音符都被静音了，没有可导出的声音。")
                                             : NI_TXT ("工程里还没有音符，无法导出。"));
        return false;
    }

    auto tailSeconds = 0.5;

    for (const auto& track : project.tracks)
    {
        if (! onlyTracks.empty() && onlyTracks.find (track.id) == onlyTracks.end())
            continue;

        // A hosted plugin's release is unknown until it has been loaded, so the
        // file gets a fixed allowance on top instead.
        if (track.kind == TrackKind::vst)
            tailSeconds = juce::jmax (tailSeconds, kHostedInstrumentTailSeconds);

        // The tail has to cover the slowest release in the FM patch, otherwise the
        // export cuts the last chord off.
        auto release = 0.0;

        for (const auto& op : track.synth.patch.ops)
            release = juce::jmax (release, fm::rateToSeconds (op.rate[3]));

        tailSeconds = juce::jmax (tailSeconds, release + 0.3);
    }

    const auto totalSeconds = schedule->lengthSeconds + tailSeconds;
    const auto totalSamples = (juce::int64) std::ceil (totalSeconds * sampleRate);

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();

    if (stream == nullptr)
    {
        errorMessage = NI_TXT ("无法创建输出文件：") + destination.getFullPathName();
        return false;
    }

    auto writer = makeWriter (settings.format, stream, sampleRate, settings.bitsPerSample);

    if (writer == nullptr)
    {
        errorMessage = NI_TXT ("无法创建音频写入器（格式与位深组合可能不被支持）。");
        return false;
    }

    PlaybackEngine engine;
    engine.prepare (sampleRate, kRenderBlockSize);
    engine.setSchedule (schedule);
    engine.setPositionSeconds (0.0);
    engine.setPlaying (true);

    auto& block = engine.getOutputBuffer();
    juce::int64 written = 0;

    while (written < totalSamples)
    {
        if (shouldAbort && shouldAbort())
        {
            errorMessage = NI_TXT ("导出已取消。");
            return false;
        }

        const auto todo = (int) juce::jmin ((juce::int64) kRenderBlockSize, totalSamples - written);

        engine.renderNextBlock (todo);

        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getWritePointer (channel);

            for (int i = 0; i < todo; ++i)
                samples[i] = juce::jlimit (-1.0f, 1.0f, samples[i]);
        }

        if (! writer->writeFromAudioSampleBuffer (block, 0, todo))
        {
            errorMessage = NI_TXT ("写入音频数据失败。");
            return false;
        }

        written += todo;

        if (onProgress)
            onProgress ((double) written / (double) totalSamples);
    }

    writer->flush();
    return true;
}

juce::String Exporter::stemFileName (const juce::String& baseName, const juce::String& trackName)
{
    // Windows forbids these in a file name; a track called "鼓 / 打击" would
    // otherwise create a path instead of a file.
    static const juce::String illegal ("\\/:*?\"<>|");
    auto name = trackName;

    for (auto c : illegal)
        name = name.replaceCharacter (c, '_');

    name = name.trim();

    if (name.isEmpty())
        name = NI_TXT ("未命名");

    return baseName + "_" + name;
}

bool Exporter::renderSelection (const Project& project,
                                const juce::File& folder,
                                const juce::String& baseName,
                                const ExportSettings& settings,
                                const std::function<void (double)>& onProgress,
                                 const std::function<bool()>& shouldAbort,
                                 juce::String& errorMessage,
                                 std::vector<juce::File>& writtenFiles,
                                 std::vector<juce::String>* skippedTracksOut,
                                 const HostedInstrumentFactory& instrumentFactory,
                                 const HostedEffectFactory& effectFactory)
{
    writtenFiles.clear();

    if (skippedTracksOut != nullptr)
        skippedTracksOut->clear();

    // Work out what to write, in a stable order: the master mix first, then the
    // tracks in the order the project holds them.
    struct Item
    {
        juce::String name;
        std::set<juce::int64> tracks;
    };

    std::vector<Item> items;

    if (settings.master)
        items.push_back ({ NI_TXT ("总混音"), {} });

    for (const auto& track : project.tracks)
    {
        if (settings.trackIds.find (track.id) == settings.trackIds.end())
            continue;

        // A track that mute / solo keeps silent gets no file at all: writing a
        // silent stem would look like a bug rather than a choice.
        if (! isTrackAudible (project, track))
        {
            if (skippedTracksOut != nullptr)
                skippedTracksOut->push_back (track.name);

            continue;
        }

        items.push_back ({ track.name, { track.id } });
    }

    if (items.empty())
    {
        errorMessage = NI_TXT ("没有勾选任何要导出的内容。");
        return false;
    }

    folder.createDirectory();
    const auto extension = ExportSettings::extensionFor (settings.format);

    // Two tracks can share a name; keep the files apart by appending an index.
    std::vector<juce::String> usedNames;

    for (size_t i = 0; i < items.size(); ++i)
    {
        auto stem = stemFileName (baseName, items[i].name);

        if (std::find (usedNames.begin(), usedNames.end(), stem) != usedNames.end())
            stem += " (" + juce::String ((int) i + 1) + ")";

        usedNames.push_back (stem);

        const auto file = folder.getChildFile (stem).withFileExtension (extension);

        // Each file reports its own progress; the batch turns that into one
        // overall fraction so a single progress window stays honest.
        const auto itemIndex = (double) i;
        const auto itemCount = (double) items.size();

        auto itemProgress = [&onProgress, itemIndex, itemCount] (double fraction)
        {
            if (onProgress)
                onProgress ((itemIndex + juce::jlimit (0.0, 1.0, fraction)) / itemCount);
        };

        juce::String itemError;

        if (! renderToFile (project, file, settings, items[i].tracks,
                            itemProgress, shouldAbort, itemError, instrumentFactory,
                            effectFactory))
        {
            errorMessage = items[i].name + "：" + itemError;
            return false;
        }

        writtenFiles.push_back (file);
    }

    if (writtenFiles.empty())
    {
        errorMessage = NI_TXT ("勾选的轨道都处于静音 / 非独奏状态，没有可导出的内容。");
        return false;
    }

    if (onProgress)
        onProgress (1.0);

    return true;
}

} // namespace hmdaw
