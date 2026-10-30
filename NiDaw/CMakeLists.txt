cmake_minimum_required(VERSION 3.22)

project(NiDaw VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC)
    # cl.exe prints its /showIncludes marker as UTF-8 ("注意: 包含文件:  "), but CMake
    # decodes it with the 936 ANSI codepage and records mojibake, so Ninja's
    # msvc_deps_prefix never matches and every dependency file comes out empty -
    # header edits then silently re-use objects built against the old struct layout.
    # Put the bytes the compiler really prints back after project() has probed it.
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# See docs/05-build-env.md for the /showIncludes story.

# The build machine's ANSI codepage is 936 (GBK); all sources are UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The test plugin is only ever built as VST3, so it has no VST2 automation to stay
# compatible with. JUCE insists on being told that explicitly when it compiles a
# VST3 wrapper; the flag is meaningless for the app, which is not a plugin.
add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0)

# JUCE is vendored under external/JUCE (8.0.15).
add_subdirectory(external/JUCE)

juce_add_gui_app(NiDaw
    PRODUCT_NAME "HM"
    COMPANY_NAME "HM"
    VERSION ${PROJECT_VERSION}
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

# The list of files both targets compile, so a new one is added in a single place
# (cmake/SharedSources.cmake). NIDAW_ROOT is what that file builds its paths from.
set (NIDAW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include ("${NIDAW_ROOT}/cmake/SharedSources.cmake")

target_sources(NiDaw PRIVATE
    ${NIDAW_SHARED_SOURCES}
    Source/UI/AutoScroll.h
    Source/Main.cpp
    Source/SelfTest/SelfTest.h
    Source/SelfTest/SelfTest.cpp
    Source/SelfTest/VstPitchProbe.h
    Source/SelfTest/VstPitchProbe.cpp
    Source/Core/Types.h
    Source/Core/ControllerMath.h
    Source/Core/MeterMath.h
    Source/Core/VstSupport.h
    Source/Core/PluginScanProtocol.h
    Source/Core/PluginRendering.h
    Source/Core/PluginGrouping.h
    Source/Core/EffectChain.h
    Source/Core/WindowPlacement.h
    Source/Core/MusicMath.h
    Source/Core/MidiArp.h
    Source/Core/ProjectDocument.h
    Source/Core/PresetStore.h
    Source/Audio/SynthEngine.h
    Source/Audio/SamplePlayer.h
    Source/Audio/SubtractiveSynth.h
    Source/Audio/MinimoogSynth.h
    Source/Audio/MinimoogSynth.cpp
    Source/Audio/WavetableSynth.h
    Source/Audio/DrumSynth.h
    Source/Audio/GranularSynth.h
    Source/Audio/HybridFxChain.h
    Source/Audio/HybridSynth.h
    Source/Core/Wavetables.h
    Source/Core/WavetableTables.h
    Source/Core/WavetableTables.cpp
    Source/Core/HybridPresets.h
    Source/Core/MinimoogPresets.h
    Source/Core/WaveformPresets.h
    Source/Audio/AudioEngine.h
    Source/Audio/Exporter.h
    Source/Audio/PluginHost.h
    Source/Audio/PluginHost.cpp
    Source/Audio/MidiInputManager.h
    Source/Audio/MidiInputManager.cpp
    Source/Audio/PluginScanChild.h
    Source/Audio/PluginScanChild.cpp
    Source/UI/AppLookAndFeel.h
    Source/UI/AppLookAndFeel.cpp
    Source/UI/Theme.h
    Source/UI/AppState.h
    Source/UI/PanelHandle.h
    Source/UI/PanelHandle.cpp
    Source/UI/TopToolbar.h
    Source/UI/TopToolbar.cpp
    Source/UI/LaneHeader.h
    Source/UI/LaneHeader.cpp
    Source/UI/TrackArea.h
    Source/UI/TrackArea.cpp
    Source/UI/TransportBar.h
    Source/UI/TransportBar.cpp
    Source/UI/TempoMarkerTrack.h
    Source/UI/TempoMarkerTrack.cpp
    Source/UI/TimelineRuler.h
    Source/UI/TimelineRuler.cpp
    Source/UI/OctaveLadder.h
    Source/UI/OctaveLadder.cpp
    Source/UI/ReferenceGroupPanel.h
    Source/UI/ReferenceGroupPanel.cpp
    Source/UI/GroupControls.h
    Source/UI/GroupControls.cpp
    Source/UI/GroupEditorPanel.h
    Source/UI/GroupEditorPanel.cpp
    Source/UI/PresetPanel.h
    Source/UI/PresetPanel.cpp
    Source/UI/SynthEditor.h
    Source/UI/SynthEditor.cpp
    Source/UI/AlgorithmDiagram.h
    Source/UI/AlgorithmDiagram.cpp
    Source/UI/EnvelopeCurve.h
    Source/UI/EnvelopeCurve.cpp
    Source/UI/SynthControls.h
    Source/UI/SynthControls.cpp
    Source/UI/PresetBar.h
    Source/UI/PresetBar.cpp
    Source/UI/SamplerEditor.h
    Source/UI/SamplerEditor.cpp
    Source/UI/SubtractiveEditor.h
    Source/UI/SubtractiveEditor.cpp
    Source/UI/MinimoogEditor.h
    Source/UI/MinimoogEditor.cpp
    Source/UI/WavetableEditor.h
    Source/UI/WavetableEditor.cpp
    Source/UI/WavetableTableWindow.h
    Source/UI/WavetableTableWindow.cpp
    Source/UI/DrumEditor.h
    Source/UI/DrumEditor.cpp
    Source/UI/GranularEditor.h
    Source/UI/GranularEditor.cpp
    Source/UI/HybridEditor.h
    Source/UI/HybridEditor.cpp
    Source/UI/HybridOscillatorWindow.h
    Source/UI/HybridOscillatorWindow.cpp
    Source/UI/HybridModulationPanel.h
    Source/UI/HybridModulationPanel.cpp
    Source/UI/HybridFxPanel.h
    Source/UI/HybridFxPanel.cpp
    Source/UI/HybridMixerPanel.h
    Source/UI/HybridMixerPanel.cpp
    Source/UI/SynthWindow.h
    Source/UI/SynthWindow.cpp
    Source/UI/PluginPicker.h
    Source/UI/PluginPicker.cpp
    Source/UI/PluginWindow.h
    Source/UI/PluginWindow.cpp
    Source/UI/EffectsBar.h
    Source/UI/EffectsBar.cpp
    Source/UI/EffectsWindow.h
    Source/UI/EffectsWindow.cpp
    Source/UI/MixerWindow.h
    Source/UI/MixerWindow.cpp
    Source/UI/MidiInputWindow.h
    Source/UI/MidiInputWindow.cpp
    Source/UI/ArpControls.h
    Source/UI/ArpControls.cpp
    Source/UI/VirtualKeyboardWindow.h
    Source/UI/VirtualKeyboardWindow.cpp
    Source/UI/NoteEditBar.h
    Source/UI/NoteEditBar.cpp
    Source/UI/SnapControl.h
    Source/UI/SnapControl.cpp
    Source/UI/ControllerLane.h
    Source/UI/ControllerLane.cpp
    Source/UI/OptionsPanel.h
    Source/UI/OptionsPanel.cpp
    Source/UI/PianoGrid.h
    Source/UI/PianoGrid.cpp
    Source/UI/EditorPanel.h
    Source/UI/EditorPanel.cpp
    Source/UI/MainComponent.h
    Source/UI/MainComponent.cpp
    Source/Tests/TestSupport.h
    Source/Tests/TestMain.cpp
    Source/Tests/MathTests.cpp
    Source/Tests/ClipTests.cpp
    Source/Tests/SerializationTests.cpp
    Source/Tests/InstrumentTests.cpp
    Source/Tests/MinimoogTests.cpp
    Source/Tests/PlaybackTests.cpp
    Source/Tests/PresetTests.cpp
    Source/Tests/PluginTests.cpp
    Source/Tests/MeterMathTests.cpp
    Source/Tests/ControllerTests.cpp
    Source/Tests/MidiInputMapTests.cpp)

target_include_directories(NiDaw PRIVATE Source)

target_compile_definitions(NiDaw PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Hosting VST3 instruments: the SDK ships with JUCE (modules/
    # juce_audio_processors_headless/format_types/VST3_SDK), so this needs no
    # download. VST2 is not an option: its SDK is neither bundled nor licensed
    # for redistribution.
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_AU=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NiDaw,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NiDaw,JUCE_VERSION>"
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_REPORT_APP_USAGE=0)
    # JUCE_DISPLAY_SPLASH_SCREEN is left at its default: removing the JUCE splash
    # screen requires an Indie/Pro JUCE licence. Add JUCE_DISPLAY_SPLASH_SCREEN=0
    # here if you hold one, or if the project complies with the AGPLv3.

target_link_libraries(NiDaw
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Emit a linker map so a crash address from a bug report can be resolved to a
# function without a debugger attached.
if(MSVC)
    target_link_options(NiDaw PRIVATE "/MAP:${CMAKE_BINARY_DIR}/NiDaw.map")
endif()

# A tiny VST3 instrument of our own. The offscreen self test loads it and renders
# through it, which is the only way to check the hosting path (instantiate, notes,
# pitch, state) without depending on what the person running the test happens to
# have installed. It is never shipped: it only exists to be tested against.
juce_add_plugin(NiDawTestInstrument
    PRODUCT_NAME "HMTestInstrument"
    COMPANY_NAME "HM"
    PLUGIN_MANUFACTURER_CODE Hmts
    PLUGIN_CODE Hmt1
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3)

target_sources(NiDawTestInstrument PRIVATE TestPlugin/TestInstrument.cpp)

target_compile_definitions(NiDawTestInstrument PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# A tiny VST3 effect of our own: stereo in, stereo out, one gain parameter. The
# self test puts it in a track's insert chain and checks that the audio really went
# through it - something a "does it crash without a plugin" test cannot tell you.
juce_add_plugin(NiDawTestEffect
    PRODUCT_NAME "HMTestEffect"
    COMPANY_NAME "HM"
    PLUGIN_MANUFACTURER_CODE Hmts
    PLUGIN_CODE Hmfx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3)

target_sources(NiDawTestEffect PRIVATE TestPlugin/TestEffect.cpp)

target_compile_definitions(NiDawTestEffect PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# Building the app brings the test plugin with it, so one build command produces
# everything the self test needs. `_All` is the aggregate that links and copies
# the bundle; the bare target only builds the shared code.
add_dependencies(NiDaw NiDawTestInstrument_All)
add_dependencies(NiDaw NiDawTestEffect_All)

# Unit tests, built as a separate console target that shares the same sources.
juce_add_console_app(NiDawTests
    PRODUCT_NAME "NiDawTests"
    COMPANY_NAME "Ni")

target_sources(NiDawTests PRIVATE
    ${NIDAW_SHARED_SOURCES}
    Source/Core/Types.h
    Source/Core/ControllerMath.h
    Source/Core/MeterMath.h
    Source/Core/VstSupport.h
    Source/Core/PluginScanProtocol.h
    Source/Core/PluginRendering.h
    Source/Core/PluginGrouping.h
    Source/Core/EffectChain.h
    Source/Core/WindowPlacement.h
    Source/Core/MusicMath.h
    Source/Core/HybridPresets.h
    Source/Core/MinimoogPresets.h
    Source/Core/WaveformPresets.h
    Source/Core/MidiArp.h
    Source/Core/KeyboardLayouts.h
    Source/Core/ProjectDocument.h
    Source/Audio/SynthEngine.h
    Source/Audio/GranularSynth.h
    Source/Tests/TestSupport.h
    Source/Tests/TestMain.cpp
    Source/Tests/MathTests.cpp
    Source/Tests/ClipTests.cpp
    Source/Tests/SerializationTests.cpp
    Source/Tests/InstrumentTests.cpp
    Source/Tests/MinimoogTests.cpp
    Source/Tests/PlaybackTests.cpp
    Source/Tests/PresetTests.cpp
    Source/Tests/PluginTests.cpp
    Source/Tests/MeterMathTests.cpp
    Source/Tests/ControllerTests.cpp
    Source/Tests/MidiInputMapTests.cpp
    Source/Tests/MidiArpTests.cpp
    Source/Tests/KeyboardLayoutTests.cpp
    Source/Tests/HybridTests.cpp
    Source/Tests/PitchShapeTests.cpp
    Source/Tests/WavetableTableTests.cpp)

target_include_directories(NiDawTests PRIVATE Source)

target_compile_definitions(NiDawTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_APPLICATION_NAME_STRING="NiDawTests"
    JUCE_APPLICATION_VERSION_STRING=${PROJECT_VERSION})

target_link_libraries(NiDawTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
