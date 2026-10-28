# The sources both targets compile: the domain logic in Core plus the parts of the
# audio engine the unit tests drive directly.
#
# This list exists in one file on purpose. It used to be written out three times
# (the app target and the in-tree test target in NiDaw/CMakeLists.txt, plus
# work/testproj/CMakeLists.txt), so adding one file - WindowPlacement.cpp was the
# last one - meant remembering to edit all three. Both projects set NIDAW_ROOT
# before including this file.
set (NIDAW_SHARED_SOURCES
    "${NIDAW_ROOT}/Source/Core/VstSupport.cpp"
    "${NIDAW_ROOT}/Source/Core/PluginScanProtocol.cpp"
    "${NIDAW_ROOT}/Source/Core/PluginGrouping.cpp"
    "${NIDAW_ROOT}/Source/Core/EffectChain.cpp"
    "${NIDAW_ROOT}/Source/Core/WindowPlacement.cpp"
    "${NIDAW_ROOT}/Source/Core/MusicMath.cpp"
    "${NIDAW_ROOT}/Source/Core/MidiInputMap.cpp"
    "${NIDAW_ROOT}/Source/Core/MidiArp.cpp"
    "${NIDAW_ROOT}/Source/Core/KeyboardLayouts.cpp"
    "${NIDAW_ROOT}/Source/Core/ProjectDocument.cpp"
    "${NIDAW_ROOT}/Source/Core/PresetStore.cpp"
    "${NIDAW_ROOT}/Source/Core/Wavetables.cpp"
    "${NIDAW_ROOT}/Source/Core/WavetableTables.cpp"
    "${NIDAW_ROOT}/Source/Audio/SynthEngine.cpp"
    "${NIDAW_ROOT}/Source/Audio/SubtractiveSynth.cpp"
    "${NIDAW_ROOT}/Source/Audio/WavetableSynth.cpp"
    "${NIDAW_ROOT}/Source/Audio/DrumSynth.cpp"
    "${NIDAW_ROOT}/Source/Audio/SamplePlayer.cpp"
    "${NIDAW_ROOT}/Source/Audio/GranularSynth.cpp"
    "${NIDAW_ROOT}/Source/Audio/HybridFxChain.cpp"
    "${NIDAW_ROOT}/Source/Audio/HybridSynth.cpp"
    "${NIDAW_ROOT}/Source/Audio/AudioEngine.cpp"
    "${NIDAW_ROOT}/Source/Audio/Exporter.cpp")
