cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS Universal Binary architectures")
endif()

project(wildin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the VST3 loads cleanly on any Windows DAW
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Local checkout if present (Windows dev machine), otherwise JUCE is downloaded (GitHub macOS build).
set(JUCE_PATH "D:/ScrapBrainAudio/sampler/JUCE" CACHE PATH "Path to local JUCE checkout")
if(EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(WILDIN_FORMATS AU VST3)
else()
    set(WILDIN_FORMATS VST3)
endif()

juce_add_plugin(wildin
    COMPANY_NAME "Scrap Brain Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE Scrb
    PLUGIN_CODE Wldn
    AU_MAIN_TYPE kAudioUnitType_Effect
    BUNDLE_ID com.scrapbrainaudio.wildin
    FORMATS ${WILDIN_FORMATS}
    PRODUCT_NAME "wildin"
    PLUGIN_NAME "Wildin'"
)

target_sources(wildin PRIVATE
    Source/AtomicDSP.h
    Source/BrainLogoData.h
    Source/LicenseManager.h
    Source/MechClicks.h
    Source/QuicksandFontData.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_compile_definitions(wildin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(wildin PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
