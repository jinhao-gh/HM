# The sources both targets compile: the domain logic in Core plus the parts of the
# audio engine the unit tests drive directly.
#
# This list exists in one file on purpose. It used to be written out three times
# (the app target and the in-tree test target in HMDAW/CMakeLists.txt, plus
# work/testproj/CMakeLists.txt), so adding one file - WindowPlacement.cpp was the
# last one - meant remembering to edit all three. Both projects set HMDAW_ROOT
# before including this file.
set (HMDAW_SHARED_SOURCES
    "${HMDAW_ROOT}/Source/Core/VstSupport.cpp"
    "${HMDAW_ROOT}/Source/Core/PluginScanProtocol.cpp"
    "${HMDAW_ROOT}/Source/Core/PluginGrouping.cpp"
    "${HMDAW_ROOT}/Source/Core/EffectChain.cpp"
    "${HMDAW_ROOT}/Source/Core/WindowPlacement.cpp"
    "${HMDAW_ROOT}/Source/Core/MusicMath.cpp"
    "${HMDAW_ROOT}/Source/Core/MidiInputMap.cpp"
    "${HMDAW_ROOT}/Source/Core/MidiArp.cpp"
    "${HMDAW_ROOT}/Source/Core/KeyboardLayouts.cpp"
    "${HMDAW_ROOT}/Source/Core/ProjectDocument.cpp"
    "${HMDAW_ROOT}/Source/Core/PresetStore.cpp"
    "${HMDAW_ROOT}/Source/Core/Wavetables.cpp"
    "${HMDAW_ROOT}/Source/Core/WavetableTables.cpp"
    "${HMDAW_ROOT}/Source/Audio/SynthEngine.cpp"
    "${HMDAW_ROOT}/Source/Audio/SubtractiveSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/MinimoogSynth.cpp"
    "${HMDAW_ROOT}/Source/Core/StudioScene.cpp"
    "${HMDAW_ROOT}/Source/Audio/StudioRenderPlan.cpp"
    "${HMDAW_ROOT}/Source/Audio/StudioRenderer.cpp"
    "${HMDAW_ROOT}/Source/Audio/GmSoundFont.cpp"
    "${HMDAW_ROOT}/Source/Audio/GmSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/WavetableSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/DrumSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/SamplePlayer.cpp"
    "${HMDAW_ROOT}/Source/Audio/GranularSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/HybridFxChain.cpp"
    "${HMDAW_ROOT}/Source/Audio/HybridSynth.cpp"
    "${HMDAW_ROOT}/Source/Audio/AudioEngine.cpp"
    "${HMDAW_ROOT}/Source/Audio/Exporter.cpp")
