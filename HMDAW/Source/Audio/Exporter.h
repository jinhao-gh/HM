#pragma once

#include "../Core/PluginRendering.h"
#include "../Core/ProjectDocument.h"
#include "SynthEngine.h"

#include <functional>
#include <set>
#include <vector>

namespace hmdaw
{

/** What to write when exporting.

    Only lossless formats so far: JUCE cannot encode MP3 (`MP3AudioFormat`'s
    writer is not implemented) and its LAME wrapper needs an external encoder.
*/
struct ExportSettings
{
    enum class Format { wav, aiff, flac };

    Format format = Format::wav;
    int    bitsPerSample = 24;     // 16 / 24, and 32 means 32-bit float on wav+aiff
    double sampleRate = 0.0;       // 0 == use the project's own rate

    bool master = true;                              // write the whole mix
    std::set<juce::int64> trackIds;                  // one file per chosen track

    /** The extensions this format can be written with. */
    static juce::String extensionFor (Format format);
    /** True when 32 means 32-bit float rather than 32-bit integer. */
    static bool supportsFloat (Format format);
    /** The bit depths the format offers, as combo-box entries. */
    static juce::Array<int> bitDepthsFor (Format format);

    /** Keeps the bit depth legal for the format (FLAC has no float). */
    void clampToFormat();
};

/** Offline (faster than realtime) render, using the very same PlaybackEngine the
    audio device drives, so a file matches what you hear.
*/
class Exporter
{
public:
    /** The block the offline render works in. A VST track's private instance is
        prepared for this size before the render starts. */
    static constexpr int kRenderBlockSize = 1024;

    /** Renders one selection (the master mix, or a single track) to one file.
        `onlyTracks` empty means the whole project. */
    static bool renderToFile (const Project& project,
                              const juce::File& destination,
                              const ExportSettings& settings,
                              const std::set<juce::int64>& onlyTracks,
                              const std::function<void (double)>& onProgress,
                              const std::function<bool()>& shouldAbort,
                              juce::String& errorMessage,
                              const HostedInstrumentFactory& instrumentFactory = {},
                              const HostedEffectFactory& effectFactory = {});

    /** Renders everything the settings ask for into `folder`:
        one file per chosen track plus, optionally, the master mix.

        `onProgress` reports the fraction of the whole batch. Files that were
        written successfully end up in `writtenFiles`. Tracks that cannot be heard
        because of mute / solo are skipped, and their names are reported through
        `skippedTracksOut` when the caller asks for it.
    */
    static bool renderSelection (const Project& project,
                                 const juce::File& folder,
                                 const juce::String& baseName,
                                 const ExportSettings& settings,
                                 const std::function<void (double)>& onProgress,
                                 const std::function<bool()>& shouldAbort,
                                 juce::String& errorMessage,
                                 std::vector<juce::File>& writtenFiles,
                                 std::vector<juce::String>* skippedTracksOut = nullptr,
                                 const HostedInstrumentFactory& instrumentFactory = {},
                                 const HostedEffectFactory& effectFactory = {});

    /** The file name a stem gets: baseName + "_" + a sanitised track name. */
    static juce::String stemFileName (const juce::String& baseName, const juce::String& trackName);
};

} // namespace hmdaw
