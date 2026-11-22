#pragma once

#include "Types.h"

#include <juce_core/juce_core.h>

namespace hmdaw
{

/** Studio scene persistence and the old-project migration. */
juce::var studioSceneToVar (const studio::Scene& scene);
bool studioSceneFromVar (const juce::var& value, studio::Scene& scene, juce::String& error);

/** Adds any missing default pieces: microphone/input/mixer for an empty scene,
    and a source for every track. */
void ensureStudioForProject (Project& project);

/** Old projects have no studio block: preserve their direct-to-master sound by
    giving every track its own mixer input/channel, fed directly from the track. */
void migrateLegacyStudio (Project& project);

} // namespace hmdaw
