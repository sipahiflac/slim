cmake_minimum_required(VERSION 3.22)
# macOS: Intel and Apple Silicon in one binary, running on macOS 11 and later. Must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(FakePhoneScreen VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C/C++ runtime statically so a clean Windows install needs no Visual C++ Redistributable
# (the VST3 loads into a DAW that may not have it either).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
find_package(Git REQUIRED)
# JUCE creates a D3D device on every GPU when the first window opens. On hybrid Windows laptops that wakes the
# idle discrete GPU and costs ~3 s each time. The patch touches a Windows-only file, so only Windows applies it.
set(JUCE_FETCH_PATCH "")
if(WIN32)
    set(JUCE_FETCH_PATCH PATCH_COMMAND ${CMAKE_COMMAND}
        -DGIT=${GIT_EXECUTABLE}
        -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/juce-skip-headless-gpus.patch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatchOnce.cmake)
endif()
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
    ${JUCE_FETCH_PATCH}
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(FakePhoneScreen
    PRODUCT_NAME "2G"
    COMPANY_NAME "Scrap Brain Audio"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Scrap Brain Audio"
    BUNDLE_ID "com.scrapbrainaudio.2g"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon/2g-icon-256.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon/2g-icon-48.png"
)

target_sources(FakePhoneScreen PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/LicenseClient.h
    Source/LicenseClient.cpp
    Source/LoopFinder.h
    Source/LoopFinder.cpp
    Source/ParallelFor.h
    Source/Params.h
    Source/Voice.h
    Source/Voice.cpp
    Source/ToneCard.h
    Source/ToneCard.cpp
    Source/SoundEngine.h
    Source/SoundEngine.cpp
)

# Windows draws the page with WebView2; macOS uses the system WKWebView and needs nothing extra.
if(WIN32)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/packages")
    list(APPEND CMAKE_MODULE_PATH "${JUCE_CMAKE_UTILS_DIR}")
    find_package(WebView2 REQUIRED)
    set(PHONE_WEBVIEW_LIBRARY juce::juce_webview2)
    set(PHONE_WEBVIEW_DEFINITIONS JUCE_USE_WIN_WEBVIEW2=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
else()
    set(PHONE_WEBVIEW_LIBRARY "")
    set(PHONE_WEBVIEW_DEFINITIONS "")
endif()

target_compile_definitions(FakePhoneScreen PRIVATE
    JUCE_WEB_BROWSER=1
    ${PHONE_WEBVIEW_DEFINITIONS}
    JUCE_USE_CURL=0
)

target_link_libraries(FakePhoneScreen PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_basics
    ${PHONE_WEBVIEW_LIBRARY}
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
)

# The web UI in Resources/ plus the artwork layers and key sounds kept in
# Source/ are copied next to the executable, where the app serves them from.
# This target is deliberately always part of the build, so even an otherwise
# up-to-date build refreshes them.
# The standalone finds its files next to the executable on Windows and in Contents/Resources inside the .app on macOS.
if(APPLE)
    set(PHONE_APP_RESOURCES "$<TARGET_BUNDLE_CONTENT_DIR:FakePhoneScreen>/Resources")
else()
    set(PHONE_APP_RESOURCES "$<TARGET_FILE_DIR:FakePhoneScreen>/Resources")
endif()
add_custom_target(SyncPhoneArtwork ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${PHONE_APP_RESOURCES}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
        "${PHONE_APP_RESOURCES}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/telback.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/telbuttons.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttondown.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttonup.wav"
        "${PHONE_APP_RESOURCES}"
    COMMENT "Syncing the web UI, phone artwork layers and key sounds next to the app"
)
add_dependencies(SyncPhoneArtwork FakePhoneScreen)

# VST3 everywhere; on macOS also an Audio Unit (Logic, GarageBand). AU needs checking with auval on a Mac.
if(APPLE)
    set(PLUGIN_FORMATS VST3 AU)
else()
    set(PLUGIN_FORMATS VST3)
endif()
juce_add_plugin(Melodragon2G
    COMPANY_NAME "Scrap Brain Audio"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Scrap Brain Audio"
    COMPANY_WEBSITE "https://scrapbrainaudio.com"
    BUNDLE_ID "com.scrapbrainaudio.2g.vst3"
    PRODUCT_NAME "2G"
    PLUGIN_NAME "2G"
    PLUGIN_MANUFACTURER_CODE ScBA
    PLUGIN_CODE Md2G
    DESCRIPTION "2G phone speaker sampler instrument"
    FORMATS ${PLUGIN_FORMATS}
    VST3_CATEGORIES Instrument Synth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Melodragon2G PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/LicenseClient.h
    Source/LicenseClient.cpp
    Source/LoopFinder.h
    Source/LoopFinder.cpp
    Source/ParallelFor.h
    Source/Params.h
    Source/Voice.h
    Source/Voice.cpp
    Source/ToneCard.h
    Source/ToneCard.cpp
    Source/SoundEngine.h
    Source/SoundEngine.cpp
)

target_link_libraries(Melodragon2G PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_basics
    ${PHONE_WEBVIEW_LIBRARY}
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
)

target_compile_definitions(Melodragon2G PRIVATE
    JUCE_WEB_BROWSER=1
    ${PHONE_WEBVIEW_DEFINITIONS}
    JUCE_USE_CURL=0
)

set(SCRAPBRAIN_LICENSE_API_URL "" CACHE STRING "HTTPS URL of the 2G license service")
if(SCRAPBRAIN_LICENSE_API_URL)
    target_compile_definitions(FakePhoneScreen PRIVATE SCRAPBRAIN_LICENSE_API_URL="${SCRAPBRAIN_LICENSE_API_URL}")
    target_compile_definitions(Melodragon2G PRIVATE SCRAPBRAIN_LICENSE_API_URL="${SCRAPBRAIN_LICENSE_API_URL}")
endif()

if(WIN32)
    target_link_libraries(FakePhoneScreen PRIVATE advapi32 bcrypt)
    target_link_libraries(Melodragon2G PRIVATE advapi32 bcrypt)
elseif(APPLE)
    # IOKit: the hardware UUID used to bind a Free licence to a computer.
    target_link_libraries(FakePhoneScreen PRIVATE "-framework IOKit" "-framework CoreFoundation")
    target_link_libraries(Melodragon2G PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

# A VST3 is a bundle. Keep the same phone WebGUI and artwork next to the
# module so the DAW editor is the phone, not a separate native control panel.
add_custom_target(SyncMelodragonWebUI ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Melodragon2G_VST3>/../Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "$<TARGET_FILE_DIR:Melodragon2G_VST3>/../Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/telback.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/telbuttons.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttondown.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttonup.wav"
        "$<TARGET_FILE_DIR:Melodragon2G_VST3>/../Resources"
    COMMENT "Bundling the 2G phone WebGUI into the VST3"
)
add_dependencies(SyncMelodragonWebUI Melodragon2G_VST3)
if(APPLE)
    # The Audio Unit bundle carries the same web UI.
    add_dependencies(SyncMelodragonWebUI Melodragon2G_AU)
    add_custom_command(TARGET SyncMelodragonWebUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Melodragon2G_AU>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "$<TARGET_FILE_DIR:Melodragon2G_AU>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Source/telback.png" "${CMAKE_CURRENT_SOURCE_DIR}/Source/telbuttons.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttondown.wav" "${CMAKE_CURRENT_SOURCE_DIR}/Source/buttonup.wav"
            "$<TARGET_FILE_DIR:Melodragon2G_AU>/../Resources"
        COMMENT "Bundling the 2G phone WebGUI into the Audio Unit")
endif()
