cmake_minimum_required(VERSION 3.22)

project(HMDAWTestsStandalone VERSION 0.1.0 LANGUAGES C CXX)

# See HMDAW/CMakeLists.txt: cl prints the /showIncludes marker as UTF-8, while CMake
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

set(HMDAW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../HMDAW")

add_subdirectory("${HMDAW_ROOT}/external/JUCE" JUCE)

# The sources both targets compile (the same file the app's own CMakeLists includes),
# so a new Core file never means editing two hand-written lists.
include("${HMDAW_ROOT}/cmake/SharedSources.cmake")

juce_add_console_app(HMDAWTests PRODUCT_NAME "HMDAWTests" COMPANY_NAME "Ni")

target_sources(HMDAWTests PRIVATE
    ${HMDAW_SHARED_SOURCES}
    "${HMDAW_ROOT}/Source/Tests/TestSupport.h"
    "${HMDAW_ROOT}/Source/Tests/TestMain.cpp"
    "${HMDAW_ROOT}/Source/Tests/MathTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/ClipTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/SerializationTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/InstrumentTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/MinimoogTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/PlaybackTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/PresetTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/PluginTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/MeterMathTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/ControllerTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/MidiInputMapTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/MidiArpTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/KeyboardLayoutTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/PitchShapeTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/HybridTests.cpp"
    "${HMDAW_ROOT}/Source/Tests/WavetableTableTests.cpp")

target_include_directories(HMDAWTests PRIVATE "${HMDAW_ROOT}/Source")

target_compile_definitions(HMDAWTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_APPLICATION_NAME_STRING="HMDAWTests"
    JUCE_APPLICATION_VERSION_STRING=${PROJECT_VERSION})

target_link_libraries(HMDAWTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

if(MSVC)
    target_link_options(HMDAWTests PRIVATE "/MAP:${CMAKE_BINARY_DIR}/HMDAWTests.map")
endif()
