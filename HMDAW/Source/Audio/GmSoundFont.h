#pragma once

#include <juce_core/juce_core.h>
#include <memory>

struct tsf;

namespace hmdaw
{

/** A loaded SoundFont, shared read-only by every GM track. */
class GmSoundFont
{
public:
    static std::shared_ptr<const GmSoundFont> load (const juce::File& file, juce::String& error);
    static std::shared_ptr<const GmSoundFont> loadBundled (juce::String& error);
    static juce::File bundledFile();

    ~GmSoundFont();

    tsf* raw() const noexcept { return font; }
    const juce::String& getPath() const noexcept { return path; }

    /** TSF preset index for a GM melodic program (bank 0), or -1. */
    int presetForProgram (int program) const noexcept;

    /** TSF preset index for the GM percussion bank, or -1. */
    int drumPreset() const noexcept;

    bool isUsable() const noexcept { return font != nullptr; }

private:
    GmSoundFont() = default;
    tsf* font = nullptr;
    juce::String path;
    int melodicPresets[128] {};
    int drumsPreset = -1;
};

} // namespace hmdaw
