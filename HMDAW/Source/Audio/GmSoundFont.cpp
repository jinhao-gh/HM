#include "GmSoundFont.h"

#include <juce_core/juce_core.h>

#define TSF_IMPLEMENTATION
#include "../../external/tsf/tsf.h"

namespace hmdaw
{

namespace
{
juce::File executableDirectory()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
        .getParentDirectory();
}

juce::File sourceResourcesDirectory()
{
    auto cwd = juce::File::getCurrentWorkingDirectory();

    for (int i = 0; i < 4; ++i)
    {
        const auto candidate = cwd.getChildFile ("HMDAW").getChildFile ("Resources")
                                   .getChildFile ("GeneralUserGS.sf2");
        if (candidate.existsAsFile())
            return candidate;

        const auto direct = cwd.getChildFile ("Resources")
                                .getChildFile ("GeneralUserGS.sf2");
        if (direct.existsAsFile())
            return direct;

        cwd = cwd.getParentDirectory();
    }

    return {};
}
} // namespace

GmSoundFont::~GmSoundFont()
{
    if (font != nullptr)
        tsf_close (font);
}

juce::File GmSoundFont::bundledFile()
{
    const auto besideExe = executableDirectory().getChildFile ("GeneralUserGS.sf2");
    if (besideExe.existsAsFile())
        return besideExe;

    return sourceResourcesDirectory();
}

std::shared_ptr<const GmSoundFont> GmSoundFont::loadBundled (juce::String& error)
{
    const auto file = bundledFile();

    if (! file.existsAsFile())
    {
        error = "找不到 GeneralUserGS.sf2";
        return {};
    }

    return load (file, error);
}

std::shared_ptr<const GmSoundFont> GmSoundFont::load (const juce::File& file,
                                                       juce::String& error)
{
    error.clear();

    if (! file.existsAsFile())
    {
        error = "音色库不存在：" + file.getFullPathName();
        return {};
    }

    auto font = std::shared_ptr<GmSoundFont> (new GmSoundFont());
    font->font = tsf_load_filename (file.getFullPathName().toRawUTF8());

    if (font->font == nullptr)
    {
        error = "无法读取 SoundFont：" + file.getFileName();
        return {};
    }

    font->path = file.getFullPathName();

    for (int program = 0; program < 128; ++program)
    {
        font->melodicPresets[program] = tsf_get_presetindex (font->font, 0, program);

        if (font->melodicPresets[program] < 0)
            font->melodicPresets[program] = tsf_get_presetindex (font->font, 0, 0);
    }

    font->drumsPreset = tsf_get_presetindex (font->font, 128, 0);

    if (font->drumsPreset < 0)
    {
        // Some banks put the kit at 128:0 only through a variation.
        for (int i = 0; i < 128 && font->drumsPreset < 0; ++i)
            font->drumsPreset = tsf_get_presetindex (font->font, 128, i);
    }

    return font;
}

int GmSoundFont::presetForProgram (int program) const noexcept
{
    const auto index = juce::jlimit (0, 127, program);
    return melodicPresets[index];
}

int GmSoundFont::drumPreset() const noexcept
{
    return drumsPreset;
}

} // namespace hmdaw
