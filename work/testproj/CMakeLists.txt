cmake_minimum_required(VERSION 3.22)

project(NiDawTestsStandalone VERSION 0.1.0 LANGUAGES C CXX)

# See NiDaw/CMakeLists.txt: cl prints the /showIncludes marker as UTF-8, while CMake
# records it as 936-decoded mojibake; restore the real bytes so Ninja can match it.
if(MSVC)
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header dependency tracking works again (see the prefix above).

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(NIDAW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../NiDaw")

add_subdirectory("${NIDAW_ROOT}/external/JUCE" JUCE)

# The sources both targets compile (the same file the app's own CMakeLists includes),
# so a new Core file never means editing two hand-written lists.
include("${NIDAW_ROOT}/cmake/SharedSources.cmake")

juce_add_console_app(NiDawTests PRODUCT_NAME "NiDawTests" COMPANY_NAME "Ni")

target_sources(NiDawTests PRIVATE
    ${NIDAW_SHARED_SOURCES}
    "${NIDAW_ROOT}/Source/Tests/TestSupport.h"
    "${NIDAW_ROOT}/Source/Tests/TestMain.cpp"
    "${NIDAW_ROOT}/Source/Tests/MathTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/ClipTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/SerializationTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/InstrumentTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/MinimoogTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/PlaybackTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/PresetTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/PluginTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/MeterMathTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/ControllerTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/MidiInputMapTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/MidiArpTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/KeyboardLayoutTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/PitchShapeTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/HybridTests.cpp"
    "${NIDAW_ROOT}/Source/Tests/WavetableTableTests.cpp")

target_include_directories(NiDawTests PRIVATE "${NIDAW_ROOT}/Source")

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
        juce::juce_recommended_config_flags)

if(MSVC)
    target_link_options(NiDawTests PRIVATE "/MAP:${CMAKE_BINARY_DIR}/NiDawTests.map")
endif()
