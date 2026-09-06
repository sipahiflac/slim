cmake_minimum_required(VERSION 3.24)

# --- macOS build settings -------------------------------------------------
# These MUST be set before project() or CMake ignores them.
#
# The test is CMAKE_HOST_APPLE, NOT APPLE: APPLE is only defined once project()
# has run, so an if(APPLE) guard up here never fires and the settings below are
# silently skipped. CMAKE_HOST_APPLE is available from the first line.
#
# Universal binary plus a deployment target old enough for the machines
# customers actually run. Without these the build targets whatever macOS the
# build machine happens to be on, and JUCE 7's juce_Windowing_mac.mm then fails
# with "'CGWindowListCreateImage' is unavailable: obsoleted in macOS 15.0" -
# that symbol is only an error when the deployment target is 15.0 or newer.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

    # JUCE 7 builds its juceaide helper through a separate nested cmake
    # invocation and forwards only CMAKE_MAKE_PROGRAM, CMAKE_BUILD_TYPE,
    # JUCE_BUILD_HELPER_TOOLS and CMAKE_INSTALL_PREFIX to it - not the
    # deployment target. That sub-build would therefore default to the host
    # macOS version and hit the same obsoleted-symbol error. The environment
    # variable is inherited by the sub-process, and both CMake and clang honour
    # it as the default deployment target.
    set(ENV{MACOSX_DEPLOYMENT_TARGET} "11.0")
endif()

project(WX VERSION 1.1.0)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
)
FetchContent_MakeAvailable(juce)

# JUCE 7's generated Windows resource does not depend on its version info file,
# so an incremental reconfigure can otherwise leave the previous PE version.
if(WIN32)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/WX_artefacts/JuceLibraryCode/WX_resources.rc")
endif()

# AU only exists on macOS - listing it on Windows makes the configure step fail.
if(APPLE)
    set(WX_PLUGIN_FORMATS Standalone VST3 AU)
else()
    set(WX_PLUGIN_FORMATS Standalone VST3)
endif()

# Create the plugin target
juce_add_plugin(WX
    COMPANY_NAME "Scrap Brain Audio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Anti
    PLUGIN_CODE Stbl
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS ${WX_PLUGIN_FORMATS}
    PRODUCT_NAME "WX"
    BUNDLE_ID "com.scrapbrainsound.stablealive"
    # A 1024 px PNG, not the old 16/32/48 .ico: JUCE cannot read .ico on macOS,
    # which is why the app used to appear with no icon at all. juceaide turns
    # this into a proper .icns / .ico for each platform.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.png"
)

# Source files
target_sources(WX
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/SynthVoice.cpp
        Source/DSP/SynthVoice.h
        Source/DSP/SeedGenerator.cpp
        Source/DSP/SeedGenerator.h
        Source/DSP/Effects.cpp
        Source/DSP/Effects.h
        Source/UI/WeatherRadarDisplay.cpp
        Source/UI/WeatherRadarDisplay.h
        Source/UI/UtilityPanel.cpp
        Source/UI/UtilityPanel.h
        Source/UI/PresetManager.cpp
        Source/UI/PresetManager.h
        Source/UI/LicenseManager.h
        Source/UI/Fonts.cpp
        Source/UI/Fonts.h
)

# Embedded typefaces. The UI used to ask for "Source Sans 3" and the platform
# default monospaced font by name - neither is installed on customer machines,
# so the plug-in rendered with whatever the OS substituted. Poppins is compiled
# into the binary instead. (SIL OFL 1.1 - assets/Poppins-OFL.txt)
# The factory presets are embedded too - PresetManager writes any that are
# missing into the user's "WX Presets" folder on first run, so they ship with
# the plug-in itself rather than depending on the installer, and they land in
# the right place on Windows and macOS alike.
#
# Drop a .wxpreset in assets/factory-presets to add one: save the patch in WX,
# then copy the file out of your own "WX Presets" folder into here.
file(GLOB WX_FACTORY_PRESETS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/factory-presets/*.wxpreset")

juce_add_binary_data(WXBinaryData
    SOURCES
        assets/PoppinsRegular.ttf
        assets/PoppinsMedium.ttf
        assets/PoppinsBold.ttf
        ${WX_FACTORY_PRESETS}
)

# Required so the binary data can be linked into the shared-library plug-in
# formats (VST3/AU) rather than only the standalone executable.
set_target_properties(WXBinaryData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Compiler flags
target_compile_definitions(WX
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(WX
    PRIVATE
        WXBinaryData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

target_compile_features(WX PUBLIC cxx_std_17)
# -fpermissive is a GCC-only flag: Clang (macOS/Xcode) rejects it outright with
# "unknown argument", which kills the whole macOS build. Keep it GCC-only.
target_compile_options(WX PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-fpermissive>
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wno-unused-parameter -Wno-sign-conversion -Wno-float-equal -Wno-shadow -Wno-deprecated-declarations>
)

# Static runtime linking for MinGW to avoid dependency DLLs (libgcc, libstdc++, libwinpthread) on client machines
if(MINGW)
    target_link_options(WX_Standalone PRIVATE -static -static-libgcc -static-libstdc++ -s)
    # Keep the plug-in self-contained. Customer systems do not have MSYS2's
    # libwinpthread-1.dll on the DAW search path.
    target_link_options(WX_VST3 PRIVATE -static -static-libgcc -static-libstdc++ -s)
endif()

# Automatically copy the Standalone .exe to the workspace root after compilation
# (Windows only - on macOS the standalone is an .app bundle, not a single file.)
if(WIN32)
    add_custom_command(TARGET WX_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:WX_Standalone>
            ${CMAKE_CURRENT_SOURCE_DIR}/WX.exe
    )
endif()
