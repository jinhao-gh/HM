#pragma once

#include <juce_core/juce_core.h>

namespace nidaw::selftest
{

/** Measures what an external VST3 instrument does with **per-note pitch bend**.

    The host spreads a track's sounding notes over MIDI channels and gives each one
    its own pitch bend, which is how a frequency that is not exactly on a semitone
    comes out at the pitch the project asks for. That only works if the plugin
    treats the bend as *per channel* and applies the bend range the host programs
    into it (±2 semitones, RPN 0). Plenty of instruments do neither: one pitch
    bend moves every voice of the patch, or the plugin keeps its own range and
    never looks at RPN 0.

    This probe answers both questions for one plugin, without needing a project:
    it plays two notes on two channels with different bends and reports which
    frequency each note really came out at. Diagnostic only - it is what
    `--vstprobe` runs.
*/
/** `setExactly` / `setLike` are experiment hooks: "Name=value" sets a parameter
    whose name matches exactly / contains "Name" to that normalised 0..1 value,
    before anything is rendered. They are how "would this plugin behave if its
    per-channel bend routing were switched on?" is answered. */
juce::String runVstPitchProbe (const juce::File& pluginFile, const juce::File& outputFolder,
                               const juce::StringArray& setExactly = {},
                               const juce::StringArray& setLike = {});

} // namespace nidaw::selftest
